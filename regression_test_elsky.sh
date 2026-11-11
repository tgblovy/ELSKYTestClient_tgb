#!/bin/bash
# =============================================================
# ELSKY 研盛测试客户端 - 目标机回归测试脚本（只读检测，不改系统）
# 用法:  chmod +x regression_test_elsky.sh && ./regression_test_elsky.sh
# 适用:  麒麟 V10/V11 / UOS V20/V25 / Ubuntu 18.04+ / Debian 10+
#        架构: x86_64 / aarch64 / loongarch64 / loong64 / mips64el (龙芯)
# 说明:  在"优化实施并重新打包后"于每台目标机运行；仅做检测与报告，不安装/不修改任何文件。
# =============================================================
set +e

APP_DIR="/usr/lib/elsky-test-client"
BIN="$APP_DIR/ELSKYTestClient"
PASS=0; FAIL=0; WARN=0

ok()   { PASS=$((PASS+1)); echo "  [PASS] $1"; }
bad()  { FAIL=$((FAIL+1)); echo "  [FAIL] $1"; }
warn() { WARN=$((WARN+1)); echo "  [WARN] $1"; }

echo "=============================================="
echo " ELSKYTestClient 目标机回归检测"
echo " 时间: $(date '+%Y-%m-%d %H:%M:%S')"
echo "=============================================="

# ---- 0. 环境 ----
ARCH=$(uname -m); DISTRO="未知"
[ -f /etc/os-release ] && DISTRO=$(grep -E '^(ID|PRETTY_NAME)=' /etc/os-release | head -1 | cut -d= -f2 | tr -d '"')
PY3=$(python3 --version 2>&1)
echo "架构: $ARCH | 发行版: $DISTRO | $PY3"
echo ""

# ---- 1. 安装结构 ----
echo "--- 1. 安装结构 ---"
[ -d "$APP_DIR" ]            && ok "安装目录 $APP_DIR 存在"          || bad "安装目录 $APP_DIR 缺失"
[ -x "$BIN" ]                && ok "主程序可执行"                   || bad "主程序缺失或无执行权限"
[ -x "$APP_DIR/elsky-test-client.sh" ] && ok "启动脚本存在" || bad "启动脚本缺失"
[ -f "$APP_DIR/lib/platforms/libqxcb.so" ] && ok "Qt xcb 平台插件就位" || bad "libqxcb.so 缺失(GUI无法启动)"
command -v elsky-test-client >/dev/null 2>&1 && ok "/usr/bin/elsky-test-client 可调用" || warn "/usr/bin/elsky-test-client 不可调用(可能未刷新PATH, 执行 hash -r)"
ls /usr/share/applications/elsky-test-client.desktop >/dev/null 2>&1 && ok "应用菜单项存在" || bad "应用菜单项缺失"
# 桌面图标（postinst 应已为所有真实用户放置）
CUR_HOME=$(getent passwd "$(id -un)" 2>/dev/null | cut -d: -f6)
DESKTOP_FOUND=""
for d in "$CUR_HOME/桌面" "$CUR_HOME/Desktop"; do
    [ -f "$d/elsky-test-client.desktop" ] && DESKTOP_FOUND="$d"
done
[ -n "$DESKTOP_FOUND" ] && ok "当前用户桌面图标存在: $DESKTOP_FOUND" || warn "当前用户桌面无图标（多用户场景请确认 postinst 是否正常执行）"
grep -q "X-Debian-Package=elsky-test-client" /usr/share/applications/elsky-test-client.desktop 2>/dev/null \
    && ok "desktop 已声明 X-Debian-Package（开始菜单右键卸载可关联）" || warn "desktop 未声明 X-Debian-Package"

# ---- 2. 离线依赖解析（LD_LIBRARY_PATH 仅指向包内 lib/）----
echo ""
echo "--- 2. 离线动态库解析 ---"
MISSING=$(LD_LIBRARY_PATH="$APP_DIR/lib" ldd "$BIN" 2>/dev/null | grep "not found" | awk '{print $1}' | sort -u)
if [ -z "$MISSING" ]; then ok "主程序 ldd 在包内 lib/ 下全解析"; else bad "主程序缺库: $MISSING"; fi
# 抽检插件与图形栈
for so in "$APP_DIR"/lib/xcbglintegrations/*.so "$APP_DIR"/lib/libQt5XcbQpa.so* "$APP_DIR"/lib/libEGL.so* "$APP_DIR"/lib/libGL.so*; do
    [ -e "$so" ] || continue
    M2=$(LD_LIBRARY_PATH="$APP_DIR/lib" ldd "$so" 2>/dev/null | grep "not found" | awk '{print $1}' | sort -u)
    [ -z "$M2" ] && ok "依赖完整: $(basename "$so")" || bad "$(basename "$so") 缺: $M2"
done

# ---- 2.5 GLIBC 兼容性检查（2026-09-22 强化：区分主程序致命级 / 包内库警告级）----
echo ""
echo "--- 2.5 GLIBC 兼容性 ---"
SYS_GLIBC=$(ldd --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+$' | head -1)
echo "  本系统 glibc: ${SYS_GLIBC:-未知}"
# 第1级：主程序本身（致命——启动器也救不了）
BIN_GLIBC_BAD=$(ldd "$BIN" 2>/dev/null | grep 'version .GLIBC_' | head -5)
if [ -n "$BIN_GLIBC_BAD" ]; then
    bad "主程序本身与本系统 glibc 不兼容(安装包在更高版本系统编译, 本机无法运行, 须重新打包):"
    echo "$BIN_GLIBC_BAD" | sed 's/^/         /'
else
    ok "主程序与本系统 glibc 兼容"
    # 第2级：包内 lib/（警告——启动器会自动回退系统库运行）
    LIB_GLIBC_BAD=$(LD_LIBRARY_PATH="$APP_DIR/lib" ldd "$BIN" 2>/dev/null | grep 'version .GLIBC_' | head -5)
    if [ -n "$LIB_GLIBC_BAD" ]; then
        warn "包内 lib/ 与本系统 glibc 不匹配(启动器将自动用系统库运行, 建议用同版本系统重新打包):"
        echo "$LIB_GLIBC_BAD" | sed 's/^/         /'
    else
        ok "包内库与本系统 glibc 兼容（离线运行最优路径）"
    fi
fi
# 抽查包内关键库的符号版本是否超出本系统 glibc（objdump/readelf 任一可用即可）
if command -v objdump >/dev/null 2>&1 || command -v readelf >/dev/null 2>&1; then
    GLIBC_OVER=0
    while IFS= read -r -d '' f; do
        [ "$(head -c 4 "$f" 2>/dev/null | od -An -tx1 | tr -d ' \n')" = "7f454c46" ] || continue
        MAXV=$( { objdump -T "$f" 2>/dev/null || readelf -V "$f" 2>/dev/null; } \
            | grep -oE 'GLIBC_[0-9]+(\.[0-9]+)+' | sort -Vu | tail -1)
        [ -z "$MAXV" ] && continue
        NEED="${MAXV#GLIBC_}"
        if [ "$NEED" != "$SYS_GLIBC" ] && [ "$(printf '%s\n' "$NEED" "$SYS_GLIBC" | sort -V | head -1)" = "$SYS_GLIBC" ]; then
            GLIBC_OVER=$((GLIBC_OVER+1))
            [ "$GLIBC_OVER" -le 5 ] && echo "    [超版本] GLIBC_$NEED  ${f#$APP_DIR/}"
        fi
    done < <(find "$APP_DIR/lib" "$APP_DIR/extra-bin" -type f \( -name '*.so*' -o -perm -u+x \) -print0 2>/dev/null)
    if [ "$GLIBC_OVER" -gt 0 ]; then
        warn "包内 $GLIBC_OVER 个文件的 glibc 符号需求高于本系统(超出的库由启动器自动回退系统库规避; 彻底解决须用同版本系统重新打包)"
    else
        ok "包内所有库/二进制的 glibc 符号需求均 <= 本系统 ${SYS_GLIBC:-?}"
    fi
else
    warn "无 objdump/readelf，跳过包内库符号版本抽查"
fi

# ---- 3. Python 依赖解析（关键: 检测是否被打包的 cpython 版 .so 遮蔽）----
echo ""
echo "--- 3. Python 老化测试依赖 ---"
echo "  本机 python3: $PY3"
# 3.1 开箱即用形态：包内解包的 PyQt5/psutil/OpenGL 应能直接 import（零安装零联网）
if [ -d "$APP_DIR/python_modules/PyQt5" ] && [ -d "$APP_DIR/python_modules/psutil" ] && [ -d "$APP_DIR/python_modules/OpenGL" ]; then
    OOO_TEST=$(cd "$APP_DIR" && LD_LIBRARY_PATH="$APP_DIR/lib" \
        PYTHONPATH="$APP_DIR/python_modules" \
        python3 -c "import PyQt5, PyQt5.QtOpenGL, psutil, OpenGL; print('ok')" 2>&1)
    if echo "$OOO_TEST" | grep -q "^ok"; then
        ok "开箱即用依赖可用（包内 PyQt5/psutil/OpenGL 直接 import 成功）"
    else
        bad "包内依赖解包存在但 import 失败（本机 python 版本与打包机不符?）: $OOO_TEST"
        warn "  程序将回退到离线 dpkg -i → 联网安装兜底链"
    fi
else
    warn "包内未按'开箱即用'形态携带 PyQt5/psutil/OpenGL（首次运行老化测试需密码/联网）"
fi
if [ -d "$APP_DIR/python_modules/psutil" ]; then
    BADSO=$(find "$APP_DIR/python_modules/psutil" -name "*.so" 2>/dev/null)
    if [ -n "$BADSO" ]; then
        warn "包内 python_modules/psutil 携带 C 扩展 $(basename "$BADSO")——若与本机 python 版本不符会遮蔽系统包, 建议打包侧删除该目录"
    fi
fi
IMPORT_TEST=$(python3 -c "import PyQt5, PyQt5.QtOpenGL, psutil, OpenGL; import PyQt5.QtWidgets; print('ok, psutil='+psutil.__file__)" 2>&1)
if echo "$IMPORT_TEST" | grep -q "^ok"; then
    ok "import PyQt5/QtOpenGL/psutil/OpenGL 成功"
    echo "      → $IMPORT_TEST"
    echo "$IMPORT_TEST" | grep -q "$APP_DIR/python_modules" && warn "psutil 来自包内 python_modules（存在版本遮蔽风险）" || ok "psutil 来自系统 dist-packages（正确）"
else
    bad "Python 导入失败: $IMPORT_TEST"
    warn "  修复参考: sudo dpkg -i $APP_DIR/python_modules/apt_packages/*.deb && sudo apt-get install -f -y"
fi

# ---- 4. 外部命令依赖 ----
echo ""
echo "--- 4. 功能外部命令 ---"
for c in ethtool ping nmcli systemctl journalctl last python3 ip sensors smartctl rtcwake; do
    if command -v "$c" >/dev/null 2>&1; then ok "命令 $c 存在"; else warn "命令 $c 缺失(对应功能可能降级/不可用)"; fi
done
# rtcwake 是循环休眠功能的关键命令(2026-09-11 飞腾真机 bug 修复)
if command -v rtcwake >/dev/null 2>&1; then
    # 测试 rtcwake 是否能写 RTC 闹钟(无 RTC 设备/权限不足会失败)
    if rtcwake -m no -s 5 -t "$(date -d '+10 seconds' +%s)" 2>/dev/null; then
        ok "rtcwake 可用(循环休眠自动唤醒功能可用)"
    else
        warn "rtcwake 存在但 RTC 写权限不足(循环休眠无法自动唤醒,会回退普通 suspend)"
    fi
else
    bad "rtcwake 不存在(循环休眠无法自动唤醒,会卡在休眠状态,见 2026-09-11 飞腾 bug)"
fi
[ -f "$APP_DIR/extra-bin/ethtool" ] && ok "包内 extra-bin/ethtool 已随包" || warn "包内未带 ethtool(WOL 在无 ethtool 机器离线不可用)"
# 网络测速已改为内网双机 TCP 对测(TCP 18964/UDP 18965),零外部工具依赖,离线可用;
# speedtest-cli 仅为公网测速的可选增强,缺失不影响内网测速功能
command -v speedtest-cli >/dev/null 2>&1 || ok "speedtest-cli 未装(内网对测模式无需此工具,网络测速离线可用)"

# glmark2 是老化测试显卡压测项的依赖(2026-09-15 新增)
# 双形态: 系统 PATH 或 包内 extra-bin/glmark2(离线携带,postinst 装到 /usr/local/bin)
GLMARK_SYS=$(command -v glmark2-es2 2>/dev/null || command -v glmark2 2>/dev/null)
GLMARK_PKG=$(find "$APP_DIR/extra-bin/glmark2" -type f -name 'glmark2*' -perm -u+x 2>/dev/null | head -1)
if [ -n "$GLMARK_SYS" ]; then
    ok "glmark2 可用(系统安装: $GLMARK_SYS)"
    # 场景数据目录(编译期硬编码 /usr/share/glmark2,缺失则所有场景报错)
    [ -d /usr/share/glmark2 ] && ok "glmark2 场景数据目录存在" || warn "/usr/share/glmark2 缺失(glmark2 会启动但场景全部报错)"
elif [ -n "$GLMARK_PKG" ] || command -v glmark2 >/dev/null 2>&1 || command -v glmark2-es2 >/dev/null 2>&1; then
    ok "glmark2 可用(包内携带, postinst 已安装到系统路径)"
    [ -d /usr/share/glmark2 ] && ok "glmark2 场景数据目录存在" || warn "/usr/share/glmark2 缺失(glmark2 会启动但场景全部报错)"
else
    warn "glmark2 不存在(老化测试 glmark2 显卡压测项将自动跳过, 其它项不受影响)"
fi

# ---- 5. sudoers 配置 ----
echo ""
echo "--- 5. 提权配置 ---"
if [ -f /etc/sudoers.d/elsky-test-client ]; then
    if grep -q "USERNAME_PLACEHOLDER" /etc/sudoers.d/elsky-test-client 2>/dev/null; then
        bad "sudoers 含 USERNAME_PLACEHOLDER 占位符, 免密对所有真实用户不生效"
    else
        ok "sudoers 文件存在且非占位"
        sudo -n -l 2>/dev/null | grep -qE "apt|dpkg|systemctl|poweroff|reboot|shutdown" && ok "免密规则可解析" || warn "当前用户未匹配到免密规则(多用户/非安装用户属预期)"
    fi
    if grep -qE "NOPASSWD:.*/usr/bin/(dpkg|apt|apt-get)" /etc/sudoers.d/elsky-test-client 2>/dev/null; then
        warn "sudoers 放行裸 dpkg/apt, 提权面偏大(建议收窄为具体命令+参数白名单)"
    fi
else
    warn "/etc/sudoers.d/elsky-test-client 不存在(可能未装或已卸载)"
fi
id -nG 2>/dev/null | grep -q dialout && ok "当前用户已在 dialout 组" || warn "当前用户不在 dialout 组(串口访问需重新登录生效)"

# ---- 6. GUI 启动冒烟（有显示环境时）----
echo ""
echo "--- 6. GUI 启动冒烟 ---"
if [ -n "$DISPLAY" ] || [ -n "$WAYLAND_DISPLAY" ]; then
    timeout 8 "$APP_DIR/elsky-test-client.sh" >/tmp/elsky_gui_smoke.log 2>&1 &
    GPID=$!
    sleep 5
    if kill -0 "$GPID" 2>/dev/null; then
        ok "GUI 进程存活(启动无缺库崩溃)"
        kill "$GPID" 2>/dev/null; sleep 1; kill -9 "$GPID" 2>/dev/null
    else
        warn "GUI 进程提前退出, 日志见 /tmp/elsky_gui_smoke.log:"
        sed -n '1,10p' /tmp/elsky_gui_smoke.log | sed 's/^/      /'
    fi
else
    warn "无 DISPLAY, 跳过 GUI 冒烟(请在有桌面环境时运行)"
fi

# ---- 7. 包内版本一致性（人工核对面）----
echo ""
echo "--- 7. 版本信息 ---"
grep -E "生成时间|目标架构|编译 Qt|动态库总计" "$APP_DIR/DEPENDENCY_CHECK.txt" 2>/dev/null | head -6

# ---- 7.5 循环休眠与断电检测专项（2026-09-11 飞腾真机 bug 回归）----
echo ""
echo "--- 7.5 循环休眠与 suspend 期间断电检测 ---"
# 7.5.1 源码侧: cycle_controller 必须用 suspendWithWake(不能是裸 suspend)
SRC_CYCLE="core/cycle_controller.cpp"
if [ -f "$SRC_CYCLE" ]; then
    if grep -q "suspendWithWake" "$SRC_CYCLE" && grep -q "hibernateWithWake" "$SRC_CYCLE"; then
        ok "源码使用 suspendWithWake/hibernateWithWake(循环休眠有 RTC 自唤醒)"
    else
        bad "源码未使用 suspendWithWake/hibernateWithWake(飞腾 D2000 真机 bug:会卡在休眠状态)"
    fi
else
    warn "源码 $SRC_CYCLE 不在当前目录(略过代码层检查)"
fi
# 7.5.2 源码侧: SystemController 必须实现 suspendWithWake
SRC_SC="core/system_controller.cpp"
if [ -f "$SRC_SC" ]; then
    if grep -q "rtcwake -m mem" "$SRC_SC" && grep -q "rtcwake -m disk" "$SRC_SC"; then
        ok "SystemController 实现 rtcwake -m mem/disk 路径(原子休眠+自唤醒)"
    else
        bad "SystemController 未实现 rtcwake 路径"
    fi
fi
# 7.5.3 源码侧: IllegalShutdownDetector 必须实现 suspend.marker 检测
SRC_SD="core/illegal_shutdown_detector.cpp"
if [ -f "$SRC_SD" ]; then
    if grep -q "markSuspend" "$SRC_SD" && grep -q "checkSuspendMarker" "$SRC_SD" && grep -q "第零点五层"; then
        ok "suspend.marker 机制完整(markSuspend/checkSuspendMarker + 第零点五层检测)"
    else
        bad "suspend.marker 机制缺失(suspend 期间断电会漏判非法关机)"
    fi
fi
# 7.5.4 运行时: 检查上一轮 suspend 留下的 marker 状态(仅在测试用机执行后才有意义)
SD_DIR="$HOME/.local/share/ELSKY/TestClient"
SD_DIR2="$HOME/.config/ELSKY/TestClient"
for d in "$SD_DIR" "$SD_DIR2"; do
    if [ -d "$d" ]; then
        if [ -f "$d/suspend.marker" ]; then
            warn "$d/suspend.marker 残留(检测启动后会被自动清除,属预期)"
        fi
        if [ -f "$d/hibernate.marker" ]; then
            warn "$d/hibernate.marker 残留(检测启动后会被自动清除,属预期)"
        fi
        ok "已定位 marker 目录: $d"
        break
    fi
done
# 7.5.5 模拟一次 suspend 期间断电(可选,需用户授权): 写 marker → 计算 bootTime → 验证检测
echo "  [手动测试] 模拟 suspend 期间断电(可重复执行,验证检测正确性):"
echo "    1) 当前时间:   $(date '+%Y-%m-%d %H:%M:%S')"
echo "    2) 模拟:       echo suspend_start_time=\$(date '+%Y-%m-%d %H:%M:%S') > $SD_DIR/suspend.marker 2>/dev/null || true"
echo "    3) 重启后,检测器应判定为: suspend 期间断电,非法关机"
echo "    4) 查看日志:  $HOME/monitor_log_service.log 或 /tmp/elsky_illegal_shutdown.log"
echo "  请人工核对: deb 内 aging_test_gui.py 行数与源码一致(应 ≥1800 行):"
wc -l "$APP_DIR/aging_test_gui.py" 2>/dev/null | awk '{print "      deb 内 aging_test_gui.py: "$1" 行"}'

# ---- 8. 卸载清理验证指引（人工执行后核对）----
echo ""
echo "--- 8. 卸载验证指引（卸载后运行本节核对清单）---"
cat <<'TIP'
  执行: sudo apt remove --purge elsky-test-client（或开始菜单右键卸载）
  卸载后应全部满足:
    [ ] dpkg -l | grep elsky-test-client 无残留
    [ ] 所有用户桌面无 elsky-test-client.desktop（~/桌面 与 ~/Desktop）
    [ ] /etc/sudoers.d/elsky-test-client 不存在
    [ ] /usr/local/bin/ethtool 已删（若为 postinst 兜底安装的）
    [ ] ~/.config/ELSKY 已删
    [ ] /var/lib/elsky-test-client 已删
    [ ] 开始菜单无'研盛测试客户端'条目
TIP

echo ""
echo "=============================================="
echo " 结果汇总: PASS=$PASS  FAIL=$FAIL  WARN=$WARN"
echo "=============================================="
[ "$FAIL" -gt 0 ] && echo ">>> 存在 FAIL 项, 请修复后重测" || echo ">>> 无 FAIL 项(WARN 请按说明人工确认)"
exit 0
