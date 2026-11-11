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
