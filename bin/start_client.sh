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
    # 修复（A4）：兼容 D2000/D3000 细分型号产物 _ft2000/_ft3000 ——
    # 先找精确名 _ft，找不到再通配 _ft*
    APP_NAME="ELSKY_test_client_ft"
    if [ ! -f "$SCRIPT_DIR/$APP_NAME" ]; then
        APP_NAME=$(ls "$SCRIPT_DIR"/ELSKY_test_client_ft* 2>/dev/null | head -1 | xargs -r basename 2>/dev/null)
    fi
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
