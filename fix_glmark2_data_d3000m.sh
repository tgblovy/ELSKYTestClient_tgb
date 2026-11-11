#!/usr/bin/env bash
# D3000M / 麒麟2503 新装系统：3D 老化 glmark2 全黑但有 FPS 的现场诊断与修复
#
# 根因（2026-11-05 日志实测，glmark2 2014.03+git20150611.fa71af2d / 景嘉微 JM920）：
#   1. /usr/share/glmark2 缺场景数据（shaders/models/textures），glmark2 编译期
#      硬编码只读这个路径，所有场景 Set up failed → 窗口全黑但 FPS 正常。
#   2. 完整数据其实就在安装包内 extra-bin/glmark2/usr/share/glmark2，
#      但老化程序唯一的数据重定向手段 --data-path 被判定为「不支持」而放弃。
#   3. 该判定是误判：这个版本明确支持 --data-path；探测执行 glmark2 --help 时
#      没有净化 LD_LIBRARY_PATH=/usr/lib/elsky-test-client/lib:（包内 Qt 的 GL 库），
#      老 glmark2 带该环境启动即加载失败/超时，帮助文本没输出 → 误判「不支持」。
#
# 本脚本不改任何软件源码，只把包内数据补齐到 /usr/share/glmark2，
# 之后任何版本 glmark2 都能直接读到数据（无需 --data-path、无需重启、无需重打包）。

PKG=/usr/lib/elsky-test-client/extra-bin/glmark2/usr/share/glmark2
SYS=/usr/share/glmark2

echo "===== 1. glmark2 二进制 ====="
command -v glmark2 || echo "  未找到 glmark2"
LD_LIBRARY_PATH= glmark2 --version 2>&1 | head -3

echo "===== 2. 系统数据目录 $SYS ====="
if [ -d "$SYS" ]; then
    for s in shaders models textures; do
        if [ -d "$SYS/$s" ]; then
            echo "  $s: 存在，$(ls -1 "$SYS/$s" 2>/dev/null | wc -l) 个条目"
        else
            echo "  $s: 缺失"
        fi
    done
    for f in shaders/light-basic.vert shaders/desktop.vert; do
        [ -f "$SYS/$f" ] && echo "  ✓ $f" || echo "  ✗ 缺 $f"
    done
else
    echo "  目录不存在"
fi

echo "===== 3. 包内数据目录 $PKG ====="
if [ -d "$PKG" ]; then
    for s in shaders models textures; do
        if [ -d "$PKG/$s" ] && [ -n "$(ls -A "$PKG/$s" 2>/dev/null)" ]; then
            echo "  ✓ $s 完整（$(ls -1 "$PKG/$s" | wc -l) 个条目）"
        else
            echo "  ✗ $s 缺失或为空"
        fi
    done
else
    echo "  ✗ 包内无数据，需重新打包并放入 glmark2-data 或 2021.12 源码包"
    exit 1
fi

echo "===== 4. 验证 --data-path 探测是否为误判 ====="
if LD_LIBRARY_PATH= glmark2 --help 2>&1 | grep -i -- '--data-path' >/dev/null; then
    echo "  ✓ 净化 LD_LIBRARY_PATH 后 --help 能输出 --data-path → 老化程序的探测属误判"
    LD_LIBRARY_PATH= glmark2 --help 2>&1 | grep -i -- '--data-path' | head -2
else
    echo "  ⚠ 仍未输出 --data-path（可能版本过老），下面走系统路径补齐更稳"
fi

echo "===== 5. 补齐数据到 $SYS ====="
if [ "$(id -u)" != "0" ]; then
    echo "  需要 root 权限，请改用： sudo bash $0"
    exit 1
fi
mkdir -p "$SYS"
for s in shaders models textures; do
    if [ -d "$PKG/$s" ] && [ -n "$(ls -A "$PKG/$s" 2>/dev/null)" ]; then
        rm -rf "$SYS/$s"
        cp -r "$PKG/$s" "$SYS/"
        echo "  ✓ 已补齐 $s"
    else
        echo "  ✗ 包内 $s 不可用，跳过"
    fi
done

echo "===== 6. 结果校验 ====="
ok=1
for s in shaders models textures; do
    [ -d "$SYS/$s" ] && [ -n "$(ls -A "$SYS/$s" 2>/dev/null)" ] || { echo "  ✗ $s 仍不完整"; ok=0; }
done
[ -f "$SYS/shaders/light-basic.vert" ] || { echo "  ✗ 仍缺 shaders/light-basic.vert"; ok=0; }
if [ "$ok" = "1" ]; then
    echo "  ✓ 数据齐全。直接重跑老化测试即可看到 glmark2 画面（无需重启软件/重新打包）"
else
    echo "  ✗ 修复未成功，请把本脚本输出发回排查"
fi
