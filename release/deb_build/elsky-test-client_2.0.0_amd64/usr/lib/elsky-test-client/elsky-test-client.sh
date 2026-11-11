#!/bin/bash
# 研盛测试客户端启动脚本（deb 安装版）
APP_DIR="/usr/lib/elsky-test-client"
APP_BIN="$APP_DIR/ELSKYTestClient"
LIB_DIR="$APP_DIR/lib"

# 兼容多平台：麒麟/统信/Ubuntu 等路径前缀可能不同
APP_DIR="$(cd "$(dirname "$(readlink -f "$0" 2>/dev/null || echo "$0")")" && pwd)"
APP_BIN="$APP_DIR/ELSKYTestClient"
LIB_DIR="$APP_DIR/lib"
PY_MODULES_DIR="$APP_DIR/python_modules"

# 加载本地动态库（lib/ 目录打包了 Qt 库等）
export LD_LIBRARY_PATH="$LIB_DIR:${LD_LIBRARY_PATH}"
# 让 Qt 平台插件也能从 lib/ 找到（修复 .desktop 双击启动找不到 xcb 插件等问题）
export QT_PLUGIN_PATH="$LIB_DIR:${QT_PLUGIN_PATH}"
# 强制使用 xcb 平台（部分发行版默认 wayland 会导致 Qt 程序无法启动）
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
# 高 DPI 支持
export QT_AUTO_SCREEN_SCALE_FACTOR=1

# 加载打包的 Python 模块（老化测试用）
if [ -d "$PY_MODULES_DIR/PyQt5" ]; then
    export PYTHONPATH="$PY_MODULES_DIR:${PYTHONPATH}"
    if [ -d "$PY_MODULES_DIR/PyQt5/Qt5/lib" ]; then
        export LD_LIBRARY_PATH="$PY_MODULES_DIR/PyQt5/Qt5/lib:${LD_LIBRARY_PATH}"
    fi
fi

# 启动前自检
if [ ! -x "$APP_BIN" ]; then
    echo "✗ 错误: 可执行文件不存在或无执行权限: $APP_BIN"
    echo "  请尝试: sudo dpkg --configure -a  或  重新安装本软件包"
    exit 1
fi

# 启动前自检：关键 Qt 库是否齐全
MISSING=$(ldd "$APP_BIN" 2>/dev/null | grep "not found" | awk '{print $1}' | sort -u | head -10)
if [ -n "$MISSING" ]; then
    echo "⚠ 检测到缺失的依赖库："
    echo "$MISSING"
    echo ""
    echo "可能原因: 1) $LIB_DIR 目录为空；2) 系统 Qt 库未安装"
    echo "解决方法:"
    echo "  sudo apt install -y libqt5core5a libqt5gui5 libqt5widgets5 libqt5network5 libqt5serialport5"
    echo ""
    # 尝试自动安装（若本用户已配置免密）
    if command -v apt-get >/dev/null 2>&1 && [ "$(id -u)" = "0" ]; then
        echo "正在自动安装系统 Qt 库..."
        apt-get install -y libqt5core5a libqt5gui5 libqt5widgets5 libqt5network5 libqt5serialport5 2>&1 | tail -5
    fi
    echo ""
fi

# 切到应用目录运行（程序运行时可能使用相对路径）
cd "$APP_DIR"
exec "$APP_BIN" "$@"
