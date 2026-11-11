import sys
import time
import threading
import os
import random
import math

# ---- [2026-10-19] Mesa llvmpipe 单线程光栅化兜底 ----
# 兆芯等无硬件 GL 驱动的平台回退 llvmpipe 软件渲染时，其多线程光栅化
# (lp_rast) 在兆芯 CPU 上有已知数据竞争 bug：3D 画布出现彩色碎块噪点、
# glmark2 黑屏。须在 QApplication/GL 库加载前设置；只影响 llvmpipe，
# 硬件 GL 驱动完全忽略，其余平台零影响。（启动脚本已导出，此处覆盖
# 手工直接运行本 GUI 的场景；setdefault 不覆盖用户显式设置）
os.environ.setdefault('LP_NUM_THREADS', '0')


# ---- [2026-10-22] glmark 老化项推倒重写:内置 GL 压测场景 ----
# 外部 glmark2 子进程方案(pty/环境注入/候选切换/EGL强制)在兆芯机上多轮实测
# 均黑窗,根因远程不可复现。新方案:不再拉起任何外部 GL 进程,改为进程内置
# QGLWidget 压测场景(与已验证能正常出画面的 3D 画布完全同一渲染路径):
# - 所有 GL 渲染统一走 Qt 主线程串行执行,无多进程并发 GL、无 GLX/EGL 混用
#   —— 从机制上消除"3D 球体彩色碎块"与"glmark 黑窗"两类故障的触发条件
# - 不依赖外部二进制/数据路径/终端缓冲,离线零依赖,全架构(ARM/AMD64/龙芯)
#   通用;跑分测试中的 glmark2 评分链路不受影响
# 故撤销上一轮的 QT_XCB_GL_INTEGRATION=egl / EGL_PLATFORM=x11 全局注入,
# 回到 3D 单独运行已被实测验证正常的默认 GL 后端。
from PyQt5.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
                             QCheckBox, QLineEdit, QPushButton, QLabel, QProgressBar,
                             QGroupBox, QFormLayout, QGridLayout, QMessageBox)
from PyQt5.QtCore import Qt, QTimer, QThread, pyqtSignal, QPointF
from PyQt5.QtGui import QPainter, QColor, QPen, QRadialGradient, QBrush, QPolygonF

# ---- 修复（D5）：重量级依赖改为可选导入 ----
# 旧实现顶层无条件 import psutil/OpenGL/QtOpenGL/complex_scene，任一缺失
# 整个老化 GUI 直接 ImportError 起不来（即使只勾选 CPU/内存/磁盘/2D）。
# 改为 try/except + 能力标志：缺 3D 依赖时 3D 项降级禁用，其它老化项不受影响。
try:
    import psutil
    HAS_PSUTIL = True
except ImportError:
    psutil = None
    HAS_PSUTIL = False

try:
    from OpenGL.GL import *
    from OpenGL.GLU import *
    from PyQt5.QtOpenGL import QGLWidget
    HAS_OPENGL = True
except ImportError:
    # 回退为普通 QWidget：Canvas3D 类仍可定义（paintGL 不会被 QGLWidget 调用），
    # 3D 老化启动时会被能力检查拦截并提示
    HAS_OPENGL = False
    from PyQt5.QtWidgets import QWidget as QGLWidget

try:
    from complex_scene import ComplexScene
    HAS_COMPLEX_SCENE = True
except ImportError:
    ComplexScene = None
    HAS_COMPLEX_SCENE = False

# ---- 模块级 worker:必须放在模块顶层(不能放在 class.method 里),
# 否则 Windows 上 multiprocessing 用 spawn 模式无法 pickle 嵌套函数
# (AttributeError: Can't pickle local object '...<locals>.worker')
# Linux 上 fork 模式不会触发这个错误,但为了跨平台一致提到模块顶层
# 设计:由 stop_event 控制退出(可 pickle),旧版同时靠"持续补充进程"保证满载
import multiprocessing as _mp
def _cpu_worker(stop_event):
    """模块级 CPU worker —— 纯计算满载循环(与旧版行为一致)"""
    # ---- 修复：孤儿进程兜底 ----
    # 统一控制停止老化用 pkill -9 杀主进程——SIGKILL 不走 Python 退出钩子，
    # multiprocessing 的 daemon 机制失效，worker 变孤儿继续 100% 占核。
    # PR_SET_PDEATHSIG 让内核在父进程死亡时直接给 worker 发 SIGKILL。
    try:
        import ctypes
        _libc = ctypes.CDLL("libc.so.6", use_errno=True)
        _libc.prctl(1, 9)  # PR_SET_PDEATHSIG=1, SIGKILL=9（仅 Linux 生效）
        # 防 race：prctl 前父进程已死的情况补查一次
        if os.getppid() == 1:
            os._exit(0)
    except Exception:
        pass  # 非 Linux 平台（Windows/macOS）忽略，走 daemon 机制
    # 关键:循环体内【禁止任何 sleep】!
    # 历史 bug:曾在每轮末尾加 time.sleep(0.005),导致单核占空比只有 ~75-85%,
    # CPU 占用率永远拉不到 100%(用户实测)。
    # "满载导致 UI 卡死"是误解:worker 是独立进程,系统调度器会公平分配时间片,
    # 旧版纯满载循环 + UI 丝滑并存,证明二者不冲突。
    while not stop_event.is_set():
        # 旧版同款纯字节码循环(10^6 次),已验证能拉满单核;
        # 每 10^6 次回到 while 检查一次 stop_event,保证停止响应 < 0.1 秒
        for _ in range(10**6):
            pass

# ---- 进程级兜底:把"未捕获异常"全部转成 UTF-8 stderr 输出 + 记录到文件,
# 而不是让 PyQt5 把 Python 进程直接杀掉(macOS launchd/Win32 上弹"应用程序 已意外关闭")
# 之前修复:每个 QTimer 回调单独 try/except;此处补一道:所有线程/未捕获异常都走这
#
# 关键:这个钩子**只是记录**,绝不能"吞掉"异常——
# 之前的实现会让 NameError 等"程序必坏"的错误也被吃掉,
# 表现就是"程序启动后没崩,但点任何按钮都没反应"(其实是 __init__ 中途挂掉)
def _global_excepthook(exc_type, exc_value, exc_tb):
    try:
        import traceback
        msg = ''.join(traceback.format_exception(exc_type, exc_value, exc_tb))
    except Exception:
        msg = f'{exc_type.__name__}: {exc_value}'
    # 写到 stderr.buffer(避免 Windows legacy console / macOS launchd 上的
    # UnicodeEncodeError 引发二次崩溃),  UTF-8 强制编码, errors='replace' 兜底
    try:
        sys.stderr.buffer.write((
            '\n[AGING-TEST excepthook] ' + msg + '\n').encode('utf-8', 'replace'))
        sys.stderr.buffer.flush()
    except Exception:
        pass
    # 同时写到 ~/.aging_test_crash.log 方便事后定位(用户截图发给我们)
    try:
        home = os.path.expanduser('~')
        log_path = os.path.join(home, '.aging_test_crash.log')
        with open(log_path, 'a', encoding='utf-8', errors='replace') as f:
            ts = time.strftime('%Y-%m-%d %H:%M:%S')
            f.write(f'[{ts}] {exc_type.__name__}: {exc_value}\n')
            f.write(msg + '\n\n')
    except Exception:
        pass
    # ---- 关键:不再调用 sys.__excepthook__,而是让异常继续被 Python 默认行为
    # 处理(打印 + 返回非零退出码)。这样程序真的出错时会立即退出,
    # 而不会"看起来在跑但功能全坏"  ←  ← 之前用户看到的"没反应"就是这种状态
    # 调用默认 hook 而不是 sys.exit(0),让 Python 自己决定是否退出
    try:
        sys.__excepthook__(exc_type, exc_value, exc_tb)
    except Exception:
        # 绝对不要再 raise,否则会无限递归
        pass
sys.excepthook = _global_excepthook

# ---- 线程异常钩子:工作线程抛未捕获异常时也会走这里(默认静默退线程,UI 无感知)
def _thread_excepthook(args):
    try:
        import traceback
        msg = ''.join(traceback.format_exception(args.exc_type, args.exc_value, args.exc_traceback))
    except Exception:
        msg = f'{args.exc_type.__name__}: {args.exc_value}'
    try:
        sys.stderr.buffer.write((
            '\n[AGING-TEST thread-excepthook] ' + msg + '\n').encode('utf-8', 'replace'))
        sys.stderr.buffer.flush()
    except Exception:
        pass
try:
    import threading
    threading.excepthook = _thread_excepthook
except Exception:
    pass   # Python < 3.8 不支持

# ============================================================================
# 显存(VRAM)真实检测 —— 与 C++ 主界面 SystemMonitor::updateGpuMemoryUI 同源同层级:
#   1) nvidia-smi(NVIDIA 全平台)
#   2) /sys/class/drm/card[0-9]+/device/mem_info_vram_{used,total}
#      (amdgpu/AMD、Xe/Intel 独显及部分国产 GPU 的 DRM 驱动;
#       飞腾/鲲鹏/龙芯/x86 等各架构 Linux 均走此标准接口)
#   3) /sys/class/drm/card[0-9]+/device/mem_info_gtt_{used,total}(AMD 集显 GTT 兜底)
#   4) 全部失败 → (0, 0, ''),UI 显示"未检测到",不再伪造随机数
# 两端使用同一层级、同一选卡规则(取总显存最大的卡) → 主界面与老化界面数值一致
# ============================================================================
import subprocess as _subprocess
import re as _re
import glob as _glob

# nvidia-smi 可用性缓存(避免每秒抛一次 FileNotFoundError)
_NVIDIA_SMI_STATE = {'checked': False, 'ok': False}

def _read_int_file(path):
    """读 sysfs 整数文件,失败返回 None(不抛异常)"""
    try:
        with open(path, 'r') as f:
            return int(f.read().strip())
    except Exception:
        return None

def _query_nvidia_smi():
    """路线1:nvidia-smi 查询,返回 (used_bytes, total_bytes, source) 或 None"""
    try:
        if not _NVIDIA_SMI_STATE['checked']:
            _NVIDIA_SMI_STATE['checked'] = True
            # 注意:用 stdout/stderr=PIPE 而非 capture_output=True ——
            # 后者是 Python 3.7+ 参数,CentOS 7 等老系统 python3.6 会 TypeError
            _subprocess.run(['nvidia-smi', '-L'], stdout=_subprocess.PIPE,
                            stderr=_subprocess.PIPE, timeout=3,
                            creationflags=getattr(_subprocess, 'CREATE_NO_WINDOW', 0))
            _NVIDIA_SMI_STATE['ok'] = True
    except Exception:
        _NVIDIA_SMI_STATE['ok'] = False
        return None
    if not _NVIDIA_SMI_STATE['ok']:
        return None
    try:
        r = _subprocess.run(
            ['nvidia-smi', '--query-gpu=memory.used,memory.total',
             '--format=csv,noheader,nounits'],
            stdout=_subprocess.PIPE, stderr=_subprocess.PIPE, timeout=3,
            creationflags=getattr(_subprocess, 'CREATE_NO_WINDOW', 0))
        if r.returncode == 0:
            lines = r.stdout.decode('utf-8', 'replace').strip().splitlines()
            if lines:   # 多卡时取第一块(与主界面 C++ 行为一致)
                parts = [p.strip() for p in lines[0].split(',')]
                if len(parts) >= 2:
                    used = int(float(parts[0]) * 1024 * 1024)     # MiB → bytes
                    total = int(float(parts[1]) * 1024 * 1024)
                    if total > 0 and used >= 0:
                        return (used, total, 'nvidia-smi')
    except Exception:
        pass
    return None

def _query_drm_mem():
    """路线2/3:Linux DRM sysfs(amdgpu/Xe 等 vram 节点 + AMD gtt 兜底)"""
    try:
        if not os.path.isdir('/sys/class/drm'):
            return None
        best_vram = None    # (used, total, 'vram')
        best_gtt = None     # (used, total, 'gtt')
        for card_dir in _glob.glob('/sys/class/drm/card*'):
            # 只接受 card[0-9]+ 目录,跳过 card0-HDMI-A-1 等连接器目录
            if not _re.fullmatch(r'card\d+', os.path.basename(card_dir)):
                continue
            dev = os.path.join(card_dir, 'device')
            vt = _read_int_file(os.path.join(dev, 'mem_info_vram_total'))
            vu = _read_int_file(os.path.join(dev, 'mem_info_vram_used'))
            if vt and vt > 0 and vu is not None and vu >= 0:
                if best_vram is None or vt > best_vram[1]:
                    best_vram = (vu, vt, 'vram')
            gt = _read_int_file(os.path.join(dev, 'mem_info_gtt_total'))
            gu = _read_int_file(os.path.join(dev, 'mem_info_gtt_used'))
            if gt and gt > 0 and gu is not None and gu >= 0:
                if best_gtt is None or gt > best_gtt[1]:
                    best_gtt = (gu, gt, 'gtt')
        if best_vram:
            return best_vram
        return best_gtt
    except Exception:
        return None

def _query_gpu_memory():
    """显存查询总入口,返回 (used_bytes, total_bytes, source);失败返回 (0, 0, '')"""
    r = _query_nvidia_smi()
    if r:
        return r
    r = _query_drm_mem()
    if r:
        return r
    return (0, 0, '')

# ---- 显存压力池:老化时真正"吃掉"一块显存,让显存占用率实实在在拉上去 ----
# (在 GL 上下文内分配 16MB/张的大纹理;任何 GL 错误都被吞掉,不影响渲染主流程)
_VRAM_TEX_SIZE = 2048            # 2048x2048 RGBA8 = 16MB / 张

def _gl_alloc_vram_pool(target_bytes):
    """在当前 GL 上下文中分配显存压力池,返回纹理 id 列表"""
    pool = []
    try:
        chunk = _VRAM_TEX_SIZE * _VRAM_TEX_SIZE * 4
        n = max(1, int(target_bytes // chunk))
        for _ in range(n):
            tex = glGenTextures(1)
            if not tex:
                break
            glBindTexture(GL_TEXTURE_2D, tex)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                         _VRAM_TEX_SIZE, _VRAM_TEX_SIZE, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, None)
            if glGetError() != GL_NO_ERROR:
                # 显存不足:删掉刚建的纹理并停止继续分配(保留已分配部分)
                try:
                    glDeleteTextures([tex])
                except Exception:
                    pass
                break
            pool.append(int(tex))
        glBindTexture(GL_TEXTURE_2D, 0)
    except Exception:
        pass
    return pool

def _gl_free_vram_pool(pool):
    """释放显存压力池(必须在 GL 上下文内调用)"""
    if not pool:
        return
    try:
        alive = [t for t in pool if t]
        if alive:
            glDeleteTextures(alive)
    except Exception:
        pass

class AgingTestGUI(QMainWindow):
    # 停止流程完成信号:后台 stopper 线程 → UI 线程(跨线程信号自动排队,线程安全)
    stopFinished = pyqtSignal()
    # glmark2 错误提示信号:worker 线程 emit → Qt 自动排队到 UI 线程弹非模态框
    glmarkErrorShown = pyqtSignal(str)
    # 3D+glmark2 合并模式画布控制信号:worker 线程 → UI 线程(线程安全)
    # True=创建并启动 3D 画布;False=停止并销毁(彻底释放 GL 上下文)
    canvasCtlRequested = pyqtSignal(bool)

    def __init__(self, auto_start=False):
        super().__init__()
        self.setWindowTitle("系统硬件老化测试工具")
        self.setGeometry(100, 100, 800, 600)

        self.auto_start = auto_start

        # 初始化变量
        self.running = False
        self._stopping = False          # 停止流程进行中(防重复点击/防重入)
        self._stop_worker_started = False   # 后台 stopper 线程已拉起(兜底去重)
        self.aging_duration = 0
        self.start_time = 0
        self.threads = []
        # 磁盘 worker 状态消息(worker 线程只写这个纯 Python 属性,
        # 由 UI 线程的 update_status 渲染 —— 修复旧版跨线程直接
        # 调 setText() 更新 Qt 控件导致的未定义行为/概率性卡死)
        self._disk_status_msg = ''
        # 显存实时采样(后台监控线程写,UI 线程读): (used, total, source)
        self._gpu_mem = (0, 0, '')
        self._gpu_monitor_stop = threading.Event()
        # QTimer 必须传 self 作为 parent,否则 AgingTestGUI 对象被 Python GC 时,
        # 未连接的 timer 仍可能再触发一次 timeout,调用 self.update_status 时
        # self 的 Qt 底层 widget 已 destroy → RuntimeError → 进程崩溃
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.update_status)

        # 负载监控/纠错（每个负载项独立跟踪成功率与异常）
        # load_progress[项] = 最近一次测得的速率（MB/s 或 %）
        # load_errors[项] = 错误信息字符串（空字符串=无错误）
        # load_targets[项] = (类型, 阈值, 单位)，用于验证"满载"是否真的达到
        self.load_progress = {
            'cpu': 0.0,    # CPU 综合占用率（%）
            'memory': 0.0, # 已用内存（%）
            'disk': 0.0,   # 磁盘写入速率（MB/s）
            'gpu2d': 0.0,  # 2D 帧率（fps）
            'gpu3d': 0.0,  # 3D 帧率（fps）
            'glmark': 0.0, # glmark2 实时帧率（fps，未跑=0）
            'network': 0.0 # 网络吞吐（KB/s）
        }
        self.load_errors = {k: '' for k in self.load_progress}
        # glmark2 子进程引用（worker 线程写 / stop 线程 terminate，仅毫秒级操作）
        self._glmark_proc = None
        # glmark2 一轮总分(Score)与实时帧率(fps)分离 —— 旧版共用
        # load_progress['glmark'] 存 Score(几百~几千),满载校验(阈值 10fps)
        # 会被 Score 误判达标,掩盖真实低帧率
        self._glmark_score = 0.0
        # glmark2 运行状态文本(worker 线程写,UI 线程读,纯 Python 属性 GIL 安全)
        self._glmark_state = ''
        # 负载阈值：低于阈值视为"满载失败"，30 秒未达阈值触发报错
        # CPU/内存阈值偏低（系统占用）：85% 视为合格满载
        # 磁盘阈值：100 MB/s（HDD 入门级，SSD/NVMe 通常远超）
        self.LOAD_TARGETS = {
            'cpu':     ('>=', 85.0,  '%'),
            'memory':  ('>=', 80.0,  '%'),
            'disk':    ('>=', 100.0, 'MB/s'),
            'gpu2d':   ('>=', 30.0,  'fps'),
            'gpu3d':   ('>=', 30.0,  'fps'),
            'glmark':  ('>=', 10.0,  'fps'),
            'network': ('>=', 100.0, 'KB/s')
        }
        self._load_check_start = None  # 满载校验计时起点

        # 先创建UI控件
        self.create_ui()

        # 停止完成信号 → UI 线程收尾(复位按钮/进度条 + 弹完成框)
        self.stopFinished.connect(self._on_stop_finished)
        self.glmarkErrorShown.connect(self._on_glmark_error)
        # 合并模式画布控制信号(worker 线程 → UI 线程):
        # True=按需创建并启动 3D 画布;False=停止并彻底销毁(释放 GL 上下文)
        self.canvasCtlRequested.connect(self._on_canvas_ctl)

        # 显存实时监控线程(常驻 daemon,每秒采样一次):
        # 在后台线程跑 nvidia-smi/读 sysfs,UI 定时器只读 self._gpu_mem,
        # 避免在 UI 线程 spawn 子进程造成界面卡顿
        gpu_mon = threading.Thread(target=self._gpu_monitor_loop, daemon=True)
        gpu_mon.start()

        # 然后启动定时器
        self.timer.start(1000)  # 程序启动时就开始更新状态
        self.update_status()  # 立即更新一次状态

        # 统一控制自动老化模式：自动设置并启动
        if self.auto_start:
            self.cpu_check.setChecked(True)
            self.memory_check.setChecked(True)
            self.disk_check.setChecked(True)
            self.gpu2d_check.setChecked(True)
            self.gpu3d_check.setChecked(True)
            # 自动老化:glmark2 已合并进 3D 测试(勾选 3D 即交替运行 3D 画布
            # 与 glmark2),不再作为独立可选项,故此处不再自动勾选复选框
            self.duration_edit.setText("168")  # 7天 = 168小时
            QTimer.singleShot(500, self.start_aging)  # 延迟500ms后自动开始

    def create_ui(self):

        # 创建主部件和布局
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QVBoxLayout(central_widget)

        # 配置区域
        config_group = QGroupBox("老化配置")
        config_layout = QFormLayout()

        # 硬件选择
        hardware_group = QGroupBox("硬件选项")
        hardware_layout = QVBoxLayout()

        self.cpu_check = QCheckBox("CPU满载")
        self.memory_check = QCheckBox("内存满载")
        self.disk_check = QCheckBox("硬盘满载")
        self.gpu2d_check = QCheckBox("2D显卡满载")
        # ★ glmark2 已合并进 3D 测试(2026-10-26):勾选 3D 即交替运行
        # 内置画布与 glmark2,不再单设复选框。glmark_check 对象保留仅作
        # 内部开关(不显示),勾选状态由"检测到可用 glmark2"自动决定
        self.gpu3d_check = QCheckBox("3D显卡满载(含glmark2专业基准)")
        self.glmark_check = QCheckBox("glmark2(内部开关,不显示)")
        self.network_check = QCheckBox("网络测试")

        hardware_layout.addWidget(self.cpu_check)
        hardware_layout.addWidget(self.memory_check)
        hardware_layout.addWidget(self.disk_check)
        hardware_layout.addWidget(self.gpu2d_check)
        hardware_layout.addWidget(self.gpu3d_check)
        # glmark_check 已合并进 3D 测试(勾选 3D 即交替运行 3D 画布与 glmark2),
        # 不再作为独立可选项加入界面,也不内部自动置位
        hardware_layout.addWidget(self.network_check)
        hardware_group.setLayout(hardware_layout)

        config_layout.addRow(hardware_group)

        # 时长设置
        self.duration_edit = QLineEdit("168")  # 默认168小时 = 7天
        config_layout.addRow("老化时长(小时):", self.duration_edit)

        config_group.setLayout(config_layout)
        main_layout.addWidget(config_group)

        # 控制按钮
        control_layout = QHBoxLayout()
        self.start_btn = QPushButton("开始老化")
        self.stop_btn = QPushButton("停止老化")
        self.stop_btn.setEnabled(False)

        control_layout.addWidget(self.start_btn)
        control_layout.addWidget(self.stop_btn)
        main_layout.addLayout(control_layout)

        # 状态显示
        # 设计目标:所有"进度条"都对齐在同一列,后面的实时数字也都在同一列,
        # 禁止用 QFormLayout 夹纯文字 label(夹在进度条中间很丑,之前就是这样)
        # 现在用 QGridLayout:col0=标题, col1=进度条(占主宽度), col2=实时数字
        status_group = QGroupBox("系统状态")
        status_layout = QGridLayout()
        status_layout.setColumnStretch(0, 0)   # 标题列:固定大小
        status_layout.setColumnStretch(1, 1)   # 进度条列:伸缩
        status_layout.setColumnStretch(2, 0)   # 实时数字列:固定
        status_layout.setHorizontalSpacing(12)
        status_layout.setVerticalSpacing(8)

        # ---- 创建进度条与绑定的"实时数字"label ----
        # 进度条默认显示百分比 + 中央文字,实时数字 label 在右侧显示额外信息
        self.cpu_usage = QProgressBar()
        self.cpu_usage.setFormat("%v%")
        self.memory_usage = QProgressBar()
        self.memory_usage.setFormat("%v%")
        self.gpu_mem_usage = QProgressBar()
        self.gpu_mem_usage.setFormat("%v%")
        # ---- 磁盘 IO 占用率:进度条按"瞬时 IO 速率 / 1000MB/s"显示 ----
        # 进度条上的文字 = "XX MB/s" 或 "X.XX GB/s",让人不用看右边 label 也能读
        self.disk_usage = QProgressBar()
        self.disk_usage.setFormat("-- MB/s")
        # ---- 实时数字列(全部 width=auto,显示具体数字)----
        self.cpu_detail_label = QLabel("--")
        self.memory_detail_label = QLabel("--")
        self.gpu_detail_label = QLabel("--")
        self.glmark_detail_label = QLabel("--")
        self.disk_detail_label = QLabel("--")
        self.time_remaining_label = QLabel("00:00:00")
        self.disk_free_label = QLabel("剩余 -- GB")
        # 全部 label 设置对齐方式 + 最小宽度,让它们在同一垂直基线上
        for lb in (self.cpu_detail_label, self.memory_detail_label,
                   self.gpu_detail_label, self.glmark_detail_label,
                   self.disk_detail_label,
                   self.disk_free_label, self.time_remaining_label):
            lb.setMinimumWidth(150)
            lb.setAlignment(Qt.AlignLeft | Qt.AlignVCenter)

        # ---- 排版 ----
        # 每行:标题 / 进度条 / 实时数字
        # 最后两行单独:剩余时间(满宽)/磁盘剩余空间(满宽靠右)
        row = 0
        # CPU行:进度条的实时数字放到同一行的右侧
        status_layout.addWidget(QLabel("CPU使用率:"), row, 0, Qt.AlignRight)
        status_layout.addWidget(self.cpu_usage, row, 1)
        status_layout.addWidget(self.cpu_detail_label, row, 2)
        row += 1
        # 内存行
        status_layout.addWidget(QLabel("内存使用率:"), row, 0, Qt.AlignRight)
        status_layout.addWidget(self.memory_usage, row, 1)
        status_layout.addWidget(self.memory_detail_label, row, 2)
        row += 1
        # 磁盘 IO 占用率行
        status_layout.addWidget(QLabel("磁盘 IO 占用率:"), row, 0, Qt.AlignRight)
        status_layout.addWidget(self.disk_usage, row, 1)
        status_layout.addWidget(self.disk_detail_label, row, 2)
        row += 1
        # 显存使用率行
        status_layout.addWidget(QLabel("显存使用率:"), row, 0, Qt.AlignRight)
        status_layout.addWidget(self.gpu_mem_usage, row, 1)
        status_layout.addWidget(self.gpu_detail_label, row, 2)
        row += 1
        # glmark2 压测状态行(未勾选显示 "--",勾选后显示 帧率/失败)
        status_layout.addWidget(QLabel("glmark2压测:"), row, 0, Qt.AlignRight)
        status_layout.addWidget(self.glmark_detail_label, row, 1, 1, 2)
        row += 1
        # 单独一行剩余时间(占满宽)
        sep_label = QLabel("\u2500" * 50)
        sep_label.setAlignment(Qt.AlignCenter)
        status_layout.addWidget(sep_label, row, 0, 1, 3)
        row += 1
        # 剩余时间:仅居中文字,不用进度条
        status_layout.addWidget(QLabel("剩余时间:"), row, 0, Qt.AlignRight)
        status_layout.addWidget(self.time_remaining_label, row, 1, 1, 2,
                                 Qt.AlignLeft)
        row += 1
        # 磁盘剩余空间(label 写在进度条区域跨两列)
        status_layout.addWidget(QLabel("磁盘剩余:"), row, 0, Qt.AlignRight)
        status_layout.addWidget(self.disk_free_label, row, 1, 1, 2,
                                 Qt.AlignLeft)
        row += 1

        status_group.setLayout(status_layout)
        main_layout.addWidget(status_group)

        # 创建独立的2D/3D窗口
        self.canvas2d_window = QMainWindow()
        self.canvas2d = Canvas2D()
        self.canvas2d_window.setCentralWidget(self.canvas2d)
        self.canvas2d_window.setWindowTitle("2D显卡测试")
        self.canvas2d_window.resize(400, 400)

        self.canvas3d_window = QMainWindow()
        # ★ 惰性 GL(2026-10-26 根因修复"glmark2黑屏+3D色块回归"):
        # 旧版在 __init__ 就无条件创建 Canvas3D(QGLWidget)——QGLWidget 构造
        # 即创建 GL 上下文,老化 GUI 进程从启动起就一直持有 GL 上下文。
        # 目标机显卡驱动(兆芯集成显卡/老 Mesa)对"跨进程并发 GL 上下文"
        # 支持很差:GUI 已持上下文时,glmark2 进程的上下文被饿死 → 黑屏;
        # 同时 3D 画布上下文被打扰 → 色块回归。跑分程序不持 GL 上下文,
        # 所以跑分里 glmark2 能正常出画面。
        # 修复:GL 画布延迟到 start_aging 且勾选 3D 时才创建;勾选 glmark2 时
        # glmark2 独占 GPU(其本身即专业 3D 满载,无需画布并行),与跑分
        # 完全同环境。停止老化后立即销毁画布,彻底释放 GL 上下文。
        self.canvas3d = None
        self.complex_scene = None
        self.canvas3d_window.setCentralWidget(QLabel(
            "3D 老化画布将在开始测试时按需创建\n"
            "（未安装 PyOpenGL/QtOpenGL 时 3D 老化不可用，其它项不受影响）"))
        self.canvas3d_window.setWindowTitle("3D显卡测试")
        self.canvas3d_window.resize(400, 400)

        # glmark2 为外部独立进程(由 glmark_worker 拉起并自绘 X 窗口),
        # 无需在 GUI 内创建窗口/画布;缺失或不可用由 worker 记录错误并继续

        # 场景切换定时器(parent=self,AgingTestGUI 关闭时随其析构,避免悬空引用)
        self.scene_switch_timer = QTimer(self)
        self.scene_switch_timer.timeout.connect(self.switch_scene)
        self.current_scene = "simple"  # simple or complex

        # 连接信号与槽
        self.start_btn.clicked.connect(self.start_aging)
        self.stop_btn.clicked.connect(self.stop_aging)

    def _gpu_monitor_loop(self):
        """显存监控线程:每秒采样一次真实显存占用(常驻,与主界面数据源一致)"""
        while not self._gpu_monitor_stop.is_set():
            try:
                self._gpu_mem = _query_gpu_memory()
            except Exception:
                pass
            # wait(1.0) 而不是 sleep:窗口关闭时可立即退出
            self._gpu_monitor_stop.wait(1.0)

    def start_aging(self):
        # 防重入:正在运行或正在停止时直接忽略
        if self.running or self._stopping:
            return

        # 验证输入
        try:
            self.aging_duration = int(self.duration_edit.text()) * 3600  # 小时转换为秒
            if self.aging_duration <= 0:
                raise ValueError
        except ValueError:
            QMessageBox.warning(self, "错误", "请输入有效的老化时长(正整数)")
            return

        # 检查至少选择一个硬件
        # (场景切换定时器必须在所有校验通过之后才启动 —— 旧版放在校验前,
        #  校验失败 return 后定时器仍在跑,60 秒后会误触发 switch_scene)
        if not (self.cpu_check.isChecked() or self.memory_check.isChecked() or
                self.disk_check.isChecked() or self.gpu2d_check.isChecked() or
                self.gpu3d_check.isChecked()):
            QMessageBox.warning(self, "错误", "请至少选择一个硬件进行老化测试")
            return

        # 启动场景切换定时器 (1分钟后切换到复杂场景)
        self.scene_switch_timer.start(60000)  # 60秒 = 1分钟

        # 初始化状态
        self.running = True
        self.start_time = time.time()
        self._load_check_start = time.time()  # 满载校验计时起点（30 秒后开始第一次校验）
        self.load_progress = {k: 0.0 for k in self.load_progress}
        self.load_errors = {k: '' for k in self.load_errors}
        self._disk_status_msg = ''
        self.start_btn.setEnabled(False)
        self.stop_btn.setEnabled(True)
        self.timer.start(1000)  # 每秒更新一次状态

        # ---- 显存压力池目标:按检测到的真实总显存 ~30% 分配 ----
        # (惰性 GL 改造后画布按需创建,目标值先存起来,
        #  由 _on_canvas_ctl 在画布创建时应用)
        try:
            _used_b, total_b, _src = self._gpu_mem
            if total_b and total_b > 0:
                vram_target = int(total_b * 0.30)
                vram_target = max(64 * 1024 * 1024,
                                  min(512 * 1024 * 1024, vram_target))
            else:
                vram_target = 128 * 1024 * 1024
        except Exception:
            vram_target = 128 * 1024 * 1024
        self._vram_target = vram_target

        # 启动老化线程(全部 daemon=True:即使个别 worker 没能及时退出,
        # 也绝不让进程"关不掉" —— 旧版非 daemon 线程会导致退出时挂起)
        if self.cpu_check.isChecked():
            cpu_thread = threading.Thread(target=self.cpu_load, daemon=True)
            self.threads.append(cpu_thread)
            cpu_thread.start()

        if self.memory_check.isChecked():
            memory_thread = threading.Thread(target=self.memory_load, daemon=True)
            self.threads.append(memory_thread)
            memory_thread.start()

        if self.disk_check.isChecked():
            disk_thread = threading.Thread(target=self.disk_load, daemon=True)
            self.threads.append(disk_thread)
            disk_thread.start()

        if self.gpu2d_check.isChecked():
            try:
                self.canvas2d_window.show()
                self.canvas2d.start_animation()
            except Exception as e:
                print(f"2D窗口初始化错误: {e}")
                QMessageBox.warning(self, "错误", f"2D窗口初始化失败: {str(e)}")

        if self.network_check.isChecked():
            network_thread = threading.Thread(target=self.network_test, daemon=True)
            self.threads.append(network_thread)
            network_thread.start()

        gpu3d_on = self.gpu3d_check.isChecked()
        if gpu3d_on:
            # ★ 3D+glmark2 合并交替模式(2026-10-26,用户方案):
            # 勾选 3D 时:内置 3D 画布先跑一段 → 完全释放 GL 上下文 →
            # glmark2 跑完整一轮(全部场景+Score) → 再回到画布,循环往复。
            # 两者永远不同时持有 GL 上下文,彻底规避目标机驱动的
            # 跨进程并发 GL 冲突(glmark2 黑屏 + 3D 色块的根因)。
            # 系统无 glmark2 时自动退化为纯画布模式(原 3D 行为)。
            # 修复（D5）：3D 依赖缺失时明确报错，不用占位窗口假装跑 3D
            if not HAS_OPENGL:
                QMessageBox.warning(
                    self, "3D 老化不可用",
                    "未安装 PyOpenGL / QtOpenGL，无法进行 3D 显卡老化。\n"
                    "请安装: sudo apt install python3-opengl python3-pyqt5.qtopengl\n"
                    "（或使用 deb 包内 python_modules/apt_packages 离线安装）\n"
                    "其它老化项不受影响，可取消勾选 3D 后继续。")
                return
            # 合并模式无论 glmark2 复选框是否勾选都会交替运行 glmark2
            # 注意:提示语绝不能放进 load_errors —— 状态栏对 load_errors
            # 非空一律显示"失败: ...",会把正常运行的 glmark2 永久显示为
            # 失败,且遮蔽真实的 FPS/Score(2026-10-26 现场误判根因之一)
            self.load_progress['glmark'] = 0.0
            self._glmark_score = 0.0
            self._glmark_state = '3D+glmark2 交替模式:准备中...'
            self.load_errors['glmark'] = ''
            merged_thread = threading.Thread(
                target=self._gpu3d_merged_worker, daemon=True)
            self.threads.append(merged_thread)
            merged_thread.start()

    def stop_aging(self):
        # ===================== 异步停止(核心修复) =====================
        # 旧版问题:stop_aging 在 **UI 线程** 里串行 join 所有 worker
        # (每个 timeout=5s,最坏 20s+)再 sleep(0.5) 再弹 QMessageBox,
        # 期间 Qt 事件循环完全被阻塞 → 界面"未响应" → 系统弹
        # "强制停止 / 等待" 对话框(用户反馈的"停止老化概率性卡住")。
        # 磁盘满载时尤其明显:worker 一轮 32MB 顺序写 + fsync,
        # fsync 不可中断,慢盘上单个 worker 就能卡住 UI 数秒。
        #
        # 新设计:UI 线程只做毫秒级的快速操作(设标志/停定时器/停动画/
        # 隐藏窗口/按钮置灰),所有耗时的 join/清理全部移到后台 stopper
        # 线程;完成后通过 stopFinished 信号(自动跨线程排队)回 UI 线程
        # 复位界面并弹"已停止"提示 —— 点击停止后界面全程保持响应。
        #
        # 防重入:正在停止 / 未在运行 → 直接返回
        if self._stopping or not self.running:
            return
        self._stopping = True
        try:
            self._stop_aging_impl()
        except RuntimeError as e:
            # widget 已 delete 的错误 - 静默处理,让停止流程完整走完
            if 'deleted' in str(e) or 'has been' in str(e) or 'wrapped C' in str(e):
                pass
            else:
                raise
        except Exception as e:
            # 任何其他异常都静默(报错仅供排查)
            try:
                import traceback
                print(f'[stop_aging] silenced: {type(e).__name__}: {e}')
                traceback.print_exc(limit=2)
            except Exception:
                pass
        # ---- 兜底:无论 impl 是否中途抛异常,stopper 线程必须被拉起 ----
        # (否则 _stopping 永远为 True,界面卡在"正在停止...")
        if not getattr(self, '_stop_worker_started', False):
            self._stop_worker_started = True
            try:
                stopper = threading.Thread(target=self._stop_worker, daemon=True)
                stopper.start()
            except Exception:
                pass

    def _stop_aging_impl(self):
        # ---- 以下全部是"毫秒级"操作,可以安全地在 UI 线程执行 ----

        # 1) 设置全局停止标志 - 所有 worker 立即可见,自然退出
        self.running = False

        # 2) 立即禁用两个按钮 + 按钮文案反馈"正在停止"
        try:
            self.stop_btn.setEnabled(False)
            self.start_btn.setEnabled(False)
            self.start_btn.setText("正在停止...")
        except Exception:
            pass

        # 3) 停止 scene 切换定时器(防止 60 秒后切场景)
        try:
            self.scene_switch_timer.stop()
        except Exception:
            pass

        # 4) 立即停止 2D/3D 动画(毫秒级;显存压力池在各自
        #    stop_animation 内部释放)
        for canvas_attr in ('canvas2d', 'canvas3d', 'complex_scene'):
            try:
                canvas = getattr(self, canvas_attr, None)
                if canvas is not None:
                    try:
                        canvas.stop_animation()
                    except (RuntimeError, AttributeError):
                        # widget 已 delete 或无 stop_animation(占位 QLabel)
                        pass
            except Exception:
                pass

        # 4.5) 立即 terminate glmark2 子进程(毫秒级 SIGTERM;
        #      worker 线程看到进程退出后自然返回,不留孤儿进程)
        try:
            proc = getattr(self, '_glmark_proc', None)
            if proc is not None and proc.poll() is None:
                proc.terminate()
        except Exception:
            pass

        # 5) 隐藏 2D/3D 窗口。2D 画布是普通 QWidget,保留引用直接重用;
        #    ★ 3D 画布是 QGLWidget —— 必须在停止时彻底销毁并释放 GL 上下文
        #    (惰性 GL 修复的一部分:测试结束后 GUI 进程不得再持有 GL 上下文,
        #     否则目标机驱动上会干扰后续 glmark2/其它 GL 程序)
        for window_attr in ('canvas2d_window', 'canvas3d_window'):
            try:
                window = getattr(self, window_attr, None)
                if window is not None:
                    try:
                        window.hide()   # 隐藏(用户立即看到窗口消失)
                    except Exception:
                        pass
            except Exception:
                pass
        try:
            self._safe_dispose_canvas(self.canvas3d)
            self._safe_dispose_canvas(self.complex_scene)
        except Exception:
            pass
        self.canvas3d = None
        self.complex_scene = None
        try:
            self.canvas3d_window.setCentralWidget(QLabel(
                "3D 老化画布将在开始测试时按需创建"))
        except Exception:
            pass

        # 6) 立即刷新事件循环,让 hide()/按钮变化马上上屏
        try:
            from PyQt5.QtWidgets import QApplication
            QApplication.processEvents()
        except Exception:
            pass

        # 7) 耗时部分(join worker 等)全部交给后台 stopper 线程
        self._stop_worker_started = True
        stopper = threading.Thread(target=self._stop_worker, daemon=True)
        stopper.start()

    def _stop_worker(self):
        """后台停止线程:等待所有 worker 有界退出,绝阻塞 UI 线程"""
        try:
            # CPU worker:看到 running=False 后 ~2s 内退出(内部还有
            # 子进程 terminate 链);磁盘 worker 跳过 fsync 后一轮内退出;
            # 网络 worker kill 子进程后立即退出 —— 总预算 12s,超时放弃等待
            # (线程全是 daemon,进程退出时自动回收,不会挂住程序)
            deadline = time.time() + 12
            for thread in list(self.threads):
                try:
                    thread.join(timeout=max(0.1, deadline - time.time()))
                except Exception:
                    pass
            try:
                self.threads.clear()
            except Exception:
                pass
        except Exception:
            pass
        # 通知 UI 线程收尾(信号跨线程自动排队,线程安全)
        try:
            self.stopFinished.emit()
        except Exception:
            pass

    def _on_stop_finished(self):
        """UI 线程收尾:复位状态/按钮/进度条,弹完成提示(由信号触发)"""
        self.aging_duration = 0
        self.start_time = 0
        self._stopping = False
        self._stop_worker_started = False
        self._disk_status_msg = ''
        # 清掉残留的磁盘速率采样,避免停止后进度条仍显示旧速率
        try:
            self.load_progress['disk'] = 0.0
        except Exception:
            pass
        try:
            self.time_remaining_label.setText("00:00:00")
            self.cpu_detail_label.setText("--")
            self.memory_detail_label.setText("--")
            self.disk_detail_label.setText("--")
            self.gpu_detail_label.setText("--")
            self.glmark_detail_label.setText("--")
            self.disk_usage.setFormat("-- MB/s")
            self.disk_usage.setValue(0)
            self.cpu_usage.setValue(0)
            self.memory_usage.setValue(0)
            self.gpu_mem_usage.setValue(0)
        except Exception:
            pass

        # 恢复"开始老化"按钮
        try:
            self.start_btn.setText("开始老化")
            self.start_btn.setEnabled(True)
            self.stop_btn.setEnabled(False)
        except Exception:
            pass

        try:
            QMessageBox.information(self, "完成", "老化测试已停止")
        except Exception:
            pass

    def closeEvent(self, event):
        # 关闭主窗口时若老化仍在跑:触发异步停止。
        # 所有 worker 都是 daemon 线程、显存监控线程也是 daemon,
        # 即使个别 worker 没来得及退出,进程退出时也会被系统回收,
        # 不会出现"窗口关了进程还挂着"的问题。
        try:
            if self.running:
                self.running = False
        except Exception:
            pass
        try:
            self._gpu_monitor_stop.set()
        except Exception:
            pass
        try:
            self.timer.stop()
            self.scene_switch_timer.stop()
        except Exception:
            pass
        # 关闭时停 2D/3D 画布动画;glmark2 子进程终止(worker 是 daemon 线程,
        # 进程退出不会自动杀子进程,必须显式 terminate)
        try:
            proc = getattr(self, '_glmark_proc', None)
            if proc is not None and proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=2)
                except Exception:
                    proc.kill()
            self._glmark_proc = None
        except Exception:
            pass
        for canvas_attr in ('canvas2d', 'canvas3d', 'complex_scene'):
            try:
                canvas = getattr(self, canvas_attr, None)
                if canvas is not None:
                    try:
                        canvas.stop_animation()
                    except Exception:
                        pass
            except Exception:
                pass
        # 惰性 GL:退出时彻底销毁 3D 画布,释放 GL 上下文
        try:
            self._safe_dispose_canvas(self.canvas3d)
            self._safe_dispose_canvas(self.complex_scene)
        except Exception:
            pass
        self.canvas3d = None
        self.complex_scene = None
        event.accept()

    def _safe_stop_canvas(self, canvas):
        """停止画布动画（部件可能已被 Qt 删除，全部异常吞掉）"""
        if canvas is None:
            return
        try:
            canvas.stop_animation()
        except Exception:
            pass

    def _safe_dispose_canvas(self, canvas):
        """释放旧画布（对已删除对象调用 deleteLater 会抛异常，安全吞掉）"""
        if canvas is None:
            return
        try:
            canvas.stop_animation()
        except Exception:
            pass
        try:
            canvas.deleteLater()
        except Exception:
            pass

    def switch_scene(self):
        """切换3D场景

        修复（A5②）：旧实现复用曾被 setCentralWidget 替换下的旧部件引用
        （QMainWindow 接管 centralWidget 所有权后，旧引用随时可能已删除），
        60~120 秒场景切换时对已删除 C++ 对象调用 → 崩溃。
        现改为【每次无条件新建】，被替换的旧部件显式 deleteLater，
        引用立即置 None，彻底杜绝悬空引用。
        """
        try:
            # 安全检查窗口有效性
            if not hasattr(self, 'canvas3d_window') or self.canvas3d_window is None:
                return
            if not self.canvas3d_window.isVisible():
                return
            if self.canvas3d is None and self.complex_scene is None:
                return  # 惰性 GL:画布未创建(glmark2 独占模式/未开始测试)
            if not HAS_OPENGL:
                return  # 3D 依赖缺失时不做切换（start_aging 已拦截 3D 启动）

            if self.current_scene == "simple":
                # 切换到复杂场景：旧 canvas3d 显式释放，新建 complex_scene
                if not HAS_COMPLEX_SCENE or ComplexScene is None:
                    return  # 缺 complex_scene 模块则保持当前场景
                self._safe_dispose_canvas(self.canvas3d)
                self.canvas3d = None
                self.complex_scene = ComplexScene()
                self.canvas3d_window.setCentralWidget(self.complex_scene)
                self.complex_scene.start_animation()
                self.current_scene = "complex"
            else:
                # 切换回简单场景：旧 complex_scene 显式释放，新建 canvas3d
                self._safe_dispose_canvas(self.complex_scene)
                self.complex_scene = None
                self.canvas3d = Canvas3D()
                self.canvas3d_window.setCentralWidget(self.canvas3d)
                self.canvas3d.start_animation()
                self.current_scene = "simple"
        except Exception as e:
            print(f"场景切换错误: {e}")
            import traceback
            traceback.print_exc()
            # 恢复默认状态
            self.current_scene = "simple"

    def update_status(self):
        # ---- 防御性编程:此函数由 QTimer 每秒调用,Qt 底层 widget 可能已被
        # deleteLater()(如 switch_canvas_to_complex_scene 时),此时任何方法调用
        # 都会抛 RuntimeError "wrapped C/C++ object has been deleted",
        # 在 QTimer 回调栈抛出会杀掉 Python 进程 → "应用程序 已意外关闭"。
        # 历史 bug:之前只有少部分代码 try/except,实际 widget 被 delete 后
        # cpu_usage.setValue / memory_usage.setValue / cpu_detail_label.setText
        # 也会触发 RuntimeError 杀掉进程。  修复:整个函数 try/except 包裹,
        # 任何 widget 失效都让定时器下一轮重试,绝不抛出。
        try:
            self._update_status_impl()
        except RuntimeError as e:
            # RuntimeError 是 widget 被 delete 后的典型错误
            # 不重启进程,而是停止定时器(避免循环崩溃)
            if 'deleted' in str(e) or 'has been' in str(e) or \
               'wrapped C' in str(e) or isinstance(e, RuntimeError):
                try:
                    self.timer.stop()
                except Exception:
                    pass
                return
            # 其它未知 RuntimeError 仍然 raise(让上层处理)
            raise
        except Exception as e:
            # 任何其它异常(网卡查询、磁盘查询等) 全部静音
            try:
                import traceback
                print(f"[update_status silenced] {type(e).__name__}: {e}")
                traceback.print_exc(limit=3)
            except Exception:
                pass

    def _update_status_impl(self):
        # 实际的 UI 更新逻辑(被 update_status 的 try/except 保护)
        # 更新系统状态
        # 修复（D5）：psutil 缺失时显示占位值而不是抛 ImportError
        if HAS_PSUTIL:
            cpu_pct = psutil.cpu_percent()
            mem_pct = psutil.virtual_memory().percent
        else:
            cpu_pct = mem_pct = 0.0
        try:
            self.cpu_usage.setValue(int(cpu_pct))
            self.memory_usage.setValue(int(mem_pct))
            # ---- 详情列 ----
            self.cpu_detail_label.setText(f"{cpu_pct:.1f}%")
            self.memory_detail_label.setText(f"{mem_pct:.1f}%")
        except (RuntimeError, AttributeError):
            # widget 在更新过程中被 deleteLater → 抛 RuntimeError
            raise RuntimeError(
                f"widget deleted: cpu_usage/label in setValue/setText")

        # 磁盘 IO 占用率:基于瞬时写速率,基准 1000 MB/s 视为满载
        # 之前这里用 disk_usage('/').percent 是磁盘容量占用率,跟"老化跑磁盘"
        # 完全没关系,所以拉不上去 —— 现在改用磁盘 IO 速率
        # ---- 关键修复:即使 load_progress['disk'] 暂时 0,也会立即显示"准备中" ----
        # 解决用户截图里"0.0 MB/s"的死气沉沉问题
        try:
            disk_io_mb = float(self.load_progress.get('disk', 0.0))
        except Exception:
            disk_io_mb = 0.0
        # 进度条:显示百分比(基于 1000 MB/s 满载)
        disk_io_pct = min(99, max(0, int((disk_io_mb / 1000.0) * 100)))
        try:
            self.disk_usage.setValue(disk_io_pct)
            # 进度条上的文字:表示 IO 速率,符合"百分比 + 数值"双重显示
            if disk_io_mb >= 1024.0:
                self.disk_usage.setFormat(f"{disk_io_mb/1024.0:.2f} GB/s")
            elif disk_io_mb > 0.01:
                self.disk_usage.setFormat(f"{disk_io_mb:.1f} MB/s")
            else:
                # worker 还没写出第一字节时显示准备中
                try:
                    if self.running and self.disk_check.isChecked():
                        self.disk_usage.setFormat("准备中...")
                    else:
                        self.disk_usage.setFormat("-- MB/s")
                except Exception:
                    self.disk_usage.setFormat("-- MB/s")
            # 详情列右侧:写更精确的 IO 速率
            if disk_io_mb > 0.01:
                if disk_io_mb >= 1024.0:
                    self.disk_detail_label.setText(
                        f"{disk_io_mb/1024.0:.2f} GB/s 写")
                else:
                    self.disk_detail_label.setText(f"{disk_io_mb:.1f} MB/s 写")
            else:
                # 磁盘 worker 的状态消息由 worker 线程写入 _disk_status_msg
                # (纯 Python 属性,GIL 安全),UI 线程在此渲染
                try:
                    self.disk_detail_label.setText(
                        self._disk_status_msg or "等待写盘...")
                except Exception:
                    self.disk_detail_label.setText("等待写盘...")
            # 磁盘剩余
            try:
                if HAS_PSUTIL:
                    du = psutil.disk_usage(os.path.expanduser('~'))
                    free_gb = du.free / (1024.0 * 1024.0 * 1024.0)
                    self.disk_free_label.setText(f"剩余 {free_gb:.1f} GB")
            except Exception:
                pass
        except (RuntimeError, AttributeError):
            raise RuntimeError("widget deleted in disk_usage block")

        # 显存使用率(真实检测):由常驻 GPU 监控线程每秒采样,
        # 数据源与 C++ 主界面 SystemMonitor 完全一致(nvidia-smi →
        # DRM mem_info_vram → DRM mem_info_gtt) → 两边数值一致、实时、准确
        # ---- 旧版为 random.randint(5,20) 伪造随机数,已废弃 ----
        try:
            used_b, total_b, gpu_src = self._gpu_mem
        except Exception:
            used_b, total_b, gpu_src = 0, 0, ''
        try:
            if total_b > 0 and used_b >= 0:
                gpu_pct = 100.0 * used_b / total_b
                gpu_pct = max(0.0, min(100.0, gpu_pct))
                used_gb = used_b / (1024.0 ** 3)
                total_gb = total_b / (1024.0 ** 3)
                self.gpu_mem_usage.setValue(int(gpu_pct))
                self.gpu_detail_label.setText(
                    f"{gpu_pct:.1f}% ({used_gb:.2f}/{total_gb:.2f}GB, {gpu_src})")
            else:
                # 检测不到显存节点(如无驱动的集显):如实显示,不伪造
                self.gpu_mem_usage.setValue(0)
                self.gpu_detail_label.setText("未检测到显存节点")
        except (RuntimeError, AttributeError):
            raise RuntimeError("widget deleted in gpu_mem_usage block")
        except Exception:
            pass

        # ---- glmark2 压测状态行(读纯 Python 属性,GIL 安全,跨线程无竞态) ----
        # glmark2 已合并进 3D 测试(勾选 3D 即交替运行),不再有独立复选框;
        # 运行时即显示其状态(合并 worker 内部会更新 load_progress['glmark'])
        try:
            if not self.running:
                glmark_txt = "--"
            elif self.load_errors.get('glmark', ''):
                glmark_txt = "失败: " + self.load_errors['glmark'].splitlines()[0]
            else:
                fps = self.load_progress.get('glmark', 0.0)
                score = getattr(self, '_glmark_score', 0.0)
                state = getattr(self, '_glmark_state', '')
                if fps > 0:
                    glmark_txt = f"{fps:.1f} fps"
                    if score > 0:
                        glmark_txt += f" | Score {score:.0f}"
                elif state:
                    glmark_txt = state
                else:
                    glmark_txt = "准备中..."
            self.glmark_detail_label.setText(glmark_txt)
        except (RuntimeError, AttributeError):
            raise RuntimeError("widget deleted in glmark_detail_label block")
        except Exception:
            pass

        # 更新负载监控（供满载校验使用）
        try:
            self.load_progress['cpu'] = cpu_pct
            self.load_progress['memory'] = mem_pct
        except Exception:
            pass

        # 更新剩余时间
        try:
            elapsed = time.time() - self.start_time
            remaining = max(0, self.aging_duration - elapsed)
            hours = int(remaining // 3600)
            minutes = int((remaining % 3600) // 60)
            seconds = int(remaining % 60)
            self.time_remaining_label.setText(
                f"{hours:02d}:{minutes:02d}:{seconds:02d}")
        except (RuntimeError, AttributeError):
            raise RuntimeError("widget deleted in time_remaining block")
        except Exception:
            pass   # 其它异常静音

        # ====== 满载校验（30 秒未达阈值 → 报错）======
        if self.running and self._load_check_start is not None:
            try:
                check_elapsed = time.time() - self._load_check_start
                if check_elapsed >= 30:
                    self._check_load_targets()
            except Exception:
                pass   # 满载校验报错不冒到 UI

        # 检查是否超时(只有在老化测试运行时才检查)
        try:
            if self.running and remaining <= 0:
                self.stop_aging()
        except Exception:
            pass

    def _check_load_targets(self):
        """
        满载校验：对每一项检查当前是否达到阈值。
        失败原因分类（精确告诉用户为什么满载跑不起来）:
          - CPU: 占用<85% → 多为多进程创建失败/权限不足
          - 内存: 占用<80% → 系统内存不足/分配受限
          - 磁盘: 速率<100MB/s → 写到了 tmpfs/O_DIRECT 失败/磁盘瓶颈
          - GPU: 帧率<30fps → OpenGL 初始化失败/驱动异常
        记录到 load_errors，对每项**最多报一次错**（避免刷屏）
        """
        # 防御性 try/except:满载校验在 update_status 中被 QTimer 30 秒一次调用
        # 即使 widget 被 deleteLater,我们不能让这个调用让主进程崩
        try:
            return self._check_load_targets_impl()
        except RuntimeError as e:
            if 'deleted' in str(e) or 'has been' in str(e) or 'wrapped C' in str(e):
                try:
                    self.timer.stop()   # 停止定时器,避免循环崩溃
                except Exception:
                    pass
                return
            raise
        except Exception:
            pass   # 满载校验报错静默不显示

    def _check_load_targets_impl(self):
        # 实际校验逻辑(被 _check_load_targets 的 try/except 保护)
        item_to_checkbox = {
            'cpu':     self.cpu_check,
            'memory':  self.memory_check,
            'disk':    self.disk_check,
            'gpu2d':   self.gpu2d_check,
            'gpu3d':   self.gpu3d_check,
            'glmark':  self.glmark_check,
            'network': self.network_check
        }
        for item, target in self.LOAD_TARGETS.items():
            if not item_to_checkbox[item].isChecked():
                continue
            if self.load_errors[item]:  # 已报错则不重复
                continue
            op, threshold, unit = target
            val = self.load_progress.get(item, 0.0)
            if op == '>=' and val < threshold:
                # 推断根因（基于业内常见原因）
                reason_map = {
                    'cpu':     "可能原因：进程创建失败/被 ulimit 限制/CPU 频率被锁低",
                    'memory':  "可能原因：系统物理内存不足/进程被 cgroup 限制/磁盘 swap 关闭",
                    'disk':    "可能原因：测试目录位于 tmpfs(内存盘)/文件系统不支持 O_DIRECT/磁盘已满",
                    'gpu2d':   "可能原因：Qt OpenGL 后端不可用/显卡驱动未安装/X11 服务器缺失",
                    'gpu3d':   "可能原因：GLX 扩展不可用/显卡驱动未安装/EGL 初始化失败",
                    'glmark':  "可能原因：glmark2 未安装/无显示环境(DISPLAY)/OpenGL 驱动缺失——见该项错误详情",
                    'network': "可能原因：网卡未连接/防火墙阻断/DNS 解析失败"
                }
                self.load_errors[item] = (
                    f"[满载失败] {item}: 当前 {val:.1f}{unit}, 需要 {op}{threshold}{unit}\n"
                    f"  {reason_map[item]}"
                )
                # 通知主程序日志（与 Qt 弹窗分开，不打扰用户但保留记录）
                if hasattr(self, 'on_load_error'):
                    try:
                        self.on_load_error(item, self.load_errors[item])
                    except Exception:
                        pass
                else:
                    # 退化为标准错误流（被 launcher 收集）
                    # 关键：必须用 sys.stderr.buffer + UTF-8 强制编码，
                    # 严禁 sys.stderr.write(str) —— 在 macOS launchd console /
                    # Windows legacy console 上 stderr 默认 ascii，
                    # 中文字符会抛 UnicodeEncodeError；这个异常位于 QTimer
                    # 回调栈中且无外层捕获，会直接杀进程（之前崩溃的根因）。
                    try:
                        import sys as _sys
                        _msg = (self.load_errors[item] + '\n').encode(
                            'utf-8', errors='replace')
                        if hasattr(_sys.stderr, 'buffer'):
                            _sys.stderr.buffer.write(_msg)
                        else:
                            _sys.stderr.write(_msg.decode('utf-8', errors='replace'))
                        _sys.stderr.flush()
                    except Exception:
                        # 兜底：绝不让 stderr 错误冒到 QTimer 回调外
                        pass

    def cpu_load(self):
        # 历史 bug:之前的 worker 是 class 内嵌套函数,
        # Windows 上 multiprocessing 用 spawn 模式启动新进程时无法 pickle
        # 嵌套函数 → AttributeError "Can't pickle local object '...<locals>.worker'"
        # → CPU 老化 worker 启动失败 → CPU 占用率永远上不去
        # 修复:用模块级 _cpu_worker(已移到文件顶部),并通过 multiprocessing.Event
        # 控制停止,Event 本身是 multiprocessing 原生类型,可以 pickle
        import multiprocessing
        # ---- 修复（D1）：Linux 默认 fork，在已有多线程(Qt GUI/GPU 监控/其它
        # 老化 worker)的进程内 fork 是 POSIX 未定义行为——fork 瞬间若其它线程
        # 正持有 malloc/Qt 内部锁，子进程直接死锁，CPU 满载项起不来。
        # spawn 模式全新进程导入模块，无死锁面。__main__ 入口处已统一
        # set_start_method('spawn')，此处兜底保证（幂等，重复调用忽略）。
        try:
            multiprocessing.set_start_method('spawn', force=False)
        except RuntimeError:
            pass  # 已经设置过（__main__ 入口处），正常路径
        except ValueError:
            pass  # 非 forkserver 上下文不支持，保持现状
        cpu_count = max(1, multiprocessing.cpu_count())
        processes = []
        # Event 用于跨进程通知 worker 停止(避免"父进程退出但 worker 还在跑"导致残留)
        stop_event = multiprocessing.Event()

        # ---- 关键修复(对齐旧版):持续补充进程 ----
        # 旧版逻辑:每秒检查存活进程数,意外死亡(fork 死锁/OOM/被杀)自动补充。
        # 上一版改成"一次性启动 cpu_count 个"后,任何 worker 意外死亡都不再补充,
        # 核数逐渐减少 → CPU 占用率拉不满。
        # 同时【不再在本线程采集 psutil.cpu_percent(interval=None)】——
        # psutil 的非阻塞 baseline 是进程内全局共享的,cpu_load 线程和
        # update_status(QTimer 每秒)双调用者会互相污染 baseline:
        # update_status 每次测到的只是"距 cpu_load 上次调用几毫秒内"的噪声。
        # 占用率采集职责完全交给 update_status(它每秒一次,间隔正好 1 秒,数据准确)。
        while self.running:
            # 统计存活进程数,死了就补(对齐旧版"持续补充"行为)
            alive = 0
            dead = []
            for p in processes:
                try:
                    if p.is_alive():
                        alive += 1
                    else:
                        dead.append(p)
                except Exception:
                    dead.append(p)
            # 清理已死进程的引用,避免列表无限增长
            for p in dead:
                try:
                    processes.remove(p)
                except Exception:
                    pass
            # 补充到 cpu_count 个
            if alive < cpu_count:
                for _ in range(cpu_count - alive):
                    try:
                        p = multiprocessing.Process(
                            target=_cpu_worker, args=(stop_event,))
                        p.daemon = True   # 主进程死时,worker 自动被强杀
                        p.start()
                        processes.append(p)
                    except Exception:
                        pass
            time.sleep(1)

        # ---- 优雅停止:设 stop_event 让 worker 自然退出(每个 worker 每轮检查一次)
        try:
            stop_event.set()
        except Exception:
            pass
        # 给 worker 1.5s 退出时间,过了就强杀
        deadline = time.time() + 1.5
        for p in processes:
            try:
                remain = max(0.0, deadline - time.time())
                p.join(timeout=remain)
                if p.is_alive():
                    p.terminate()
                    p.join(timeout=0.3)
                    if p.is_alive():
                        p.kill()
            except Exception:
                pass

    def memory_load(self):
        # 修复（D5）：psutil 缺失时线程直接退出并标记错误，不让线程内 ImportError
        if not HAS_PSUTIL or psutil is None:
            self.load_errors['memory'] = 'psutil 未安装，内存老化不可用'
            return
        import gc

        # 获取系统总内存
        total_memory = psutil.virtual_memory().total
        # 只使用80%的内存，留20%给系统
        max_memory_usage = total_memory * 0.8
        current_usage = 0
        memory_blocks = []
        block_size = 1024 * 1024 * 50  # 50MB块

        while self.running:
            # 检查当前内存使用情况
            available_memory = psutil.virtual_memory().available
            # 上报内存占用率
            self.load_progress['memory'] = psutil.virtual_memory().percent

            # 如果可用内存足够，分配新的内存块
            if available_memory > block_size * 2 and current_usage < max_memory_usage:
                try:
                    # 分配内存块
                    block = bytearray(block_size)
                    memory_blocks.append(block)
                    current_usage += block_size
                    time.sleep(0.1)
                except MemoryError:
                    time.sleep(1)
            else:
                # 内存不足时，释放一部分内存
                if len(memory_blocks) > 0:
                    # 随机释放一些内存块
                    release_count = max(1, len(memory_blocks) // 5)
                    for _ in range(release_count):
                        if memory_blocks:
                            del memory_blocks[-1]
                            current_usage -= block_size
                    gc.collect()  # 强制垃圾回收
                time.sleep(1)

    def disk_load(self):
        """
        硬盘满载测试（跨平台稳定版）

        历史 bug 与本版修复:
          旧 bug 1: 写到 /tmp 临时文件（Ubuntu 上 /tmp 是 tmpfs 内存盘）→
                    "写硬盘"实际写内存，磁盘压力为 0
          旧 bug 2: 单文件 + 多线程串行 open → IO 阻塞顺序，多线程无收益
          旧 bug 3: 只 flush() 不 fsync() → 内核缓冲区未真正提交到磁盘
          旧 bug 4: 无任何报错/成功率上报
          旧 bug 5: 用 O_DIRECT —— 在 macOS / Windows / 部分 Linux 文件系统
                    上 os.O_DIRECT 常量根本不存在 → AttributeError 让 worker
                    立即退出，磁盘实测负载为 0；之后满载校验报错经 stderr
                    写中文触发 UnicodeEncodeError 导致 PyQt5 QTimer 回调崩溃。
          旧 bug 6: worker 启动时(makedirs/选盘)需要数秒,期间 UI 一直 0%
                    用户看不出"worker 在跑" —— 现在启动后立即 heartbeat,
                    即使实际写盘还没开始,也让 UI 显示"启动中"状态
        修复策略:
          1) 写到实际物理盘（psutil 找最大可写挂载点，写到 ~/.elsky_aging_test/）
          2) 多线程各写独立文件，避免单文件锁竞争
          3) 普通 buffered write + fsync 强制刷盘（fsync() 调用跨平台一致）
          4) 任何异常全部本地吞掉，绝不让异常冒出 worker（不再使用 O_DIRECT，
             让 macOS / Windows 与 Linux 走同一条代码路径）
          5) 速率上报 + 错误记录到 self.load_errors
          6) 启动后立即 heartbeat —— 即使磁盘跑不起来也告诉用户"已开跑"
        """
        import os

        # ---- 启动 heartbeat:让 UI 立刻看到"磁盘 worker 已起来"----
        # 即使 makedirs 失败,这一行也已经把数字写到了 shared dict,
        # update_status() 拿到的 disk_io_mb 会从 0 变成一个心跳值,
        # UI 上"磁盘 IO 占用率"立刻动起来(虽然数字不大,但是"在动")
        # 注意:本函数运行在 worker 线程,严禁直接调 Qt 控件(旧版跨线程
        # setText 属于未定义行为,是停止时概率性卡死的帮凶)——
        # 状态只写入 _disk_status_msg 纯 Python 属性,由 UI 线程渲染
        try:
            self.load_progress['disk'] = 0.1   # 0.1 MB/s = "启动中" 标记
            self._disk_status_msg = "磁盘 worker 启动中..."
        except Exception:
            pass

        # ---- 选盘:取真实可写的物理挂载点(排除 tmpfs 只读 / 等)----
        actual_disk = os.path.expanduser('~')   # 最终兜底:用户主目录(macOS/Linux 必可写)
        selected_reason = ""   # 用于错误信息
        try:
            best_free = 0
            for part in psutil.disk_partitions(all=False):
                fstype = (part.fstype or '').lower()
                if fstype in ('tmpfs', 'devtmpfs', 'overlay', 'squashfs',
                              'proc', 'sysfs', 'devpts', 'cgroup', 'cgroup2',
                              'fuse.gvfsd-fuse', 'fuse.snapfuse',
                              'iso9660', 'udf', 'cdfs'):
                    continue
                mp = part.mountpoint or ''
                # 只接受"以 / 开头的本地挂载点"
                # (排除 macOS 上 devfs / smbfs / afpfs 等挂载,虽能写但不稳)
                if not mp.startswith('/'):
                    continue
                try:
                    u = psutil.disk_usage(mp)
                except Exception:
                    continue
                if u.free <= 0:
                    continue
                # 取剩余空间最大的挂载点
                if u.free > best_free:
                    # 必须能写(没有权限的挂载点不可用;psutil 不报权限,
                    # 这里用实际创建小文件做探测,失败则跳过)
                    try:
                        probe = os.path.join(mp, '.elsky_probe')
                        with open(probe, 'wb') as f:
                            f.write(b'1')
                        os.remove(probe)
                        best_free = u.free
                        actual_disk = mp
                        selected_reason = (
                            f"{mp} (剩余 {u.free/(1024.0**3):.1f}GB, "
                            f"{part.fstype})")
                    except Exception:
                        continue
        except Exception:
            pass

        # ---- 在目标盘上建测试目录(失败立即退出并报错)----
        test_dir = os.path.join(actual_disk, '.elsky_aging_test')
        try:
            os.makedirs(test_dir, exist_ok=True)
            # ---- 选盘成功后立刻把状态更新为"已选盘"(线程安全方式) ----
            try:
                self._disk_status_msg = f"已选盘:{selected_reason or actual_disk}"
            except Exception:
                pass
        except Exception as e:
            self.load_errors['disk'] = f"无法创建测试目录 {test_dir}: {e}"
            try:
                self._disk_status_msg = f"创建目录失败:{e}"
            except Exception:
                pass
            return

        thread_count = 4
        thread_files = [os.path.join(test_dir, f'aging_{i}.bin')
                        for i in range(thread_count)]

        def disk_worker(file_path, thread_idx):
            block_size = 4 * 1024 * 1024   # 4MB 块
            try:
                data = os.urandom(block_size)
            except Exception:
                # 极端情况下熵池不足时改用零(纯顺写压力,不影响主测目标)
                data = b'\x00' * block_size
            try:
                rand_page = os.urandom(4096)
            except Exception:
                rand_page = b'\x00' * 4096
            loop_count = 0
            total_bytes = 0
            # 顺序写结束后文件长度 = 8 × 4MB = 32MB,随机写 pos 上限 32MB 对齐 4096
            file_blocks = 32 * 1024 * 1024 // 4096   # 8192 个 4KB 块
            try:
                # 速率采样:记录本轮起始时字节数 / 起始时间戳,
                # 每轮结束时计算瞬时速率(MB/s,1MB = 1e6 bytes)
                # 用"最近 1 秒窗口"而不是平均速率,这样压力跑起来后 UI 立刻有反馈
                last_sample_bytes = 0
                last_sample_time = time.time()
                while self.running:
                    # ---- 顺序写 32MB ----
                    fd = None
                    try:
                        try:
                            fd = os.open(file_path,
                                         os.O_WRONLY | os.O_CREAT | os.O_TRUNC,
                                         0o644)
                        except Exception:
                            fd = None
                        if fd is not None:
                            try:
                                for _ in range(8):
                                    if not self.running:
                                        break
                                    try:
                                        os.write(fd, data)
                                        total_bytes += block_size
                                        loop_count += 1
                                    except OSError as e:
                                        # ENOSPC(28)磁盘满 → 立刻终止本线程,
                                        # 其它 worker / 主线程不受影响
                                        if e.errno == 28:
                                            self.load_errors['disk'] = (
                                                f"线程 {thread_idx}: 磁盘已满")
                                            return
                                        # 其它 OSError:跳过本轮,下一轮重试
                                        break
                                # fsync:强制刷盘(跨平台一致;只对成功 open 的 fd)
                                # ---- 关键修复:停止时跳过 fsync ----
                                # fsync(32MB) 在慢盘上可达数秒且不可中断,
                                # 旧版停止时 worker 还在做 fsync → join 超时 →
                                # UI 卡死弹"强制停止/等待"。测试数据本来就是
                                # 垃圾数据,停止时无需落盘。
                                if self.running:
                                    try:
                                        os.fsync(fd)
                                    except Exception:
                                        pass
                            finally:
                                try:
                                    os.close(fd)
                                except Exception:
                                    pass
                    except Exception:
                        pass

                    # ---- 停止时不再进入随机写阶段,直接退出 ----
                    if not self.running:
                        break

                    # ---- 随机写(4KB × 1000 次,pos 在 32MB 文件内 4KB 对齐)----
                    fd = None
                    try:
                        try:
                            fd = os.open(file_path, os.O_WRONLY, 0o644)
                            if fd is not None:
                                for _ in range(1000):
                                    if not self.running:
                                        break
                                    pos = random.randint(0, file_blocks - 1) * 4096
                                    try:
                                        os.lseek(fd, pos, os.SEEK_SET)
                                        os.write(fd, rand_page)
                                        total_bytes += 4096
                                    except OSError:
                                        break
                        except Exception:
                            pass
                    finally:
                        if fd is not None:
                            try:
                                os.close(fd)
                            except Exception:
                                pass

                    # ---- 速率上报(瞬时采样:这次比上次多了多少 / 间隔多少时间)----
                    # 这里由 4 个线程各算各的瞬时速率,主线程取 max,UI 才看得到!
                    try:
                        now = time.time()
                        dt = now - last_sample_time
                        db = total_bytes - last_sample_bytes
                        if dt > 0:
                            # 转 MB/s(1MB = 1e6 bytes,符合工业常见表达)
                            inst_mb_s = (db / dt) / 1e6
                            # 与其它线程的最大值比较(无锁读在 CPython GIL 下安全)
                            try:
                                cur = self.load_progress.get('disk', 0.0) or 0.0
                                if inst_mb_s > cur:
                                    self.load_progress['disk'] = inst_mb_s
                            except Exception:
                                self.load_progress['disk'] = inst_mb_s
                        last_sample_bytes = total_bytes
                        last_sample_time = now
                    except Exception:
                        pass
            except Exception as e:
                # worker 内任何异常全部本地吞掉,绝不让 Thread 杀掉主进程
                try:
                    self.load_errors['disk'] = f"线程 {thread_idx}: {e}"
                except Exception:
                    pass
            finally:
                # ---- 清理本线程文件 ----
                try:
                    if os.path.exists(file_path):
                        os.remove(file_path)
                except Exception:
                    pass

        # ---- 启动 4 个 worker 并等待退出 ----
        threads = []
        for i in range(thread_count):
            try:
                t = threading.Thread(target=disk_worker,
                                     args=(thread_files[i], i),
                                     daemon=True)
                t.start()
                threads.append(t)
            except Exception:
                pass

        # ---- 关键:每个 worker 启动后立即 dispatch 一个 1-byte 写,作为立即可见的写盘 ----
        # 之前:4 个 worker 启动后第一秒什么都没写,等到 os.open + os.write(4MB) 完成,
        #       数秒后才看到第一次 delta,UI 在这数秒一直 0.0 —— 现在先写 1 字节"激活" UI
        # 这种"先发个心跳包"的写法对任何文件系统都无害(无 O_DIRECT 限制)
        try:
            actual_write_marker = False
            for fp in thread_files:
                try:
                    with open(fp, 'wb') as f:
                        f.write(b'\x00')   # 1 byte 标记
                    actual_write_marker = True
                except Exception:
                    pass
            if actual_write_marker:
                # 把心跳值再拉高一些,UI 第一秒就能看到 IO 速率 > 0
                try:
                    self.load_progress['disk'] = 1.5
                    self._disk_status_msg = "IO 心跳:worker 已启动"
                except Exception:
                    pass
        except Exception:
            pass
        # ---- 等待 worker 退出(有界:旧版 t.join() 无超时,worker 若卡在
        # 不可中断的 IO 上会导致本线程永远不退出 → 停止流程永远不结束) ----
        join_deadline = time.time() + 10
        for t in threads:
            try:
                t.join(timeout=max(0.1, join_deadline - time.time()))
            except Exception:
                pass
        # ---- 清理测试目录(失败不影响退出)----
        try:
            for f in thread_files:
                if os.path.exists(f):
                    os.remove(f)
            os.rmdir(test_dir)
        except Exception:
            pass

    @staticmethod
    def _default_gateway():
        """读取默认网关 IP(离线回退 ping 目标);失败返回 None"""
        try:
            import subprocess as _sp
            out = _sp.run(['ip', 'route', 'show', 'default'],
                          capture_output=True, timeout=3).stdout.decode(
                              'utf-8', 'ignore')
            # 格式: default via 192.168.1.1 dev eth0
            if ' via ' in out:
                return out.split(' via ')[1].split()[0]
        except Exception:
            pass
        return None

    def network_test(self):
        # ---- 网络老化(可中断版) ----
        # 旧版用 os.system() 阻塞执行 ping/wget:ping 超时 ~10s、wget 慢链路上
        # 可达数十秒,期间线程完全不可中断 → 停止时 join(5) 永远等不到,
        # 拖慢整个停止流程。改用 Popen + 轮询:检测到 self.running=False
        # 立即 kill 子进程,保证"点击停止立即停"。
        import subprocess

        def run_bounded(cmd, hard_timeout):
            """运行外部命令;老化停止或硬超时时 kill 子进程。返回退出码或 None"""
            try:
                proc = subprocess.Popen(
                    cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            except Exception:
                return None   # 命令不存在(如 Windows 无 wget),与旧版行为一致
            start = time.time()
            while proc.poll() is None:
                if not self.running:
                    try:
                        proc.kill()
                    except Exception:
                        pass
                    return None
                if time.time() - start > hard_timeout:
                    try:
                        proc.kill()
                    except Exception:
                        pass
                    return None
                time.sleep(0.2)
            return proc.returncode

        while self.running:
            # 连通性(ping 单包超时 2s,整体硬超时 8s)
            # 修复（D4）：老化机房常为离线环境,外网 8.8.8.8 不可达会让网络
            # 老化项永远失败。先 ping 外网,失败则回退 ping 默认网关(内网),
            # 离线机器也能真实执行网络收发老化;两者都不通才判定失败。
            wan_ok = run_bounded(['ping', '-c', '1', '-W', '2', '8.8.8.8'], 8) == 0
            gateway = None
            lan_ok = False
            if not wan_ok:
                gateway = self._default_gateway()
                if gateway:
                    lan_ok = run_bounded(['ping', '-c', '1', '-W', '2', gateway], 8) == 0
            if wan_ok or lan_ok:
                try:
                    self.load_progress['network'] = max(
                        self.load_progress.get('network', 0.0), 1.0)
                except Exception:
                    pass
            if not self.running:
                break
            # 网络收发压测(停止时立即 kill)
            # 修复: 离线机房 wget 公网测速站(希腊 otenet)必挂满 90s 超时且
            # 零产出——仅外网可达时才走公网下载测速;离线内网回退为对网关的
            # 大包 ping(-s 65000×10),同样产生真实网络收发负载,老化语义不变,
            # 且消除每轮 90 秒空等。
            if wan_ok:
                # 公网下载测速(1MB 测试文件)
                t0 = time.time()
                rc = run_bounded(['wget', '-O', '/dev/null',
                                  'http://speedtest.ftp.otenet.gr/files/test1Mb.db'], 90)
                if rc == 0:
                    dt = time.time() - t0
                    if dt > 0:
                        try:
                            # 1MB = 1024KB → KB/s,供满载校验使用
                            self.load_progress['network'] = max(
                                self.load_progress.get('network', 0.0),
                                1024.0 / dt)
                        except Exception:
                            pass
            elif gateway:
                # 离线内网回退: 对网关发 10 个 65KB 大包产生真实收发负载
                t0 = time.time()
                rc = run_bounded(['ping', '-s', '65000', '-c', '10', '-W', '2',
                                  gateway], 30)
                if rc == 0:
                    dt = time.time() - t0
                    if dt > 0:
                        try:
                            # 10 包 × 65KB(单向) × 2(收发) / 耗时秒 ≈ KB/s
                            self.load_progress['network'] = max(
                                self.load_progress.get('network', 0.0),
                                10.0 * 65.0 * 2.0 / dt)
                        except Exception:
                            pass
            # 间隔等待(可中断,0.5s 粒度检查停止标志)
            for _ in range(10):
                if not self.running:
                    break
                time.sleep(0.5)

    def _find_glmark_binary(self):
        """查找 glmark2 可执行文件候选列表（按当前会话类型排序）。

        包内路径约定(打包脚本负责解包):
            <应用目录>/extra-bin/glmark2/bin/glmark2*
        覆盖各发行版变体名:glmark2-es2 / glmark2 / glmark2-es2-wayland 等。

        排序策略:
        - X11 会话(有 DISPLAY): 优先 X11 变体(glmark2-es2 / glmark2)
        - Wayland 会话(无 DISPLAY): 优先 wayland 变体, X11 变体兜底
          (Xwayland 存在时 X11 变体也能跑)
        - 返回全部可用候选(去重),由 glmark_worker 逐个尝试,
          某个变体启动失败(如无 Xwayland)自动换下一个
        """
        import shutil
        app_dir = os.path.dirname(os.path.abspath(__file__))

        # 按会话类型确定变体优先级(与跑分测试 testGpu 一致:glmark2-es2 优先)
        has_x = bool(os.environ.get('DISPLAY'))
        is_wayland = os.environ.get('XDG_SESSION_TYPE', '') == 'wayland'
        if not has_x and is_wayland:
            names = ('glmark2-es2-wayland', 'glmark2-wayland',
                     'glmark2-es2', 'glmark2')
        else:
            names = ('glmark2-es2', 'glmark2',
                     'glmark2-es2-wayland', 'glmark2-wayland')

        candidates = []
        # 1) 系统 PATH 优先:与跑分测试 testGpu 的 whichTool 行为一致。
        #    实测跑分用系统 PATH 的 glmark2-es2 能正常出画面,故优先复用
        #    系统已验证可用的二进制(避免包内自编译变体在个别驱动上渲染异常)
        for name in names:
            p = shutil.which(name)
            if p:
                candidates.append(p)
        # 2) 应用包内(离线兜底):打包脚本从源码编译并整包打入 glmark2,
        #    目标机无系统 glmark2 时(离线/Kylin/统信)由此提供(见 cross_platform_package_deb.sh)
        for sub in ('extra-bin/glmark2/bin', 'extra-bin/glmark2/usr/bin'):
            d = os.path.join(app_dir, sub)
            if os.path.isdir(d):
                for name in names:
                    p = os.path.join(d, name)
                    if os.path.isfile(p) and os.access(p, os.X_OK):
                        candidates.append(p)
        # 去重(保持顺序)
        seen = set()
        return [c for c in candidates if not (c in seen or seen.add(c))]

    def glmark_worker(self):
        """glmark2 独占循环 worker(仅勾 glmark2、未勾 3D 时启动)。

        行为(与跑分测试 testGpu 同一调用链路,确保出画面与效果一致):
        - 找不到 glmark2 → 记录 load_errors['glmark'],安静返回(不阻塞其它项)
        - 循环运行 glmark2 基准:每轮跑完全部场景(约 2~5 分钟)后自动重启,
          老化期间 GPU 持续满载;实时解析各场景 FPS / 最终 Score
        - self.running=False 时立即退出,子进程由 stop_aging/closeEvent
          毫秒级 terminate
        """
        import subprocess as _sp
        candidates = self._find_glmark_binary()
        if not candidates:
            self.load_errors['glmark'] = (
                "[glmark2缺失] 包内与系统均未找到 glmark2 可执行文件\n"
                "  安装: sudo apt install glmark2  (或用本 deb 包内 extra-bin 离线版)\n"
                "  其它老化项不受影响,继续运行")
            return
        failed_variants = []
        while self.running:
            ok = self._run_glmark_round(candidates, failed_variants)
            if not ok:
                break   # 全部候选失败,错误已记录并弹窗
            if not self.running:
                break
            import time as _t
            _t.sleep(1)   # 轮次间短暂间隔

    def _glmark_app_root(self):
        """定位打包根目录(含 lib/ 与 python_modules/),用于净化 glmark2 的 LD_LIBRARY_PATH。

        打包启动器(cross_platform_package_deb.sh:1223)会把包内 lib/ 注入
        LD_LIBRARY_PATH 并传给本进程;glmark2 子进程继承后又会错用 Qt 自带的
        mesa/GL 库(与系统显卡驱动不匹配)→ 窗口打开但全黑。剥离这些包内目录,
        让 glmark2 用系统 GL 库即可修复(与跑分测试 testGpu 行为一致)。
        """
        d = os.path.dirname(os.path.abspath(__file__))
        for _ in range(6):
            if (os.path.isdir(os.path.join(d, 'lib'))
                    or os.path.isdir(os.path.join(d, 'python_modules'))):
                return d
            p = os.path.dirname(d)
            if p == d:
                break
            d = p
        return os.path.dirname(os.path.abspath(__file__))

    def _glmark_data_ok(self, data_dir):
        """检查 glmark2 场景数据目录是否完整(shaders/models/textures 三者齐备)。

        数据不完整时 glmark2 照常开窗、照常输出 FPS,但所有场景加载失败 →
        窗口全黑(飞腾 D2000 现场实测:28MB 流量...实为 /usr/share/glmark2 丢失)。

        [2026-11-06 加固] 只查"目录存在且非空"会把半残目录误判为完整:
        现场 D3000M 出现 shaders 目录在、但 light-basic.vert / desktop.vert
        缺失 → 所有场景 Set up failed 仍全黑。故追加 shader 文件数量校验。
        """
        if not data_dir or not os.path.isdir(data_dir):
            return False
        for sub in ('shaders', 'models', 'textures'):
            p = os.path.join(data_dir, sub)
            if not os.path.isdir(p):
                return False
            try:
                if not os.listdir(p):
                    return False
            except Exception:
                return False
        # 至少要有 5 个顶点着色器文件才认为 shaders 是可用的完整集合
        try:
            verts = [n for n in os.listdir(os.path.join(data_dir, 'shaders'))
                     if n.endswith('.vert')]
            if len(verts) < 5:
                return False
        except Exception:
            return False
        return True

    def _glmark_data_dirs(self):
        """返回按优先级排序的 glmark2 场景数据目录候选(存在与否都列出,由调用方检查)。"""
        app_dir = os.path.dirname(os.path.abspath(__file__))
        return [
            # 包内离线版数据(打包脚本保证二进制与数据同目录成套)
            os.path.join(app_dir, 'extra-bin/glmark2/usr/share/glmark2'),
            os.path.join(app_dir, 'extra-bin/glmark2/share/glmark2'),
            # 系统标准路径(apt 版 / 源码 --prefix=/usr 版;postinst 也会补齐到这里)
            '/usr/share/glmark2',
            # 手工 make install 默认前缀(历史部署残留)
            '/usr/local/share/glmark2',
        ]

    def _glmark_supports_data_path(self, bin_path):
        """探测二进制是否支持 --data-path 参数(探测结果按路径缓存)。

        glmark2 官方(2014.03~2023.01)不读 GLMARK2_DATA_PATH 环境变量,数据路径
        只认编译期硬编码的 /usr/share/glmark2 和 --data-path 命令行参数;
        老版本可能不带该参数,传了会 getopt 报错退出,故先探测再用。
        """
        cache = getattr(self, '_glmark_dp_cache', None)
        if cache is None:
            cache = self._glmark_dp_cache = {}
        if bin_path in cache:
            return cache[bin_path]
        supported = False
        try:
            import subprocess as _sp
            out = _sp.run([bin_path, '--help'], stdout=_sp.PIPE,
                          stderr=_sp.STDOUT, timeout=10).stdout or b''
            supported = b'data-path' in out
        except Exception:
            supported = False
        cache[bin_path] = supported
        return supported

    def _glmark_ensure_system_data(self):
        """系统 /usr/share/glmark2 缺失/半残时,用包内数据补齐(运行时兜底)。

        [2026-11-06 D3000M / 麒麟2503 现场]
        该系统自带 glmark2 2014.03+git20150611.fa71af2d 连 --version 都不认,
        --help(净化 LD_LIBRARY_PATH 后)也没有 --data-path,即确实不支持该参数;
        而 glmark2 的数据路径是编译期硬编码的 /usr/share/glmark2。
        所以唯一对全版本有效的办法,就是把包内完整的 shaders/models/textures
        铺到该路径 —— 现场一条 cp 命令后立即出画面且 FPS 正常。
        打包脚本 postinst 会在安装时做同样的事,这里是旧包/目录被清理时的兜底。
        """
        sysdir = '/usr/share/glmark2'
        if getattr(self, '_glmark_sysdata_note', None) is not None:
            return self._glmark_sysdata_note
        self._glmark_sysdata_note = ''
        src = None
        for d in self._glmark_data_dirs():
            if d != sysdir and self._glmark_data_ok(d):
                src = d
                break
        if not src:
            self._glmark_sysdata_note = '包内无完整场景数据'
            return self._glmark_sysdata_note

        import shutil
        import shlex
        import subprocess as _sp2
        # 1) 当前用户有写权限时直接复制(多数产线以 root 运行)
        try:
            os.makedirs(sysdir, exist_ok=True)
            for sub in ('shaders', 'models', 'textures'):
                dst = os.path.join(sysdir, sub)
                if os.path.isdir(dst):
                    shutil.rmtree(dst, ignore_errors=True)
                shutil.copytree(os.path.join(src, sub), dst)
        except Exception:
            pass
        if self._glmark_data_ok(sysdir):
            self._glmark_sysdata_note = f'运行时已自动补齐({src} → {sysdir})'
            return self._glmark_sysdata_note

        # 2) 非交互提权:sudo -n 未配免密会立即失败,不弹密码框、不阻塞老化
        try:
            quoted = shlex.quote(src)
            script = ('mkdir -p /usr/share/glmark2 && '
                      'for s in shaders models textures; do '
                      'rm -rf /usr/share/glmark2/$s; '
                      f'cp -r {quoted}/$s /usr/share/glmark2/; done')
            _sp2.run(['sudo', '-n', 'sh', '-c', script],
                     stdout=_sp2.DEVNULL, stderr=_sp2.DEVNULL, timeout=20)
        except Exception:
            pass
        if self._glmark_data_ok(sysdir):
            self._glmark_sysdata_note = f'已通过 sudo -n 补齐({src} → {sysdir})'
        else:
            self._glmark_sysdata_note = (
                f'需手动补齐(包内已有完整数据 {src}): '
                f'sudo cp -r {src}/{{shaders,models,textures}} /usr/share/glmark2/')
        return self._glmark_sysdata_note

    def _glmark_cmd(self, bin_path):
        """构造 glmark2 启动命令(带数据路径参数)。

        - 系统 /usr/share/glmark2 数据完整 → 不加任何参数,与跑分 testGpu 一致。
        - 系统数据缺失/半残 → 先用包内数据自愈补齐硬编码路径(对所有版本有效);
          补不成且二进制支持 --data-path 时才走参数重定向。
        - 返回 (cmd_list, data_note) 供日志记录。
        """
        if not self._glmark_data_ok('/usr/share/glmark2'):
            self._glmark_ensure_system_data()
        if self._glmark_data_ok('/usr/share/glmark2'):
            note = getattr(self, '_glmark_sysdata_note', '')
            return [bin_path], "系统数据完整(/usr/share/glmark2)" + \
                (f" [自愈: {note}]" if note else "")
        fix = getattr(self, '_glmark_sysdata_note', '')
        for d in self._glmark_data_dirs():
            if self._glmark_data_ok(d):
                if self._glmark_supports_data_path(bin_path):
                    return [bin_path, '--data-path', d], \
                        f"数据重定向 --data-path {d}(系统默认路径缺失)"
                return [bin_path], \
                    (f"⚠ 找到数据 {d} 但该二进制不支持 --data-path,无法重定向"
                     + (f"；自愈未成功: {fix}" if fix else ""))
        return [bin_path], ("⚠ 未找到任何完整的 glmark2 场景数据(预期黑屏)"
                            + (f"；{fix}" if fix else ""))

    def _make_glmark_env(self, bin_path, force_soft, strip_pkg_lib=True):
        """构造 glmark2 子进程环境。

        - strip_pkg_lib=True: 剥离打包注入的 LD_LIBRARY_PATH(包内 lib/ 含
          Qt 自带 mesa/GL 库),让 glmark2 用系统 GL 库。
        - strip_pkg_lib=False: 完整继承启动器环境 —— 与跑分测试 testGpu 的
          QProcess 裸调用完全一致(现场实测跑分 glmark2 能正常出画面,
          故把"跑分同款"作为第一优先策略,2026-10-26)。
        - 仅当 glmark2 取自包内 extra-bin 离线版时,补回其专属数据路径
          GLMARK2_DATA_PATH(shader/模型);离线版同样用系统 GL 库运行。
        - force_soft=True 时追加 LIBGL_ALWAYS_SOFTWARE=1 + GALLIUM_DRIVER=llvmpipe
          强制软件渲染,作为硬件驱动黑窗的兜底。
        """
        env = dict(os.environ)
        app_dir = os.path.dirname(os.path.abspath(__file__))
        root = self._glmark_app_root() if strip_pkg_lib else None
        # 1) 净化 LD_LIBRARY_PATH:去掉所有位于打包根目录下的库目录
        old_ld = env.get('LD_LIBRARY_PATH', '')
        if old_ld and root:
            keep = []
            for p in old_ld.split(':'):
                if not p:
                    continue
                rp = os.path.realpath(p)
                if rp == root or rp.startswith(root + os.sep):
                    continue   # 去掉包内 lib/(Qt 自带 mesa/GL,导致黑窗)
                keep.append(p)
            env['LD_LIBRARY_PATH'] = ':'.join(keep)
        # 2) 包内 extra-bin 离线版:补数据路径环境变量。
        #    注意:官方 glmark2(2014.03~2023.01 全系)不读 GLMARK2_DATA_PATH 环境变量,
        #    数据路径只认编译期硬编码的 /usr/share/glmark2 和 --data-path 命令行参数
        #    (已核实上游源码 options.cpp,无 getenv 调用)。这里设环境变量仅为兼容
        #    可能存在的补丁版;真正生效的机制是 _glmark_cmd() 的 --data-path 参数
        #    + postinst 把包内数据补齐到 /usr/share/glmark2。
        if os.path.join('extra-bin', 'glmark2') in bin_path:
            for data_sub in ('extra-bin/glmark2/share/glmark2',
                             'extra-bin/glmark2/usr/share/glmark2'):
                dd = os.path.join(app_dir, data_sub)
                if os.path.isdir(dd):
                    env['GLMARK2_DATA_PATH'] = dd
                    break
        # 3) 软件渲染兜底
        if force_soft:
            env['LIBGL_ALWAYS_SOFTWARE'] = '1'
            env['GALLIUM_DRIVER'] = 'llvmpipe'
        return env

    def _run_glmark_round(self, candidates, failed_variants):
        """运行一轮 glmark2(全部场景,直到进程自然退出)。

        每个候选二进制按四级策略依次尝试:
          1. 跑分同款(继承启动器环境,与 testGpu 的 QProcess 裸调用完全一致;
             现场实测该路径 glmark2 能正常出画面,故最优先)
          2. 净化环境(剥离包内 lib/,让 glmark2 用系统 GL)
          3. 继承环境 + LIBGL_ALWAYS_SOFTWARE=1 软件渲染兜底
          4. 净化环境 + 软件渲染兜底
        启动后至多等待 45s 边读边判定是否真正渲染(产出 FPS):
          无 FPS(上下文创建失败/首场景过慢)→ 该策略失败,换下一策略/候选;
          产出 FPS → 正常读完整轮输出并解析 Score。
        每次启动前复位 load_progress['glmark'],防止旧值残留误判。
        self.running=False 时立即终止返回 True。
        全部输出落盘 /tmp/glmark2_aging.log 便于现场排查。
        """
        import subprocess as _sp
        import select as _select
        app_dir = os.path.dirname(os.path.abspath(__file__))
        log_path = '/tmp/glmark2_aging.log'

        def _parse(line, log_f):
            try:
                if log_f:
                    log_f.write(line)
                    log_f.flush()
            except Exception:
                pass
            # 数据缺失检测:glmark2 找不到 shader/模型/纹理时会打这些错误,
            # 此时窗口全黑但 FPS 照常输出 —— 不能只看 FPS 判定渲染正常
            if ('Failed to open models directory' in line
                    or ('Failed to open' in line and 'shaders' in line)):
                data_missing[0] = True
            try:
                if 'FPS:' in line:
                    fps = float(line.split('FPS:')[1].strip().split()[0])
                    self.load_progress['glmark'] = fps
                    self._glmark_state = f"{fps:.0f} fps"
                elif 'glmark2 Score' in line:
                    score = float(line.split('Score:')[1].strip().split()[0])
                    self._glmark_score = score
                elif 'GL_RENDERER' in line or 'renderer' in line:
                    self._glmark_state = line.strip()[:60]
            except (ValueError, IndexError):
                pass

        def _kill(proc):
            try:
                if proc is not None and proc.poll() is None:
                    proc.terminate()
                    proc.wait(timeout=3)
            except Exception:
                try:
                    proc.kill()
                except Exception:
                    pass

        while self.running:
            # 失败记录带 [策略] 后缀,按前缀匹配排除已失败的候选
            remaining = [c for c in candidates
                         if not any(f[0].startswith(c + '[') or f[0] == c
                                    for f in failed_variants)]
            if not remaining:
                summary = '\n'.join(
                    f"  {name}: {detail}" for name, detail in failed_variants[:8])
                # 数据缺失场景给专属修复指引(2026-11-04 D2000 现场:
                # 黑屏+FPS 正常输出的真凶是场景数据丢失,装数据即修复)
                if failed_variants and all(
                        '场景数据缺失' in d for _, d in failed_variants):
                    # [2026-11-06] 数据本来就在本机包内,旧文案让用户去别的机器
                    # 打包 / 下载源码,是绕远路;这里直接给出可复制的一条命令。
                    pkg_data = self._glmark_data_dirs()[0]
                    self.load_errors['glmark'] = (
                        "[glmark2场景数据缺失] 系统 /usr/share/glmark2 缺"
                        "shader/模型/纹理,画面全黑(FPS 仍正常输出)\n"
                        "  完整数据已随安装包附带,无需去其它机器拷或下载源码。\n"
                        "  修复(任选一,修完重跑老化即可,无需重启软件/重新打包):\n"
                        "  ① 一条命令补齐(推荐):\n"
                        f"     sudo mkdir -p /usr/share/glmark2 && sudo cp -r "
                        f"{pkg_data}/{{shaders,models,textures}} /usr/share/glmark2/\n"
                        "  ② 用新打包脚本重新打包安装(postinst 会自动补齐)\n"
                        "  说明:部分系统自带 glmark2(如 2014.03)不支持 --data-path,\n"
                        "        数据只能放到编译期硬编码的 /usr/share/glmark2。\n"
                        "  完整输出: /tmp/glmark2_aging.log\n" + summary)
                else:
                    self.load_errors['glmark'] = (
                        "[glmark2启动失败] 所有变体/策略均无法运行\n"
                        "  常见原因: 无显示环境/显卡驱动不支持 OpenGL/缺少运行库\n"
                        "  完整输出: /tmp/glmark2_aging.log\n" + summary)
                self.glmarkErrorShown.emit(self.load_errors['glmark'])
                return False
            bin_path = remaining[0]
            data_missing = [False]   # 闭包内可变:glmark2 自报的场景数据缺失

            # 启动命令:数据完整时不加参数(与跑分完全一致);数据缺失时
            # 尝试 --data-path 重定向到包内/已知完整数据目录
            cmd, data_note = self._glmark_cmd(bin_path)

            # 四级策略:第 1 级完整复刻跑分测试 testGpu 的调用方式
            # (继承启动器环境、不注入/剥离任何变量)—— 现场实测跑分 glmark2
            # 能正常出画面,故最优先;第 2 级剥离包内库目录;3/4 级为软件渲染
            # 兜底(硬件 GL 黑窗时 llvmpipe 仍可压测)
            strategies = (
                ('跑分同款(继承环境)', False, False),
                ('净化环境(剥离包内库)', True, False),
                ('继承环境+软件渲染', False, True),
                ('净化+软件渲染', True, True),
            )
            for tag, strip_pkg, force_soft in strategies:
                if not self.running:
                    return True
                env = self._make_glmark_env(bin_path, force_soft,
                                            strip_pkg_lib=strip_pkg)
                # 复位上一轮 FPS:防止旧值残留导致本轮误判"已渲染"
                self.load_progress['glmark'] = 0.0
                data_missing[0] = False
                try:
                    proc = _sp.Popen(cmd, stdout=_sp.PIPE,
                                     stderr=_sp.STDOUT, env=env)
                    self._glmark_proc = proc
                except Exception as e:
                    failed_variants.append((f"{bin_path}[{tag}]", f"启动异常: {e}"))
                    continue

                self._glmark_state = f"启动中...[{tag}]"
                log_f = None
                try:
                    log_f = open(log_path, 'a')
                    log_f.write(
                        f"\n===== {time.strftime('%F %T')} launch: "
                        f"{bin_path} [{tag}] =====\n"
                        f"数据: {data_note}\n"
                        f"LD_LIBRARY_PATH={env.get('LD_LIBRARY_PATH', '')}\n"
                        f"DISPLAY={os.environ.get('DISPLAY', '')} "
                        f"WAYLAND_DISPLAY={os.environ.get('WAYLAND_DISPLAY', '')} "
                        f"XDG_SESSION_TYPE={os.environ.get('XDG_SESSION_TYPE', '')}\n")
                    log_f.flush()
                except Exception:
                    log_f = None

                # 至多 45s 内边读边判定是否真正渲染(产出 FPS)。
                # 注意:必须边读边解析,否则管道缓冲不消费会导致死锁/误判未渲染。
                # 45s 而非 12s:老版 glmark2(2014.03)首场景 15~30s 才打第一条
                # FPS,12s 会误杀正常进程、再落入软渲染黑窗(2026-10-26 现场)
                t0 = time.time()
                fps_seen = False
                exited = False
                while time.time() - t0 < 45:
                    if not self.running:
                        _kill(proc)
                        try:
                            if log_f:
                                log_f.close()
                        except Exception:
                            pass
                        return True
                    if proc.poll() is not None:
                        exited = True
                        break
                    try:
                        r, _, _ = _select.select([proc.stdout], [], [], 0.3)
                    except Exception:
                        r = []
                    if r:
                        try:
                            raw = proc.stdout.readline()
                        except Exception:
                            raw = b''
                        if not raw:
                            continue
                        try:
                            line = raw.decode('utf-8', 'ignore')
                        except Exception:
                            continue
                        _parse(line, log_f)
                        if self.load_progress['glmark'] > 0:
                            fps_seen = True
                            break
                # 启动期间进程已退出:把剩余输出读完再判定
                if exited:
                    try:
                        for raw_line in proc.stdout:
                            try:
                                line = raw_line.decode('utf-8', 'ignore')
                            except Exception:
                                continue
                            _parse(line, log_f)
                    except Exception:
                        pass
                    fps_seen = self.load_progress['glmark'] > 0
                if not fps_seen:
                    note = ("45s 内未产出 FPS(GL上下文创建失败或首场景过慢)"
                            ",详见 /tmp/glmark2_aging.log")
                    try:
                        if proc.poll() is None:
                            _kill(proc)
                    except Exception:
                        pass
                    failed_variants.append((f"{bin_path}[{tag}]", note))
                    try:
                        if log_f:
                            log_f.close()
                    except Exception:
                        pass
                    continue   # 换下一策略
                # 数据缺失检测:FPS 有了但 glmark2 自报 shader/模型/纹理加载失败
                # → 窗口全黑(2026-11-04 飞腾 D2000 现场:黑屏但 248fps 照常输出),
                # 不能只看 FPS 就认定渲染正常
                if data_missing[0]:
                    note = ("场景数据缺失(shader/模型/纹理不可用,窗口全黑但 FPS "
                            f"正常输出)。数据状态: {data_note}")
                    try:
                        if proc.poll() is None:
                            _kill(proc)
                    except Exception:
                        pass
                    if log_f:
                        try:
                            log_f.write(f"[aging] 判定失败: {note}\n")
                            log_f.close()
                        except Exception:
                            pass
                    # 数据问题与环境策略无关:该候选全部策略一并标记失败,
                    # 跳出策略循环,由外层 while 换下一候选
                    for _t2, _s2, _f2 in strategies:
                        failed_variants.append((f"{bin_path}[{_t2}]", note))
                    try:
                        if self._glmark_proc is proc:
                            self._glmark_proc = None
                    except Exception:
                        pass
                    break
                # 已进入正常渲染:继续读剩余输出直到进程退出
                try:
                    for raw_line in proc.stdout:
                        if not self.running:
                            break
                        try:
                            line = raw_line.decode('utf-8', 'ignore')
                        except Exception:
                            continue
                        _parse(line, log_f)
                except Exception:
                    pass
                try:
                    if log_f:
                        log_f.close()
                except Exception:
                    pass
                try:
                    if self._glmark_proc is proc:
                        self._glmark_proc = None
                except Exception:
                    pass
                if not self.running:
                    _kill(proc)
                    return True
                try:
                    proc.wait(timeout=5)
                except Exception:
                    pass
                return True   # 本轮成功
            # 该候选全部策略均未成功 → 外层 while 自动换下一候选
        return False


    def _on_canvas_ctl(self, show):
        """合并模式画布控制槽(UI 线程执行,worker 经信号调用,线程安全)。

        show=True : 惰性创建 3D 画布(简单场景)并显示、启动动画、应用显存目标
        show=False: 停止动画、隐藏窗口、彻底销毁画布并释放 GL 上下文
        """
        try:
            if show:
                if self.canvas3d is None and self.complex_scene is None:
                    self.current_scene = "simple"
                    self.canvas3d = Canvas3D()
                    self.canvas3d_window.setCentralWidget(self.canvas3d)
                    try:
                        vram = getattr(self, '_vram_target', 128 * 1024 * 1024)
                        self.canvas3d.set_vram_target(vram)
                    except Exception:
                        pass
                self.canvas3d_window.show()
                canvas = self.canvas3d if self.canvas3d is not None else self.complex_scene
                if canvas is not None:
                    canvas.start_animation()
            else:
                for attr in ('canvas3d', 'complex_scene'):
                    try:
                        c = getattr(self, attr, None)
                        if c is not None:
                            try:
                                c.stop_animation()
                            except Exception:
                                pass
                    except Exception:
                        pass
                try:
                    self.canvas3d_window.hide()
                except Exception:
                    pass
                self._safe_dispose_canvas(self.canvas3d)
                self._safe_dispose_canvas(self.complex_scene)
                self.canvas3d = None
                self.complex_scene = None
                try:
                    self.canvas3d_window.setCentralWidget(QLabel(
                        "3D 老化画布将在开始测试时按需创建"))
                except Exception:
                    pass
        except Exception as e:
            try:
                print(f"[canvas_ctl] {type(e).__name__}: {e}")
            except Exception:
                pass


    def _gpu3d_merged_worker(self):
        """3D+glmark2 合并交替 worker(勾选 3D 时启动)。

        交替循环:内置 3D 画布跑 CANVAS_PHASE_SEC 秒 → 请求 UI 线程彻底
        销毁画布(释放 GL 上下文) → glmark2 跑完整一轮 → 再回画布,循环。
        两者永不同时持有 GL 上下文,规避目标机驱动跨进程并发 GL 冲突。
        系统/包内无 glmark2 时自动退化为纯画布模式(原 3D 行为)。
        """
        import time as _t
        # 画布阶段时长:默认 120s(60s简单+60s复杂场景,一轮切换),可环境变量调
        CANVAS_PHASE_SEC = int(os.environ.get('AGING_CANVAS_PHASE_SEC', '120'))
        # 用户方案:勾选 3D 即启用交替模式(画布 ↔ glmark2 循环),
        # 无论 glmark2 复选框是否勾选;无 glmark2 时自动纯画布
        candidates = self._find_glmark_binary()
        if not candidates:
            self.load_errors['glmark'] = (
                "[glmark2缺失] 包内与系统均未找到 glmark2,本次仅运行内置 3D 画布\n"
                "  安装: sudo apt install glmark2  (或用本 deb 包内 extra-bin 离线版)")
        failed_variants = []
        canvas_on = False
        try:
            while self.running:
                # ---- 阶段A:内置 3D 画布 ----
                if not canvas_on:
                    self.canvasCtlRequested.emit(True)
                    canvas_on = True
                    _t.sleep(1.5)   # 等 UI 线程创建/显示画布
                phase_start = _t.time()
                while self.running and _t.time() - phase_start < CANVAS_PHASE_SEC:
                    _t.sleep(1)
                if not self.running:
                    break
                # ---- 阶段B:glmark2 完整一轮 ----
                if not candidates:
                    continue   # 无 glmark2:纯画布模式,画布持续运行
                self.canvasCtlRequested.emit(False)
                canvas_on = False
                # 等 UI 线程彻底销毁画布(释放 GL 上下文),最多 15s
                for _ in range(30):
                    if self.canvas3d is None and self.complex_scene is None:
                        break
                    if not self.running:
                        break
                    _t.sleep(0.5)
                if not self.running:
                    break
                ok = self._run_glmark_round(candidates, failed_variants)
                if not ok:
                    # glmark2 全部失败 → 后续只跑画布(错误已记录弹窗)
                    candidates = []
                    if not self.running:
                        break
                    continue
        finally:
            # 收尾:确保画布销毁(与 stop_aging 的销毁幂等,重复调用安全)
            try:
                self.canvasCtlRequested.emit(False)
            except Exception:
                pass

    def _on_glmark_error(self, msg):
        """glmark2 错误非模态弹窗(UI 线程,不阻塞其它操作)"""
        try:
            QMessageBox.warning(self, "glmark2 老化异常", msg)
        except Exception:
            pass

class Canvas2D(QWidget):
    def __init__(self):
        super().__init__()
        self.animation_running = False
        self.angle = 0
        # QTimer 必须传 parent=self,否则 close+GC 时仍有触发 timeout 风险
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.update)

    def start_animation(self):
        try:
            self.animation_running = True
            self.timer.start(50)  # 20fps
        except Exception as e:
            print(f"动画启动错误: {e}")
            self.animation_running = False

    def stop_animation(self):
        try:
            self.animation_running = False
            # timer 已被 deleteLater(用户中途关过窗口)时,stop() 抛 RuntimeError
            # 必须 try/except 包裹,否则 stop_aging 中途挂掉后续 close() 不执行
            if self.timer is not None:
                self.timer.stop()
        except (RuntimeError, AttributeError):
            # widget/timer 已被 delete
            pass
        except Exception:
            pass

    def paintEvent(self, event):
        if not self.animation_running:
            return

        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        painter.setRenderHint(QPainter.SmoothPixmapTransform)

        # 清屏
        painter.fillRect(self.rect(), QColor(0, 0, 0))

        # 绘制多个旋转的几何图形
        width = self.width()
        height = self.height()

        # 主图形 - 旋转的星星
        painter.save()
        painter.translate(width/2, height/2)
        painter.rotate(self.angle)

        # 绘制五角星
        star_points = []
        for i in range(5):
            # 外点
            outer_x = 100 * math.cos(math.radians(90 + i*72))
            outer_y = 100 * math.sin(math.radians(90 + i*72))
            star_points.append(QPointF(outer_x, outer_y))

            # 内点
            inner_x = 40 * math.cos(math.radians(90 + i*72 + 36))
            inner_y = 40 * math.sin(math.radians(90 + i*72 + 36))
            star_points.append(QPointF(inner_x, inner_y))

        gradient = QRadialGradient(0, 0, 120)
        gradient.setColorAt(0, QColor(255, random.randint(0,255), random.randint(0,255)))
        gradient.setColorAt(1, QColor(random.randint(0,255), random.randint(0,255), 255))

        painter.setBrush(QBrush(gradient))
        painter.setPen(QPen(QColor(255,255,255), 2))
        painter.drawPolygon(QPolygonF(star_points))
        painter.restore()

        # 绘制背景粒子效果
        for _ in range(100):
            x = random.randint(0, width)
            y = random.randint(0, height)
            size = random.randint(1, 3)
            color = QColor(random.randint(200,255), random.randint(200,255), random.randint(200,255), random.randint(100,200))
            painter.setBrush(color)
            painter.setPen(Qt.NoPen)
            painter.drawEllipse(x, y, size, size)

        self.angle += 3
        if self.angle >= 360:
            self.angle = 0

class Canvas3D(QGLWidget):
    def __init__(self):
        super().__init__()
        self.animation_running = False
        self.angle_x = 0
        self.angle_y = 0
        self.angle_z = 0
        # ---- 显存压力池:老化时真正吃掉一块显存,让显存占用率实打实拉上去 ----
        # (纹理必须在 GL 上下文内分配,paintGL 首帧触发;stop 时释放)
        self._vram_pool = []
        self._vram_target = 128 * 1024 * 1024   # 默认 128MB,start_aging 会按实测显存调整
        # QTimer 必须传 parent=self,避免悬空信号导致进程崩溃
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.update)

    def set_vram_target(self, target_bytes):
        """设置显存压力池目标大小(由 AgingTestGUI.start_aging 按实测显存下发)"""
        try:
            self._vram_target = max(0, int(target_bytes))
        except Exception:
            pass

    def initializeGL(self):
        glEnable(GL_DEPTH_TEST)
        glClearColor(0.1, 0.1, 0.1, 1.0)

    def resizeGL(self, width, height):
        # 修复（D2）：窗口最小化/离屏时 width/height 可能为 0，
        # width/height 除零抛 ZeroDivisionError 会从 Qt 回调冒出杀进程
        if width <= 0 or height <= 0:
            return
        glViewport(0, 0, width, height)
        glMatrixMode(GL_PROJECTION)
        glLoadIdentity()
        gluPerspective(45, width/height, 0.1, 100.0)

    def paintGL(self):
        if not self.animation_running:
            return

        # ---- 显存压力池:首帧分配(必须在本 widget 的 GL 上下文内) ----
        if not self._vram_pool and self._vram_target > 0:
            self._vram_pool = _gl_alloc_vram_pool(self._vram_target)

        try:
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)
            glMatrixMode(GL_MODELVIEW)
            glLoadIdentity()
            glTranslatef(0.0, 0.0, -5.0)

            # 启用光照
            glEnable(GL_LIGHTING)
            glEnable(GL_LIGHT0)
            light_pos = [1.0, 1.0, 1.0, 0.0]
            glLightfv(GL_LIGHT0, GL_POSITION, light_pos)

            # 旋转立方体
            glRotatef(self.angle_x, 1, 0, 0)
            glRotatef(self.angle_y, 0, 1, 0)
            glRotatef(self.angle_z, 0, 0, 1)

            # 绘制多个复杂几何体
            quadric = gluNewQuadric()
            gluQuadricNormals(quadric, GLU_SMOOTH)

            # 设置材质属性
            glMaterialfv(GL_FRONT, GL_AMBIENT, [0.2, 0.2, 0.2, 1.0])
            glMaterialfv(GL_FRONT, GL_SPECULAR, [1.0, 1.0, 1.0, 1.0])
            glMaterialfv(GL_FRONT, GL_SHININESS, [50.0])

            # 设置颜色
            colors = [
                [1.0, 0.0, 0.0],  # 红
                [0.0, 1.0, 0.0],  # 绿
                [0.0, 0.0, 1.0],  # 蓝
                [1.0, 1.0, 0.0],  # 黄
                [1.0, 0.0, 1.0],  # 紫
                [0.0, 1.0, 1.0]   # 青
            ]

            # 6个球体沿3D椭圆轨道旋转，形成六芒星视觉幻象
            num_spheres = 6
            for i in range(num_spheres):
                glPushMatrix()

                # 每个球体有独立的椭圆轨道参数
                orbit_angle = (self.angle_x + i * 60) % 360
                rad_angle = math.radians(orbit_angle)

                # 椭圆轨道参数 (倾斜30度)
                ellipse_x = 1.8 * math.cos(rad_angle)
                ellipse_y = 1.2 * math.sin(rad_angle)
                ellipse_z = 0.8 * math.sin(rad_angle * 2)  # Z轴波动

                # 应用椭圆轨道
                glTranslatef(ellipse_x, ellipse_y, ellipse_z)

                # 球体自转 (保持与单球体时相同的自转效果)
                glRotatef(self.angle_x * 1.5, 0, 1, 0)
                glRotatef(self.angle_y * 1.2, 1, 0, 0)

                # 动态大小变化 (在最高点时最大)
                height_factor = 1.0 + math.sin(math.radians(orbit_angle)) * 0.5
                max_size = 0.5
                current_size = max_size * height_factor

                # 绘制彩色球体 (与单球体时相同的七彩变色效果)
                for j in range(6):
                    glPushMatrix()
                    glRotatef(self.angle_x + j*60, 1, 1, 1)
                    glTranslatef(0.3, 0.3, 0.3)
                    glMaterialfv(GL_FRONT, GL_DIFFUSE, colors[j] + [1.0])
                    gluSphere(quadric, current_size, 32, 32)
                    glPopMatrix()

                glPopMatrix()  # 平衡矩阵堆栈

            gluDeleteQuadric(quadric)

            # 绘制背景星空（禁用光照）
            glDisable(GL_LIGHTING)
            glBegin(GL_POINTS)
            for _ in range(200):
                x = random.uniform(-5, 5)
                y = random.uniform(-5, 5)
                z = random.uniform(-5, 5)
                glColor3f(random.uniform(0.5, 1.0), random.uniform(0.5, 1.0), random.uniform(0.5, 1.0))
                glVertex3f(x, y, z)
            glEnd()
            glEnable(GL_LIGHTING)

            self.angle_x += 1.2
            self.angle_y += 1.5
            self.angle_z += 0.8
            if self.angle_x >= 360:
                self.angle_x = 0
            if self.angle_y >= 360:
                self.angle_y = 0
            if self.angle_z >= 360:
                self.angle_z = 0
        except Exception as e:
            print(f"3D渲染错误: {e}")
            self.animation_running = False
            return

    def start_animation(self):
        try:
            self.animation_running = True
            self.timer.start(50)  # 20fps
        except Exception as e:
            print(f"3D动画启动错误: {e}")
            self.animation_running = False

    def stop_animation(self):
        try:
            self.animation_running = False
            # timer 已被 deleteLater(用户中途关过窗口)时,stop() 抛 RuntimeError
            # 必须 try/except 包裹,否则 stop_aging 中途挂掉后续 close() 不执行
            if self.timer is not None:
                self.timer.stop()
        except (RuntimeError, AttributeError):
            # widget/timer 已被 delete
            pass
        except Exception:
            pass
        # ---- 释放显存压力池(必须切回本 widget 的 GL 上下文) ----
        if self._vram_pool:
            try:
                self.makeCurrent()
                _gl_free_vram_pool(self._vram_pool)
                self.doneCurrent()
            except Exception:
                pass
            self._vram_pool = []


if __name__ == "__main__":
    # 检测是否为统一控制自动老化模式（双重检测：命令行参数 + 环境变量）
    auto_start = "--auto-start" in sys.argv or os.environ.get("AGING_AUTO_START", "") == "1"

    app = QApplication(sys.argv)
    window = AgingTestGUI(auto_start=auto_start)
    window.show()
    sys.exit(app.exec_())