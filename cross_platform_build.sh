#!/bin/bash
echo "=== 研盛测试客户端跨平台编译脚本 ==="

# 检测当前平台
ARCH=$(uname -m)
if [ "$ARCH" = "aarch64" ]; then
    PLATFORM="ft"
    PLATFORM_NAME="飞腾ARM64"
    echo "✓ 检测到飞腾ARM64平台"
    
    # 检测具体飞腾型号
    CPU_MODEL=$(grep -i "model name" /proc/cpuinfo | head -1 | tr -d ' ' | cut -d: -f2)
    if echo "$CPU_MODEL" | grep -q "FT-2000"; then
        PLATFORM="ft2000"
        PLATFORM_NAME="飞腾D2000"
        echo "✓ 检测到飞腾D2000平台"
        export CXXFLAGS="$CXXFLAGS -march=armv8-a -mtune=cortex-a72"
    elif echo "$CPU_MODEL" | grep -q "FT-2500"; then
        PLATFORM="ft3000" 
        PLATFORM_NAME="飞腾D3000"
        echo "✓ 检测到飞腾D3000平台"
    fi
elif [[ "$ARCH" =~ "x86_64" ]]; then
    PLATFORM="x86"
    PLATFORM_NAME="X86_64"
    echo "✓ 检测到X86_64平台"
elif [[ "$ARCH" =~ "i386" || "$ARCH" =~ "i686" ]]; then
    PLATFORM="x86"
    PLATFORM_NAME="X86"
    echo "✓ 检测到X86平台"
else
    echo "错误: 不支持的平台架构: $ARCH"
    exit 1
fi

echo "平台: $ARCH ($PLATFORM_NAME)"

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
if ! command -v qmake >/dev/null 2>&1; then
    echo "错误: 未找到qmake，请确保Qt已安装"
    echo "请安装Qt5开发包:"
    if command -v apt-get >/dev/null 2>&1; then
        echo "sudo apt-get install qt5-default qtbase5-dev qtchooser qt5-qmake"
    elif command -v yum >/dev/null 2>&1; then
        echo "sudo yum install qt5-qtbase-devel"
    fi
    exit 1
fi

QMAKE_VERSION=$(qmake -v | grep "Using Qt version" | awk '{print $4}')
echo "✓ 检测到Qt版本: $QMAKE_VERSION"

# 3. 生成Qt项目文件
echo "生成Qt项目文件..."
cat > "$PROJECT_ROOT/ELSKYTestClient.pro" << 'EOF'
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
EOF

# 4. 创建输出目录
mkdir -p "$PROJECT_ROOT/bin"
mkdir -p "$PROJECT_ROOT/build/obj"
mkdir -p "$PROJECT_ROOT/build/moc"
mkdir -p "$PROJECT_ROOT/build/rcc"
mkdir -p "$PROJECT_ROOT/build/ui"

# 5. 编译客户端
echo "编译研盛测试客户端..."
cd "$PROJECT_ROOT"

if qmake ELSKYTestClient.pro && make -j$(nproc); then
    # 重命名可执行文件包含平台信息
    if [ -f "bin/ELSKY_test_client" ]; then
        mv "bin/ELSKY_test_client" "bin/ELSKY_test_client_${PLATFORM}"
        echo "✓ 客户端编译成功: bin/ELSKY_test_client_${PLATFORM}"
        
        # 显示文件信息
        file "bin/ELSKY_test_client_${PLATFORM}"
    else
        echo "✗ 错误: 编译成功但未找到输出文件"
        exit 1
    fi
else
    echo "✗ 客户端编译失败"
    exit 1
fi

# 6. 检查依赖库
echo "检查依赖库..."
ldd "bin/ELSKY_test_client_${PLATFORM}" | grep "not found" && {
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
    echo "✓ 检测到飞腾ARM64平台"
    APP_NAME="ELSKY_test_client_ft"
elif [[ "$ARCH" =~ "x86_64" ]]; then
    echo "✓ 检测到X86_64平台"
    APP_NAME="ELSKY_test_client_x86"
elif [[ "$ARCH" =~ "i386" || "$ARCH" =~ "i686" ]]; then
    echo "✓ 检测到X86平台"
    APP_NAME="ELSKY_test_client_x86"
else
    echo "✗ 错误: 不支持的平台架构: $ARCH"
    echo ""
    echo "支持的平台:"
    echo "  - x86_64, i386, i686 (兆芯、海光、Intel、AMD等)"
    echo "  - aarch64 (飞腾ARM64平台)"
    echo ""
    echo "当前目录可执行文件:"
    ls -la "$SCRIPT_DIR"/ELSKY_test_client_* 2>/dev/null || echo "  无"
    exit 1
fi

# 检查可执行文件是否存在
if [ ! -f "$SCRIPT_DIR/$APP_NAME" ]; then
    echo "✗ 错误: 找不到对应平台的可执行文件: $APP_NAME"
    echo ""
    echo "当前目录文件:"
    ls -la "$SCRIPT_DIR"
    exit 1
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
echo "  - bin/ELSKY_test_client_${PLATFORM} (主程序)"
echo "  - bin/start_client.sh (跨平台启动脚本)"
echo "平台: $PLATFORM_NAME"
echo "架构: $ARCH"

# 显示编译结果
if [ -f "bin/ELSKY_test_client_${PLATFORM}" ]; then
    echo "✓ 编译验证成功"
    exit 0
else
    echo "✗ 编译验证失败"
    exit 1
fi
