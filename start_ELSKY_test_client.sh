#!/bin/bash
echo "=== 启动 研盛测试客户端 ==="
echo "1111111111检测系统架构..."

ARCH=$(uname -m)
APP_NAME="ELSKYTestClient"

if [ "$ARCH" = "aarch64" ]; then
    echo "检测到飞腾ARM64平台"
elif [[ "$ARCH" =~ "x86_64" || "$ARCH" =~ "i386" || "$ARCH" =~ "i686" ]]; then
    echo "检测到X86平台"
else
    echo "错误: 不支持的平台架构: $ARCH"
    echo "支持的平台:"
    echo "  - x86_64, i386, i686 (X86平台)"
    echo "  - aarch64 (飞腾ARM64平台)"
    exit 1
fi

if [ -f "$APP_NAME" ]; then
    echo "启动: $APP_NAME"
    chmod +x "$APP_NAME"
    ./"$APP_NAME"
else
    echo "错误: 找不到可执行文件 $APP_NAME"
    echo "请先编译项目: qmake && make"
    exit 1
fi
