#!/bin/bash
# =============================================================================
# 统信 UOS / 麒麟 现场卸载入口一键修复（无需重新编译、重新打包）
# 用法: sudo bash fix_uninstall_on_site.sh
#
# 背景：dde-launcher 启动器右键的「卸载」是硬编码菜单，固定走
#   org.deepin.dde.Lastore1.RemovePackage（应用商店链路），对 dpkg -i
#   手工安装的第三方包常点击无反应。deepin Wiki 明示「启动器卸载仅对部分
#   应用有效，请勿依赖于该卸载方式」，这个按钮本软件无法修复。
# 本脚本在已安装的机器上补一个「卸载研盛测试客户端」独立应用图标：
#   启动器 / 应用菜单 / 桌面都能看到，点击后打开终端显示卸载全过程与结果。
# 幂等：可重复执行；只读诊断，不改动 dpkg 数据库、不删除任何用户数据。
# =============================================================================
set +e
APP=/usr/lib/elsky-test-client
PKG=elsky-test-client
DESK=/usr/share/applications/elsky-test-client-uninstall.desktop

if [ "$(id -u)" -ne 0 ]; then
    echo "请用管理员权限运行: sudo bash $0"
    exit 1
fi
if [ ! -d "$APP" ]; then
    echo "✗ 未检测到已安装的研盛测试客户端（$APP 不存在）"
    echo "  请先在目标机安装本软件的 deb 包，再运行本脚本。"
    exit 1
fi
echo "=== 现场补装卸载入口 ==="

echo "  → 写入卸载脚本"
cat > "$APP/uninstall.sh" << 'EOF_SITE'
#!/bin/bash
# 研盛测试客户端 卸载入口
# [2026-11-11 三次修复] 不再依赖 pkexec 单条路径：
#   dde-launcher 右键的原生「卸载」走 org.deepin.dde.Lastore1.RemovePackage，
#   只对应用商店安装的包有效（deepin Wiki: "启动器卸载仅对部分应用有效，
#   请勿依赖于该卸载方式"），第三方 dpkg -i 的包点击无反应属桌面环境限制。
#   本脚本由「卸载研盛测试客户端」独立图标 / 桌面图标右键 / 终端调用，
#   优先用 postinst 已配置的 sudoers 免密 sudo -n，绕开 polkit 与安全中心拦截。
set +e
PKG=elsky-test-client
LOG=/tmp/elsky-test-client-uninstall.log
WAIT=0
for _a in "$@"; do [ "$_a" = "--wait" ] && WAIT=1; done

echo "=== $(date) 研盛测试客户端卸载 ===" | tee "$LOG"
echo "  包名: $PKG" | tee -a "$LOG"

# 提权路径：root 直跑 > sudo -n 免密 > pkexec > 明确提示终端命令。
# 用白名单内的 apt-get --version 探测 sudo 免密是否生效，
# 不能用 sudo -n true —— true 不在 sudoers 白名单里，会被误判为无权限。
if [ "$(id -u)" -eq 0 ]; then
    RUN=""
    echo "  权限: root（直接执行）" | tee -a "$LOG"
elif command -v sudo >/dev/null 2>&1 \
     && sudo -n /usr/bin/apt-get --version >/dev/null 2>&1; then
    RUN="sudo -n"
    echo "  权限: sudo 免密（postinst 已配置 sudoers 白名单）" | tee -a "$LOG"
elif command -v pkexec >/dev/null 2>&1; then
    RUN="pkexec"
    echo "  权限: pkexec（若被安全中心拦截，请改用终端命令）" | tee -a "$LOG"
else
    echo "" | tee -a "$LOG"
    echo "  ✗ 无法取得管理员权限。请在终端手动执行：" | tee -a "$LOG"
    echo "      sudo apt purge -y $PKG" | tee -a "$LOG"
    [ "$WAIT" -eq 1 ] && { echo "按回车键关闭..."; read -r _ 2>/dev/null || sleep 5; }
    exit 1
fi

echo "  正在卸载，请勿关闭窗口..." | tee -a "$LOG"
echo "" | tee -a "$LOG"
# PIPESTATUS[0] 必须直接取 apt-get 的退出码；把 apt-get 包进 { } 会取到
# 复合命令最后一条 echo 的 0，掩盖真实失败。
$RUN /usr/bin/apt-get purge -y "$PKG" </dev/null 2>&1 | tee -a "$LOG"
rc=${PIPESTATUS[0]}

echo "" | tee -a "$LOG"
if [ "$rc" -eq 0 ]; then
    echo "  ✓ 卸载完成（退出码 0）" | tee -a "$LOG"
    MSG="研盛测试客户端已卸载"
else
    echo "  ✗ 卸载失败（退出码 $rc）" | tee -a "$LOG"
    echo "    请在终端手动执行: sudo apt purge -y $PKG" | tee -a "$LOG"
    echo "    完整日志: $LOG" | tee -a "$LOG"
    MSG="卸载失败(退出码 $rc)，详情见 $LOG"
fi
if command -v notify-send >/dev/null 2>&1 && [ -n "${DISPLAY:-}" ]; then
    notify-send "研盛测试客户端" "$MSG" 2>/dev/null || true
fi
# 由 Terminal=true 的 .desktop 调起时保持窗口，让用户看清结果。
if [ "$WAIT" -eq 1 ]; then
    echo ""
    echo "按回车键关闭窗口..."
    read -r _ 2>/dev/null || sleep 8
fi
exit "$rc"
EOF_SITE
chmod 755 "$APP/uninstall.sh"

echo "  → 写入诊断脚本"
cat > "$APP/diagnose_uninstall.sh" << 'EOF_SITE'
#!/bin/bash
# 统信 UOS / deepin 卸载链路现场诊断（只读，不改系统）
set +e
OUT=/tmp/elsky-uninstall-diagnose.txt
{
echo "===== 研盛测试客户端 卸载诊断 $(date) ====="
echo "[系统] $(grep PRETTY_NAME /etc/os-release 2>/dev/null | head -1)"
echo "[内核] $(uname -a)"
echo ""
echo "--- 1. 包状态 ---"
dpkg -l elsky-test-client 2>&1 | tail -3
echo "dpkg -S 主 desktop: $(dpkg -S /usr/share/applications/elsky-test-client.desktop 2>&1 | head -1)"
echo ""
echo "--- 2. 卸载入口 ---"
for f in /usr/lib/elsky-test-client/uninstall.sh \
         /usr/lib/elsky-test-client/diagnose_uninstall.sh \
         /usr/share/applications/elsky-test-client-uninstall.desktop \
         /usr/share/applications/elsky-test-client.desktop; do
    if [ -e "$f" ]; then
        echo "  [OK]   $f  $(stat -c '%A %s B' "$f" 2>/dev/null)"
    else
        echo "  [缺失] $f"
    fi
done
echo "  主 desktop Actions: $(grep -E '^Actions=|^\[Desktop Action' /usr/share/applications/elsky-test-client.desktop 2>/dev/null | tr '\n' ' ')"
echo ""
echo "--- 3. 提权链路（决定自带卸载入口能否成功）---"
echo "  当前身份: $(id)"
if command -v sudo >/dev/null 2>&1; then
    if sudo -n /usr/bin/apt-get --version >/dev/null 2>&1; then
        echo "  sudo -n apt-get: 免密可用 ✓"
    else
        echo "  sudo -n apt-get: 不可用（将回退 pkexec 或提示终端命令）"
    fi
else
    echo "  sudo: 未安装"
fi
if command -v pkexec >/dev/null 2>&1; then
    echo "  pkexec: $(command -v pkexec)"
else
    echo "  pkexec: 未安装"
fi
echo "  sudoers: $(ls -l /etc/sudoers.d/elsky-test-client 2>&1)"
echo ""
echo "--- 4. DDE / lastore（决定启动器原生「卸载」是否可用）---"
for proc in dde-launcher dde-launchpad lastore-daemon dde-polkit-agent; do
    p=$(pgrep -a "$proc" 2>/dev/null | head -1)
    echo "  $proc: ${p:-未运行}"
done
if command -v busctl >/dev/null 2>&1; then
    busctl list 2>/dev/null | grep -i lastore || echo "  lastore D-Bus 名称: 未注册"
fi
echo "  启动器缓存文件数: $(ls -1 ~/.config/deepin/dde-launcher-app-*.conf 2>/dev/null | wc -l)"
echo ""
echo "--- 5. 结论 ---"
echo "  启动器右键那个「卸载」由 dde-launcher 硬编码，链路是"
echo "    appId → .desktop → dpkg -S 包名 → org.deepin.dde.Lastore1.PackageExists/RemovePackage"
echo "  它依赖应用商店(lastore)记录，对 dpkg -i 手工安装的包常点击无反应，"
echo "  属桌面环境限制，本软件无法修复（deepin Wiki 明示请勿依赖该方式）。"
echo "  请改用启动器/桌面里的「卸载研盛测试客户端」独立图标，"
echo "  或在终端执行: sudo apt purge -y elsky-test-client"
echo ""
echo "===== 诊断结束，结果已存 $OUT ====="
} 2>&1 | tee "$OUT"
EOF_SITE
chmod 755 "$APP/diagnose_uninstall.sh"

echo "  → 写入独立卸载图标 $DESK"
cat > "$DESK" << 'EOF_SITE'
[Desktop Entry]
Version=1.0
Type=Application
Name=卸载研盛测试客户端
Name[en]=Uninstall ELSKY Test Client
Comment=卸载研盛测试客户端（打开终端显示进度与结果）
Exec=/usr/lib/elsky-test-client/uninstall.sh --wait
TryExec=/usr/lib/elsky-test-client/uninstall.sh
Icon=user-trash
Terminal=true
Categories=System;Utility;
StartupNotify=false
X-Debian-Package=elsky-test-client
X-Deepin-Vendor=elsky
EOF_SITE
chmod 644 "$DESK"

# 给所有真实登录用户的桌面放一份（只对已存在桌面目录的用户）
PLACE=0
while IFS=: read -r u_name u_pw u_uid u_gid u_gecos u_home u_shell; do
    if [ "$u_uid" -ge 1000 ] 2>/dev/null && [ -d "$u_home" ]; then
        for dd in "$u_home/桌面" "$u_home/Desktop"; do
            [ -d "$dd" ] || continue
            rm -f "$dd/elsky-test-client-uninstall.desktop" 2>/dev/null
            ln -s "$DESK" "$dd/elsky-test-client-uninstall.desktop" 2>/dev/null \
                && chown -h "$u_name" "$dd/elsky-test-client-uninstall.desktop" 2>/dev/null \
                && PLACE=$((PLACE+1))
        done
    fi
done < <(getent passwd 2>/dev/null)
echo "  ✓ 桌面卸载图标已放置: $PLACE 处"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
fi

echo ""
echo "=== 完成 ==="
echo "  卸载请点：启动器 / 桌面 →「卸载研盛测试客户端」（会开终端显示结果）"
echo "  或桌面主图标右键 →「卸载研盛测试客户端」"
echo "  或终端执行：sudo apt purge -y $PKG"
echo "  诊断命令：bash $APP/diagnose_uninstall.sh"
echo ""
echo "  注意：启动器里对主图标右键弹出的那个「卸载」是 DDE 自带菜单，"
echo "        走应用商店(lastore)链路，本软件无法修复，请勿使用。"
exit 0
