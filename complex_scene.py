from OpenGL.GL import *
from OpenGL.GLU import *
# 历史遗留清理(跨架构兼容加固):
# 1) 曾 `from PIL import Image` —— Image 从未被使用,却是硬依赖;
#    打包依赖链(basic_tester.cpp 检查命令/deb 下载列表/tar 模块复制列表)
#    均不含 python3-pil → 未预装 PIL 的目标机上 aging_test_gui.py
#    启动即 ImportError。已删除。
# 2) 曾 import OpenGL.GLUT 并调用 glutInit/glutSolidCube —— 实测在
#    freeglut 绑定无效的环境(部分国产化/精简 Linux 及 Windows PyOpenGL
#    组合)上 glutSolidCube 每帧抛 access violation,复杂场景渲染中断、
#    日志刷屏。已改为手绘六面体(_draw_cube_fallback,纯 GL 1.1 核心
#    函数,任何 OpenGL 实现都支持),完全移除 freeglut 运行时依赖。
from PyQt5.QtOpenGL import QGLWidget
from PyQt5.QtCore import QTimer
import math
import random

class ComplexScene(QGLWidget):
    def __init__(self):
        super().__init__()
        self.animation_running = False
        # 修复（A5①）：QTimer 必须传 parent=self —— 旧实现 QTimer() 无父对象，
        # 场景部件被 deleteLater 后悬空 timer 仍触发 self.update()，
        # 对已删除的 C++ 对象调用导致崩溃
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.update)

        # ---- 显存压力池:老化时真正吃掉一块显存(与 Canvas3D 行为一致) ----
        # 场景每 60 秒切换时:旧场景 stop_animation 释放 → 新场景首帧重新分配,
        # 老化期间显存占用持续保持高位
        self._vram_pool = []
        self._vram_target = 128 * 1024 * 1024   # 默认 128MB,由 GUI 按实测显存调整

        # 修复（D3）：quadric 只创建一次复用 —— 旧实现每帧 gluNewQuadric()
        # 从不删除，7 天老化累积上千万次泄漏分配
        self._quadric = None

        # 场景参数
        self.scene_time = 0
        self.camera_angle = 0
        self.camera_height = 1.5
        self.light_positions = []
        self.particles = []
        self.init_scene()

    def set_vram_target(self, target_bytes):
        """设置显存压力池目标大小(由 AgingTestGUI.start_aging 按实测显存下发)"""
        try:
            self._vram_target = max(0, int(target_bytes))
        except Exception:
            pass

    def init_scene(self):
        # 初始化光照位置
        self.light_positions = [
            [5.0, 5.0, 5.0],  # 主光源
            [0.0, 3.0, 0.0],  # 点光源1
            [3.0, 3.0, 0.0],  # 点光源2
            [0.0, 3.0, 3.0]   # 点光源3
        ]

        # 初始化粒子系统
        self.init_particles(5000)

        # 加载纹理
        self.load_textures()

    def load_textures(self):
        # 这里应该加载实际纹理，暂时用空纹理代替
        self.textures = {}
        textures_to_load = {
            'ground': 'textures/ground.jpg',
            'skybox': ['textures/skybox_right.jpg', 'textures/skybox_left.jpg',
                      'textures/skybox_top.jpg', 'textures/skybox_bottom.jpg',
                      'textures/skybox_front.jpg', 'textures/skybox_back.jpg']
        }

        # 实际项目中应该实现纹理加载逻辑
        pass

    def init_particles(self, count):
        for _ in range(count):
            self.particles.append({
                'position': [random.uniform(-10, 10),
                            random.uniform(0, 5),
                            random.uniform(-10, 10)],
                'velocity': [random.uniform(-0.01, 0.01),
                            random.uniform(0, 0.05),
                            random.uniform(-0.01, 0.01)],
                'color': [random.uniform(0.5, 1.0),
                         random.uniform(0.5, 1.0),
                         random.uniform(0.5, 1.0),
                         random.uniform(0.3, 0.8)],
                'size': random.uniform(0.05, 0.2),
                'life': random.uniform(50, 200)
            })

    def initializeGL(self):
        # 注:不再调用 glutInit/GLUT —— 立方体绘制已改用手绘六面体,
        # 完全不依赖 freeglut(见文件头部说明)
        glEnable(GL_DEPTH_TEST)
        glEnable(GL_LIGHTING)
        glEnable(GL_LIGHT0)
        glEnable(GL_LIGHT1)
        glEnable(GL_LIGHT2)
        glEnable(GL_LIGHT3)
        glClearColor(0.1, 0.1, 0.1, 1.0)

        # Reset matrix stack to default state when scene initializes
        glMatrixMode(GL_MODELVIEW)
        while glGetIntegerv(GL_MODELVIEW_STACK_DEPTH) > 1:
            glPopMatrix()
        glLoadIdentity()

    def resizeGL(self, width, height):
        # 修复（D2）：height=0 时 width/height 触发 ZeroDivisionError（窗口最小化瞬间）
        if height < 1:
            height = 1
        glViewport(0, 0, width, height)
        glMatrixMode(GL_PROJECTION)
        glLoadIdentity()
        gluPerspective(45, width / height, 0.1, 100.0)

    def paintGL(self):
        if not self.animation_running:
            return

        # ---- 显存压力池:首帧分配(必须在本 widget 的 GL 上下文内) ----
        # 延迟导入避免与 aging_test_gui 的循环 import(方法调用时两模块均已加载)
        if not self._vram_pool and self._vram_target > 0:
            try:
                from aging_test_gui import _gl_alloc_vram_pool
                self._vram_pool = _gl_alloc_vram_pool(self._vram_target)
            except Exception:
                self._vram_pool = []

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)

        # 设置相机
        self.setup_camera()

        # 绘制地面
        self.draw_ground()

        # 绘制中心模型
        self.draw_center_model()

        # 绘制环绕物体
        self.draw_surrounding_objects()

        # 绘制粒子系统
        self.draw_particles()

        # 更新场景时间
        self.scene_time += 1
        self.camera_angle = (self.camera_angle + 0.5) % 360
        self.camera_height = 1.5 + math.sin(self.scene_time * 0.01) * 0.5

    def setup_camera(self):
        glMatrixMode(GL_MODELVIEW)
        glLoadIdentity()

        # 旋转相机
        cam_x = 8 * math.cos(math.radians(self.camera_angle))
        cam_z = 8 * math.sin(math.radians(self.camera_angle))
        gluLookAt(cam_x, self.camera_height, cam_z,
                 0, 0, 0,
                 0, 1, 0)

    def draw_ground(self):
        glBegin(GL_QUADS)
        glColor3f(0.3, 0.3, 0.3)
        size = 20
        for x in range(-size, size):
            for z in range(-size, size):
                glVertex3f(x, 0, z)
                glVertex3f(x+1, 0, z)
                glVertex3f(x+1, 0, z+1)
                glVertex3f(x, 0, z+1)
        glEnd()

    def draw_center_model(self):
        # 这里应该绘制复杂模型，暂时用简单球体代替
        glMatrixMode(GL_MODELVIEW)
        # 保存当前矩阵状态
        original_matrix = glGetDoublev(GL_MODELVIEW_MATRIX)

        # 应用变换
        glTranslatef(0, 1, 0)
        glRotatef(self.scene_time, 0, 1, 0)
        glColor3f(0.8, 0.2, 0.2)
        # 修复（D3）：复用惰性创建的 quadric —— 旧代码每帧 gluNewQuadric() 从不删除
        if self._quadric is None:
            self._quadric = gluNewQuadric()
        gluSphere(self._quadric, 1.0, 32, 32)

        # 恢复原始矩阵状态
        glLoadMatrixd(original_matrix)
    def draw_surrounding_objects(self):
        count = 5  # 使用优化的矩阵管理，完全避免堆栈操作
        glMatrixMode(GL_MODELVIEW)  # 明确设置矩阵模式

        # 保存原始矩阵状态
        original_matrix = glGetDoublev(GL_MODELVIEW_MATRIX)

        for i in range(count):
            glLoadMatrixd(original_matrix)  # 恢复原始矩阵
            angle = i * (360/count)
            radius = 3 + math.sin(self.scene_time * 0.02 + i) * 0.5
            x = radius * math.cos(math.radians(angle))
            z = radius * math.sin(math.radians(angle))
            y = 1 + math.sin(self.scene_time * 0.05 + i) * 0.5

            glTranslatef(x, y, z)
            glRotatef(self.scene_time * 2, 1, 1, 1)

            r = (math.sin(i + self.scene_time * 0.01) + 1) / 2
            g = (math.sin(i + self.scene_time * 0.02) + 1) / 2
            b = (math.sin(i + self.scene_time * 0.03) + 1) / 2
            glColor3f(r, g, b)

            size = 0.2 + math.sin(self.scene_time * 0.02 + i) * 0.05
            # 统一使用手绘六面体(纯 GL 1.1):不再调用 glutSolidCube ——
            # 实测 freeglut 函数指针绑定无效的环境下(部分国产化/精简系统
            # 及 Windows PyOpenGL 组合)它每帧抛 access violation,导致
            # 复杂场景渲染中断、日志刷屏。手绘六面体视觉一致且零依赖。
            self._draw_cube_fallback(size)

        # 恢复原始矩阵状态
        glLoadMatrixd(original_matrix)

    @staticmethod
    def _draw_cube_fallback(size):
        """无 freeglut 系统的立方体绘制兜底:手绘六面体替代 glutSolidCube。
        仅用 GL 1.1 立即模式核心函数,任何 OpenGL 实现都支持。"""
        h = size / 2.0
        # 6 个面,每面 4 个顶点
        faces = [
            (-h, -h,  h), (h, -h,  h), (h,  h,  h), (-h,  h,  h),   # 前
            (h, -h, -h), (-h, -h, -h), (-h,  h, -h), (h,  h, -h),   # 后
            (h, -h,  h), (h, -h, -h), (h,  h, -h), (h,  h,  h),     # 右
            (-h, -h, -h), (-h, -h,  h), (-h,  h,  h), (-h,  h, -h), # 左
            (-h,  h,  h), (h,  h,  h), (h,  h, -h), (-h,  h, -h),   # 上
            (-h, -h, -h), (h, -h, -h), (h, -h,  h), (-h, -h,  h),   # 下
        ]
        normals = [
            (0, 0, 1), (0, 0, -1), (1, 0, 0),
            (-1, 0, 0), (0, 1, 0), (0, -1, 0),
        ]
        glBegin(GL_QUADS)
        for i in range(6):
            glNormal3f(*normals[i])
            for v in faces[i * 4:(i + 1) * 4]:
                glVertex3f(*v)
        glEnd()

    def draw_particles(self):
        glDisable(GL_LIGHTING)
        glEnable(GL_BLEND)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)

        glBegin(GL_QUADS)
        for p in self.particles:
            glColor4f(*p['color'])
            size = p['size']
            x, y, z = p['position']

            glVertex3f(x-size, y-size, z)
            glVertex3f(x+size, y-size, z)
            glVertex3f(x+size, y+size, z)
            glVertex3f(x-size, y+size, z)

            # 更新粒子位置
            p['position'][0] += p['velocity'][0]
            p['position'][1] += p['velocity'][1]
            p['position'][2] += p['velocity'][2]
            p['life'] -= 1

            # 重置死亡的粒子
            if p['life'] <= 0 or y < 0:
                p['position'] = [random.uniform(-10, 10),
                                random.uniform(0, 5),
                                random.uniform(-10, 10)]
                p['life'] = random.uniform(50, 200)
        glEnd()

        glDisable(GL_BLEND)
        glEnable(GL_LIGHTING)

    def start_animation(self):
        self.animation_running = True
        self.timer.start(33)  # 30fps

    def stop_animation(self):
        try:
            self.animation_running = False
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
                from aging_test_gui import _gl_free_vram_pool
                self.makeCurrent()
                _gl_free_vram_pool(self._vram_pool)
                self.doneCurrent()
            except Exception:
                pass
            self._vram_pool = []