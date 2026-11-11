import sys
import time
import threading
import psutil
import os
import random
import math
from PyQt5.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
                             QCheckBox, QLineEdit, QPushButton, QLabel, QProgressBar,
                             QGroupBox, QFormLayout, QMessageBox)
from PyQt5.QtCore import Qt, QTimer, QThread, pyqtSignal, QPointF
from PyQt5.QtGui import QPainter, QColor, QPen, QRadialGradient, QBrush, QPolygonF
from OpenGL.GL import *
from OpenGL.GLU import *
from PyQt5.QtOpenGL import QGLWidget
from complex_scene import ComplexScene

class AgingTestGUI(QMainWindow):
    def __init__(self, auto_start=False):
        super().__init__()
        self.setWindowTitle("系统硬件老化测试工具")
        self.setGeometry(100, 100, 800, 600)

        self.auto_start = auto_start

        # 初始化变量
        self.running = False
        self.aging_duration = 0
        self.start_time = 0
        self.threads = []
        self.timer = QTimer()
        self.timer.timeout.connect(self.update_status)

        # 先创建UI控件
        self.create_ui()

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
        self.gpu3d_check = QCheckBox("3D显卡满载")
        self.network_check = QCheckBox("网络测试")

        hardware_layout.addWidget(self.cpu_check)
        hardware_layout.addWidget(self.memory_check)
        hardware_layout.addWidget(self.disk_check)
        hardware_layout.addWidget(self.gpu2d_check)
        hardware_layout.addWidget(self.gpu3d_check)
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
        status_group = QGroupBox("系统状态")
        status_layout = QFormLayout()

        self.cpu_usage = QProgressBar()
        self.memory_usage = QProgressBar()
        self.disk_usage = QProgressBar()
        self.time_remaining = QLabel("00:00:00")
        self.gpu_mem_usage = QProgressBar()

        status_layout.addRow("CPU使用率:", self.cpu_usage)
        status_layout.addRow("内存使用率:", self.memory_usage)
        status_layout.addRow("磁盘使用率:", self.disk_usage)
        status_layout.addRow("显存使用率:", self.gpu_mem_usage)
        status_layout.addRow("剩余时间:", self.time_remaining)

        status_group.setLayout(status_layout)
        main_layout.addWidget(status_group)

        # 创建独立的2D/3D窗口
        self.canvas2d_window = QMainWindow()
        self.canvas2d = Canvas2D()
        self.canvas2d_window.setCentralWidget(self.canvas2d)
        self.canvas2d_window.setWindowTitle("2D显卡测试")
        self.canvas2d_window.resize(400, 400)

        self.canvas3d_window = QMainWindow()
        self.canvas3d = Canvas3D()
        self.complex_scene = ComplexScene()
        self.canvas3d_window.setCentralWidget(self.canvas3d)
        self.canvas3d_window.setWindowTitle("3D显卡测试")
        self.canvas3d_window.resize(400, 400)

        # 场景切换定时器
        self.scene_switch_timer = QTimer()
        self.scene_switch_timer.timeout.connect(self.switch_scene)
        self.current_scene = "simple"  # simple or complex

        # 连接信号与槽
        self.start_btn.clicked.connect(self.start_aging)
        self.stop_btn.clicked.connect(self.stop_aging)

    def start_aging(self):
        # 验证输入
        try:
            self.aging_duration = int(self.duration_edit.text()) * 3600  # 小时转换为秒
            if self.aging_duration <= 0:
                raise ValueError
        except ValueError:
            QMessageBox.warning(self, "错误", "请输入有效的老化时长(正整数)")
            return

        # 启动场景切换定时器 (1分钟后切换到复杂场景)
        self.scene_switch_timer.start(60000)  # 60秒 = 1分钟

        # 检查至少选择一个硬件
        if not (self.cpu_check.isChecked() or self.memory_check.isChecked() or
                self.disk_check.isChecked() or self.gpu2d_check.isChecked() or
                self.gpu3d_check.isChecked()):
            QMessageBox.warning(self, "错误", "请至少选择一个硬件进行老化测试")
            return

        # 初始化状态
        self.running = True
        self.start_time = time.time()
        self.start_btn.setEnabled(False)
        self.stop_btn.setEnabled(True)
        self.timer.start(1000)  # 每秒更新一次状态

        # 启动老化线程
        if self.cpu_check.isChecked():
            cpu_thread = threading.Thread(target=self.cpu_load)
            self.threads.append(cpu_thread)
            cpu_thread.start()

        if self.memory_check.isChecked():
            memory_thread = threading.Thread(target=self.memory_load)
            self.threads.append(memory_thread)
            memory_thread.start()

        if self.disk_check.isChecked():
            disk_thread = threading.Thread(target=self.disk_load)
            self.threads.append(disk_thread)
            disk_thread.start()

        if self.gpu2d_check.isChecked():
            try:
                self.canvas2d_window.show()
                self.canvas2d.start_animation()
            except Exception as e:
                print(f"2D窗口初始化错误: {e}")
                QMessageBox.warning(self, "错误", f"2D窗口初始化失败: {str(e)}")

        if self.gpu3d_check.isChecked():
            try:
                self.canvas3d_window.show()
                self.canvas3d.start_animation()
            except Exception as e:
                print(f"3D窗口初始化错误: {e}")
                QMessageBox.warning(self, "错误", f"3D窗口初始化失败: {str(e)}")

        if self.network_check.isChecked():
            network_thread = threading.Thread(target=self.network_test)
            self.threads.append(network_thread)
            network_thread.start()

    def stop_aging(self):
        self.running = False
        self.start_btn.setEnabled(True)
        self.stop_btn.setEnabled(False)

        # 停止所有线程
        for thread in self.threads:
            thread.join(timeout=1)
        self.threads.clear()

        # 停止动画并关闭窗口
        self.canvas2d.stop_animation()
        self.canvas3d.stop_animation()
        self.complex_scene.stop_animation()
        self.scene_switch_timer.stop()
        self.canvas2d_window.close()
        self.canvas3d_window.close()

        # 重置老化时长和倒计时显示
        self.aging_duration = 0
        self.start_time = 0
        self.time_remaining.setText("00:00:00")

        QMessageBox.information(self, "完成", "老化测试已停止")

    def switch_scene(self):
        """切换3D场景"""
        try:
            # 安全检查窗口有效性
            if not hasattr(self, 'canvas3d_window') or self.canvas3d_window is None:
                print("错误：canvas3d_window不存在")
                return

            # 检查窗口是否已经被关闭
            if not self.canvas3d_window.isVisible():
                print("错误：canvas3d_window已被关闭或隐藏")
                return

            if self.current_scene == "simple":
                # 切换到复杂场景
                # 先停止当前动画，带安全检查
                if hasattr(self, 'canvas3d') and self.canvas3d is not None:
                    try:
                        self.canvas3d.stop_animation()
                    except Exception as e:
                        print(f"停止canvas3d动画时出错: {e}")

                # 确保complex_scene对象存在
                if not hasattr(self, 'complex_scene') or self.complex_scene is None:
                    self.complex_scene = ComplexScene()

                # 安全地设置中央窗口部件
                try:
                    self.canvas3d_window.setCentralWidget(self.complex_scene)
                    self.complex_scene.start_animation()
                except Exception as e:
                    print(f"设置复杂场景时出错: {e}")
                    # 如果出错，尝试重新创建
                    try:
                        self.complex_scene.deleteLater()
                    except:
                        pass
                    self.complex_scene = ComplexScene()
                    self.canvas3d_window.setCentralWidget(self.complex_scene)
                    self.complex_scene.start_animation()

                self.current_scene = "complex"
            else:
                # 切换回简单场景
                # 先停止当前动画，带安全检查
                if hasattr(self, 'complex_scene') and self.complex_scene is not None:
                    try:
                        self.complex_scene.stop_animation()
                    except Exception as e:
                        print(f"停止complex_scene动画时出错: {e}")

                # 确保canvas3d对象存在
                if not hasattr(self, 'canvas3d') or self.canvas3d is None:
                    self.canvas3d = Canvas3D()

                # 安全地设置中央窗口部件
                try:
                    self.canvas3d_window.setCentralWidget(self.canvas3d)
                    self.canvas3d.start_animation()
                except Exception as e:
                    print(f"设置简单场景时出错: {e}")
                    # 如果出错，尝试重新创建
                    try:
                        self.canvas3d.deleteLater()
                    except:
                        pass
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
            # 安全关闭窗口
            try:
                if hasattr(self, 'canvas3d_window') and self.canvas3d_window is not None:
                    self.canvas3d_window.close()
            except:
                pass

    def update_status(self):
        # 更新系统状态
        self.cpu_usage.setValue(int(psutil.cpu_percent()))
        self.memory_usage.setValue(int(psutil.virtual_memory().percent))
        self.disk_usage.setValue(int(psutil.disk_usage('/').percent))

        # 获取显存使用率(模拟)
        gpu_usage = random.randint(70, 95) if self.gpu2d_check.isChecked() or self.gpu3d_check.isChecked() else random.randint(5, 20)
        self.gpu_mem_usage.setValue(gpu_usage)

        # 更新剩余时间
        elapsed = time.time() - self.start_time
        remaining = max(0, self.aging_duration - elapsed)
        hours = int(remaining // 3600)
        minutes = int((remaining % 3600) // 60)
        seconds = int(remaining % 60)
        self.time_remaining.setText(f"{hours:02d}:{minutes:02d}:{seconds:02d}")

        # 检查是否超时（只有在老化测试运行时才检查）
        if self.running and remaining <= 0:
            self.stop_aging()

    def cpu_load(self):
        import multiprocessing
        cpu_count = multiprocessing.cpu_count()
        processes = []

        def worker():
            while self.running:
                # 更高效的计算密集型任务
                for _ in range(10**6):
                    pass

        while self.running:
            if len(processes) < cpu_count:
                p = multiprocessing.Process(target=worker)
                p.start()
                processes.append(p)
            time.sleep(1)

        for p in processes:
            p.terminate()

    def memory_load(self):
        import psutil
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
        import threading
        import tempfile

        # 创建多个线程同时进行磁盘操作
        thread_count = 4  # 可以根据需要调整
        threads = []

        def disk_worker():
            # 创建一个大的临时文件
            with tempfile.NamedTemporaryFile(delete=False) as tmp:
                file_path = tmp.name

            try:
                # 使用更大的数据块和更密集的写入
                block_size = 1024 * 1024 * 50  # 50MB块
                data = os.urandom(block_size)

                while self.running:
                    with open(file_path, 'wb') as f:
                        # 循环写入多次以增加负载
                        for _ in range(10):
                            f.write(data)
                            f.flush()

                    # 随机写入模式
                    with open(file_path, 'rb+') as f:
                        for _ in range(20):
                            pos = random.randint(0, block_size * 9)
                            f.seek(pos)
                            f.write(os.urandom(1024 * 1024))
                            f.flush()

                    time.sleep(0.01)  # 极短间隔，保持高负载
            finally:
                # 清理临时文件
                if os.path.exists(file_path):
                    os.remove(file_path)

        # 启动多个磁盘工作线程
        for _ in range(thread_count):
            t = threading.Thread(target=disk_worker)
            t.start()
            threads.append(t)

        # 等待所有线程完成
        for t in threads:
            t.join()

    def network_test(self):
        while self.running:
            try:
                # 测试网络连通性
                os.system("ping -c 1 8.8.8.8 > /dev/null 2>&1")
                # 测试下载速度 (1MB测试文件)
                os.system("wget -O /dev/null http://speedtest.ftp.otenet.gr/files/test1Mb.db > /dev/null 2>&1")
            except Exception as e:
                print(f"网络测试错误: {e}")
            time.sleep(5)

class Canvas2D(QWidget):
    def __init__(self):
        super().__init__()
        self.animation_running = False
        self.angle = 0
        self.timer = QTimer()
        self.timer.timeout.connect(self.update)

    def start_animation(self):
        try:
            self.animation_running = True
            self.timer.start(50)  # 20fps
        except Exception as e:
            print(f"动画启动错误: {e}")
            self.animation_running = False

    def stop_animation(self):
        self.animation_running = False
        self.timer.stop()

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
        self.timer = QTimer()
        self.timer.timeout.connect(self.update)

    def initializeGL(self):
        glEnable(GL_DEPTH_TEST)
        glClearColor(0.1, 0.1, 0.1, 1.0)

    def resizeGL(self, width, height):
        glViewport(0, 0, width, height)
        glMatrixMode(GL_PROJECTION)
        glLoadIdentity()
        gluPerspective(45, width/height, 0.1, 100.0)

    def paintGL(self):
        if not self.animation_running:
            return

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
        self.animation_running = False
        self.timer.stop()

if __name__ == "__main__":
    # 检测是否为统一控制自动老化模式（双重检测：命令行参数 + 环境变量）
    auto_start = "--auto-start" in sys.argv or os.environ.get("AGING_AUTO_START", "") == "1"

    app = QApplication(sys.argv)
    window = AgingTestGUI(auto_start=auto_start)
    window.show()
    sys.exit(app.exec_())