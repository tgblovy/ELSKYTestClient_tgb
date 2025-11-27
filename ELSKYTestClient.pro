QT += core gui widgets network serialport
CONFIG += c++17

TARGET = ELSKYTestClient
TEMPLATE = app

# 源文件 - 确保没有重复
SOURCES += \
    main.cpp \
    core/client_window.cpp \
    core/basic_tester.cpp \
    core/server_connector.cpp \
    core/auto_start_manager.cpp \
    core/wake_on_lan.cpp \
    core/cycle_controller.cpp \
    core/system_monitor.cpp \
    core/device_manager.cpp \
    core/logger.cpp \
    core/system_controller.cpp \
    dialogs/com_test_dialog.cpp \
    dialogs/usb_speed_dialog.cpp \
    common/protocol.cpp

# 头文件
HEADERS += \
    core/client_window.h \
    core/basic_tester.h \
    core/server_connector.h \
    core/auto_start_manager.h \
    core/wake_on_lan.h \
    core/cycle_controller.h \
    core/system_monitor.h \
    core/device_manager.h \
    core/logger.h \
    core/system_controller.h \
    dialogs/com_test_dialog.h \
    dialogs/usb_speed_dialog.h \
    common/protocol.h

# 系统架构检测
contains(QT_ARCH, x86_64) {
    DEFINES += X86_ARCHITECTURE
    message("Building for X86_64 architecture")
} else {
contains(QT_ARCH, i386) {
    DEFINES += X86_ARCHITECTURE
    message("Building for X86 architecture")
} else {
contains(QT_ARCH, arm64) {
    DEFINES += ARM_ARCHITECTURE
    message("Building for ARM64 architecture")
} else {
contains(QT_ARCH, aarch64) {
    DEFINES += ARM_ARCHITECTURE
    message("Building for AARCH64 architecture")
} else {
    DEFINES += UNKNOWN_ARCHITECTURE
    message("Building for unknown architecture: $$QT_ARCH")
}}}}

# 确保串口功能可用
DEFINES += QT_SERIALPORT_LIB

# 编译选项
QMAKE_CXXFLAGS += -O2
QMAKE_LFLAGS += -no-pie

# 目标目录
DESTDIR = $$PWD/bin
OBJECTS_DIR = $$PWD/build/obj
MOC_DIR = $$PWD/build/moc
RCC_DIR = $$PWD/build/rcc
UI_DIR = $$PWD/build/ui
