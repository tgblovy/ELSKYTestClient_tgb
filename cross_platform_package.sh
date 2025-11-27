#!/bin/bash
echo "=== 研盛测试客户端跨平台打包脚本 ==="

# 设置变量 - 修复项目根目录检测
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CURRENT_USER=$(whoami)
HOME_DIR=$(eval echo ~$CURRENT_USER)
VERSION=$(date +%Y%m%d_%H%M)

# 检测平台
ARCH=$(uname -m)
if [ "$ARCH" = "aarch64" ]; then
    CURRENT_PLATFORM="ft"
    CURRENT_PLATFORM_NAME="飞腾ARM64"
elif [[ "$ARCH" =~ "x86_64" ]]; then
    CURRENT_PLATFORM="x86"
    CURRENT_PLATFORM_NAME="X86_64"
elif [[ "$ARCH" =~ "i386" || "$ARCH" =~ "i686" ]]; then
    CURRENT_PLATFORM="x86"
    CURRENT_PLATFORM_NAME="X86"
else
    echo "错误: 不支持的平台: $ARCH"
    exit 1
fi

# 检测桌面环境
if [ -d "$HOME_DIR/桌面" ]; then
    DESKTOP_NAME="桌面"
elif [ -d "$HOME_DIR/Desktop" ]; then
    DESKTOP_NAME="Desktop"
else
    DESKTOP_NAME="桌面"
    echo "警告: 未找到标准桌面目录，使用默认名称'桌面'"
fi

echo "当前用户: $CURRENT_USER"
echo "当前平台: $ARCH ($CURRENT_PLATFORM_NAME)"
echo "桌面目录: $DESKTOP_NAME"
echo "项目根目录: $PROJECT_ROOT"

# 检查编译输出 - 修复路径检测
BIN_DIR="$PROJECT_ROOT/bin"
if [ ! -f "$BIN_DIR/ELSKY_test_client_$CURRENT_PLATFORM" ]; then
    echo "错误: 未找到当前平台的编译输出"
    echo "期望路径: $BIN_DIR/ELSKY_test_client_$CURRENT_PLATFORM"
    echo "bin目录内容:"
    ls -la "$BIN_DIR" 2>/dev/null || echo "bin目录不存在"
    echo ""
    echo "请先运行编译脚本: ./cross_platform_build.sh"
    exit 1
fi

echo "✓ 找到编译输出: $BIN_DIR/ELSKY_test_client_$CURRENT_PLATFORM"

# 创建部署目录
DEPLOY_DIR="$HOME_DIR/$DESKTOP_NAME/ELSKYTestClient_Universal_$VERSION"
echo "创建部署目录: $DEPLOY_DIR"
rm -rf "$DEPLOY_DIR"
mkdir -p "$DEPLOY_DIR"
cd "$DEPLOY_DIR"

# 1. 复制当前平台的可执行文件
echo "复制可执行文件..."
cp "$BIN_DIR/ELSKY_test_client_$CURRENT_PLATFORM" .
echo "✓ 复制 $CURRENT_PLATFORM_NAME 版本"

# 2. 尝试复制其他平台的可执行文件（如果存在）
if [ "$CURRENT_PLATFORM" = "ft" ] && [ -f "$BIN_DIR/ELSKY_test_client_x86" ]; then
    cp "$BIN_DIR/ELSKY_test_client_x86" .
    echo "✓ 复制X86版本"
elif [ "$CURRENT_PLATFORM" = "x86" ] && [ -f "$BIN_DIR/ELSKY_test_client_ft" ]; then
    cp "$BIN_DIR/ELSKY_test_client_ft" .
    echo "✓ 复制飞腾版本"
fi

# 3. 复制跨平台启动脚本
if [ -f "$BIN_DIR/start_client.sh" ]; then
    cp "$BIN_DIR/start_client.sh" .
    chmod +x start_client.sh
    echo "✓ 复制跨平台启动脚本"
else
    echo "⚠ 警告: 未找到跨平台启动脚本，将创建新的"
    # 创建跨平台启动脚本
    cat > start_client.sh << 'EOF'
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
    chmod +x start_client.sh
fi

# 4. 创建库目录并收集依赖库
mkdir -p lib
echo "收集依赖库..."
if [ -f "ELSKY_test_client_${CURRENT_PLATFORM}" ]; then
    echo "分析依赖库: ELSKY_test_client_${CURRENT_PLATFORM}"
    ldd "ELSKY_test_client_${CURRENT_PLATFORM}" | grep "=> /" | awk '{print $3}' | while read lib; do
        if [ -f "$lib" ]; then
            lib_name=$(basename "$lib")
            echo "复制: $lib_name"
            cp "$lib" lib/
        fi
    done
    echo "✓ 依赖库收集完成"
    
    # 显示收集的库文件
    echo "收集的库文件:"
    ls -la lib/ | grep "\.so" || echo "  无.so文件"
else
    echo "⚠ 警告: 未找到可执行文件，跳过依赖库收集"
fi

# 5. 创建动态库加载脚本
cat > load_libraries.sh << 'EOF'
#!/bin/bash
# 动态库加载脚本

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIB_DIR="$SCRIPT_DIR/lib"

# 设置库路径
export LD_LIBRARY_PATH="$LIB_DIR:$LD_LIBRARY_PATH"

echo "=== 研盛测试客户端 - 库加载 ==="
echo "设置库路径: $LIB_DIR"

# 检查库文件
if [ -d "$LIB_DIR" ]; then
    echo "找到库文件:"
    ls "$LIB_DIR"/*.so* 2>/dev/null | while read lib; do
        echo "  - $(basename $lib)"
    done
else
    echo "⚠ 警告: 库目录不存在"
fi

echo "启动主程序..."
# 启动主程序
"$SCRIPT_DIR/start_client.sh"
EOF
chmod +x load_libraries.sh

# 6. 创建安装脚本 - 修复路径问题
cat > install.sh << 'EOF'
#!/bin/bash
echo "=== 安装研盛测试客户端(跨平台版) ==="

# 获取当前目录
INSTALL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CURRENT_USER=$(whoami)
HOME_DIR=$(eval echo ~$CURRENT_USER)

# 检测桌面目录
DESKTOP_DIR=""
DESKTOP_NAME=""
if [ -d "$HOME_DIR/桌面" ]; then
    DESKTOP_DIR="$HOME_DIR/桌面"
    DESKTOP_NAME="桌面"
    echo "✓ 检测到中文桌面目录: 桌面"
elif [ -d "$HOME_DIR/Desktop" ]; then
    DESKTOP_DIR="$HOME_DIR/Desktop"
    DESKTOP_NAME="Desktop"
    echo "✓ 检测到英文桌面目录: Desktop"
else
    echo "⚠ 警告: 未找到桌面目录，尝试创建中文桌面目录"
    DESKTOP_DIR="$HOME_DIR/桌面"
    DESKTOP_NAME="桌面"
    mkdir -p "$DESKTOP_DIR" 2>/dev/null
fi

# 检测平台
ARCH=$(uname -m)
if [ "$ARCH" = "aarch64" ]; then
    PLATFORM_NAME="飞腾ARM64"
elif [[ "$ARCH" =~ "x86_64" ]]; then
    PLATFORM_NAME="X86_64"
elif [[ "$ARCH" =~ "i386" || "$ARCH" =~ "i686" ]]; then
    PLATFORM_NAME="X86"
else
    PLATFORM_NAME="未知"
fi

echo "系统平台: $ARCH ($PLATFORM_NAME)"
echo "当前用户: $CURRENT_USER"
echo "桌面目录: $DESKTOP_NAME"
echo "安装目录: $INSTALL_DIR"

# 设置文件权限
echo "设置执行权限..."
chmod +x "$INSTALL_DIR"/*.sh 2>/dev/null
chmod +x "$INSTALL_DIR"/ELSKY_test_client_* 2>/dev/null

# 创建桌面快捷方式
echo "创建桌面快捷方式..."
cat > "$INSTALL_DIR/ELSKYTestClient.desktop" << DESKTOP_EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=研盛测试客户端
Comment=电源管理和网络唤醒测试工具 - 支持飞腾和X86平台
Exec=sh -c "cd \"$INSTALL_DIR\" && ./load_libraries.sh"
Icon=utilities-system-monitor
Categories=Utility;
Terminal=false
StartupNotify=true
X-Platform-Support=ft,x86
DESKTOP_EOF

# 复制桌面快捷方式
if [ -d "$DESKTOP_DIR" ]; then
    cp "$INSTALL_DIR/ELSKYTestClient.desktop" "$DESKTOP_DIR/"
    chmod +x "$DESKTOP_DIR/ELSKYTestClient.desktop"
    echo "✓ 桌面快捷方式已创建: $DESKTOP_NAME/ELSKYTestClient.desktop"
    
    # 尝试更新桌面图标缓存
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database "$HOME_DIR/.local/share/applications"
        echo "✓ 更新桌面图标缓存"
    fi
else
    echo "⚠ 警告: 桌面目录不存在，无法创建快捷方式"
    echo "快捷方式已创建在: $INSTALL_DIR/ELSKYTestClient.desktop"
fi

# 显示包含的版本
echo ""
echo "包含的平台版本:"
if [ -f "$INSTALL_DIR/ELSKY_test_client_ft" ]; then
    echo "  ✓ 飞腾ARM64版本"
fi
if [ -f "$INSTALL_DIR/ELSKY_test_client_x86" ]; then
    echo "  ✓ X86版本"
fi

# 检查当前平台对应的版本是否存在
CURRENT_APP="ELSKY_test_client_$( [ "$ARCH" = "aarch64" ] && echo "ft" || echo "x86" )"
if [ -f "$INSTALL_DIR/$CURRENT_APP" ]; then
    echo "  ✓ 当前平台版本: $CURRENT_APP"
else
    echo "  ⚠ 当前平台版本不存在: $CURRENT_APP"
fi

echo ""
echo "安装完成！"
echo "支持的平台: 飞腾ARM64, X86(兆芯/海光/Intel/AMD)"
echo "当前平台: $PLATFORM_NAME"
if [ -d "$DESKTOP_DIR" ]; then
    echo "桌面快捷方式: $DESKTOP_NAME/ELSKYTestClient.desktop"
fi
echo ""
echo "启动方式:"
echo "1. 双击桌面图标'研盛测试客户端'"
echo "2. 或进入目录运行: ./load_libraries.sh"
echo "3. 或直接运行: ./start_client.sh"
echo ""
echo "注意: 程序会自动检测系统平台并运行对应版本"
EOF
chmod +x install.sh

# 7. 创建使用说明
cat > README.txt << EOF
研盛测试客户端 - 跨平台版使用说明
================================

一、功能特性
✅ 电源管理（关机、重启、休眠）
✅ 网络唤醒（WOL）
✅ 系统监控（CPU、内存、温度）
✅ 设备检测（USB、串口）
✅ 循环测试控制
✅ 服务器连接管理

二、支持平台
• 飞腾ARM64平台 (aarch64)
• X86平台 (x86_64, i386, i686)
  - 兆芯平台
  - 海光平台  
  - Intel平台
  - AMD平台

三、安装方法
1. 解压到任意目录
2. 运行安装脚本: ./install.sh
3. 双击桌面图标启动

四、文件说明
• ELSKY_test_client_ft    - 飞腾ARM64版本
• ELSKY_test_client_x86   - X86版本  
• start_client.sh        - 跨平台启动脚本
• load_libraries.sh      - 库加载脚本
• install.sh            - 安装脚本
• lib/                  - 依赖库目录

五、手动启动
方法1: 使用库加载脚本（推荐）
  ./load_libraries.sh

方法2: 直接启动
  ./start_client.sh

六、连接配置
• 默认服务器端口: 8888
• 支持自定义服务器IP和端口
• 自动重连机制

七、故障排除

1. 启动失败
   chmod +x *.sh
   chmod +x ELSKY_test_client_*
   ldd ELSKY_test_client_*

2. 库文件缺失
   程序已包含常用依赖库，如果仍有缺失：

   飞腾平台:
     sudo apt-get install libqt5core5a libqt5gui5 libqt5widgets5 libqt5network5

   X86平台:  
     sudo yum install qt5-qtbase qt5-qtnetwork

技术支持: 唐贵宝
版本: $VERSION
平台: $CURRENT_PLATFORM_NAME

打包时间: $(date +"%Y-%m-%d %H:%M:%S")
EOF

# 8. 创建版本信息
cat > VERSION << EOF
PRODUCT=ELSKYTestClient
VERSION=2.0.0
BUILD_DATE=$(date +%Y-%m-%d)
BUILD_USER=$CURRENT_USER
BUILD_PLATFORM=$CURRENT_PLATFORM_NAME
SUPPORTED_PLATFORMS=ft,x86
CONTAINS_VERSIONS:
$(ls ELSKY_test_client_* 2>/dev/null | while read file; do echo "  - $file"; done)
EOF

# 9. 设置文件权限
echo "设置文件权限..."
chmod +x ELSKY_test_client_* 2>/dev/null
chmod +x *.sh

# 10. 显示打包内容
echo "打包内容:"
ls -la

# 11. 打包成压缩文件
cd ..
PACKAGE_NAME="ELSKYTestClient_Universal_${VERSION}.tar.gz"
tar -czf "$PACKAGE_NAME" "$(basename $DEPLOY_DIR)"

echo ""
echo "=== 跨平台打包完成 ==="
echo "部署包: $(pwd)/$PACKAGE_NAME"
echo "大小: $(du -h $PACKAGE_NAME | cut -f1)"
echo ""
echo "包含的平台版本:"
if [ -f "$BIN_DIR/ELSKY_test_client_ft" ]; then echo "  ✓ 飞腾ARM64版本"; fi
if [ -f "$BIN_DIR/ELSKY_test_client_x86" ]; then echo "  ✓ X86版本"; fi
echo ""
echo "部署目录内容:"
ls -la "$DEPLOY_DIR"
echo ""
echo "部署步骤："
echo "1. 将 $PACKAGE_NAME 复制到目标电脑"
echo "2. 解压: tar -xzf $PACKAGE_NAME"
echo "3. 进入目录: cd $(basename $DEPLOY_DIR)"
echo "4. 运行安装: ./install.sh"
echo "5. 双击桌面图标启动"
echo ""
echo "注意: 此包包含多平台版本，会自动检测系统平台运行对应版本"
