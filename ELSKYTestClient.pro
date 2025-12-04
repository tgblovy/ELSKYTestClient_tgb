QT       += core gui network widgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = ELSKY_test_client
TEMPLATE = app

# 设置C++标准
CONFIG += c++11

# 发布模式
#CONFIG += release

# Qt模块（必须包含widgets）
QT += core gui
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

# C++标准
CONFIG += c++11

# 调试配置（关键部分）
CONFIG += debug          # 启用调试
CONFIG -= release        # 禁用发布模式
CONFIG -= release_debug  # 禁用混合模式

# 平台特定配置
QMAKE_CXXFLAGS +=
QMAKE_CXXFLAGS += -march=armv8-a
message("编译飞腾ARM64版本")

SOURCES += \
    main.cpp \
    core/auto_start_manager.cpp \
    core/client_window.cpp \
    core/basic_tester.cpp \
    core/server_connector.cpp \
    core/system_controller.cpp \
    core/network_speed_tester.cpp \
    core/wake_on_lan.cpp \
    core/cycle_controller.cpp \
    core/system_monitor.cpp \
    core/device_manager.cpp \
    core/logger.cpp \
    dialogs/com_test_dialog.cpp \
    dialogs/usb_speed_dialog.cpp \
    common/protocol.cpp

HEADERS += \
    core/auto_start_manager.h \
    core/client_window.h \
    core/basic_tester.h \
    core/server_connector.h \
    core/system_controller.h \
    core/network_speed_tester.h \
    core/wake_on_lan.h \
    core/cycle_controller.h \
    core/system_monitor.h \
    core/device_manager.h \
    core/logger.h \
    dialogs/com_test_dialog.h \
    dialogs/usb_speed_dialog.h \
    common/protocol.h

FORMS +=

# 资源文件
RESOURCES +=

# 依赖库
LIBS += -lpthread

# 输出目录 - 使用相对路径避免变量转义问题
DESTDIR = ./bin
OBJECTS_DIR = ./build/obj
MOC_DIR = ./build/moc
RCC_DIR = ./build/rcc
UI_DIR = ./build/ui

# 包含路径
INCLUDEPATH += ./core \
               ./dialogs \
               ./common
