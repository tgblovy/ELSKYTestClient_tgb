from OpenGL.GL import *
from OpenGL.GLU import *
from OpenGL.GLUT import glutSolidCube, glutInit, glutInitDisplayMode, GLUT_RGB, GLUT_DOUBLE, GLUT_DEPTH  # 添加必要的GLUT常量
from PyQt5.QtOpenGL import QGLWidget
from PyQt5.QtCore import QTimer
import math
import random
from PIL import Image

class ComplexScene(QGLWidget):
    def __init__(self):
        super().__init__()
        self.animation_running = False
        self.timer = QTimer()
        self.timer.timeout.connect(self.update)

        # 场景参数
        self.scene_time = 0
        self.camera_angle = 0
        self.camera_height = 1.5
        self.light_positions = []
        self.particles = []
        self.init_scene()

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
        # 初始化FreeGLUT
        import sys
        glutInit(sys.argv)
        glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH)

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
        glViewport(0, 0, width, height)
        glMatrixMode(GL_PROJECTION)
        glLoadIdentity()
        gluPerspective(45, width/height, 0.1, 100.0)

    def paintGL(self):
        if not self.animation_running:
            return

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
        gluSphere(gluNewQuadric(), 1.0, 32, 32)

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
            glutSolidCube(size)

        # 恢复原始矩阵状态
        glLoadMatrixd(original_matrix)

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
        self.animation_running = False
        self.timer.stop()