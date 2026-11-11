QT       += core gui network widgets

# QtOpenGL 模块条件化（2026-10-15 兆芯/麒麟2503 编译修复）:
# 主程序代码只使用 QtGui 自带的 QOpenGLContext/QOpenGLShaderProgram 等
# （均有 #ifndef QT_NO_OPENGL 保护），不使用旧式 QtOpenGL 模块（QGLWidget）。
# 麒麟2503 等系统默认未安装 libqt5opengl5-dev 时，无条件 QT += opengl 会让
# qmake 直接报 "Project ERROR: Unknown module(s) in QT: opengl" 编译失败。
# 条件化后：有该模块的平台照常启用，缺失的平台自动跳过，功能不受影响
# （跑分测试内置 GL 基准走 QtGui 的 QOpenGL 类，不依赖 QtOpenGL 模块）。
qtHaveModule(opengl) {
    QT += opengl
    message("✓ 检测到 QtOpenGL 模块，已启用")
} else {
    message("ℹ 未检测到 QtOpenGL 模块，跳过（代码不依赖它，功能不受影响）")
}

# 串口支持检测（多重方法：qtHaveModule + 文件探测）
# 修复: 原 contains(QT_CONFIG, serialport) 在某些Qt5版本上失效
# 修复: 宏名统一为 QT_SERIALPORT_LIB（与代码中 #ifdef 一致）
defineTest(checkSerialPort) {
    # 方法1: qtHaveModule (Qt5.3+ 推荐)
    qtHaveModule(serialport) {
        return(true)
    }
    # 方法2: 直接检测模块头文件是否存在
    exists($$[QT_INSTALL_HEADERS]/QtSerialPort/QSerialPort) {
        return(true)
    }
    # 方法3: 检测 QT_CONFIG 中的 serialport (兼容旧版)
    contains(QT_CONFIG, serialport) {
        return(true)
    }
    return(false)
}

checkSerialPort() {
    QT += serialport
    # 注意: 当 QT += serialport 成功时，qmake 会自动定义 QT_SERIALPORT_LIB
    # 不需要手动 DEFINES += QT_SERIALPORT_LIB
    message("✓ 启用串口支持 (Qt SerialPort)")
} else {
    warning("⚠ 警告: 未检测到 Qt SerialPort 模块，将禁用串口功能")
    warning("  请安装: sudo apt install libqt5serialport5 libqt5serialport5-dev")
    warning("  或:     sudo yum install qt5-qtserialport qt5-qtserialport-devel")
}

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = ELSKY_test_client
TEMPLATE = app

# 设置C++标准
CONFIG += c++11

# 发布模式
CONFIG += release

# 平台特定配置 - 多架构支持（飞腾ARM64 / X86 / 龙芯 LoongArch / 老龙芯 MIPS64el）
# 【龙芯兼容】判定优先级：构建脚本注入 > qmake 自动识别 > 既有 aarch64/x86 分支。
# 关键：龙芯分支绝不能加 -march=armv8-a / -march=x86-64 —— 龙芯 g++ 不识别
# 这类 ARM/X86 参数，硬加会直接编译失败；使用编译器默认原生架构即可。
# 注入变量 ELSKY_TARGET_SUFFIX=loong 由 cross_platform_build.sh 传入，
# 用于规避部分国产系统 qmake -query QMAKE_HOST.arch 取值非标准的问题。
contains(ELSKY_TARGET_SUFFIX, loong) {
    message("编译龙芯版本 (LoongArch/MIPS64el, 构建脚本注入)")
    TARGET = ELSKY_test_client_loong
} else: contains(QMAKE_HOST.arch, aarch64) {
    # 飞腾ARM64架构
    QMAKE_CXXFLAGS += -march=armv8-a
    message("编译飞腾ARM64版本")
    TARGET = ELSKY_test_client_ft
} else: contains(QMAKE_HOST.arch, x86_64|i386|i686) {
    # X86架构（包括兆芯、海光、Intel、AMD平台）
    QMAKE_CXXFLAGS += -march=x86-64
    message("编译X86版本")
    TARGET = ELSKY_test_client_x86
} else: contains(QMAKE_HOST.arch, loongarch64|loong64|loongarch|mips64el|mips64|mipsel) {
    # 兜底：未经构建脚本、直接 qmake 时也能自动识别龙芯
    message("编译龙芯版本 (LoongArch/MIPS64el, qmake 自动识别)")
    TARGET = ELSKY_test_client_loong
}

SOURCES += \
	core/illegal_shutdown_detector.cpp \
    main.cpp \
    core/auto_start_manager.cpp \
    core/client_window.cpp \
    core/basic_tester.cpp \
    core/benchmark_tester.cpp \
    core/server_connector.cpp \
    core/system_controller.cpp \
    core/network_speed_tester.cpp \
    core/wake_on_lan.cpp \
    core/cycle_controller.cpp \
    core/system_monitor.cpp \
    core/device_manager.cpp \
    core/logger.cpp \
    core/unified_control_manager.cpp \
    core/privilege_helper.cpp \
    dialogs/com_test_dialog.cpp \
    dialogs/usb_speed_dialog.cpp \
    dialogs/unified_control_dialog.cpp \
    common/protocol.cpp

HEADERS += \
    core/auto_start_manager.h \
    core/client_window.h \
    core/basic_tester.h \
    core/benchmark_tester.h \
	core/illegal_shutdown_detector.h \
    core/server_connector.h \
    core/system_controller.h \
    core/network_speed_tester.h \
    core/wake_on_lan.h \
    core/cycle_controller.h \
    core/system_monitor.h \
    core/device_manager.h \
    core/logger.h \
    core/unified_control_manager.h \
    core/privilege_helper.h \
    dialogs/com_test_dialog.h \
    dialogs/usb_speed_dialog.h \
    dialogs/unified_control_dialog.h \
    common/protocol.h

FORMS +=

# 资源文件
RESOURCES +=

# 依赖库
LIBS += -lpthread

# 让可执行文件自带 rpath，优先从 $ORIGIN/lib 查找动态库
# 这样即使不通过启动脚本（未设 LD_LIBRARY_PATH）也能找到同目录的 lib/
# 修复（B2/RPATH 失效）：旧写法 QMAKE_LFLAGS += -Wl,-rpath,'$$ORIGIN/lib'
# 中的 $$ORIGIN 被 qmake 当作未定义变量展开为空串，链接产物 RUNPATH 只剩
# 空条目（readelf -d 显示 "/lib:"），$ORIGIN 完全丢失，直接双击运行时
# 找不到包内 lib/。
# 正确做法：用 qmake 内置 $$LITERAL_DOLLAR（值 = 一个字面 $）拼接出
# 字面 $ORIGIN，经 QMAKE_RPATHDIR 生成 -Wl,-rpath,$ORIGIN/lib，
# 最终产物 RUNPATH 为 "$ORIGIN/lib:$ORIGIN"（已实测验证）。
# 注意：qmake 默认产出 DT_RUNPATH（new-dtags），优先级低于 LD_LIBRARY_PATH，
# 启动脚本"LD_LIBRARY_PATH 指向包内 lib/ 优先"的三级自愈机制不受影响。
QMAKE_RPATHLINKDIR = $$PWD/lib
ELSKY_DOLLAR = $$LITERAL_DOLLAR
QMAKE_RPATHDIR += $${ELSKY_DOLLAR}ORIGIN/lib $${ELSKY_DOLLAR}ORIGIN

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
