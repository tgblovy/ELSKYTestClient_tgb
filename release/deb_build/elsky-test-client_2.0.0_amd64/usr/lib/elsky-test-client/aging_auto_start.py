#!/usr/bin/env python3
"""统一控制自动老化入口 - 跳过设置界面，直接以默认配置开始老化
默认测试项：CPU满载、内存满载、硬盘满载、2D显卡满载、3D显卡满载
默认老化时间：7天（10080分钟）
"""
import sys
import os

# 确保能导入同目录下的模块
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from PyQt5.QtWidgets import QApplication
from aging_test_gui import AgingTestGUI

if __name__ == "__main__":
    app = QApplication(sys.argv)
    # 硬编码 auto_start=True，不依赖任何命令行参数或环境变量
    window = AgingTestGUI(auto_start=True)
    window.show()
    sys.exit(app.exec_())
