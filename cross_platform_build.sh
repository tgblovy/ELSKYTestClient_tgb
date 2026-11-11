#!/bin/bash
echo "=== 研盛测试客户端跨平台编译脚本 ==="

# 检测当前平台
ARCH=$(uname -m)
if [ "$ARCH" = "aarch64" ]; then
    PLATFORM="ft"
    PLATFORM_NAME="飞腾ARM64"
    COMPILER_FLAGS="-march=armv8-a"
    COMPILER_MESSAGE="编译飞腾ARM64版本"
    echo "✓ 检测到飞腾ARM64平台"
    
    # 检测具体飞腾型号
    CPU_MODEL=$(grep -i "model name" /proc/cpuinfo | head -1 | tr -d ' ' | cut -d: -f2)
    if echo "$CPU_MODEL" | grep -q "FT-2000"; then
        PLATFORM="ft2000"
        PLATFORM_NAME="飞腾D2000"
        COMPILER_FLAGS="$COMPILER_FLAGS -mtune=cortex-a72"
        echo "✓ 检测到飞腾D2000平台"
    elif echo "$CPU_MODEL" | grep -q "FT-2500"; then
        PLATFORM="ft3000" 
        PLATFORM_NAME="飞腾D3000"
        echo "✓ 检测到飞腾D3000平台"
    fi
elif [[ "$ARCH" =~ "x86_64" ]]; then
    PLATFORM="x86"
    PLATFORM_NAME="X86_64"
    COMPILER_FLAGS="-march=x86-64"
    COMPILER_MESSAGE="编译X86_64版本"
    echo "✓ 检测到X86_64平台"
elif [[ "$ARCH" =~ "i386" || "$ARCH" =~ "i686" ]]; then
    PLATFORM="x86"
    PLATFORM_NAME="X86"
    COMPILER_FLAGS="-march=i686"
    COMPILER_MESSAGE="编译X86版本"
    echo "✓ 检测到X86平台"
elif [[ "$ARCH" =~ ^(loongarch64|loong64|loongarch|mips64el|mips64|mipsel)$ ]]; then
    # 龙芯 LoongArch(3A5000/3A6000/3C5000, uname=loongarch64/loong64)
    # 及老龙芯 MIPS64el(3A3000/3A4000, uname=mips64el)。
    # 不指定任何 -march：龙芯编译器不识别 ARM/X86 的 -march 参数，
    # 交给编译器按本机原生架构编译。COMPILER_FLAGS 仅用于末尾回显。
    PLATFORM="loong"
    PLATFORM_NAME="龙芯"
    COMPILER_FLAGS=""
    COMPILER_MESSAGE="编译龙芯版本(原生架构,不指定-march)"
    echo "✓ 检测到龙芯平台 ($ARCH)"
else
    echo "错误: 不支持的平台架构: $ARCH"
    exit 1
fi

echo "平台: $ARCH ($PLATFORM_NAME)"

# 产物平台后缀（S4 命名链修复）：与仓库 ELSKYTestClient.pro 的 TARGET 命名、
# 打包脚本（cross_platform_package*.sh）的 CURRENT_PLATFORM 取值三方一致——
# aarch64（含 D2000/D3000 细分型号）统一 ft；x86 家族统一 x86。
# 旧版按细分型号 mv 出 _ft2000/_ft3000，打包脚本与启动脚本只认 _ft/_x86，
# 在 D2000/D3000 上编译后打包必报"未找到编译输出"。
if [ "$ARCH" = "aarch64" ]; then
    PLATFORM_SUFFIX="ft"
elif [[ "$ARCH" =~ ^(loongarch64|loong64|loongarch|mips64el|mips64|mipsel)$ ]]; then
    # 龙芯独立后缀，与 .pro 的 TARGET、打包脚本的 PLATFORM 三方一致
    PLATFORM_SUFFIX="loong"
else
    PLATFORM_SUFFIX="x86"
fi

# 设置项目根目录
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
echo "项目根目录: $PROJECT_ROOT"

# 1. 清理编译文件
echo "清理编译文件..."
find "$PROJECT_ROOT" -name "Makefile" -delete
find "$PROJECT_ROOT" -name "*.o" -delete
find "$PROJECT_ROOT" -name "moc_*" -delete
find "$PROJECT_ROOT" -name "*.so" -delete
find "$PROJECT_ROOT" -name "ELSKY_test_client*" -type f -executable -delete

# 2. 检查Qt环境
echo "检查Qt环境..."

# 尝试在多个候选位置直接找 qmake
# 根因修复（统信1070编译报错 could not exec '.../qt4/bin/qmake'）:
# 统信1070 等系统 /usr/bin/qmake 是 qtchooser 链接，default.conf 指向 Qt4
# （且 qt4 二进制缺失）。旧逻辑会误选 Qt4/失效 qmake 且不校验大版本。
# 现在强制校验 `qmake -query QT_VERSION` 必须为 5.*，否则跳过该候选。
find_real_qmake() {
    local c out
    # glob 候选可覆盖所有架构（aarch64/x86_64/mips64el/loongarch64/sw_64 等）
    for c in /usr/lib/*-linux-*/qt5/bin/qmake \
             /usr/lib/qt5/bin/qmake \
             /usr/bin/qmake-qt5 \
             /usr/bin/qmake; do
        [ -x "$c" ] || continue
        out=$("$c" -query QT_VERSION 2>/dev/null) || continue
        case "$out" in
            5.*) echo "$c"; return 0 ;;
            *) echo "  跳过 $c（Qt 版本 ${out:-未知}，本项目仅支持 Qt5）" >&2 ;;
        esac
    done
    return 1
}

REAL_QMAKE=$(find_real_qmake)
if [ -n "$REAL_QMAKE" ]; then
    # 把真实 qmake 路径放到 PATH 最前面，覆盖失效/指向 Qt4 的 qmake 符号链接
    export PATH="$(dirname "$REAL_QMAKE"):$PATH"
    export QT_SELECT=5
    echo "  找到可用的 Qt5 qmake: $REAL_QMAKE"
fi

if ! command -v qmake >/dev/null 2>&1; then
    echo "错误: 未找到qmake，请确保Qt5开发包已安装"
    echo "请执行（按发行版选择）:"
    if command -v apt-get >/dev/null 2>&1; then
        echo "  Debian/Ubuntu/统信/麒麟: sudo apt-get install qtbase5-dev qtchooser qt5-qmake"
        echo "  或: sudo apt-get install qt5-default libqt5serialport5-dev"
    elif command -v yum >/dev/null 2>&1; then
        echo "  CentOS/RHEL: sudo yum install qt5-qtbase-devel qt5-qtserialport-devel"
    elif command -v dnf >/dev/null 2>&1; then
        echo "  Fedora: sudo dnf install qt5-qtbase-devel qt5-qtserialport-devel"
    fi
    exit 1
fi

# 检查编译工具链（g++/make，Ubuntu 默认不带 build-essential 会编译失败）
if ! command -v make >/dev/null 2>&1 || ! command -v g++ >/dev/null 2>&1; then
    MISSING_TOOLS=""
    command -v make  >/dev/null 2>&1 || MISSING_TOOLS="$MISSING_TOOLS make"
    command -v g++   >/dev/null 2>&1 || MISSING_TOOLS="$MISSING_TOOLS g++"
    echo "✗ 缺少编译工具: $MISSING_TOOLS"
    echo "  Ubuntu/Debian/统信/麒麟: sudo apt-get install build-essential"
    echo "  Fedora/CentOS:            sudo dnf install gcc-c++ make"
    exit 1
fi

# 严格校验：qmake 必须可执行且为 Qt5（拒绝 Qt4 / 损坏的 qtchooser 链接）
QMAKE_OUTPUT=$(qmake -v 2>&1)
QMAKE_VERSION=$(echo "$QMAKE_OUTPUT" | grep "Using Qt version" | awk '{print $4}')
case "$QMAKE_VERSION" in
    5.*) echo "✓ 检测到Qt版本: $QMAKE_VERSION (Qt5, 校验通过)" ;;
    *)
        echo "错误: 未找到可用的 Qt5 qmake（当前解析结果: ${QMAKE_VERSION:-不可用}）"
        echo "qmake -v 输出: $QMAKE_OUTPUT"
        echo ""
        echo "常见原因: /usr/bin/qmake 是 qtchooser 链接且指向 Qt4（统信1070 默认如此，qt4 二进制缺失）"
        echo "请执行以下命令安装 Qt5 开发环境后重试:"
        if command -v apt-get >/dev/null 2>&1; then
            echo "  统信UOS/麒麟/Debian/Ubuntu: sudo apt install qtbase5-dev qt5-qmake qtchooser libqt5serialport5-dev"
            echo "  旧版系统可加装:             sudo apt install qt5-default"
        elif command -v dnf >/dev/null 2>&1; then
            echo "  Fedora: sudo dnf install qt5-qtbase-devel qt5-qtserialport-devel"
        elif command -v yum >/dev/null 2>&1; then
            echo "  CentOS/RHEL: sudo yum install qt5-qtbase-devel qt5-qtserialport-devel"
        fi
        exit 1
        ;;
esac

# 2.5 检测并安装 Qt SerialPort 模块（编译期依赖）
echo "检查 Qt SerialPort 模块..."
SERIALPORT_OK=false

# 方法1: 检测头文件是否存在
# 注意：不要硬编码 /usr/lib/qt5/bin/qmake 路径（统信/不同发行版路径不同）
# glob 覆盖全部 Debian 多架构目录（aarch64/x86_64/arm/mips64el/loongarch64/sw_64 等）
if [ -d "$(qmake -query QT_INSTALL_HEADERS 2>/dev/null)/QtSerialPort" ] || \
   ls -d /usr/include/*-linux-*/qt5/QtSerialPort >/dev/null 2>&1 || \
   [ -d "/usr/include/qt5/QtSerialPort" ]; then
    SERIALPORT_OK=true
    echo "✓ 已检测到 Qt SerialPort 头文件"
fi

# 方法2: 用 pkg-config 检测
if [ "$SERIALPORT_OK" = false ] && command -v pkg-config >/dev/null 2>&1; then
    if pkg-config --exists Qt5SerialPort 2>/dev/null; then
        SERIALPORT_OK=true
        echo "✓ pkg-config 检测到 Qt5SerialPort"
    fi
fi

# 方法3: 用 qmake -query 检测模块
if [ "$SERIALPORT_OK" = false ]; then
    QTHDR=$(qmake -query QT_INSTALL_HEADERS 2>/dev/null)
    if [ -n "$QTHDR" ] && [ -d "$QTHDR/QtSerialPort" ]; then
        SERIALPORT_OK=true
        echo "✓ qmake 检测到 Qt SerialPort"
    fi
fi

# 如果未检测到，自动安装
if [ "$SERIALPORT_OK" = false ]; then
    echo "⚠ 未检测到 Qt SerialPort 模块，尝试自动安装..."
    if command -v apt-get >/dev/null 2>&1; then
        echo "使用 apt 安装: libqt5serialport5 libqt5serialport5-dev"
        pkexec apt-get install -y libqt5serialport5 libqt5serialport5-dev 2>&1
        if [ $? -eq 0 ]; then
            SERIALPORT_OK=true
            echo "✓ apt 安装 Qt SerialPort 成功"
        else
            echo "✗ apt 安装失败"
        fi
    elif command -v yum >/dev/null 2>&1; then
        echo "使用 yum 安装: qt5-qtserialport qt5-qtserialport-devel"
        pkexec yum install -y qt5-qtserialport qt5-qtserialport-devel 2>&1
        if [ $? -eq 0 ]; then
            SERIALPORT_OK=true
            echo "✓ yum 安装 Qt SerialPort 成功"
        else
            echo "✗ yum 安装失败"
        fi
    fi
fi

if [ "$SERIALPORT_OK" = false ]; then
    echo "============================================================"
    echo "✗ 警告: Qt SerialPort 模块未安装，串口功能将被禁用"
    echo "  请手动安装:"
    echo "    Debian/Ubuntu/统信/麒麟: sudo apt install libqt5serialport5 libqt5serialport5-dev"
    echo "    CentOS/RHEL:             sudo yum install qt5-qtserialport qt5-qtserialport-devel"
    if [ "$ARCH" = "aarch64" ]; then
        echo "  ★ 当前为飞腾ARM64主打平台，串口(COM)测试是核心功能——"
        echo "    离线编译机请提前准备对应发行版的 arm64 包:"
        echo "    libqt5serialport5 + libqt5serialport5-dev 的 .deb 离线安装"
    fi
    echo "  将继续编译，但 COM 测试功能将不可用"
    echo "============================================================"
else
    echo "✓ Qt SerialPort 模块就绪"
fi

# 3. 使用 Qt 项目文件
# 根因修复（S4）：旧版每次用 heredoc 全量覆盖 ELSKYTestClient.pro——
#   ① heredoc 内嵌的是旧快照：仓库 .pro 的任何改进（源文件增删、
#      RPATH 修复、串口检测增强）编译一次就被覆盖回退；
#   ② 旧 heredoc 的 rpath 写法经 bash/qmake 双重展开后 $$ORIGIN 丢失，
#      产物 RUNPATH 只剩空条目（B2 "直接双击找不到包内 lib/" 的反复来源）；
#   ③ 旧 heredoc 产出无后缀的 ELSKY_test_client 再 mv 加平台后缀，
#      ft2000/ft3000 机器产出 _ft2000/_ft3000，而打包脚本与 start_client.sh
#      只认 _ft/_x86——命名链断裂，D2000/D3000 上打包必报"未找到编译输出"。
# 现在：仓库 ELSKYTestClient.pro 是唯一权威版本——自带平台判断
# （aarch64→ELSKY_test_client_ft / x86→ELSKY_test_client_x86，与打包脚本
# 命名完全一致）与 -march 选项，直接编译它；仅当 .pro 缺失时才生成兜底副本。
echo "检查 Qt 项目文件..."
PRO_FILE="$PROJECT_ROOT/ELSKYTestClient.pro"
if [ -f "$PRO_FILE" ]; then
    echo "✓ 使用仓库项目文件: ELSKYTestClient.pro（权威版本，不覆盖）"
else
    echo "⚠ 未找到 ELSKYTestClient.pro，生成兜底项目文件（建议尽快从版本库恢复权威版本）"
    # 引用 heredoc（'PROEOF'）：内容原样写入，$ 无需转义，杜绝展开陷阱
    # [2026-10-15] opengl 条件化：代码只用 QtGui 的 QOpenGL* 类，不用 QtOpenGL 模块
    #（QGLWidget）；麒麟2503 无 libqt5opengl5-dev 时无条件 QT += opengl 会直接报
    # "Unknown module(s) in QT: opengl"。与权威 .pro 保持一致。
    cat > "$PRO_FILE" << 'PROEOF'
QT       += core gui network widgets

qtHaveModule(opengl) {
    QT += opengl
    message("✓ 检测到 QtOpenGL 模块，已启用")
} else {
    message("ℹ 未检测到 QtOpenGL 模块，跳过（代码不依赖它，功能不受影响）")
}

# 串口支持检测（多重方法：qtHaveModule + 文件探测）
defineTest(checkSerialPort) {
    qtHaveModule(serialport): return(true)
    exists($$[QT_INSTALL_HEADERS]/QtSerialPort/QSerialPort): return(true)
    contains(QT_CONFIG, serialport): return(true)
    return(false)
}
checkSerialPort() {
    QT += serialport
    message("✓ 启用串口支持 (Qt SerialPort)")
} else {
    warning("⚠ 警告: 未检测到 Qt SerialPort 模块，将禁用串口功能")
    warning("  请安装: sudo apt install libqt5serialport5 libqt5serialport5-dev")
    warning("  或:     sudo yum install qt5-qtserialport qt5-qtserialport-devel")
}

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = ELSKY_test_client
TEMPLATE = app

CONFIG += c++11
CONFIG += release

# 平台特定配置 - 多架构支持（命名与打包脚本 CURRENT_PLATFORM 一致）
# 龙芯分支不指定 -march，理由同权威 .pro。
contains(ELSKY_TARGET_SUFFIX, loong) {
    message("编译龙芯版本 (构建脚本注入)")
    TARGET = ELSKY_test_client_loong
} else: contains(QMAKE_HOST.arch, aarch64) {
    QMAKE_CXXFLAGS += -march=armv8-a
    message("编译飞腾ARM64版本")
    TARGET = ELSKY_test_client_ft
} else: contains(QMAKE_HOST.arch, x86_64|i386|i686) {
    QMAKE_CXXFLAGS += -march=x86-64
    message("编译X86版本")
    TARGET = ELSKY_test_client_x86
} else: contains(QMAKE_HOST.arch, loongarch64|loong64|loongarch|mips64el|mips64|mipsel) {
    message("编译龙芯版本 (qmake 自动识别)")
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
RESOURCES +=

LIBS += -lpthread

# 让可执行文件自带 rpath，优先从 $ORIGIN/lib 查找动态库
# 正确做法：用 qmake 内置 $$LITERAL_DOLLAR 拼出字面 $ORIGIN（$$ORIGIN
# 直接写会被 qmake 当未定义变量展开为空串——B2 bug 的根因）。
QMAKE_RPATHLINKDIR = $$PWD/lib
ELSKY_DOLLAR = $$LITERAL_DOLLAR
QMAKE_RPATHDIR += $${ELSKY_DOLLAR}ORIGIN/lib $${ELSKY_DOLLAR}ORIGIN

DESTDIR = $$PWD/bin
OBJECTS_DIR = $$PWD/build/obj
MOC_DIR = $$PWD/build/moc
RCC_DIR = $$PWD/build/rcc
UI_DIR = $$PWD/build/ui

INCLUDEPATH += $$PWD/core \
               $$PWD/dialogs \
               $$PWD/common
PROEOF
fi

# 飞腾细分型号的额外性能优化通过 qmake 命令行注入，不改动 .pro
# （.pro 的 aarch64 分支已含 -march=armv8-a，此处仅追加细分 tune）
EXTRA_QMAKE_ARGS=""
if [ "$PLATFORM" = "ft2000" ]; then
    EXTRA_QMAKE_ARGS="QMAKE_CXXFLAGS+=-mtune=cortex-a72"
    echo "✓ D2000 附加优化: -mtune=cortex-a72（qmake 命令行注入）"
fi
if [ "$PLATFORM" = "loong" ]; then
    # 龙芯：显式注入架构族，确保 .pro 命中龙芯分支（不写 -march），
    # 不依赖 QMAKE_HOST.arch 在各发行版上的取值差异。
    EXTRA_QMAKE_ARGS="${EXTRA_QMAKE_ARGS:+$EXTRA_QMAKE_ARGS }ELSKY_TARGET_SUFFIX=loong"
    echo "✓ 龙芯平台: 注入 ELSKY_TARGET_SUFFIX=loong（原生架构编译,不指定 -march）"
fi

# 4. 创建输出目录
mkdir -p "$PROJECT_ROOT/bin"
mkdir -p "$PROJECT_ROOT/build/obj"
mkdir -p "$PROJECT_ROOT/build/moc"
mkdir -p "$PROJECT_ROOT/build/rcc"
mkdir -p "$PROJECT_ROOT/build/ui"

# 5. 编译客户端
echo "编译研盛测试客户端..."
cd "$PROJECT_ROOT"

# 仓库 .pro 已按平台直接产出带后缀的可执行文件
# （aarch64→ELSKY_test_client_ft / x86→ELSKY_test_client_x86），无需 mv
EXPECTED_BIN="bin/ELSKY_test_client_${PLATFORM_SUFFIX}"

if qmake ELSKYTestClient.pro $EXTRA_QMAKE_ARGS && make -j$(nproc); then
    if [ -f "$EXPECTED_BIN" ]; then
        echo "✓ 客户端编译成功: $EXPECTED_BIN"

        # 显示文件信息
        echo "文件信息:"
        file "$EXPECTED_BIN"
    else
        echo "✗ 错误: 编译成功但未找到输出文件 $EXPECTED_BIN"
        echo "  bin/ 目录内容:"
        ls -la bin/ 2>/dev/null
        exit 1
    fi
else
    echo "✗ 客户端编译失败"
    exit 1
fi

# 6. 检查依赖库
echo "检查依赖库..."
ldd "$EXPECTED_BIN" | grep "not found" && {
    echo "⚠ 警告: 发现未解析的依赖"
}

# 7. 创建跨平台启动脚本
echo "创建跨平台启动脚本..."
cat > "bin/start_client.sh" << 'EOF'
#!/bin/bash
# 研盛测试客户端 - 跨平台启动脚本

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
echo "=== 研盛测试客户端 ==="
echo "脚本目录: $SCRIPT_DIR"

# 检测架构
ARCH=$(uname -m)
APP_NAME=""

if [ "$ARCH" = "aarch64" ]; then
    echo "检测到飞腾ARM64平台"
    APP_NAME="ELSKY_test_client_ft"
elif [[ "$ARCH" =~ "x86_64" ]]; then
    echo "检测到X86_64平台"
    APP_NAME="ELSKY_test_client_x86"
elif [[ "$ARCH" =~ "i386" || "$ARCH" =~ "i686" ]]; then
    echo "检测到X86平台"
    APP_NAME="ELSKY_test_client_x86"
elif [[ "$ARCH" =~ ^(loongarch64|loong64|loongarch|mips64el|mips64|mipsel)$ ]]; then
    echo "检测到龙芯平台 ($ARCH)"
    APP_NAME="ELSKY_test_client_loong"
else
    echo "错误: 不支持的平台架构: $ARCH"
    echo ""
    echo "支持的平台:"
    echo "  - x86_64, i386, i686 (兆芯、海光、Intel、AMD等)"
    echo "  - aarch64 (飞腾ARM64平台)"
    echo "  - loongarch64, loong64, mips64el (龙芯 LoongArch/MIPS64el)"
    echo ""
    echo "当前目录可执行文件:"
    ls -la "$SCRIPT_DIR"/ELSKY_test_client_* 2>/dev/null || echo "  无"
    exit 1
fi

# 检查可执行文件是否存在
if [ ! -f "$SCRIPT_DIR/$APP_NAME" ]; then
    echo "错误: 找不到对应平台的可执行文件: $APP_NAME"
    echo ""
    echo "当前目录文件:"
    ls -la "$SCRIPT_DIR"
    exit 1
fi

# GLIBC 兼容性预检（版本冲突无法通过安装依赖解决，必须明确诊断）
GLIBC_ERR=$(ldd "$SCRIPT_DIR/$APP_NAME" 2>/dev/null | grep 'version .GLIBC_' | head -3)
if [ -n "$GLIBC_ERR" ]; then
    echo "✗ [致命] 程序依赖的库与本系统 glibc 版本不兼容:"
    echo "$GLIBC_ERR"
    echo "  根因: 本部署包在更高 glibc 的系统上编译/打包（glibc 无法向下降级）。"
    echo "  解决: 请在与本系统相同版本（如统信UOS 1070/麒麟V10 = glibc 2.28）的系统上重新编译打包。"
    exit 1
fi

# 自动检测并安装缺失依赖
echo "检查依赖库..."
MISSING=$(ldd "$SCRIPT_DIR/$APP_NAME" 2>/dev/null | grep "not found")
if [ -n "$MISSING" ]; then
    echo "检测到缺失依赖:"
    echo "$MISSING"
    echo "尝试自动安装依赖..."
    DEPS_OK=false
    if command -v apt-get >/dev/null 2>&1; then
        pkexec apt-get update -qq && pkexec apt-get install -y libqt5core5a libqt5gui5 libqt5widgets5 libqt5network5 libqt5serialport5 libqt5serialport5-dev 2>&1 && DEPS_OK=true
    elif command -v yum >/dev/null 2>&1; then
        pkexec yum install -y qt5-qtbase qt5-qtbase-gui qt5-qtnetwork qt5-qtserialport qt5-qtserialport-devel 2>&1 && DEPS_OK=true
    fi
    if [ "$DEPS_OK" = true ]; then
        echo "依赖安装完成"
        STILL_MISSING=$(ldd "$SCRIPT_DIR/$APP_NAME" 2>/dev/null | grep "not found")
        if [ -n "$STILL_MISSING" ]; then
            echo "仍有缺失依赖:"
            echo "$STILL_MISSING"
        else
            echo "所有依赖已满足"
        fi
    else
        echo "警告: 自动安装失败，请手动安装缺失的依赖"
    fi
else
    echo "依赖检查通过"
fi

# 配置 sudo 免密（用于 LAN 唤醒 Magic 模式设置）
SUDOERS_FILE="/etc/sudoers.d/elsky_wol"
if [ ! -f "$SUDOERS_FILE" ]; then
    echo "配置 LAN 唤醒免密权限（需要一次性授权）..."
    CURRENT_USER=$(whoami)
    pkexec bash -c "echo '$CURRENT_USER ALL=(ALL) NOPASSWD: /usr/sbin/ethtool, /usr/bin/nmcli' > $SUDOERS_FILE && chmod 440 $SUDOERS_FILE" 2>/dev/null
    if [ -f "$SUDOERS_FILE" ]; then
        echo "LAN 唤醒免密权限配置完成"
    fi
fi

# 设置执行权限
chmod +x "$SCRIPT_DIR/$APP_NAME"

echo "启动客户端..."
echo "平台: $ARCH"
echo "程序: $APP_NAME"
echo "================================="

# 切换到脚本目录
cd "$SCRIPT_DIR"

# 启动程序
exec "./$APP_NAME" "$@"
EOF

chmod +x "bin/start_client.sh"

echo ""
echo "=== 编译完成 ==="
echo "输出文件:"
echo "  - $EXPECTED_BIN (主程序)"
echo "  - bin/start_client.sh (跨平台启动脚本)"
echo "平台: $PLATFORM_NAME"
echo "架构: $ARCH"
echo "编译选项: $COMPILER_FLAGS"

# 显示编译结果
if [ -f "$EXPECTED_BIN" ]; then
    echo "✓ 编译验证成功"
    exit 0
else
    echo "✗ 编译验证失败"
    exit 1
fi
