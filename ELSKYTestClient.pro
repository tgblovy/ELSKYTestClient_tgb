QT       += core gui network widgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = ELSKY_test_client
TEMPLATE = app

# 设置C++标准
CONFIG += c++11

# 发布模式
CONFIG += release

# 平台特定配置
contains(ARCH, aarch64) {
    # 飞腾平台优化
    QMAKE_CXXFLAGS += -march=armv8-a
    message("编译飞腾ARM64版本")
} else {
    # X86平台优化
    QMAKE_CXXFLAGS += -march=x86-64
    message("编译X86版本")
}

SOURCES += \
    main.cpp \
	core/auto_start_manager.cpp \
    core/client_window.cpp \
    core/basic_tester.cpp \
    core/server_connector.cpp \
    core/system_controller.cpp \
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

# 输出目录
DESTDIR = $$PWD/bin
OBJECTS_DIR = $$PWD/build/obj
MOC_DIR = $$PWD/build/moc
RCC_DIR = $$PWD/build/rcc
UI_DIR = $$PWD/build/ui

# 包含路径
INCLUDEPATH += $$PWD/core \
               $$PWD/dialogs \
               $$PWD/common
