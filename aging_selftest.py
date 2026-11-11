#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""老化测试工具自检脚本(跨架构: x86 / 飞腾 / 鲲鹏 / 龙芯; Windows / Linux / macOS)

针对本次两项修复的现场验证:
  修复1: 显存真实检测(主界面/老化界面一致) —— 验证 _query_gpu_memory()
  修复2: 停止老化丝滑不卡死(硬盘满载场景) —— 验证磁盘 worker 停止时序、
         CPU worker 停止时序、GUI 停止流程(需 --with-gui)

用法:
  python3 aging_selftest.py              # 基础自检(无 GUI,安全,可在任何机器跑)
  python3 aging_selftest.py --with-gui   # 含 GUI 完整启停流程测试(需要显示环境)
                                         # GUI 模式会真实跑 3 轮"开始老化→停止"
                                         # 默认只勾选 硬盘满载+3D显卡,约 40 秒

输出: 逐项 PASS/FAIL/SKIP + 最终汇总报告(退出码 0=全部通过, 1=有失败)

注意: 全部测试逻辑必须在 if __name__ == '__main__' 保护下执行,
否则 Windows spawn 模式的 multiprocessing 子进程会重复执行整个脚本。
"""
import os
import sys
import time
import threading

# 保证无论从哪个目录启动都能找到同目录的被测模块
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Windows 老控制台中文兜底
try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

RESULTS = []   # (名称, 状态, 详情)

def report(name, ok, detail=''):
    status = 'PASS' if ok is True else ('FAIL' if ok is False else 'SKIP')
    RESULTS.append((name, status, detail))
    print(f"[{status}] {name}" + (f" —— {detail}" if detail else ''))

def section(title):
    print("\n" + "=" * 64)
    print(title)
    print("=" * 64)

def main():
    # ========================================================================
    section("0. 环境信息")
    # ========================================================================
    print(f"  系统:   {sys.platform}")
    try:
        import platform
        print(f"  架构:   {platform.machine()}")
        print(f"  平台:   {platform.platform()}")
        print(f"  Python: {platform.python_version()}")
    except Exception:
        pass

    # ========================================================================
    section("1. 模块导入检查(依赖完整性)")
    # ========================================================================
    # 逐模块检查:报出具体缺哪个模块,方便目标机对照安装
    # (PIL 已从 complex_scene.py 移除,不再要求 python3-pil;
    #  numpy 为 PyOpenGL 可选加速,缺失仅告警不影响功能)
    for mod, pkg in [("PyQt5", "python3-pyqt5"),
                     ("PyQt5.QtOpenGL", "python3-pyqt5.qtopengl"),
                     ("psutil", "python3-psutil"),
                     ("OpenGL", "python3-opengl")]:
        try:
            __import__(mod)
            report(f"依赖模块 {mod}", True, f"对应包: {pkg}")
        except Exception as e:
            report(f"依赖模块 {mod}", False,
                   f"缺失! 请安装 {pkg} ({type(e).__name__}: {e})")
    try:
        import numpy  # noqa: F401
        report("可选模块 numpy", True, "PyOpenGL 数组加速可用")
    except Exception:
        report("可选模块 numpy", None,
               "未安装(仅影响 PyOpenGL 数组加速,不影响功能,可装 python3-numpy)")
    try:
        import aging_test_gui as agui   # noqa: F401  (含上述依赖的完整导入链)
        import complex_scene            # noqa: F401
        report("导入 aging_test_gui / complex_scene", True,
               "完整导入链通过")
    except Exception as e:
        report("导入 aging_test_gui / complex_scene", False,
               f"{type(e).__name__}: {e}")
        print("\n依赖缺失,后续测试中止。安装: "
              "sudo apt install python3-pyqt5 python3-pyqt5.qtopengl "
              "python3-psutil python3-opengl")
        print("\n===== 汇总: FAIL(依赖缺失) =====")
        sys.exit(1)

    # ========================================================================
    section("1.5 跨平台环境检查(各架构 Linux 兼容性)")
    # ========================================================================
    try:
        import platform as _plat
        print(f"  架构: {_plat.machine()}  系统: {_plat.system()} "
              f"Python: {_plat.python_version()}")
        if _plat.python_version_tuple()[0] == '3' and \
                int(_plat.python_version_tuple()[1]) < 7:
            report("Python 版本", False,
                   f"Python {_plat.python_version()} 过老(需≥3.6,建议≥3.7)")
        else:
            report("Python 版本", True, _plat.python_version())
    except Exception as e:
        report("Python 版本", None, str(e))
    # Linux: /sys/class/drm 显存节点可读性(本次显存检测的数据源)
    if sys.platform.startswith('linux'):
        import subprocess as _sp
        try:
            r = _sp.run(['cat', '/sys/class/drm/card0/device/mem_info_vram_total'],
                        stdout=_sp.PIPE, stderr=_sp.PIPE, timeout=3)
            if r.returncode == 0:
                report("DRM sysfs 显存节点可读", True,
                       f"card0 vram_total={int(r.stdout.strip())/1048576:.0f}MB")
            else:
                report("DRM sysfs 显存节点可读", None,
                       "本机 GPU 驱动未暴露 mem_info_vram 节点(如集显/NVIDIA,"
                       "显存将走 nvidia-smi 或显示'未检测到',属正常)")
        except Exception:
            report("DRM sysfs 显存节点可读", None, "无法探测(不影响其他功能)")
        # freeglut 库(仅信息展示:复杂场景已改用手绘立方体,不再依赖 freeglut)
        try:
            r = _sp.run(['ldconfig', '-p'], stdout=_sp.PIPE, timeout=3)
            has_glut = b'glut' in r.stdout
            report("freeglut 库", None,
                   "已安装(信息项)" if has_glut else
                   "未检测到 libglut(无影响:复杂场景已改用手绘立方体,零 freeglut 依赖)")
        except Exception:
            pass

    # ========================================================================
    section("2. 显存真实检测(修复1:不再伪造 20%/随机数)")
    # ========================================================================
    try:
        used, total, src = agui._query_gpu_memory()
        if total > 0:
            pct = 100.0 * used / total
            report("显存查询", True,
                   f"数据源={src}, 已用={used/1024/1024:.0f}MB, "
                   f"总量={total/1024/1024:.0f}MB, 占用={pct:.1f}%")
            # 连续采样两次验证"实时性"
            time.sleep(1.2)
            used2, total2, src2 = agui._query_gpu_memory()
            report("显存连续采样", total2 > 0 and src2 == src,
                   f"第二次采样: 已用={used2/1024/1024:.0f}MB (源={src2})")
        else:
            # 非失败:该机器无 NVIDIA/AMD DRM 显存节点(如 Intel 集显/软渲染),
            # 属于"如实显示未检测到",两端行为一致即为正确
            has_drm = os.path.isdir('/sys/class/drm')
            report("显存查询", None,
                   "未检测到显存节点(无NVIDIA nvidia-smi、无DRM mem_info_* 节点);"
                   f"Linux DRM 目录存在={has_drm};软件将如实显示'未检测到',"
                   "主界面与老化界面行为一致")
    except Exception as e:
        report("显存查询", False, f"{type(e).__name__}: {e}")

    # ========================================================================
    section("3. 硬盘满载停止时序(修复2核心:停止不再卡死)")
    # ========================================================================
    class _DiskStub:
        """模拟 AgingTestGUI 中 disk_load 依赖的最小属性集"""
        def __init__(self):
            self.running = True
            self.load_progress = {'disk': 0.0}
            self.load_errors = {'disk': ''}
            self._disk_status_msg = ''

    try:
        stub = _DiskStub()
        th = threading.Thread(target=agui.AgingTestGUI.disk_load, args=(stub,),
                              daemon=True)
        th.start()
        # 让磁盘 worker 跑满 5 秒(真实写盘压力,含 fsync)
        time.sleep(5)
        rate = stub.load_progress.get('disk', 0.0)
        t0 = time.time()
        stub.running = False              # 等价于点击"停止老化"
        th.join(timeout=30)
        stop_secs = time.time() - t0
        alive = th.is_alive()
        report("磁盘 worker 停止时序", (not alive) and stop_secs < 10,
               f"写盘速率~{rate:.1f}MB/s, 停止耗时 {stop_secs:.2f}s "
               f"(要求<10s;旧版可达20s+且UI冻结)")
        if alive:
            report("磁盘 worker 残留", False,
                   "停止 30s 后线程仍存活(daemon 会随进程退出,但请反馈)")
        # 清理残留测试目录(如果有)
        for base in (os.path.expanduser('~'), '/'):
            d = os.path.join(base, '.elsky_aging_test')
            if os.path.isdir(d):
                try:
                    for f in os.listdir(d):
                        os.remove(os.path.join(d, f))
                    os.rmdir(d)
                except Exception:
                    pass
    except Exception as e:
        report("磁盘 worker 停止时序", False, f"{type(e).__name__}: {e}")

    # ========================================================================
    section("4. CPU 满载 worker 停止时序")
    # ========================================================================
    try:
        import multiprocessing as mp
        stop_event = mp.Event()
        p = mp.Process(target=agui._cpu_worker, args=(stop_event,), daemon=True)
        p.start()
        time.sleep(2)
        t0 = time.time()
        stop_event.set()
        p.join(timeout=10)
        stop_secs = time.time() - t0
        report("CPU worker 停止时序", (not p.is_alive()) and stop_secs < 5,
               f"停止耗时 {stop_secs:.2f}s (要求<5s)")
    except Exception as e:
        report("CPU worker 停止时序", False, f"{type(e).__name__}: {e}")

    # ========================================================================
    section("5. GUI 完整启停流程(需要显示环境,加 --with-gui 启用)")
    # ========================================================================
    if '--with-gui' in sys.argv:
        try:
            from PyQt5.QtWidgets import QApplication, QMessageBox
            app = QApplication.instance() or QApplication(sys.argv)
            # 自动化测试:屏蔽模态弹窗(仅测试进程内生效,不影响产品代码)
            QMessageBox.information = staticmethod(lambda *a, **k: QMessageBox.Ok)
            QMessageBox.warning = staticmethod(lambda *a, **k: QMessageBox.Ok)

            win = agui.AgingTestGUI(auto_start=False)
            win.show()
            win.cpu_check.setChecked(False)     # 自检不跑 CPU 满载,避免干扰现场
            win.memory_check.setChecked(False)
            win.disk_check.setChecked(True)     # 重点验证:硬盘满载(用户反馈的卡死场景)
            win.gpu2d_check.setChecked(False)
            win.gpu3d_check.setChecked(True)    # 验证显存压力池
            win.glmark_check.setChecked(False)  # 自检不跑 glmark2(需要外部二进制+显示环境)
            win.network_check.setChecked(False)
            win.duration_edit.setText("1")

            all_ok = True
            details = []
            complex_scene_hit = False
            for round_no in range(1, 4):        # 连续 3 轮启停,复现"重复启停"场景
                win.start_aging()
                # 缩短场景切换定时器到 2 秒,强制覆盖"复杂场景"路径
                # (complex_scene.py 的 GLUT 兜底/显存压力池/PIL 移除验证)
                try:
                    win.scene_switch_timer.start(2000)
                except Exception:
                    pass
                # 驱动事件循环跑 5 秒(真实老化中,覆盖到场景切换)
                t_end = time.time() + 5
                last = time.time()
                while time.time() < t_end:
                    app.processEvents()
                    time.sleep(0.02)
                    try:
                        if win.complex_scene is not None and \
                                win.complex_scene.animation_running:
                            complex_scene_hit = True
                    except Exception:
                        pass
                gpu_used, gpu_total, gpu_src = win._gpu_mem

                # ---- 点击"停止老化":stop_aging() 必须立即返回(UI 线程不阻塞) ----
                t0 = time.time()
                win.stop_aging()
                click_secs = time.time() - t0

                # ---- 等待后台停止完成,同时监测 UI 事件循环最大卡顿 ----
                max_gap2 = 0.0
                last = time.time()
                deadline = time.time() + 25
                while win._stopping and time.time() < deadline:
                    app.processEvents()
                    time.sleep(0.02)
                    now = time.time()
                    max_gap2 = max(max_gap2, now - last)
                    last = now
                total_secs = time.time() - t0

                ok = (click_secs < 1.5 and not win._stopping
                      and total_secs < 25 and max_gap2 < 2.0
                      and win.start_btn.isEnabled())
                all_ok = all_ok and ok
                gpu_txt = (f"{gpu_used/1048576:.0f}MB/{gpu_total/1048576:.0f}MB({gpu_src})"
                           if gpu_total > 0 else "未检测到")
                details.append(
                    f"第{round_no}轮: stop_aging()返回 {click_secs:.2f}s(要求<1.5s), "
                    f"整体停止 {total_secs:.2f}s, 停止期间UI最大卡顿 {max_gap2:.2f}s, "
                    f"老化中显存采样={gpu_txt}")
            report("GUI 三轮启停(硬盘满载+3D)", all_ok,
                   "; ".join(details) if all_ok else "存在异常: " + "; ".join(details))
            # 复杂场景(complex_scene.py: PIL 移除/GLUT 兜底/显存压力池)覆盖验证
            report("复杂场景路径覆盖", complex_scene_hit,
                   "60秒场景切换已触发并正常渲染" if complex_scene_hit else
                   "未触发到复杂场景(3D动画未进入 complex_scene,请反馈)")

            # 验证停止后界面复位
            report("停止后界面复位", (not win.running) and win.start_btn.isEnabled()
                   and win.start_btn.text() == "开始老化",
                   f"running={win.running}, 开始按钮='{win.start_btn.text()}'")
            win.close()
        except Exception as e:
            import traceback
            traceback.print_exc()
            report("GUI 三轮启停(硬盘满载+3D)", False, f"{type(e).__name__}: {e}")
    else:
        report("GUI 完整启停流程", None, "未启用(加 --with-gui 参数执行)")

    # ========================================================================
    section("汇总报告")
    # ========================================================================
    fails = [r for r in RESULTS if r[1] == 'FAIL']
    skips = [r for r in RESULTS if r[1] == 'SKIP']
    passes = [r for r in RESULTS if r[1] == 'PASS']
    print(f"  通过 {len(passes)} 项, 失败 {len(fails)} 项, 跳过 {len(skips)} 项")
    for name, status, detail in RESULTS:
        print(f"  [{status}] {name}")
    if fails:
        print("\n结论: 存在失败项,请把本报告完整输出发回定位。")
        sys.exit(1)
    print("\n结论: 自检通过。(SKIP 项为环境不支持/未启用,不算失败)")
    sys.exit(0)

if __name__ == '__main__':
    main()
