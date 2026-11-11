#!/bin/bash
# =============================================================
# 研盛测试客户端 - deb 安装包打包脚本
# 功能：
#   1. 收集当前平台编译产物 + Python 离线依赖 + 依赖库
#   2. 构建标准 deb 目录结构
#   3. 生成桌面图标（.desktop）、应用菜单图标
#   4. postinst 安装后脚本：配置 sudoers 免密（一次性）、拨号组权限
#   5. 使用 dpkg-deb 打包为 .deb
#
# 用法：
#   ./cross_platform_package_deb.sh
# 产出：
#   release/elsky-test-client_<版本>_<架构>.deb
#
# 安装方式（目标机）：
#   双击 .deb 或用命令：
#   sudo dpkg -i elsky-test-client_*.deb
#   （或 sudo apt install ./elsky-test-client_*.deb）
# 安装后桌面出现"研盛测试客户端"图标，双击即可启动。
# =============================================================
# 注意：不使用 set -e——任意子步骤失败（apt-get download 无源、rm 无匹配文件等），
# 都不应导致脚本整体退出。apt 离线 .deb 下载失败时降级为提示"目标机需联网安装"，
# 但 .deb 包本身仍正常生成（apt_packages 目录为空不影响 deb 结构）。
set +e

echo "=== 研盛测试客户端 deb 打包脚本 ==="

# 项目根目录
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="$PROJECT_ROOT/bin"

# 版本与包元信息（可在此调整）
PKG_NAME="elsky-test-client"
PKG_VERSION="${PKG_VERSION:-2.1}"
PKG_MAINTAINER="ELSKY <support@elsky.com>"
PKG_DESCRIPTION="研盛测试客户端 - 电源管理、网络唤醒、系统监控、设备检测、老化测试工具"

# 检测架构 -> deb 架构名
ARCH=$(uname -m)
case "$ARCH" in
    aarch64)
        DEB_ARCH="arm64"
        PLATFORM="ft"
        PLATFORM_NAME="飞腾ARM64"
        ;;
    x86_64)
        DEB_ARCH="amd64"
        PLATFORM="x86"
        PLATFORM_NAME="X86_64"
        ;;
    i386|i686)
        DEB_ARCH="i386"
        PLATFORM="x86"
        PLATFORM_NAME="X86"
        ;;
    loongarch64|loong64|loongarch|mips64el|mips64|mipsel)
        # 龙芯 deb 架构名以本机 dpkg 的真实输出为准：麒麟/统信/龙芯官方系统
        # 可能是 loongarch64、loong64 或 mips64el，硬编码任一值都可能让
        # dpkg -i 报 "package architecture does not match system" 拒绝安装。
        DEB_ARCH="$(dpkg --print-architecture 2>/dev/null | tr -d '[:space:]')"
        case "$DEB_ARCH" in
            loongarch64|loong64|loongarch|mips64el|mips64|mipsel) : ;;
            *) DEB_ARCH="loong64" ;;
        esac
        PLATFORM="loong"
        PLATFORM_NAME="龙芯"
        ;;
    *)
        echo "错误: 不支持的平台架构: $ARCH"
        exit 1
        ;;
esac

echo "平台: $ARCH ($PLATFORM_NAME) -> deb架构: $DEB_ARCH"
echo "版本: $PKG_VERSION"

# 检查编译产物
# 修复（A4）：cross_platform_build.sh 在飞腾 D2000/D3000 上产出
# ELSKY_test_client_ft2000 / _ft3000，而旧代码硬编码只找 _ft →
# FT-2000/FT-2500 机型打包直接报"未找到编译产物"退出。
# 改为两级查找：先精确名 ELSKY_test_client_ft，再通配 ELSKY_test_client_ft*
APP_BIN=""
if [ -f "$BIN_DIR/ELSKY_test_client_${PLATFORM}" ]; then
    APP_BIN="ELSKY_test_client_${PLATFORM}"
else
    # 通配匹配 ft2000/ft3000 等细分型号产物
    APP_BIN=$(ls "$BIN_DIR"/ELSKY_test_client_${PLATFORM}* 2>/dev/null | head -1 | xargs -r basename 2>/dev/null)
fi
if [ -z "$APP_BIN" ] || [ ! -f "$BIN_DIR/$APP_BIN" ]; then
    echo "错误: 未找到编译产物（期望 ELSKY_test_client_${PLATFORM} 或 ELSKY_test_client_${PLATFORM}*）"
    echo "请先运行: ./cross_platform_build.sh"
    ls -la "$BIN_DIR" 2>/dev/null | grep ELSKY || true
    exit 1
fi
echo "✓ 找到编译产物: $BIN_DIR/$APP_BIN"

# ---- 外来二进制检测（防止把别处编译的旧产物在本机打包）----
# 典型事故：在 A 机（新系统）编译后把整个工程目录拷到 B 机（统信1070）打包，
# bin/ 里还是 A 机的产物 → 装到 B 机必报 GLIBC_2.29 not found。
# 这里校验：主程序要求的最大 GLIBC 不得超过编译机自身 glibc。
BUILD_GLIBC=$(ldd --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+$' | head -1)
EXE_MAX_GLIBC=$( { objdump -T "$BIN_DIR/$APP_BIN" 2>/dev/null || readelf -V "$BIN_DIR/$APP_BIN" 2>/dev/null; } \
    | grep -oE 'GLIBC_[0-9]+(\.[0-9]+)+' | sort -Vu | tail -1)
if [ -n "$EXE_MAX_GLIBC" ] && [ -n "$BUILD_GLIBC" ]; then
    EXE_NEED="${EXE_MAX_GLIBC#GLIBC_}"
    if [ "$EXE_NEED" != "$BUILD_GLIBC" ] && \
       [ "$(printf '%s\n' "$EXE_NEED" "$BUILD_GLIBC" | sort -V | head -1)" = "$BUILD_GLIBC" ]; then
        echo "✗✗✗ 打包中止：编译产物是'外来'的 ✗✗✗"
        echo "  $APP_BIN 要求 ${EXE_MAX_GLIBC}，但本机 glibc 只有 $BUILD_GLIBC。"
        echo "  说明 bin/ 下的产物是在更高版本系统上编译的（例如从其它机器拷贝了整个工程目录），"
        echo "  这样打出的包装到本系统必定报 GLIBC not found。"
        echo "  解决: 在本机重新编译后再打包:"
        echo "    rm -rf build bin/ELSKY_test_client_* && ./cross_platform_build.sh && ./cross_platform_package_deb.sh"
        exit 1
    fi
    echo "✓ 编译产物与本机 glibc 兼容（最大需求 ${EXE_MAX_GLIBC} <= 本机 $BUILD_GLIBC）"
fi

# 检查 dpkg-deb
if ! command -v dpkg-deb >/dev/null 2>&1; then
    echo "错误: 未找到 dpkg-deb，请在 Debian/Ubuntu/统信/麒麟系统上运行本脚本"
    exit 1
fi

# ---- 0. 用 qmake 精确定位编译用 Qt 的安装路径（最可靠来源）----
# 背景：Qt 库/插件若装在非标准路径（/opt/Qt5.x、麒麟/统信自定义路径等），
# 硬编码的 SYS_LIB_DIRS / QT_PLUGIN_CANDIDATES 都找不到 → lib/ 缺 Qt →
# 目标机（如海光统信）报"缺少 Qt 依赖"双击无反应。
# 编译时用的 qmake 一定知道自己 Qt 的 lib/plugins 位置。
# 根因修复（统信1070 误选 Qt4.8.7）：/usr/bin/qmake 是 qtchooser 链接且指向 Qt4，
# 旧逻辑把 `command -v qmake` 排在候选第一位且不校验大版本。
# 现在强制校验 `qmake -query QT_VERSION` 必须为 5.*，且 Qt5 专用路径优先。
find_real_qmake() {
    local c out
    # glob 候选可覆盖所有架构（aarch64/x86_64/mips64el/loongarch64/sw_64 等）
    for c in /usr/lib/*-linux-*/qt5/bin/qmake \
             /usr/lib/qt5/bin/qmake \
             /usr/bin/qmake-qt5; do
        [ -x "$c" ] || continue
        out=$("$c" -query QT_VERSION 2>/dev/null) || continue
        case "$out" in
            5.*) echo "$c"; return 0 ;;
        esac
    done
    # 兜底: PATH 中的 qmake（同样校验大版本，防止统信 qtchooser 指向 Qt4）
    c=$(command -v qmake 2>/dev/null)
    if [ -n "$c" ] && [ -x "$c" ]; then
        out=$("$c" -query QT_VERSION 2>/dev/null) || out=""
        case "$out" in
            5.*) echo "$c"; return 0 ;;
            *) echo "  ⚠ PATH 中的 qmake($c) 是 Qt ${out:-未知}，已跳过（仅支持 Qt5）" >&2 ;;
        esac
    fi
    return 1
}
QT_LIB_FROM_QMAKE=""
QT_PLUGINS_FROM_QMAKE=""
REAL_QMAKE=$(find_real_qmake)
if [ -n "$REAL_QMAKE" ]; then
    QT_LIB_FROM_QMAKE=$("$REAL_QMAKE" -query QT_INSTALL_LIBS 2>/dev/null)
    QT_PLUGINS_FROM_QMAKE=$("$REAL_QMAKE" -query QT_INSTALL_PLUGINS 2>/dev/null)
    QT_VER_FROM_QMAKE=$("$REAL_QMAKE" -query QT_VERSION 2>/dev/null)
    echo "✓ 编译用 Qt 定位成功:"
    echo "    qmake:     $REAL_QMAKE (Qt ${QT_VER_FROM_QMAKE:-未知版本})"
    echo "    Qt 库目录:   ${QT_LIB_FROM_QMAKE:-未获取}"
    echo "    Qt 插件目录: ${QT_PLUGINS_FROM_QMAKE:-未获取}"
else
    echo "⚠ 未找到可用的 Qt5 qmake，Qt 库收集将仅依赖标准系统路径"
    echo "  统信UOS/麒麟/Debian/Ubuntu 请安装: sudo apt install qtbase5-dev qt5-qmake qtchooser"
fi

# ============ 构建 deb 目录结构 ============
BUILD_ROOT="$PROJECT_ROOT/release/deb_build"
PACKAGE_DIR="$BUILD_ROOT/${PKG_NAME}_${PKG_VERSION}_${DEB_ARCH}"
INSTALL_PREFIX="usr/lib/elsky-test-client"   # 程序安装路径 /usr/lib/elsky-test-client
DEBIAN_DIR="$PACKAGE_DIR/DEBIAN"
INSTALL_DIR="$PACKAGE_DIR/$INSTALL_PREFIX"

echo ""
echo "清理并创建打包目录..."
rm -rf "$PACKAGE_DIR"
mkdir -p "$DEBIAN_DIR" "$INSTALL_DIR"

# ---- 1. 复制可执行文件 ----
echo "复制可执行文件..."
cp "$BIN_DIR/$APP_BIN" "$INSTALL_DIR/ELSKYTestClient"
chmod 755 "$INSTALL_DIR/ELSKYTestClient"

# ---- 2. 复制 Python 老化测试脚本 ----
echo "复制 Python 脚本..."
# aging_selftest.py: 跨架构自检工具(显存检测/停止时序/依赖完整性),
# 部署到目标机后执行 `python3 aging_selftest.py --with-gui` 即可验证
for py in aging_test_gui.py aging_auto_start.py complex_scene.py aging_selftest.py; do
    if [ -f "$PROJECT_ROOT/$py" ]; then
        cp "$PROJECT_ROOT/$py" "$INSTALL_DIR/"
        echo "  ✓ $py"
    fi
done

# ---- 3. 收集动态库（全量离线打包）----
# 目标：无网络环境下开箱即用。三层收集确保无遗漏：
#   层1: 主程序 ldd 直接依赖
#   层2: 递归收集——对已收集的库/插件再跑 ldd，直到无新增（Qt插件、3D栈的传递依赖）
#   层3: 显式图形栈清单——libGL/libEGL/libdrm/mesa/xcb 全家桶（dlopen 运行时加载，ldd 看不到）
# 唯一不打包：glibc 家族运行时（版本绑定编译机内核接口，目标机必有，
# 且 ld-linux 加载器与 libc 必须配套，随包反而必崩）。
# 注意：libstdc++/libgcc_s 自 S3 起改为【随包携带】——它们不绑定内核，
# 目标机（如统信1070 GLIBCXX 仅 3.4.24）系统版本低于编译机需求时，
# RUNPATH/LD_LIBRARY_PATH 优先命中包内副本，根治
# "version `GLIBCXX_3.4.xx' not found" 启动失败。
echo "收集动态库（全量离线模式）..."
LIB_DIR="$INSTALL_DIR/lib"
mkdir -p "$LIB_DIR"

# 常见库目录（按发行版；qmake 提供的编译 Qt 路径放最前，优先级最高）
# 追加龙芯 multiarch 目录（LoongArch 新/旧世界、老龙芯 MIPS64el）。
# 仅扩展候选列表，既有目录顺序不变；$QT_LIB_FROM_QMAKE（qmake 查询）仍是首选。
SYS_LIB_DIRS="$QT_LIB_FROM_QMAKE /usr/lib/aarch64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/lib/i386-linux-gnu /usr/lib/arm-linux-gnueabihf /usr/lib/loongarch64-linux-gnu /usr/lib/loong64-linux-gnu /usr/lib/mips64el-linux-gnuabi64 /usr/lib /usr/lib64 /lib/aarch64-linux-gnu /lib/x86_64-linux-gnu /lib/loongarch64-linux-gnu /lib/mips64el-linux-gnuabi64 /lib"

# 判断库名是否属于 glibc 家族（不打包：绑定内核接口/加载器配套，目标机必有）
# 注意：libstdc++/libgcc_s 不在排除列——随包携带（见段首注释 S3），
# 解决目标机系统 libstdc++ 版本低于编译机导致的 GLIBCXX not found。
is_system_lib() {
    case "$1" in
        libc.so*|libm.so*|libpthread*|libdl*|librt*|libresolv*|libnsl*|libutil*|libcrypt*|ld-linux*|libBrokenLocale*|libSegFault*|libanl*)
            return 0 ;;
        *)
            return 1 ;;
    esac
}

# ---- GLIBC 目标基线工具（2026-10-09 新增，供依赖收集过滤 + 7.5 门禁共用） ----
# 原理：包内 lib/ 是 RUNPATH/LD_LIBRARY_PATH 的优先查找路径。
# 若把"要求 GLIBC > 目标基线"的编译机系统库收进包里，目标机加载器会
# 优先命中包内副本 → 必报 "version `GLIBC_2.xx' not found"，
# 连回退目标机系统自带同名库的机会都没有。
# 因此：收集阶段就把超标库拦截在包外（collect_deps_of / Qt 插件复制处），
# 目标机自动回退使用自己系统自带的同名库（如 libsystemd/libudev/libwebp，
# 统信1070/麒麟V10 桌面版全部自带），这比带超标库严格更优。
# [2026-10-16] 记录用户是否显式指定基线（未指定时门禁允许"自动降级到编译机 glibc"，
# 兆芯/麒麟2503 等编译机=目标机的场景直接运行脚本即可完成打包；显式指定则严格拦截）
MIN_TARGET_GLIBC_EXPLICIT="${MIN_TARGET_GLIBC:-}"
MIN_TARGET_GLIBC="${MIN_TARGET_GLIBC:-2.28}"   # 目标基线：统信UOS 1070 / 银河麒麟V10 SP1 / Debian10 = 2.28
# [2026-10-15] host 快捷方式（兆芯/麒麟2503 等编译机=目标机的场景）:
#   MIN_TARGET_GLIBC=host ./cross_platform_package_deb.sh
# 自动取编译机 glibc 作为基线——本包保证在与编译机相同或更新 glibc 的系统上运行
#（如麒麟2503 glibc 2.31 编译+部署到同款系统）。不设置时默认仍为 2.28，
# 统信1070/麒麟V10 打包行为完全不变。
if [ "$MIN_TARGET_GLIBC" = "host" ]; then
    _HOST_GLIBC=$(ldd --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+' | head -1)
    if [ -n "$_HOST_GLIBC" ]; then
        MIN_TARGET_GLIBC="$_HOST_GLIBC"
        GLIBC_BASELINE_FROM_HOST=1
        echo "ℹ MIN_TARGET_GLIBC=host: 目标基线取编译机 glibc(${MIN_TARGET_GLIBC})"
        echo "  注意: 本包仅保证在与编译机相同或更新 glibc 的系统上正常运行;"
        echo "        如需部署到统信1070/麒麟V10(glibc 2.28)，请勿使用 host，保持默认基线。"
    else
        echo "⚠ 无法识别编译机 glibc 版本，回退默认基线 2.28"
        MIN_TARGET_GLIBC="2.28"
    fi
fi

# [2026-10-17] 库收集前的自动基线对齐（修复麒麟2503 打包"libQt5Core.so.5 未打包进 lib/"致命缺失）:
# 库收集阶段（collect_deps_of / Qt 全家桶收集）用 MIN_TARGET_GLIBC 过滤"超标库"，
# 若编译机 glibc 高于默认基线 2.28（如麒麟2503 glibc 2.31+），其系统 Qt 库会被全部
# 误剔除，随后自检报"致命缺失，禁止打包"。既然门禁已允许"未显式指定时以编译机 glibc
# 为基线"，此处就把对齐提前到收集之前——收集与门禁用同一个有效基线，行为一致。
# 显式指定 MIN_TARGET_GLIBC（含 =host）时不做调整，统信1070 等场景行为零变化。
if [ -z "$MIN_TARGET_GLIBC_EXPLICIT" ] && [ -n "${BUILD_GLIBC:-}" ]; then
    if [ "$(printf '%s\n' "$MIN_TARGET_GLIBC" "$BUILD_GLIBC" | sort -V | head -1)" = "$MIN_TARGET_GLIBC" ] \
       && [ "$MIN_TARGET_GLIBC" != "$BUILD_GLIBC" ]; then
        echo "⚠⚠⚠ 自动基线对齐（库收集前）⚠⚠⚠"
        echo "  编译机 glibc($BUILD_GLIBC) 高于默认基线 $MIN_TARGET_GLIBC。"
        echo "  为避免编译机系统库（Qt/图形栈等）被超标拦截误剔除，"
        echo "  已自动将有效目标基线提升为编译机 glibc($BUILD_GLIBC) 继续打包。"
        echo "  本包仅适用于与编译机相同或更新 glibc 的系统；"
        echo "  如需部署统信1070/麒麟V10(glibc 2.28)，请在低 glibc 机器上编译打包，"
        echo "  或显式指定: MIN_TARGET_GLIBC=2.28 ./cross_platform_package_deb.sh（将严格拦截）。"
        MIN_TARGET_GLIBC="$BUILD_GLIBC"
    fi
fi

ver_gt() {  # 判断 $1 > $2（版本比较）
    [ "$1" != "$2" ] && [ "$(printf '%s\n' "$1" "$2" | sort -V | head -1)" = "$2" ]
}

elf_max_glibc() {
    local f="$1" out
    out=$(objdump -T "$f" 2>/dev/null | grep -oE 'GLIBC_[0-9]+(\.[0-9]+)+' | sort -Vu | tail -1)
    [ -z "$out" ] && out=$(readelf -V "$f" 2>/dev/null | grep -oE 'GLIBC_[0-9]+(\.[0-9]+)+' | sort -Vu | tail -1)
    echo "$out"
}

is_elf() { [ "$(head -c 4 "$1" 2>/dev/null | od -An -tx1 | tr -d ' \n')" = "7f454c46" ]; }

# 单个 .so 的 GLIBC 要求是否 <= 目标基线（0=兼容可收进包，1=超标应剔除）
lib_compat_ok() {
    local f="$1" maxv need
    is_elf "$f" || return 0
    maxv=$(elf_max_glibc "$f")
    [ -z "$maxv" ] && return 0
    need="${maxv#GLIBC_}"
    ! ver_gt "$need" "$MIN_TARGET_GLIBC"
}

# 被拦截/剔除的超标库清单（打包结束的门禁段统一汇总提示）
GLIBC_SKIP_LIST_FILE="$(mktemp /tmp/elsky_glibc_skip.XXXXXX)"

# 从一个 ELF 文件的 ldd 输出提取库路径，收集到 LIB_DIR（排除 glibc 家族）
# 2026-10-09 增强：GLIBC 超标库拦截——要求高于 MIN_TARGET_GLIBC 的编译机系统库
# 不收进包（目标机回退用系统自带同名库），并记录到 GLIBC_SKIP_LIST_FILE 供门禁汇总
collect_deps_of() {
    local elf="$1"
    [ -f "$elf" ] || return 0
    ldd "$elf" 2>/dev/null \
        | sed -nE 's/.*=> ([^ ]+) \(0x[0-9a-f]+\)/\1/p' \
        | while read -r lib; do
            if [ -f "$lib" ] && ! is_system_lib "$(basename "$lib")"; then
                if ! lib_compat_ok "$lib"; then
                    echo "$(basename "$lib")" >> "$GLIBC_SKIP_LIST_FILE" 2>/dev/null
                    continue
                fi
                cp -n "$lib" "$LIB_DIR/" 2>/dev/null || true
            fi
        done
}

# 层1：主程序直接依赖
collect_deps_of "$BIN_DIR/$APP_BIN"

# 强制收集 Qt 核心库与 SerialPort（部分精简编译环境 ldd 可能漏列）
# 同样走 GLIBC 超标拦截（2026-10-09）
for qtlib in libQt5Core libQt5Gui libQt5Widgets libQt5Network libQt5DBus libQt5SerialPort; do
    for sp_path in $SYS_LIB_DIRS; do
        for f in "$sp_path"/${qtlib}.so*; do
            [ -f "$f" ] || continue
            if ! lib_compat_ok "$f"; then
                echo "$(basename "$f")" >> "$GLIBC_SKIP_LIST_FILE" 2>/dev/null
                continue
            fi
            cp -n "$f" "$LIB_DIR/" 2>/dev/null || true
        done
    done
done

# 强制收集 Qt 全家桶：编译用 Qt（qmake 定位）lib 目录下的所有 Qt 库
# 这是根治"目标机缺 Qt 依赖"的关键——ldd 只列主程序直接依赖，
# 而 xcb 平台插件运行时还需要 libQt5XcbQpa/libQt5DBus/libQt5OpenGL 等，
# 全量带走编译机同版本 Qt，保证 ABI 完全一致。
if [ -n "$QT_LIB_FROM_QMAKE" ] && [ -d "$QT_LIB_FROM_QMAKE" ]; then
    QT_ALL_COUNT=0
    for f in "$QT_LIB_FROM_QMAKE"/libQt5*.so*; do
        if [ -f "$f" ]; then
            if ! lib_compat_ok "$f"; then
                echo "$(basename "$f")" >> "$GLIBC_SKIP_LIST_FILE" 2>/dev/null
                continue
            fi
            cp -n "$f" "$LIB_DIR/" 2>/dev/null && QT_ALL_COUNT=$((QT_ALL_COUNT + 1))
        fi
    done
    echo "  ✓ 从编译 Qt 目录全量收集 $QT_ALL_COUNT 个 Qt 库: $QT_LIB_FROM_QMAKE"
else
    echo "  ⚠ 未能从 qmake 定位 Qt 库目录，回退标准路径逐个收集"
    # 回退：从所有 SYS_LIB_DIRS 收集全部 libQt5*
    for sp_path in $SYS_LIB_DIRS; do
        for f in "$sp_path"/libQt5*.so*; do
            [ -f "$f" ] || continue
            if ! lib_compat_ok "$f"; then
                echo "$(basename "$f")" >> "$GLIBC_SKIP_LIST_FILE" 2>/dev/null
                continue
            fi
            cp -n "$f" "$LIB_DIR/" 2>/dev/null || true
        done
    done
fi

# ---- 层3a：显式收集 3D/图形栈（dlopen 加载，主程序 ldd 看不到）----
# Qt xcb 插件运行时通过 GLX/EGL 加载 3D 库；统信1070等精简系统没有 → 崩溃
# [2026-10-17] 重要修复：GL/mesa 栈改收进 lib/gl-fallback/ 子目录，不再进入
# 默认查找路径！此前直接收进 lib/ 会随 LD_LIBRARY_PATH 优先加载，在目标机
# （如麒麟2503）上包内 mesa/glvnd 库与系统 DRI 驱动不匹配 → 3D 测试花屏、
# glmark2 无法运行（不勾选 glmark 也复现）。现改为：默认用系统 GL 栈
# （与显卡驱动匹配），仅启动脚本检测到系统缺 OpenGL 时才启用 gl-fallback，
# 统信1070 等精简系统的兜底能力保留。
mkdir -p "$LIB_DIR/gl-fallback"
for gllib in libGL libGLX libOpenGL libGLdispatch libEGL libGLESv2 libgbm libdrm \
             libglapi libosmesa libswrast libglvnd; do
    for sp_path in $SYS_LIB_DIRS; do
        for f in "$sp_path"/${gllib}.so*; do
            [ -f "$f" ] || continue
            if ! lib_compat_ok "$f"; then
                echo "$(basename "$f")" >> "$GLIBC_SKIP_LIST_FILE" 2>/dev/null
                continue
            fi
            cp -n "$f" "$LIB_DIR/gl-fallback/" 2>/dev/null || true
        done
    done
done
# gl-fallback 为空（系统本就没有这些库？不太可能）则清掉空目录
rmdir "$LIB_DIR/gl-fallback" 2>/dev/null || true

# ---- 层3b：显式收集 xcb/X11 客户端库（Qt xcb 平台插件的运行时依赖）----
for xlib in libX11 libX11-xcb libXext libXi libXrender libXrandr libXinerama libXcursor \
            libXfixes libXtst libXau libXdmcp libXrender libXss libxcb \
            libxcb-xinerama libxcb-randr libxcb-render libxcb-shape libxcb-shm \
            libxcb-sync libxcb-xfixes libxcb-xkb libxcb-icccm libxcb-image \
            libxcb-keysyms libxcb-render-util libxcb-util libxkbcommon libxkbcommon-x11 \
            libfontconfig libfreetype libharfbuzz libpng16 libjpeg libdouble-conversion; do
    for sp_path in $SYS_LIB_DIRS; do
        for f in "$sp_path"/${xlib}.so*; do
            [ -f "$f" ] || continue
            if ! lib_compat_ok "$f"; then
                echo "$(basename "$f")" >> "$GLIBC_SKIP_LIST_FILE" 2>/dev/null
                continue
            fi
            cp -n "$f" "$LIB_DIR/" 2>/dev/null || true
        done
    done
done

# ---- 层3c：收集 WOL 所需的 ethtool 二进制 ----
# WOL（网络唤醒）功能依赖 ethtool 命令；统信/麒麟精简版可能未预装，
# 离线环境无法 apt 安装 → 打包编译机同架构二进制，postinst 在系统缺失时
# 安装到 /usr/local/bin（ethtool 仅依赖 libc/libm 等系统必有库，可安全移植）
echo "  收集 ethtool（WOL 功能依赖）..."
EXTRA_BIN_DIR="$INSTALL_DIR/extra-bin"
mkdir -p "$EXTRA_BIN_DIR"
ETHTOOL_BIN=$(command -v ethtool 2>/dev/null)
if [ -n "$ETHTOOL_BIN" ]; then
    cp -n "$ETHTOOL_BIN" "$EXTRA_BIN_DIR/" 2>/dev/null && \
        echo "  ✓ ethtool 已收集: $ETHTOOL_BIN"
    # 校验其依赖是否全是系统必有库（libc/libm 家族）——若有其它依赖则一并收进 lib/
    for deplib in $(ldd "$ETHTOOL_BIN" 2>/dev/null | sed -nE 's/.*=> ([^ ]+) \(0x[0-9a-f]+\)/\1/p'); do
        depname=$(basename "$deplib")
        if ! is_system_lib "$depname" && [ ! -e "$LIB_DIR/$depname" ]; then
            if ! lib_compat_ok "$deplib"; then
                echo "$depname" >> "$GLIBC_SKIP_LIST_FILE" 2>/dev/null
                continue
            fi
            cp -n "$deplib" "$LIB_DIR/" 2>/dev/null && \
                echo "  ✓ ethtool 依赖库 $depname 已并入 lib/"
        fi
    done
else
    # 修复（B4）：编译机未装 ethtool 时不再只警告 ——
    # 回退 apt-get download 当前架构的 ethtool deb 并解出二进制，
    # 保证 WOL 功能在无 ethtool 的目标机上离线可用
    ETHTOOL_DEB=$(cd "$EXTRA_BIN_DIR" && timeout 15 apt-get download ethtool 2>/dev/null | tail -1 | xargs -r basename 2>/dev/null)
    ETHTOOL_DEB_FILE=$(ls "$EXTRA_BIN_DIR"/ethtool_*.deb 2>/dev/null | head -1)
    if [ -n "$ETHTOOL_DEB_FILE" ]; then
        dpkg-deb -x "$ETHTOOL_DEB_FILE" "$EXTRA_BIN_DIR/ethtool_extract" 2>/dev/null
        ETHTOOL_EXTRACTED=$(find "$EXTRA_BIN_DIR/ethtool_extract" -name ethtool -type f 2>/dev/null | head -1)
        if [ -n "$ETHTOOL_EXTRACTED" ]; then
            cp "$ETHTOOL_EXTRACTED" "$EXTRA_BIN_DIR/ethtool" && chmod 755 "$EXTRA_BIN_DIR/ethtool"
            echo "  ✓ ethtool 已通过 apt 下载并解包: $(basename "$ETHTOOL_DEB_FILE")"
            # 连同其非系统依赖一起收进 lib/
            for deplib in $(ldd "$EXTRA_BIN_DIR/ethtool" 2>/dev/null | sed -nE 's/.*=> ([^ ]+) \(0x[0-9a-f]+\)/\1/p'); do
                depname=$(basename "$deplib")
                if ! is_system_lib "$depname" && [ ! -e "$LIB_DIR/$depname" ]; then
                    if ! lib_compat_ok "$deplib"; then
                        echo "$depname" >> "$GLIBC_SKIP_LIST_FILE" 2>/dev/null
                        continue
                    fi
                    cp -n "$deplib" "$LIB_DIR/" 2>/dev/null && \
                        echo "  ✓ ethtool 依赖库 $depname 已并入 lib/"
                fi
            done
        fi
        rm -rf "$EXTRA_BIN_DIR/ethtool_extract" "$ETHTOOL_DEB_FILE" 2>/dev/null
    fi
    if [ ! -f "$EXTRA_BIN_DIR/ethtool" ]; then
        echo "  ⚠ 编译机未安装 ethtool 且 apt 下载失败，WOL 功能在无 ethtool 的目标机上将不可用"
        echo "    建议: sudo apt-get install ethtool 后重新打包"
    fi
fi

# ---- 工具函数：glmark2 源码编译（第 4 级回退） ----
# 适用场景：统信UOS/麒麟官方源无 glmark2，offline_debs/ 也未放置，
# 编译机本地也未装时，从 GitHub 拉 glmark2 2021.12 源码本地编译。
#
# 源码获取多镜像链（按顺序尝试，任一成功即停止）：
#   1) $PROJECT_ROOT/offline_src/glmark2*.tar.gz（离线兜底，人工放置）
#   2) ghfast.top 镜像（国内通常可达）
#   3) ghproxy.net 镜像
#   4) gh-proxy.com 镜像
#   5) codeload.github.com 直连（境外/开发机一般可通）
#
# 关键技术点：
#   - glmark2 2021.12 用 meson 构建（meson ≥ 0.49，ninja）。
#     meson_options.txt 中 flavors 是数组选项（drm-gl/drm-glesv2/wayland-gl/
#     wayland-glesv2/x11-gl/x11-glesv2），不是老版本的 native-gl-backend 等。
#   - 必须用旧式 meson 位置参数 `meson build_dir -Dflavors=...`（兼容 0.49+；
#     `meson setup` 子命令是 0.60+ 引入的，新旧两套都能跑）。
#   - 必须 --prefix=/usr（不是 meson 默认的 /usr/local），
#     否则 DESTDIR 产物布局是 usr/local/bin/... 而 postinst 找 /usr/share/glmark2。
#   - GCC 12+ 编译 src/libmatrix/program.h 必报 `std::exchange` 缺 <utility> 头
#     错误（program.h 第 44 行），必须自动 patch（sed 在 15 行前插入 #include <utility>）。
#   - flavors 探测：用 pkg-config --exists 检 glesv2/gl/x11 决定可编译的 flavor 子集，
#     缺哪个 flavor 就自动去掉（不会编译出用不了的二进制）。
#   - 编译产物路径必须对齐 apt 解包布局：$EXTRA_BIN_DIR/glmark2/usr/{bin,share/glmark2,share/man}，
#     因此 DESTDIR 装完后把 $STAGE/usr 整个 mv 到 $EXTRA_BIN_DIR/glmark2/usr。
#   - 【2026-11-11 统信1070 现场修复】旧版"meson 缺失→waf 回退"路径已删除：
#     glmark2 2021.12 是 meson 项目，源码包内残留的 waf 脚本不认识 meson 的
#     flavor 参数（--with-x11-gl），configure 必报 no such option；旧版又用
#     `| tail` 吞掉退出码，一路误判成功直到"未生成可执行文件"才失败。
#     现在：① 只走 meson；② 子命令重定向到日志后判 $?，不再丢退出码。
#   - 【2026-11-11 二次修复】构建工具只读探测：打包脚本绝不自动 apt/sudo/pip 安装。
#     上一版在此调用 apt-get install，统信 UOS 现场触发 SIGTTIN → apt-get 进入
#     T(stopped)，timeout 的 SIGTERM 对 stopped 进程无效，整个打包永久卡死。
#     工具缺失时立即返回 1（glmark2 属可选增强项，打包继续，不中断）。
# 失败原因明确输出（meson 缺哪个 .pc / 哪个 .h），方便用户装对应 -dev 包。
build_glmark2_from_source() {
    local SRC_DIR="$EXTRA_BIN_DIR/.glmark_src_$$"
    local STAGE="$EXTRA_BIN_DIR/.glmark_stage_$$"
    local TARBALL="$SRC_DIR/glmark2.tar.gz"
    local SRC_URL_BASE="https://github.com/glmark2/glmark2/archive/refs/tags/2021.12"
    local MIRRORS=(
        "https://ghfast.top/${SRC_URL_BASE}.tar.gz"
        "https://ghproxy.net/${SRC_URL_BASE}.tar.gz"
        "https://gh-proxy.com/${SRC_URL_BASE}.tar.gz"
        "${SRC_URL_BASE}.tar.gz"
    )
    mkdir -p "$SRC_DIR" "$STAGE"

    # ---- 0. 构建工具探测（只读；绝不改系统 / 绝不提权 / 绝不联网）----
    # [2026-11-11 二次修复] 上一版在此处尝试 apt-get install / sudo -n / pip install，
    # 统信 UOS 现场实测会卡死整个打包：apt-get 在打包脚本里试图读终端触发
    # SIGTTIN → 进程进入 T(stopped)，而 timeout 的 SIGTERM 对 stopped 进程无效，
    # 脚本永久挂起（现场 ps aux 显示 timeout/sudo/apt-get 三个进程全为 T）。
    # 打包脚本不应改变编译机系统状态。现在只探测，缺失立即明确失败并给指引。
    export PATH="$HOME/.local/bin:$PATH"
    local _missing=""
    command -v meson >/dev/null 2>&1 || _missing="${_missing} meson"
    command -v ninja >/dev/null 2>&1 || _missing="${_missing} ninja"
    if [ -n "$_missing" ]; then
        echo "  ✗ 源码编译工具缺失:${_missing}（打包脚本不会自动安装，以免改动系统或卡住）"
        echo "    统信/麒麟离线环境请勿走源码编译：把离线资产解到"
        echo "      $PROJECT_ROOT/offline_bin/glmark2/ 后重打即可（无需编译/联网/root）。"
        echo "    确需源码编译时，请先在终端手动装好工具与图形开发包："
        echo "      sudo apt install meson ninja-build libx11-dev libgl-dev libgles2-dev libegl-dev libjpeg-dev libpng-dev"
        rm -rf "$SRC_DIR" "$STAGE" 2>/dev/null
        return 1
    fi
    echo "  ✓ 构建工具就绪: meson=$(command -v meson) ninja=$(command -v ninja)"
    # [2026-11-04] 不再开头就删除已有 extra-bin/glmark2:
    # 改为暂存到 glmark2.new,全部成功后才原子替换,编译失败不丢已有产物
    local OUT_DIR="$EXTRA_BIN_DIR/glmark2.new"
    rm -rf "$OUT_DIR" 2>/dev/null

    # ---- 1. 获取源码：本地优先 → 多镜像回退 ----
    local got_src=0
    if ls "$PROJECT_ROOT"/offline_src/glmark2*.tar.gz >/dev/null 2>&1; then
        local local_tar
        local_tar=$(ls "$PROJECT_ROOT"/offline_src/glmark2*.tar.gz | head -1)
        cp "$local_tar" "$TARBALL" && got_src=1 && \
            echo "  ℹ 使用本地离线源码: $(basename "$local_tar")"
    fi
    if [ "$got_src" -eq 0 ]; then
        for url in "${MIRRORS[@]}"; do
            echo "  ⏳ 尝试下载: $url"
            # --max-time 180（3 分钟），9 MB 大文件国内镜像通常 < 30s
            if curl -sSL --max-time 180 -o "$TARBALL" "$url" 2>/dev/null \
               && [ -s "$TARBALL" ] \
               && tar tzf "$TARBALL" >/dev/null 2>&1; then
                got_src=1
                echo "  ✓ 源码下载成功: $url"
                break
            else
                echo "  ⚠ 镜像不可达或文件损坏: $url"
                rm -f "$TARBALL" 2>/dev/null
            fi
        done
    fi
    if [ "$got_src" -eq 0 ]; then
        echo "  ✗ glmark2 源码下载失败（所有镜像均不可达，离线 src/ 也无包）"
        rm -rf "$SRC_DIR" "$STAGE" 2>/dev/null
        return 1
    fi

    # ---- 2. 解压：GitHub tarball 顶层目录 glmark2-2021.12/ ----
    local BUILD_BASE="$SRC_DIR/glmark2-2021.12"
    rm -rf "$BUILD_BASE" 2>/dev/null
    tar xzf "$TARBALL" -C "$SRC_DIR" 2>/dev/null
    if [ ! -d "$BUILD_BASE" ]; then
        echo "  ✗ 解压后未找到源码目录: $BUILD_BASE"
        rm -rf "$SRC_DIR" "$STAGE" 2>/dev/null
        return 1
    fi

    # ---- 3. 关键 patch：GCC 12+ 缺 <utility> 头（program.h:44 std::exchange） ----
    if [ -f "$BUILD_BASE/src/libmatrix/program.h" ] \
       && ! grep -q '#include <utility>' "$BUILD_BASE/src/libmatrix/program.h"; then
        # 在第 15 行前插入 #include <utility>（line 15i 在 line 15 之前）
        sed -i '15i #include <utility>' "$BUILD_BASE/src/libmatrix/program.h" \
            && echo "  ✓ 已 patch src/libmatrix/program.h（GCC 12+ 缺 <utility> 头修复）"
    fi

    # ---- 4. 探测 flavors 数组（pkg-config 决定可编译子集） ----
    local FLAVORS=()
    # x11 后端（绝大多数桌面环境）
    if pkg-config --exists x11 2>/dev/null && pkg-config --exists gl 2>/dev/null; then
        FLAVORS+=("x11-gl")
    fi
    if pkg-config --exists x11 2>/dev/null && pkg-config --exists glesv2 2>/dev/null; then
        FLAVORS+=("x11-glesv2")
    fi
    # drm 后端（无 X 的纯帧缓冲场景；通常 desktop 不用，但加上无副作用）
    if pkg-config --exists glesv2 2>/dev/null && pkg-config --exists egl 2>/dev/null \
       && pkg-config --exists libdrm 2>/dev/null; then
        FLAVORS+=("drm-glesv2")
    fi
    if [ "${#FLAVORS[@]}" -eq 0 ]; then
        echo "  ✗ 未检测到任何可用的图形后端（缺 x11/gl/glesv2/egl/libdrm 的 -dev 包）"
        echo "    尝试安装: sudo apt-get install libx11-dev libgl-dev libgles2-dev libegl-dev libdrm-dev"
        rm -rf "$SRC_DIR" "$STAGE" 2>/dev/null
        return 1
    fi
    local FLAVORS_CSV
    FLAVORS_CSV=$(IFS=,; echo "${FLAVORS[*]}")
    echo "  ℹ 将编译 flavors: $FLAVORS_CSV"

    # ---- 5. 构建：meson + ninja（唯一路径，2026-11-11 重写）----
    # 所有子命令改为「输出重定向到日志 + 判 $?」：旧版 `2>&1 | tail -N`
    # 的退出码来自 tail 而非 meson/ninja，configure 失败会被误判为成功。
    local BUILD_DIR="$BUILD_BASE/build"
    local BUILD_LOG="$SRC_DIR/build.log"
    rm -rf "$BUILD_DIR" 2>/dev/null

    echo "  ⏳ meson 配置 (--prefix=/usr -Dflavors=$FLAVORS_CSV)..."
    if ! (cd "$BUILD_BASE" && meson "$BUILD_DIR" --prefix=/usr \
          -Dflavors="$FLAVORS_CSV" --buildtype=release \
          --auto-features=disabled > "$BUILD_LOG" 2>&1); then
        echo "  ✗ meson 配置失败（通常为缺 -dev 包）："
        tail -40 "$BUILD_LOG" 2>/dev/null | sed 's/^/      /'
        echo "    常见解决: sudo apt-get install libgl-dev libgles2-dev libegl-dev libx11-dev libdrm-dev libjpeg-dev libpng-dev"
        rm -rf "$SRC_DIR" "$STAGE" 2>/dev/null
        return 1
    fi
    echo "  ⏳ ninja 编译（首次编译通常 30-120 秒）..."
    if ! (cd "$BUILD_BASE" && ninja -C "$BUILD_DIR" > "$BUILD_LOG" 2>&1); then
        echo "  ✗ 编译失败："
        tail -30 "$BUILD_LOG" 2>/dev/null | sed 's/^/      /'
        rm -rf "$SRC_DIR" "$STAGE" 2>/dev/null
        return 1
    fi
    echo "  ⏳ DESTDIR 安装到临时 stage 目录..."
    # DESTDIR 产出 $STAGE/usr/bin/glmark2* + $STAGE/usr/share/glmark2/...
    if ! (cd "$BUILD_BASE" && DESTDIR="$STAGE" ninja -C "$BUILD_DIR" install > "$BUILD_LOG" 2>&1); then
        echo "  ✗ DESTDIR 安装失败："
        tail -20 "$BUILD_LOG" 2>/dev/null | sed 's/^/      /'
        rm -rf "$SRC_DIR" "$STAGE" 2>/dev/null
        return 1
    fi

    # ---- 6. 布局对齐：DESTDIR 产物对齐到 extra-bin/glmark2/usr/ ----
    # meson --prefix=/usr 装到 DESTDIR 之后是 $STAGE/usr/{bin,share}/...，
    # 与 apt 解包的 $EXTRA_BIN_DIR/glmark2/usr/{bin,share}/... 完全一致。
    if [ ! -d "$STAGE/usr" ]; then
        echo "  ✗ 编译产物布局异常：未找到 $STAGE/usr"
        rm -rf "$SRC_DIR" "$STAGE" "$OUT_DIR" 2>/dev/null
        return 1
    fi
    mkdir -p "$OUT_DIR"
    # 用 cp -r + rm 防止 mv 跨设备失败（STAGE 与 EXTRA_BIN_DIR 不同 fs 时）
    cp -r "$STAGE/usr" "$OUT_DIR/" && rm -rf "$STAGE/usr" 2>/dev/null

    # ---- 7. 验证：找二进制 + 场景数据 ----
    local glmark_bin
    glmark_bin=$(find "$OUT_DIR/usr/bin" -maxdepth 1 -name 'glmark2*' -perm -u+x 2>/dev/null | head -3)
    if [ -z "$glmark_bin" ]; then
        echo "  ✗ 编译完成但未生成可执行文件"
        rm -rf "$SRC_DIR" "$STAGE" "$OUT_DIR" 2>/dev/null
        return 1
    fi
    # [2026-11-04] 场景数据完整性检查 + 编译机兜底:
    # 二进制与数据必须成套进包,否则目标机上"窗口开但全黑、FPS 照常输出"
    # (飞腾 D2000 现场实测教训)。产物缺数据时从编译机 /usr/share/glmark2 补。
    local _need_data_sub=""
    local _gs
    for _gs in shaders models textures; do
        if [ ! -d "$OUT_DIR/usr/share/glmark2/$_gs" ] \
           || [ -z "$(ls -A "$OUT_DIR/usr/share/glmark2/$_gs" 2>/dev/null)" ]; then
            _need_data_sub="$_need_data_sub $_gs"
        fi
    done
    if [ -n "$_need_data_sub" ]; then
        echo "  ⚠ 编译产物缺场景数据($_need_data_sub )，尝试从编译机 /usr/share/glmark2 补齐..."
        if [ -d /usr/share/glmark2 ]; then
            mkdir -p "$OUT_DIR/usr/share/glmark2" 2>/dev/null
            for _gs in shaders models textures; do
                if [ -d "/usr/share/glmark2/$_gs" ] \
                   && { [ ! -d "$OUT_DIR/usr/share/glmark2/$_gs" ] \
                        || [ -z "$(ls -A "$OUT_DIR/usr/share/glmark2/$_gs" 2>/dev/null)" ]; }; then
                    rm -rf "$OUT_DIR/usr/share/glmark2/$_gs" 2>/dev/null
                    cp -r "/usr/share/glmark2/$_gs" \
                        "$OUT_DIR/usr/share/glmark2/" 2>/dev/null
                fi
            done
        fi
        # 补齐后复查(仍缺则失败退出——目标机 glmark2 必黑屏;原有产物不动)
        local _still_miss=""
        for _gs in shaders models textures; do
            [ -d "$OUT_DIR/usr/share/glmark2/$_gs" ] \
                || _still_miss="$_still_miss $_gs"
        done
        if [ -n "$_still_miss" ]; then
            echo "  ✗✗ glmark2 场景数据仍缺($_still_miss )：包内 glmark2 在目标机必然黑屏！"
            echo "     处理: 联网机下载 https://github.com/glmark2/glmark2/archive/refs/tags/2021.12.tar.gz"
            echo "           放入 $PROJECT_ROOT/offline_src/ 后重新打包；或编译机 apt 安装 glmark2 提供数据"
            rm -rf "$SRC_DIR" "$STAGE" "$OUT_DIR" 2>/dev/null
            return 1
        fi
        echo "  ✓ 场景数据已从编译机补齐"
    fi
    echo "  ✓ glmark2 源码编译成功（flavors=$FLAVORS_CSV）"
    echo "$glmark_bin" | sed 's/^/      /'

    # ---- 8. 收集非系统依赖到 lib/ ----
    local gbin
    for gbin in $glmark_bin; do
        collect_deps_of "$gbin"
    done

    # ---- 9. 原子替换:全部成功后才覆盖已有 extra-bin/glmark2 ----
    rm -rf "$EXTRA_BIN_DIR/glmark2" 2>/dev/null
    mv "$OUT_DIR" "$EXTRA_BIN_DIR/glmark2" 2>/dev/null \
        || { cp -r "$OUT_DIR/." "$EXTRA_BIN_DIR/glmark2/" 2>/dev/null; rm -rf "$OUT_DIR"; }
    # ---- 10. 清理源码/构建目录（只留产物） ----
    rm -rf "$SRC_DIR" "$STAGE" 2>/dev/null
    return 0
}

# ---- 层3d：收集 glmark2 显卡压测（老化测试 GPU 压测项依赖，2026-09-15 新增；源码编译回退 2026-10-08 增） ----
# 老化测试 GUI 的"glmark2显卡压测"勾选项依赖 glmark2 可执行文件；
# 目标机（麒麟/统信）多数未预装，离线环境无法 apt 安装 →
# 打包时按以下【多级回退链】尝试获取 glmark2：
#   第 1 级：apt-get download glmark2 / glmark2-es2
#   第 2 级：$PROJECT_ROOT/offline_debs/ 离线 deb（人工放置）
#   第 2.4 级：$PROJECT_ROOT/glmark2_offline_<架构>.tar.gz 资产包自动解包（2026-11-11
#              二次修复新增，推荐通道：现场只需把网盘资产包放到项目根目录）
#   第 2.5 级：$PROJECT_ROOT/offline_bin/glmark2/ 预置二进制+数据（成套校验后收进包）
#   第 3 级：编译机本地已装 glmark2（直接复制二进制+数据）
#   第 4 级：下载 glmark2 2021.12 源码并本地编译（最后兜底，函数见上）
# 运行时 aging_test_gui.py 的 _find_glmark_binary() 优先找包内路径。
# 注意：glmark2 依赖 libGL/libpng/libjpeg/libX11 等桌面常见库，
#      老化场景必然有桌面环境（DISPLAY），这些库在目标机均存在；
#      额外把非系统依赖收进 lib/ 作兜底（与 ethtool 同策略）。
#
# 调试开关：GLMARK_FORCE_SOURCE_BUILD=1 时跳过前置级别直接走源码编译（回归测试/特定版本场景用）
echo "  收集 glmark2（老化测试显卡压测项依赖，可选增强项）..."
GLMARK_TMP="$EXTRA_BIN_DIR/.glmark_dl_$$"
mkdir -p "$GLMARK_TMP"
# 说明：glmark2 为可选增强项。统信UOS/麒麟官方源通常不含 glmark2（"无法定位软件包"属正常现象），
# 缺失时老化测试的 glmark2 压测项在目标机自动降级跳过，其余老化项不受任何影响。
GLMARK_OK=""

# ===== 第 1 级：apt 下载（glmark2 → glmark2-es2） =====
# 注意:apt 主包不含场景数据(数据在独立的 glmark2-data 包),数据缺失由
# 链末的 glmark2_ensure_data() 统一校验补齐(2026-11-04)
if [ -z "${GLMARK_FORCE_SOURCE_BUILD:-}" ] || [ "$GLMARK_FORCE_SOURCE_BUILD" != "1" ]; then
    GLMARK_DEB_FILE=$(cd "$GLMARK_TMP" && timeout 30 apt-get download glmark2 2>/dev/null >/dev/null; ls "$GLMARK_TMP"/glmark2_*.deb 2>/dev/null | head -1)
    if [ -z "$GLMARK_DEB_FILE" ]; then
        # 部分发行版包名为 glmark2-es2（Debian 12+/Ubuntu 22.04+）
        GLMARK_DEB_FILE=$(cd "$GLMARK_TMP" && timeout 30 apt-get download glmark2-es2 2>/dev/null >/dev/null; ls "$GLMARK_TMP"/glmark2-es2_*.deb 2>/dev/null | head -1)
    fi
    if [ -n "$GLMARK_DEB_FILE" ]; then
        dpkg-deb -x "$GLMARK_DEB_FILE" "$EXTRA_BIN_DIR/glmark2" 2>/dev/null
        GLMARK_BIN_FOUND=$(find "$EXTRA_BIN_DIR/glmark2" -type f \( -name 'glmark2*' \) -perm -u+x 2>/dev/null | head -3)
        if [ -n "$GLMARK_BIN_FOUND" ]; then
            echo "  ✓ glmark2 已通过 apt 下载并解包: $(basename "$GLMARK_DEB_FILE")"
            # [2026-10-19] 记录是否含 GLX 版:仅 es2(EGL) 时,X11 下会出现
            # "渲染正常但不上屏"的黑屏问题(GLX 版无此问题),后续补编 GLX 版
            if ! find "$EXTRA_BIN_DIR/glmark2" -type f -name 'glmark2' -perm -u+x 2>/dev/null | grep -q .; then
                GLMARK_NEED_GLX=1
                echo "  ℹ 包内仅 glmark2-es2(EGL 版),将在后续级别补编 GLX 版 glmark2"
            fi
            echo "$GLMARK_BIN_FOUND" | sed 's/^/      /'
            # 把非系统依赖收进 lib/ 兜底（libGL/libpng/libjpeg 等桌面常见库一般已存在，
            # 此处只收"编译机有而目标机可能没有"的库，is_system_lib 过滤系统必备库）
            for gbin in $GLMARK_BIN_FOUND; do
                collect_deps_of "$gbin"
            done
            GLMARK_OK="apt"
        else
            echo "  ⚠ glmark2 deb 下载成功但解包未找到可执行文件，回退到下一级"
            rm -rf "$EXTRA_BIN_DIR/glmark2" 2>/dev/null
        fi
        rm -rf "$GLMARK_TMP" 2>/dev/null
    fi
fi

# ===== 第 2 级：offline_debs/ 人工放置 =====
# 离线兜底：编译机源里没有 glmark2 时，支持人工放置离线 deb
# （在任意联网 Debian/Ubuntu 上: apt-get download glmark2 或 glmark2-es2，
#  把 .deb 放入项目 offline_debs/ 目录后重新打包即可携带）
if [ -z "$GLMARK_OK" ] && { [ -z "${GLMARK_FORCE_SOURCE_BUILD:-}" ] || [ "$GLMARK_FORCE_SOURCE_BUILD" != "1" ]; }; then
    GLMARK_DEB_FILE=$(ls "$PROJECT_ROOT"/offline_debs/glmark2*.deb 2>/dev/null | head -1)
    if [ -n "$GLMARK_DEB_FILE" ]; then
        rm -rf "$GLMARK_TMP" && mkdir -p "$GLMARK_TMP"
        dpkg-deb -x "$GLMARK_DEB_FILE" "$EXTRA_BIN_DIR/glmark2" 2>/dev/null
        GLMARK_BIN_FOUND=$(find "$EXTRA_BIN_DIR/glmark2" -type f \( -name 'glmark2*' \) -perm -u+x 2>/dev/null | head -3)
        if [ -n "$GLMARK_BIN_FOUND" ]; then
            echo "  ℹ 使用 offline_debs/ 中的离线包: $(basename "$GLMARK_DEB_FILE")"
            echo "  ✓ glmark2 已通过 offline_debs 解包"
            echo "$GLMARK_BIN_FOUND" | sed 's/^/      /'
            for gbin in $GLMARK_BIN_FOUND; do
                collect_deps_of "$gbin"
            done
            GLMARK_OK="offline_debs"
        else
            echo "  ⚠ offline_debs/ 中 glmark2 deb 解包未找到可执行文件，回退到下一级"
            rm -rf "$EXTRA_BIN_DIR/glmark2" 2>/dev/null
        fi
        rm -rf "$GLMARK_TMP" 2>/dev/null
    fi
fi

# ===== 第 2.4 级：离线资产包自动装载（2026-11-11 二次修复，推荐通道）=====
# 现场把网盘提供的 glmark2_offline_<架构>.tar.gz 放在项目根目录即可；
# 放在桌面/下载目录也能自动找到（见下方 _OA_SEARCH）。脚本自动解出
# offline_bin/glmark2/，无需手工解压、无需编译、无需联网/root。
# 资产包内路径固定: offline_bin/glmark2/{usr/bin,usr/share/glmark2,MANIFEST.txt}
# 架构按 DEB_ARCH 精确匹配，避免把 amd64 二进制误收进 arm64 包。
if [ -z "$GLMARK_OK" ] && { [ -z "${GLMARK_FORCE_SOURCE_BUILD:-}" ] || [ "$GLMARK_FORCE_SOURCE_BUILD" != "1" ]; }; then
    _OA_ROOT="$PROJECT_ROOT/offline_bin/glmark2"
    _OA_READY=0
    for _cand in "$_OA_ROOT/usr/bin" "$_OA_ROOT/bin" "$_OA_ROOT"; do
        if find "$_cand" -maxdepth 1 -type f -name 'glmark2*' -perm -u+x 2>/dev/null | grep -q . \
           && [ -d "$_OA_ROOT/usr/share/glmark2/shaders" ] \
           && [ -d "$_OA_ROOT/usr/share/glmark2/models" ] \
           && [ -d "$_OA_ROOT/usr/share/glmark2/textures" ]; then
            _OA_READY=1; break
        fi
    done
    if [ "$_OA_READY" -eq 1 ]; then
        echo "  ℹ 已存在成套 offline_bin/glmark2/，直接使用"
    else
        _OA_TAR=""
        _OA_SEARCH=(
            "$PROJECT_ROOT/glmark2_offline_${DEB_ARCH}.tar.gz"
            "$PROJECT_ROOT/offline_assets/glmark2_offline_${DEB_ARCH}.tar.gz"
            "$PROJECT_ROOT/offline_bin/glmark2_offline_${DEB_ARCH}.tar.gz"
        )
        # 现场从网盘下载的文件常直接留在桌面/下载目录；一并纳入搜索，
        # 避免操作员漏做"复制到项目根目录"这一步导致 glmark2 再次缺席。
        _oa_user="${SUDO_USER:-${USER:-}}"
        for _d in "$HOME/Desktop" "$HOME/桌面" "$HOME/Downloads" "$HOME/下载" \
                 "/home/$_oa_user/Desktop" "/home/$_oa_user/桌面" \
                 "/home/$_oa_user/Downloads" "/home/$_oa_user/下载"; do
            [ -n "$_d" ] && [ -d "$_d" ] \
                && _OA_SEARCH+=("$_d/glmark2_offline_${DEB_ARCH}.tar.gz")
        done
        for _cand in "${_OA_SEARCH[@]}"; do
            if [ -f "$_cand" ]; then _OA_TAR="$_cand"; break; fi
        done
        if [ -n "$_OA_TAR" ]; then
            # 路径安全校验：包内不得有绝对路径或 .. 逃逸，防止解包写穿项目目录
            if tar -tzf "$_OA_TAR" 2>/dev/null | grep -Eq '(^/|(^|/)\.\.(/|$))'; then
                echo "  ⚠ 离线资产包内含非法路径，已忽略: $_OA_TAR"
            else
                echo "  ℹ 发现离线资产包 $(basename "$_OA_TAR")，自动解包到项目根目录..."
                if (cd "$PROJECT_ROOT" && tar -xzf "$_OA_TAR") 2>/dev/null; then
                    echo "  ✓ 离线资产包已解包（offline_bin/glmark2/）"
                else
                    echo "  ⚠ 离线资产包解包失败，请校验文件完整性: $_OA_TAR"
                fi
            fi
        fi
    fi
fi

# ===== 第 2.5 级：offline_bin/ 预置二进制 + 场景数据 =====
# 目录来源二选一：
#   a) 【推荐】第 2.4 级自动解包网盘提供的 glmark2_offline_<架构>.tar.gz；
#   b) 手工从一台"已经能跑 glmark2"的同架构 Linux 复制（麒麟2503 自带老版）：
#        mkdir -p offline_bin/glmark2/usr/bin offline_bin/glmark2/usr/share
#        cp -a "$(command -v glmark2)" offline_bin/glmark2/usr/bin/
#        cp -a /usr/share/glmark2 offline_bin/glmark2/usr/share/
# 脚本成套校验（可执行文件 + shaders/models/textures）后直接收进包，
# 无需编译、无需联网、无需 root，二进制与场景数据同机同 ABI。
if [ -z "$GLMARK_OK" ] && { [ -z "${GLMARK_FORCE_SOURCE_BUILD:-}" ] || [ "$GLMARK_FORCE_SOURCE_BUILD" != "1" ]; }; then
    _OB_ROOT="$PROJECT_ROOT/offline_bin/glmark2"
    _OB_BIN=""
    for _cand in "$_OB_ROOT/usr/bin" "$_OB_ROOT/bin" "$_OB_ROOT"; do
        if find "$_cand" -maxdepth 1 -type f -name 'glmark2*' -perm -u+x 2>/dev/null | grep -q .; then
            _OB_BIN="$_cand"; break
        fi
    done
    _OB_SHARE=""
    for _cand in "$_OB_ROOT/usr/share/glmark2" "$_OB_ROOT/share/glmark2" "$_OB_ROOT/glmark2"; do
        if [ -d "$_cand/shaders" ] && [ -d "$_cand/models" ] && [ -d "$_cand/textures" ]; then
            _OB_SHARE="$_cand"; break
        fi
    done
    if [ -n "$_OB_BIN" ] && [ -n "$_OB_SHARE" ]; then
        mkdir -p "$EXTRA_BIN_DIR/glmark2/usr/bin" "$EXTRA_BIN_DIR/glmark2/usr/share"
        cp -r "$_OB_BIN"/glmark2* "$EXTRA_BIN_DIR/glmark2/usr/bin/" 2>/dev/null
        cp -r "$_OB_SHARE" "$EXTRA_BIN_DIR/glmark2/usr/share/" 2>/dev/null
        GLMARK_BIN_FOUND=$(find "$EXTRA_BIN_DIR/glmark2/usr/bin" -maxdepth 1 \
            -type f -name 'glmark2*' -perm -u+x 2>/dev/null)
        if [ -n "$GLMARK_BIN_FOUND" ]; then
            chmod 755 $GLMARK_BIN_FOUND 2>/dev/null
            for gbin in $GLMARK_BIN_FOUND; do
                collect_deps_of "$gbin"
            done
            echo "  ✓ glmark2 已从 offline_bin/ 收进包（二进制 + 场景数据成套）"
            GLMARK_OK="offline_bin"
        fi
    elif [ -n "$_OB_BIN" ] || [ -n "$_OB_SHARE" ]; then
        echo "  ⚠ offline_bin/glmark2/ 不成套（需同时有可执行文件与 shaders/models/textures），跳过"
    fi
fi

# ===== 第 3 级：编译机本地已装（deb 解包优先，这里兜底二进制+数据） =====
if [ -z "$GLMARK_OK" ] && { [ -z "${GLMARK_FORCE_SOURCE_BUILD:-}" ] || [ "$GLMARK_FORCE_SOURCE_BUILD" != "1" ]; }; then
    # 优先 GLX 版 glmark2(与 3D 老化测试同走 GLX,最兼容);EGL 版(glmark2-es2)
    # 在 X11+EGL 不可用时只会开窗黑屏。与 _find_glmark_binary 的 X11 优先级一致
    # [2026-10-19] 收集编译机上已装的全部变体(GLX 优先装,es2 一并带上):
    # 老化测试按 PATH 查找候选,包里变体越全,离线机的回退层数越多
    GLMARK_LOCAL_ALL=""
    for _gn in glmark2 glmark2-es2 glmark2-wayland glmark2-es2-wayland; do
        _gp=$(command -v "$_gn" 2>/dev/null)
        [ -n "$_gp" ] && GLMARK_LOCAL_ALL="$GLMARK_LOCAL_ALL $_gp"
    done
    GLMARK_LOCAL_ALL=$(printf '%s\n' $GLMARK_LOCAL_ALL | awk 'NF && !seen[$0]++')
    GLMARK_LOCAL=$(echo $GLMARK_LOCAL_ALL | awk '{print $1}')
    if [ -n "$GLMARK_LOCAL" ]; then
        mkdir -p "$EXTRA_BIN_DIR/glmark2/bin" "$EXTRA_BIN_DIR/glmark2/share"
        for _gp in $GLMARK_LOCAL_ALL; do
            cp "$_gp" "$EXTRA_BIN_DIR/glmark2/bin/" 2>/dev/null && \
                chmod 755 "$EXTRA_BIN_DIR/glmark2/bin/$(basename "$_gp")" 2>/dev/null
            collect_deps_of "$_gp"
        done
        # glmark2 场景数据（shader/纹理）在 /usr/share/glmark2，必须带走
        if [ -d /usr/share/glmark2 ]; then
            cp -r /usr/share/glmark2 "$EXTRA_BIN_DIR/glmark2/share/" 2>/dev/null
        fi
        echo "  ✓ glmark2 已从编译机本地收集: $GLMARK_LOCAL_ALL"
        if ! find "$EXTRA_BIN_DIR/glmark2" -type f -name 'glmark2' -perm -u+x 2>/dev/null | grep -q .; then
            GLMARK_NEED_GLX=1
        fi
        GLMARK_OK="local"
    fi
fi

# ===== 第 4 级：源码编译（最后兜底，统信UOS/麒麟源无 glmark2 时启用） =====
# 当 apt/offline_debs/本地 三级都拿不到时，从 GitHub 拉取 glmark2 2021.12 源码编译。
# 也支持：把 glmark2*.tar.gz 放入 $PROJECT_ROOT/offline_src/ 跳过下载
if [ -z "$GLMARK_OK" ] || [ "${GLMARK_NEED_GLX:-}" = "1" ]; then
    if [ -n "$GLMARK_OK" ]; then
        echo "  ℹ 包内缺 GLX 版 glmark2(X11 下最稳)，尝试源码编译补齐（失败不影响已有 es2 版）..."
    else
        echo "  ℹ 前面各级均未获取到 glmark2，尝试最后一级：源码编译..."
    fi
    if build_glmark2_from_source; then
        GLMARK_OK="source"
        GLMARK_NEED_GLX=""
    elif [ -n "$GLMARK_OK" ]; then
        echo "  ⚠ GLX 版补编未成功，包内仍只有 EGL 版 glmark2-es2"
        echo "    (X11 下可能黑屏；可联网机器 apt-get download glmark2 放入 offline_debs/ 后重打)"
    fi
fi

# ===== 数据完整性统一兜底（2026-11-04 增） =====
# 关键事实:Debian/Ubuntu 把 glmark2 场景数据拆在独立的 glmark2-data 包里,
# `apt-get download glmark2` 主包只有二进制!第 1/2/3 级都可能收进
# "有二进制无数据"的产物 → 目标机窗口全黑但 FPS 照常输出(飞腾 D2000 现场实测)。
# 这里对已收集的 extra-bin/glmark2 统一校验,数据缺失时按序兜底:
#   1) apt-get download glmark2-data(联网 Debian/Ubuntu 编译机)
#   2) offline_debs/glmark2-data*.deb(人工放置)
#   3) offline_src/glmark2*.tar.gz 或镜像下载源码包 → 直接提取 data/ 目录
#      (纯数据拷贝,无需编译,离线友好;postinst 会把数据铺到 /usr/share/glmark2)
glmark_pkg_data_complete() {
    local d _s
    for d in "$EXTRA_BIN_DIR/glmark2/usr/share/glmark2" "$EXTRA_BIN_DIR/glmark2/share/glmark2"; do
        local ok=1
        for _s in shaders models textures; do
            if [ ! -d "$d/$_s" ] || [ -z "$(ls -A "$d/$_s" 2>/dev/null)" ]; then
                ok=0; break
            fi
        done
        # [2026-11-06] 与 postinst / 老化程序同一口径:shader 集合半残也视为
        # 不完整,必须重新补齐,否则会打出"有二进制无可用数据"的黑屏包。
        if [ "$ok" = "1" ]; then
            local _nv
            _nv=$(find "$d/shaders" -maxdepth 1 -type f -name '*.vert' 2>/dev/null | wc -l)
            [ "$_nv" -ge 5 ] || ok=0
        fi
        [ "$ok" = "1" ] && return 0
    done
    return 1
}

glmark2_ensure_data() {
    # 0) 无二进制 → 无需数据;已完整 → 直接返回
    [ -z "$GLMARK_OK" ] && return 0
    glmark_pkg_data_complete && return 0

    local _tmp="$EXTRA_BIN_DIR/.glmark_data_$$"
    local _dst="$EXTRA_BIN_DIR/glmark2/usr/share/glmark2"
    mkdir -p "$_dst" 2>/dev/null
    rm -rf "$_tmp" 2>/dev/null && mkdir -p "$_tmp"
    echo "  ⚠ 包内 glmark2 缺场景数据(apt 主包不含数据,数据在独立的 glmark2-data 包),自动补齐..."

    # 1) apt 下载 glmark2-data / 2) offline_debs 离线 data 包
    local _deb=""
    _deb=$(cd "$_tmp" && timeout 30 apt-get download glmark2-data 2>/dev/null >/dev/null; ls "$_tmp"/glmark2-data*.deb 2>/dev/null | head -1)
    if [ -z "$_deb" ]; then
        _deb=$(ls "$PROJECT_ROOT"/offline_debs/glmark2-data*.deb 2>/dev/null | head -1)
        [ -n "$_deb" ] && echo "  ℹ 使用 offline_debs/ 中的数据包: $(basename "$_deb")"
    fi
    if [ -n "$_deb" ]; then
        if dpkg-deb -x "$_deb" "$_tmp/x" 2>/dev/null; then
            local _src
            _src=$(find "$_tmp/x" -type d -name glmark2 -path '*/share/*' 2>/dev/null | head -1)
            if [ -n "$_src" ]; then
                local _s
                for _s in shaders models textures; do
                    rm -rf "$_dst/$_s" 2>/dev/null
                    cp -r "$_src/$_s" "$_dst/" 2>/dev/null
                done
            fi
        fi
    fi

    # 3) 源码 tarball 提取 data/(无需编译)
    if ! glmark_pkg_data_complete; then
        local _tb=""
        _tb=$(ls "$PROJECT_ROOT"/offline_src/glmark2*.tar.gz 2>/dev/null | head -1)
        [ -n "$_tb" ] && echo "  ℹ 从离线源码包提取数据: $(basename "$_tb")"
        if [ -z "$_tb" ]; then
            local _url _SRC_BASE="https://github.com/glmark2/glmark2/archive/refs/tags/2021.12"
            for _url in "https://ghfast.top/${_SRC_BASE}.tar.gz" \
                        "https://ghproxy.net/${_SRC_BASE}.tar.gz" \
                        "https://gh-proxy.com/${_SRC_BASE}.tar.gz" \
                        "${_SRC_BASE}.tar.gz"; do
                echo "  ⏳ 尝试下载数据源码包: $_url"
                if curl -sSL --max-time 180 -o "$_tmp/gm.tgz" "$_url" 2>/dev/null \
                   && [ -s "$_tmp/gm.tgz" ] && tar tzf "$_tmp/gm.tgz" >/dev/null 2>&1; then
                    _tb="$_tmp/gm.tgz"
                    break
                fi
                rm -f "$_tmp/gm.tgz" 2>/dev/null
            done
        fi
        if [ -n "$_tb" ]; then
            rm -rf "$_tmp/src" && mkdir -p "$_tmp/src"
            if tar xzf "$_tb" -C "$_tmp/src" 2>/dev/null; then
                local _dsrc
                _dsrc=$(find "$_tmp/src" -maxdepth 2 -type d -name data 2>/dev/null | head -1)
                if [ -n "$_dsrc" ] && [ -d "$_dsrc/shaders" ]; then
                    local _s
                    for _s in shaders models textures; do
                        rm -rf "$_dst/$_s" 2>/dev/null
                        cp -r "$_dsrc/$_s" "$_dst/" 2>/dev/null
                    done
                fi
            fi
        fi
    fi

    rm -rf "$_tmp" 2>/dev/null
    if glmark_pkg_data_complete; then
        echo "  ✓ glmark2 场景数据已补齐(包内二进制+数据成套,目标机装机后 postinst 自动铺到 /usr/share/glmark2)"
        return 0
    fi
    echo "  ✗✗ 包内 glmark2 场景数据无法补齐:目标机 glmark2 将黑屏(FPS 照常输出但画面全黑)!"
    echo "     处理(任选一后重新打包):"
    echo "       a) 联网 Debian/Ubuntu 机: apt-get download glmark2-data,把 .deb 放入 $PROJECT_ROOT/offline_debs/"
    echo "       b) 下载 https://github.com/glmark2/glmark2/archive/refs/tags/2021.12.tar.gz"
    echo "          放入 $PROJECT_ROOT/offline_src/ (脚本自动从 data/ 提取,无需编译)"
    return 1
}
glmark2_ensure_data

# ===== 失败提示 =====
if [ -z "$GLMARK_OK" ]; then
    echo "  ℹ glmark2 未随包（可选增强项，非打包失败）:"
    echo "    统信UOS/麒麟官方源不含 glmark2（\"无法定位软件包\"属正常现象），编译机本地也未安装。"
    echo "    影响: 仅老化测试的 glmark2 显卡压测项在目标机自动降级跳过，其余功能完全不受影响。"
    echo "    如需启用，按推荐顺序任选一后重新打包："
    echo "      a) 【推荐·完全离线可靠】把网盘资产包 glmark2_offline_<架构>.tar.gz"
    echo "         解压到项目根目录，得到 $PROJECT_ROOT/offline_bin/glmark2/"
    echo "         （内含 usr/bin/glmark2* 二进制 + usr/share/glmark2 场景数据）"
    echo "         放好后重打即可：无需编译、无需联网、无需 root、不会卡住。"
    echo "      b) 在编译机上: sudo apt-get install glmark2（需系统源收录此包）"
    echo "      c) 联网 Debian/Ubuntu: apt-get download glmark2 glmark2-es2 glmark2-data"
    echo "         把所有 .deb 放入 $PROJECT_ROOT/offline_debs/"
    echo "      d) 源码编译（不推荐）：需【手动】先装 meson/ninja 与图形 -dev 包，且需联网下源码；"
    echo "         打包脚本不会自动安装任何系统包。手动命令："
    echo "           sudo apt install meson ninja-build libgl-dev libgles2-dev libegl-dev libx11-dev libjpeg-dev libpng-dev"
    echo "      e) 离线源码: 下载 2021.12.tar.gz 放入 $PROJECT_ROOT/offline_src/ 后重打"
fi
if [ -n "$GLMARK_OK" ]; then
    # 数据状态终检(ensure_data 失败时明确告警,防止"有二进制无数据"的黑屏包流出)
    if ! glmark_pkg_data_complete; then
        echo "  ✗✗ 警告:包内 glmark2 二进制无完整场景数据 → 目标机必然黑屏!"
        echo "     修复见上方 glmark2_ensure_data 输出的处理指引(a/b 两案)"
    fi
fi
rm -rf "$GLMARK_TMP" 2>/dev/null

# ---- 收集 Qt 插件目录 ----
# Qt 插件（platforms/xcb、xcbglintegrations 等）由 QFactoryLoader 在运行时
# dlopen 加载，主程序 ldd 完全看不到，必须整个目录带走。
# qmake -query QT_INSTALL_PLUGINS 是编译用 Qt 的真实插件目录（最可靠），
# 官方 Qt 装在 /opt 或麒麟/统信自定义路径时，仅靠标准路径会漏收 → 目标机起不来。
# 追加龙芯 multiarch 的 Qt 插件目录；$QT_PLUGINS_FROM_QMAKE 仍为首选。
QT_PLUGIN_CANDIDATES="$QT_PLUGINS_FROM_QMAKE /usr/lib/aarch64-linux-gnu/qt5/plugins /usr/lib/x86_64-linux-gnu/qt5/plugins /usr/lib/loongarch64-linux-gnu/qt5/plugins /usr/lib/loong64-linux-gnu/qt5/plugins /usr/lib/mips64el-linux-gnuabi64/qt5/plugins /usr/lib/qt5/plugins /usr/lib/qt/plugins"
QT_PLUGINS_SRC=""
for p in $QT_PLUGIN_CANDIDATES; do
    if [ -d "$p/platforms" ]; then
        QT_PLUGINS_SRC="$p"
        break
    fi
done
if [ -n "$QT_PLUGINS_SRC" ]; then
    # platforms（xcb/eglfs/linuxfb 等）+ 全部常用插件目录 + xcbglintegrations（GLX/EGL 3D 集成）
    for plugin_dir in platforms imageformats iconengines styles platformthemes sqldrivers \
                      xcbglintegrations bearer virtualkeyboard; do
        if [ -d "$QT_PLUGINS_SRC/$plugin_dir" ]; then
            mkdir -p "$LIB_DIR/$plugin_dir"
            cp -rn "$QT_PLUGINS_SRC/$plugin_dir/"* "$LIB_DIR/$plugin_dir/" 2>/dev/null
            # GLIBC 超标插件过滤（2026-10-09）：编译机系统插件要求 GLIBC 高于目标基线时，
            # 带进包目标机必报 GLIBC not found（插件由 QFactoryLoader dlopen，一个超标
            # 插件只拖垮对应功能模块，剔除后 Qt 回退探测其它插件目录/系统插件）。
            find "$LIB_DIR/$plugin_dir" -maxdepth 2 -type f \( -name '*.so' -o -name '*.so.*' \) -print0 2>/dev/null \
            | while IFS= read -r -d '' pf; do
                if ! lib_compat_ok "$pf"; then
                    echo "$(basename "$pf")" >> "$GLIBC_SKIP_LIST_FILE" 2>/dev/null
                    rm -f "$pf" 2>/dev/null
                fi
            done
        fi
    done
    echo "  ✓ 复制 Qt 插件目录（platforms/imageformats/xcbglintegrations 等）"
else
    echo "  ⚠ 未找到系统 Qt 插件目录（Qt GUI 可能无法启动）"
fi

# ---- 层2：递归收集到不动点 ----
# 对 lib/ 下所有 .so（含插件目录里的）反复跑 ldd 收集其依赖，直到一轮下来无新增文件
echo "  递归收集传递依赖（Qt 插件/3D 栈的二级依赖）..."
for round in 1 2 3 4 5; do
    BEFORE=$(find "$LIB_DIR" -name '*.so*' -type f 2>/dev/null | wc -l)
    find "$LIB_DIR" -name '*.so*' -type f 2>/dev/null | while read -r so; do
        collect_deps_of "$so"
    done
    AFTER=$(find "$LIB_DIR" -name '*.so*' -type f 2>/dev/null | wc -l)
    if [ "$BEFORE" = "$AFTER" ]; then
        echo "  ✓ 第 $round 轮收敛，无新增依赖"
        break
    fi
done

# 创建 soname 链接（如果只有带版本号的 .so.X.Y 但缺 .so.X 软链）
cd "$LIB_DIR"
for f in *.so.*.*; do
    [ -f "$f" ] || continue
    base="${f%.*.*}"        # libQt5Core
    mid="${f%.*}"           # libQt5Core.so.5
    [ -e "$mid" ] || ln -sf "$f" "$mid" 2>/dev/null || true
    [ -e "$base" ] || ln -sf "$f" "$base" 2>/dev/null || true
done
cd "$PROJECT_ROOT" >/dev/null

LIB_COUNT=$(ls -1 "$LIB_DIR" 2>/dev/null | wc -l)
echo "  ✓ 收集 $LIB_COUNT 个动态库（含 soname 链接）"

# ---- 4. 收集 Python 离线依赖 ----
# 关键策略：PyQt5/QtOpenGL/psutil/OpenGL_accelerate 等含 .so 二进制扩展的模块
# **绝对不能**直接 cp（psutil 同样含 C 扩展 _psutil_linux.cpython-*.so，绑定 ABI），
# 它们的 .so 编译绑定到具体 ABI（glibc/python/linux-platform tag），
# 跨架构/跨 Python 版本部署必报"ModuleNotFoundError / 架构不匹配"。
# 正确做法：apt-get download 下载当前架构的官方 .deb（自带所有依赖与 ABI 兼容），
# 目标机 dpkg -i 安装，由 apt 维护 ABI 兼容性。
# 只有纯 Python 包（PyOpenGL、pyserial）可以直接复制。
echo "收集 Python 离线依赖..."
PY_MODULES_DIR="$INSTALL_DIR/python_modules"
mkdir -p "$PY_MODULES_DIR"
PY_COPIED=false
APT_PKG_DIR="$PY_MODULES_DIR/apt_packages"
mkdir -p "$APT_PKG_DIR"

# 0) ★零网络方案（最可靠，优先采用）：从编译机已安装的 python3 直接复制★
# 前提（由打包铁律保证）：编译机与目标机同发行版同架构同 Python（如统信1070 打统信1070 的包），
# 此时编译机 dist-packages 里的 PyQt5/psutil/OpenGL 与目标机 100% ABI 兼容。
# 编译机既然能跑老化测试 GUI，这些模块就已安装 → 完全不需要联网下载任何 .deb。
# 旧流程只有 apt 下载一条路，在统信源缺包/CDN 慢（75KB/s）时 4 个主包永远凑不齐 → 打包中止。
PY_OOO_LOCAL=false
if python3 -c "import PyQt5, PyQt5.QtOpenGL, psutil, OpenGL" 2>/dev/null; then
    PY_VER0=$(python3 -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')" 2>/dev/null)
    PY_SRC0=""
    for p0 in /usr/lib/python3/dist-packages \
              /usr/lib/python${PY_VER0}/site-packages \
              /usr/local/lib/python${PY_VER0}/dist-packages; do
        if [ -d "$p0/PyQt5" ] && { [ -d "$p0/psutil" ] || [ -f "$p0/psutil.so" ]; }; then
            PY_SRC0="$p0"; break
        fi
    done
    if [ -n "$PY_SRC0" ]; then
        echo "  检测到编译机已安装 PyQt5/psutil/OpenGL（$PY_SRC0），零网络复制（同机同 ABI）..."
        rm -rf "$PY_MODULES_DIR/PyQt5" "$PY_MODULES_DIR/psutil" \
               "$PY_MODULES_DIR/OpenGL" "$PY_MODULES_DIR/OpenGL_accelerate" 2>/dev/null
        cp -r "$PY_SRC0/PyQt5"    "$PY_MODULES_DIR/" 2>/dev/null
        cp -r "$PY_SRC0/psutil"   "$PY_MODULES_DIR/" 2>/dev/null || cp "$PY_SRC0/psutil.so" "$PY_MODULES_DIR/" 2>/dev/null
        cp -r "$PY_SRC0/OpenGL"   "$PY_MODULES_DIR/" 2>/dev/null
        [ -d "$PY_SRC0/OpenGL_accelerate" ] && cp -r "$PY_SRC0/OpenGL_accelerate" "$PY_MODULES_DIR/" 2>/dev/null
        find "$PY_MODULES_DIR" -type d \( -name '__pycache__' -o -name 'tests' -o -name 'test' \) -exec rm -rf {} + 2>/dev/null
        # 用打包内容做最终验证（PYTHONPATH 指向包内）
        if PYTHONPATH="$PY_MODULES_DIR" python3 -c "import PyQt5, PyQt5.QtOpenGL, psutil, OpenGL" 2>/dev/null; then
            PY_OOO_LOCAL=true
            PY_COPIED=true
            OOO_SIZE=$(du -sh "$PY_MODULES_DIR" 2>/dev/null | awk '{print $1}')
            echo "  ✓ 零网络方案成功：开箱即用模块已就位（$OOO_SIZE），并验证 import 通过"
        else
            echo "  ⚠ 复制后验证失败（可能混入不兼容模块），回退到 apt 下载方案"
            rm -rf "$PY_MODULES_DIR/PyQt5" "$PY_MODULES_DIR/psutil" \
                   "$PY_MODULES_DIR/OpenGL" "$PY_MODULES_DIR/OpenGL_accelerate" 2>/dev/null
        fi
    fi
fi

# 1) 下载当前架构的 PyQt5 系 apt 离线包及其完整依赖树（作为零网络方案之后的第二层保险）
# 关键：仅下载主包不够——dpkg -i 会因依赖（如 libqt5opengl5/libqt5network5 等）缺失报错。
# 使用 apt-get --download-only 是最稳的：apt 自动解析完整依赖树并把所有 .deb 下到 /var/cache/apt/archives
# 注意：
#   a) 清单只保留老化测试实际 import 的包（PyQt5/QtOpenGL/sip/psutil/PyOpenGL）+ Qt 运行库。
#      旧清单里的 qtwebkit/qtmultimedia/qtsql/qtxmlpatterns 在统信源不存在，
#      会导致 apt-get --download-only 整体失败（E: 无法定位软件包）→ 全部依赖树都下不了。
#   b) 先用 apt-cache 逐个过滤掉源里不存在的包，进一步保证闭包下载不因单个缺包而全灭。
#   c) 加入 Qt 运行库包（libqt5core5a 等）——编译机上已安装时默认不下载，
#      用 --reinstall 强制下载，作为目标机 lib/ 意外缺失时的离线自愈来源
if [ "$PY_OOO_LOCAL" != true ] && command -v apt-get >/dev/null 2>&1; then
    NEEDED_PKGS="python3-pyqt5 python3-pyqt5.sip python3-pyqt5.qtopengl python3-psutil python3-opengl \
libqt5core5a libqt5gui5 libqt5widgets5 libqt5network5 libqt5serialport5 libqt5dbus5 libqt5opengl5"
    # 预过滤：只保留源里真实存在的包（防止一个缺包拖垮整个 --download-only）
    AVAIL_PKGS=""
    for pkg in $NEEDED_PKGS; do
        if apt-cache show "$pkg" >/dev/null 2>&1; then
            AVAIL_PKGS="$AVAIL_PKGS $pkg"
        else
            echo "  ℹ 源中无 $pkg，跳过（不影响：核心依赖会随闭包自动解析）"
        fi
    done
    rm -rf /var/cache/apt/archives/*.deb 2>/dev/null
    # apt-get --download-only 下载到 /var/cache/apt/archives/，自动解析全部依赖
    # 慢速 CDN（统信 75KB/s 级）实测需要数分钟：超时从 90s 放宽到 600s，失败自动重试 1 次
    echo "  下载 PyQt5/psutil/OpenGL 及完整依赖树离线 .deb（慢速源可能需要几分钟）..."
    DL_OK=false
    for attempt in 1 2; do
        if timeout 600 apt-get install --download-only --reinstall -y --no-install-recommends \
            -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30 -o Acquire::Retries=3 \
            $AVAIL_PKGS >/dev/null 2>&1 && ls /var/cache/apt/archives/*.deb >/dev/null 2>&1; then
            DL_OK=true
            break
        fi
        echo "  ⚠ 第 $attempt 次闭包下载失败/超时，重试..."
    done
    if [ "$DL_OK" = true ]; then
        cp /var/cache/apt/archives/*.deb "$APT_PKG_DIR/" 2>/dev/null
        DEB_COUNT=$(ls -1 "$APT_PKG_DIR"/*.deb 2>/dev/null | wc -l)
        echo "  ✓ 已下载 $DEB_COUNT 个 .deb 包（含完整依赖树，按当前架构）"
        PY_COPIED=true
    else
        # 回退：逐包 apt-get download（3 次重试、单包 60s；旧版 10s 在慢速 CDN 上必失败）
        echo "  apt-get --download-only 失败，回退到逐包下载（每包重试 3 次）..."
        for pkg in $AVAIL_PKGS; do
            for try in 1 2 3; do
                (cd "$APT_PKG_DIR" && timeout 60 apt-get download "$pkg" 2>/dev/null) && break
                [ "$try" = 3 ] && echo "  ⚠ $pkg 三次下载均失败（慢速源/缺包），可手动放入 $APT_PKG_DIR/"
                sleep 1
            done
        done
        # 人工离线兜底：项目 offline_debs/ 目录里的 .deb 一并收包
        ls "$PROJECT_ROOT"/offline_debs/*.deb >/dev/null 2>&1 && \
            cp "$PROJECT_ROOT"/offline_debs/*.deb "$APT_PKG_DIR/" 2>/dev/null && \
            echo "  ✓ 已并入 offline_debs/ 中的手动离线包"
        DEB_COUNT=$(ls -1 "$APT_PKG_DIR"/*.deb 2>/dev/null | wc -l)
        if [ "$DEB_COUNT" -gt 0 ]; then
            echo "  ⚠ 已下载 $DEB_COUNT 个 .deb（可能缺依赖，离线安装时由 dpkg/apt 兜底）"
            PY_COPIED=true
        else
            echo "  ⚠ 未获取到任何 .deb（编译机可能未配置 apt 源或网络不可用）"
        fi
    fi
elif [ "$PY_OOO_LOCAL" = true ]; then
    echo "  ℹ 开箱即用模块已就绪，跳过在线下载（如需 .deb 第二保险层，可清空系统 PyQt5 后重打包）"
else
    echo "  ⚠ 无 apt-get，跳过 .deb 下载"
fi

# 1.5) ★核心修复：把 Python 依赖 .deb 直接解包进 python_modules/ ★
# 旧模式：只携带 .deb 文件，等首次运行老化测试时由程序触发 dpkg -i 安装 ——
#   ① 需要 sudo 密码（sudoers 失效时必弹密码框）
#   ② dpkg -i 的依赖链可能不满足，还需 apt-get install -f（联网）
#   ③ 失败后回退 apt-get install（联网）→ 离线环境必失败弹错误框
# 新模式：打包时用 dpkg-deb -x 把 PyQt5/psutil/OpenGL 解包为"开箱即用"的
#   文件形态放进 python_modules/，launcher 设 PYTHONPATH 直接 import ——
#   零安装、零密码、零联网，全新系统装完 deb 即可运行老化测试。
# ABI 说明：解包内容与编译机同发行版同架构同 Python 版本（apt 下载自编译机源），
#   同发行版目标机 100% 匹配；跨发行版/跨 Python 版本时 import 会失败，
#   程序自动回退到"离线 dpkg -i → 联网安装"兜底链（保留原逻辑）。
echo "解包 Python 依赖到 python_modules/（开箱即用模式）..."
if [ "$PY_OOO_LOCAL" = true ]; then
    echo "  ℹ 零网络方案已就位，跳过 .deb 解包"
    PY_DEBS_FOR_EXTRACT=""
else
    EXTRACT_TMP="$PY_MODULES_DIR/.extract_$$"
    mkdir -p "$EXTRACT_TMP"
    PY_DEBS_FOR_EXTRACT=$(ls "$APT_PKG_DIR"/python3-pyqt5_*.deb \
                            "$APT_PKG_DIR"/python3-pyqt5.sip_*.deb \
                            "$APT_PKG_DIR"/python3-pyqt5.qtopengl_*.deb \
                            "$APT_PKG_DIR"/python3-psutil_*.deb \
                            "$APT_PKG_DIR"/python3-opengl_*.deb 2>/dev/null)
fi
if [ -n "$PY_DEBS_FOR_EXTRACT" ]; then
    for deb in $PY_DEBS_FOR_EXTRACT; do
        dpkg-deb -x "$deb" "$EXTRACT_TMP" 2>/dev/null
    done
    # 找到解包出的 dist-packages 目录（Debian 系标准布局）
    DISTPKG_DIR=$(find "$EXTRACT_TMP" -maxdepth 5 -type d -name dist-packages 2>/dev/null | head -1)
    if [ -n "$DISTPKG_DIR" ]; then
        # 复制 PyQt5 / PyQt5.sip / psutil / OpenGL 到 python_modules/
        for pkgdir in PyQt5 psutil OpenGL OpenGL_accelerate; do
            if [ -d "$DISTPKG_DIR/$pkgdir" ]; then
                rm -rf "$PY_MODULES_DIR/$pkgdir"
                cp -r "$DISTPKG_DIR/$pkgdir" "$PY_MODULES_DIR/" && \
                    echo "  ✓ 已解包 $pkgdir（开箱即用）"
            fi
        done
        # PyQt5.sip 有时是单独的顶层目录或 .so 文件
        for extra in PyQt5.sip; do
            if [ -d "$DISTPKG_DIR/$extra" ] && [ ! -d "$PY_MODULES_DIR/$extra" ]; then
                cp -r "$DISTPKG_DIR/$extra" "$PY_MODULES_DIR/" 2>/dev/null
            fi
        done
    fi
    rm -rf "$EXTRACT_TMP"
    # 清理解包产生的 __pycache__
    find "$PY_MODULES_DIR" -type d -name '__pycache__' -exec rm -rf {} + 2>/dev/null
    # 校验：PyQt5 + psutil + OpenGL 三者齐备才算开箱即用模式生效
    if [ -d "$PY_MODULES_DIR/PyQt5" ] && [ -d "$PY_MODULES_DIR/psutil" ] && [ -d "$PY_MODULES_DIR/OpenGL" ]; then
        echo "  ✓ 老化测试依赖已按'开箱即用'形态打包（同发行版目标机零安装零联网）"
    else
        echo "  ⚠ 解包不完整（缺 PyQt5/psutil/OpenGL 之一），目标机将回退到安装模式"
    fi
else
    rm -rf "$EXTRACT_TMP"
    echo "  ⚠ 无可解包的 Python 依赖 .deb，目标机将回退到安装模式（需密码/联网）"
fi

# 2) 补充纯 Python 包（serial/pyserial，串口测试用）
# 策略演进说明（覆盖旧 A3 注释）：
#   旧 A3（09-09 上午）：psutil/PyQt5 含 C 扩展不得盲 cp，一律剔除走 apt .deb 通道
#     → 目标机首次运行老化测试仍需 dpkg -i（密码/依赖链问题）
#   新策略（09-09 下午）：PyQt5/psutil/OpenGL 由 1.5 节从**同发行版 apt deb** 解包
#     （dpkg-deb -x，受控 ABI 来源），文件就位即 import，零安装零密码零联网；
#     跨发行版/跨 Python 目标机 import 失败时，程序回退 dpkg -i → 联网安装兜底链
#   本节仅补充串口测试用的纯 Python 模块（serial/pyserial）
DIST_PATHS=""
PY_VER=$(python3 -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')" 2>/dev/null)
[ -n "$PY_VER" ] && DIST_PATHS="
    /usr/lib/python3/dist-packages
    /usr/lib/python${PY_VER}/site-packages
    /usr/local/lib/python${PY_VER}/dist-packages
    /usr/lib/python3/site-packages
"
PY_SRC=""
for p in $DIST_PATHS; do
    if [ -d "$p" ] && { [ -d "$p/OpenGL" ] || [ -d "$p/serial" ] || [ -d "$p/pyserial" ]; }; then
        PY_SRC="$p"; break
    fi
done

if [ -n "$PY_SRC" ]; then
    # 仅补充 1.5 节解包未覆盖的纯 Python 模块（serial/pyserial 串口测试用）；
    # OpenGL/psutil 已由 1.5 节从 apt deb 解包（同 ABI 受控来源），此处不覆盖
    for mod in serial pyserial; do
        if [ -d "$PY_SRC/$mod" ] && [ ! -d "$PY_MODULES_DIR/$mod" ]; then
            cp -r "$PY_SRC/$mod" "$PY_MODULES_DIR/" 2>/dev/null && echo "  ✓ 纯 Python 模块 $mod"
        elif [ -f "$PY_SRC/$mod.py" ] && [ ! -f "$PY_MODULES_DIR/$mod.py" ]; then
            cp "$PY_SRC/$mod.py" "$PY_MODULES_DIR/" 2>/dev/null && echo "  ✓ 纯 Python 模块 $mod.py"
        fi
    done
    # 注意：不再强制清除 psutil —— 1.5 节已按"同发行版 apt deb 解包"的受控方式
    # 打包 psutil（开箱即用模式），与旧版"盲 cp 系统 dist-packages"有本质区别
    # 清理 __pycache__ 与测试目录，减小体积并避免加载陈旧 .pyc
    find "$PY_MODULES_DIR" -type d \( -name '__pycache__' -o -name 'tests' -o -name 'test' \) -exec rm -rf {} + 2>/dev/null
fi

if [ "$PY_COPIED" != true ]; then
    echo "  ⚠ 未收集到 Python 依赖，deb 将依赖系统 apt 在线安装（Depends 已声明）"
fi

# PyQt5 自带 Qt 库（python_modules/PyQt5/Qt5/lib）的依赖也要收集到主 lib/ 目录：
# 3D 老化测试用 OpenGL → 需要 libGL/libEGL；目标机没有时 Python 端同样崩溃
if [ -d "$PY_MODULES_DIR/PyQt5/Qt5/lib" ]; then
    echo "  递归收集 PyQt5 自带 Qt 库的依赖..."
    find "$PY_MODULES_DIR/PyQt5/Qt5/lib" -name '*.so*' -type f 2>/dev/null | while read -r so; do
        collect_deps_of "$so"
    done
    echo "  ✓ PyQt5 Qt 库依赖已并入 lib/"
fi

# ---- 4.5 Python 离线依赖主包完整性校验 ----
# 目标：老化测试在无网目标机必须可用。
# 达标形态二选一：
#   A) 开箱即用（python_modules/PyQt5+psutil+OpenGL 齐备）——零安装零联网，首选
#   B) 4 个主包 .deb 齐备（首次运行时程序自动 dpkg -i 离线安装）
echo ""
echo "校验 Python 离线依赖完整性..."
PY_DEB_MISSING=""
for mainpkg in python3-pyqt5_ python3-pyqt5.qtopengl_ python3-psutil_ python3-opengl_; do
    if ! ls "$APT_PKG_DIR"/${mainpkg}*.deb >/dev/null 2>&1; then
        PY_DEB_MISSING="$PY_DEB_MISSING ${mainpkg%_}"
    fi
done
OOO_COMPLETE=false
if [ -d "$PY_MODULES_DIR/PyQt5" ] && [ -d "$PY_MODULES_DIR/psutil" ] && [ -d "$PY_MODULES_DIR/OpenGL" ]; then
    OOO_COMPLETE=true
fi
if [ "$OOO_COMPLETE" = true ]; then
    echo "  ✓ 开箱即用形态完整（PyQt5/psutil/OpenGL 已随包，目标机零安装零联网）"
    if [ -n "$PY_DEB_MISSING" ]; then
        echo "  ℹ .deb 兜底层缺:$PY_DEB_MISSING（开箱即用已覆盖同发行版目标机，不影响离线运行）"
    fi
elif [ -z "$PY_DEB_MISSING" ]; then
    PY_DEB_TOTAL=$(ls -1 "$APT_PKG_DIR"/*.deb 2>/dev/null | wc -l)
    echo "  ✓ Python 离线依赖完整（$PY_DEB_TOTAL 个 .deb，含 4 个主包及完整依赖树）"
else
    echo "  ✗ Python 离线依赖缺失:$PY_DEB_MISSING"
    echo "    老化测试在无网目标机将无法运行（其它功能不受影响）！"
    if [ "${ALLOW_MISSING_PY_DEBS:-0}" = "1" ]; then
        echo "    （检测到 ALLOW_MISSING_PY_DEBS=1，继续打包）"
    else
        echo ""
        echo "  ✗✗✗ 打包中止：不满足'全功能离线运行'要求 ✗✗✗"
        echo "  修复方法（在编译机上执行任一）:"
        echo "    1)【推荐·零网络】在编译机安装老化测试依赖后重新打包（脚本会自动从系统复制）:"
        echo "       sudo apt install -y python3-pyqt5 python3-pyqt5.qtopengl python3-psutil python3-opengl"
        echo "       （装好后无需联网即可重新打包）"
        echo "    2) 联网重新运行本脚本（脚本会自动下载，慢速源耐心等待数分钟）"
        echo "    3) 手动下载后放入: $APT_PKG_DIR/ 或 $PROJECT_ROOT/offline_debs/"
        echo "       apt-get download python3-pyqt5 python3-pyqt5.qtopengl python3-psutil python3-opengl"
        echo "    4) 确实允许老化测试在线安装时，用以下命令强制打包:"
        echo "       ALLOW_MISSING_PY_DEBS=1 ./cross_platform_package_deb.sh"
        exit 1
    fi
fi

# ---- 5. 创建启动脚本 ----
echo "创建启动脚本..."
cat > "$INSTALL_DIR/elsky-test-client.sh" << 'EOF'
#!/bin/bash
# 研盛测试客户端启动脚本（deb 安装版）
# 定位实际安装目录（支持 /usr/bin/elsky-test-client 等软链接调用）
APP_DIR="$(cd "$(dirname "$(readlink -f "$0" 2>/dev/null || echo "$0")")" && pwd)"
APP_BIN="$APP_DIR/ELSKYTestClient"
LIB_DIR="$APP_DIR/lib"
PY_MODULES_DIR="$APP_DIR/python_modules"

# 启动前自检：可执行文件
if [ ! -x "$APP_BIN" ]; then
    echo "✗ 错误: 可执行文件不存在或无执行权限: $APP_BIN"
    echo "  请尝试: sudo dpkg --configure -a  或  重新安装本软件包"
    exit 1
fi

# ---- GLIBC 兼容性分级预检 ----
# 第1级：主程序本身与本系统 glibc 是否兼容（不带包内库路径、纯系统解析）。
#   主程序若不兼容 → 无论怎么换库都跑不起来（glibc 无法降级），必须重新打包。
# 第2级：包内 lib/ 与本系统是否兼容。不兼容但主程序兼容时，自动改用系统库启动，
#   保证"装完即可运行"（同时提示重新打包以获得最佳兼容性）。
SYS_GLIBC=$(ldd --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+$' | head -1)
BIN_SYS_LDD=$(ldd "$APP_BIN" 2>/dev/null)
if echo "$BIN_SYS_LDD" | grep -q 'version .GLIBC_'; then
    echo "✗ [致命] 主程序本身与本系统 glibc(${SYS_GLIBC:-未知}) 不兼容，无法启动:"
    echo "$BIN_SYS_LDD" | grep 'version .GLIBC_' | head -3 | sed 's/^/    /'
    echo "  根因: 本安装包是在更高 glibc 版本的系统上编译的（glibc 只能向上兼容、无法向下降级）。"
    echo "  解决: 必须在与本机相同系统版本（如统信UOS 1070/麒麟V10 SP1 = glibc 2.28）的编译机上，"
    echo "        重新执行 ./cross_platform_build.sh && ./cross_platform_package_deb.sh，再安装新 deb。"
    exit 1
fi
USE_BUNDLE_LIB=true
_CHKPATH="$LIB_DIR"; [ -d "$LIB_DIR/gl-fallback" ] && _CHKPATH="$LIB_DIR:$LIB_DIR/gl-fallback"
BUNDLE_LDD=$(LD_LIBRARY_PATH="$_CHKPATH" ldd "$APP_BIN" 2>/dev/null)
if echo "$BUNDLE_LDD" | grep -q 'version .GLIBC_'; then
    echo "⚠ 包内 lib/ 与本系统 glibc 不匹配（安装包与本机系统版本不一致），自动改用系统库启动:"
    echo "$BUNDLE_LDD" | grep 'version .GLIBC_' | head -3 | sed 's/^/    /'
    echo "  本机系统 Qt 库已可支撑运行，功能不受影响；建议后续用与本机相同系统的编译机重新打包。"
    USE_BUNDLE_LIB=false
fi

# 加载动态库：默认优先包内 lib/（与编译机同版本 Qt，规避系统 Qt 版本差异）；
# 包内库与本系统不兼容时（USE_BUNDLE_LIB=false）跳过，纯用系统库
if [ "$USE_BUNDLE_LIB" = true ] && [ -d "$LIB_DIR" ]; then
    export LD_LIBRARY_PATH="$LIB_DIR:${LD_LIBRARY_PATH}"
    # Qt 平台插件路径（lib/platforms/libqxcb.so 等）
    export QT_PLUGIN_PATH="$LIB_DIR:${QT_PLUGIN_PATH}"
fi
# [2026-10-17] GL 栈兜底：包内 GL/mesa 库（lib/gl-fallback/）默认不加载——
# 目标机系统 GL 与显卡驱动匹配，包内库优先反而导致 3D 花屏 / glmark2 无法运行。
# 仅当系统确实缺 OpenGL 运行库（统信1070 等精简系统）时才启用包内备用 GL。
if [ -d "$LIB_DIR/gl-fallback" ] && ! ldconfig -p 2>/dev/null | grep -q 'libGL\.so\.1'; then
    export LD_LIBRARY_PATH="$LIB_DIR/gl-fallback:${LD_LIBRARY_PATH}"
    echo "⚠ 系统缺少 OpenGL 运行库，启用包内备用 GL 栈（gl-fallback）"
fi
# 强制 xcb 平台（部分发行版默认 wayland 会导致 Qt 程序无法启动）
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
# [2026-10-19] Mesa llvmpipe 单线程光栅化：兆芯等平台无硬件 GL 驱动时回退
# llvmpipe 软件渲染，其多线程光栅化(lp_rast)在兆芯 CPU 上有已知数据竞争
# bug —— 表现为 3D 画布几何体上出现彩色碎块噪点(花屏)、glmark2 黑屏/渲染
# 异常。LP_NUM_THREADS=0 限制为单线程光栅化即可修复；该变量只被 llvmpipe
# 读取，硬件 GL 驱动下完全忽略，对其余平台零影响。
export LP_NUM_THREADS="${LP_NUM_THREADS:-0}"
# 高 DPI 支持
export QT_AUTO_SCREEN_SCALE_FACTOR=1

# 加载打包的 Python 模块（老化测试用）
if [ -d "$PY_MODULES_DIR/PyQt5" ]; then
    export PYTHONPATH="$PY_MODULES_DIR:${PYTHONPATH}"
    if [ "$USE_BUNDLE_LIB" = true ] && [ -d "$PY_MODULES_DIR/PyQt5/Qt5/lib" ]; then
        export LD_LIBRARY_PATH="$PY_MODULES_DIR/PyQt5/Qt5/lib:${LD_LIBRARY_PATH}"
    fi
fi

# ---- 依赖自检与三级自愈 ----
check_missing() {
    # 检查路径含 gl-fallback（GL 栈默认不在运行路径但真实存在，避免误报 not found）
    local chkpath="$LIB_DIR"
    [ -d "$LIB_DIR/gl-fallback" ] && chkpath="$LIB_DIR:$LIB_DIR/gl-fallback"
    if [ "$USE_BUNDLE_LIB" = true ]; then
        LD_LIBRARY_PATH="$chkpath" ldd "$APP_BIN" 2>/dev/null | grep "not found" | awk '{print $1}' | sort -u
    else
        ldd "$APP_BIN" 2>/dev/null | grep "not found" | awk '{print $1}' | sort -u
    fi
}

MISSING=$(check_missing)
if [ -n "$MISSING" ]; then
    echo "⚠ 检测到缺失的依赖库:"
    echo "$MISSING"
    echo ""

    # 自愈1: 离线安装包内自带的 Qt 库 .deb
    #（python_modules/apt_packages 中打包了 libqt5* 离线 deb，无需联网）
    APT_DIR="$PY_MODULES_DIR/apt_packages"
    if [ -d "$APT_DIR" ]; then
        QT_DEBS=$(ls "$APT_DIR"/libqt5*.deb "$APT_DIR"/qt5*.deb 2>/dev/null)
        if [ -n "$QT_DEBS" ]; then
            echo "[自愈1] 尝试离线安装包内 Qt 库 .deb..."
            if [ "$(id -u)" = "0" ]; then
                dpkg -i $QT_DEBS 2>&1 | tail -3
            elif command -v sudo >/dev/null 2>&1; then
                sudo dpkg -i $QT_DEBS 2>&1 | tail -3
            elif command -v pkexec >/dev/null 2>&1; then
                pkexec dpkg -i $QT_DEBS 2>&1 | tail -3
            fi
            MISSING=$(check_missing)
        fi
    fi

    # 自愈2: 在线安装系统 Qt 库（需联网）
    if [ -n "$MISSING" ] && command -v apt-get >/dev/null 2>&1; then
        echo "[自愈2] 尝试在线安装系统 Qt 库（需要联网）..."
        SUDO_PREFIX=""
        [ "$(id -u)" != "0" ] && SUDO_PREFIX="sudo"
        $SUDO_PREFIX apt-get install -y libqt5core5a libqt5gui5 libqt5widgets5 \
            libqt5network5 libqt5serialport5 2>&1 | tail -3
        MISSING=$(check_missing)
    fi

    # 自愈3: 终极兜底——用动态链接器直接指定库路径启动
    #（绕过一切环境变量/缓存问题，只要 lib/ 里库齐全就能跑；包内库不兼容时禁用）
    if [ -n "$MISSING" ] && [ "$USE_BUNDLE_LIB" = true ]; then
        LOADER=""
        case "$(uname -m)" in
            x86_64)    LOADER="/lib64/ld-linux-x86-64.so.2" ;;
            aarch64)   LOADER="/lib/ld-linux-aarch64.so.1" ;;
            i386|i686) LOADER="/lib/ld-linux.so.2" ;;
            loongarch64|loong64|loongarch)
                # 龙芯 LoongArch LP64D 动态链接器路径在不同发行版有差异，逐个探测
                for _l in /lib64/ld-linux-loongarch-lp64d.so.1 \
                          /lib/ld-linux-loongarch-lp64d.so.1 \
                          /lib/loongarch64-linux-gnu/ld-linux-loongarch-lp64d.so.1; do
                    [ -x "$_l" ] && { LOADER="$_l"; break; }
                done
                ;;
            mips64el|mips64)
                for _l in /lib/mips64el-linux-gnuabi64/ld.so.1 \
                          /lib64/ld.so.1 /lib/ld.so.1; do
                    [ -x "$_l" ] && { LOADER="$_l"; break; }
                done
                ;;
        esac
        _LOADER_LIBPATH="$LIB_DIR"
        [ -d "$LIB_DIR/gl-fallback" ] && _LOADER_LIBPATH="$LIB_DIR:$LIB_DIR/gl-fallback"
        if [ -x "$LOADER" ]; then
            echo "[自愈3] 使用动态链接器兜底启动（库路径: $_LOADER_LIBPATH）..."
            cd "$APP_DIR"
            exec "$LOADER" --library-path "$_LOADER_LIBPATH" "$APP_BIN" "$@"
        fi
    fi
    if [ -n "$MISSING" ]; then
        echo "✗ 无法自动修复缺失依赖，请手动安装:"
        echo "  sudo apt install -y libqt5core5a libqt5gui5 libqt5widgets5 libqt5network5 libqt5serialport5"
        exit 1
    fi
    echo "✓ 缺失依赖已自动修复"
fi

# 切到应用目录运行（程序运行时可能使用相对路径）
cd "$APP_DIR"
exec "$APP_BIN" "$@"
EOF
chmod 755 "$INSTALL_DIR/elsky-test-client.sh"

# ---- 5.1 生成 qt.conf（Qt 从可执行文件所在目录的 lib/ 找插件与库）----
# 第四重保险：即使 LD_LIBRARY_PATH / QT_PLUGIN_PATH 被桌面环境清掉，
# Qt 也能通过 qt.conf 找到同目录 lib/ 下的平台插件
cat > "$INSTALL_DIR/qt.conf" << 'EOF'
[Paths]
Prefix = .
Plugins = lib
Libraries = lib
EOF
chmod 644 "$INSTALL_DIR/qt.conf"

# 同时创建 /usr/bin/elsky-test-client 命令（用户终端直接调用）
mkdir -p "$PACKAGE_DIR/usr/bin"
cat > "$PACKAGE_DIR/usr/bin/elsky-test-client" << 'EOF'
#!/bin/bash
exec /usr/lib/elsky-test-client/elsky-test-client.sh "$@"
EOF
chmod 755 "$PACKAGE_DIR/usr/bin/elsky-test-client"

# ---- 6. 创建 .desktop 桌面图标 ----
echo "创建桌面图标..."
DESKTOP_FILE="$INSTALL_DIR/elsky-test-client.desktop"
# [2026-11-11] .desktop 不再声明 MimeType=application/octet-stream：
# 该声明会把所有二进制文件都关联到本应用，污染文件管理器右键菜单。
cat > "$DESKTOP_FILE" << 'EOF'
[Desktop Entry]
Version=1.0
Type=Application
Name=研盛测试客户端
GenericName=研盛测试客户端
Comment=电源管理、网络唤醒、系统监控、设备检测、老化测试工具
Exec=/usr/lib/elsky-test-client/elsky-test-client.sh %F
Icon=elsky-test-client
Terminal=false
Categories=Utility;System;
StartupNotify=true
StartupWMClass=ELSKYTestClient
X-Debian-Package=elsky-test-client
X-Deepin-AppID=elsky-test-client
X-Deepin-Vendor=elsky
Actions=Uninstall;

[Desktop Action Uninstall]
Name=卸载研盛测试客户端
Name[en]=Uninstall ELSKY Test Client
Exec=/usr/lib/elsky-test-client/uninstall.sh
Icon=elsky-test-client
EOF
# 注意：必须可执行，否则 DDE/KDE 文件管理器双击 .desktop 无效
chmod 755 "$DESKTOP_FILE"

# ---- 自带卸载脚本 + 现场诊断脚本（2026-11-11 三次修复）----
# 查证 dde-launcher 源码：右键菜单由 MenuWorker 硬编码，原生「卸载」固定走
# org.deepin.dde.Lastore1 的 PackageExists / RemovePackage，只对应用商店安装的
# 包有效；freedesktop 的 Actions= 只有 dde-file-manager 桌面右键才认，启动器不认。
# 因此前两轮「加 Desktop Action 让启动器出现卸载项」的做法在启动器里根本不显示。
# 本轮改为注册一个普通应用图标（见下方 R22），启动器必然枚举；脚本同时支持
# sudo -n 免密 / pkexec / root 三条提权路径，成功失败都有可见输出。
cat > "$INSTALL_DIR/uninstall.sh" << 'EOF'
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
EOF
chmod 755 "$INSTALL_DIR/uninstall.sh"

# ---- 现场诊断脚本（定位「点了没反应」到底卡在哪一环）----
cat > "$INSTALL_DIR/diagnose_uninstall.sh" << 'EOF'
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
EOF
chmod 755 "$INSTALL_DIR/diagnose_uninstall.sh"

# 应用菜单入口（/usr/share/applications）
mkdir -p "$PACKAGE_DIR/usr/share/applications"
cp "$DESKTOP_FILE" "$PACKAGE_DIR/usr/share/applications/elsky-test-client.desktop"
chmod 644 "$PACKAGE_DIR/usr/share/applications/elsky-test-client.desktop"

# ---- 启动器内一定可见的独立卸载入口（2026-11-11 三次修复）----
# dde-launcher 的右键菜单是 MenuWorker 硬编码的固定 8 项，只认 lastore；
# freedesktop 的 Actions= 只有 dde-file-manager（桌面图标右键）才读。
# 因此改为注册一个普通 Type=Application 图标：只要是应用，启动器必然枚举显示，
# 用户点它就能卸载。Terminal=true 让全过程与结果可见，失败也不会变成「无反应」。
UNINSTALL_DESKTOP="$INSTALL_DIR/elsky-test-client-uninstall.desktop"
cat > "$UNINSTALL_DESKTOP" << 'EOF'
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
EOF
chmod 755 "$UNINSTALL_DESKTOP"
cp "$UNINSTALL_DESKTOP" "$PACKAGE_DIR/usr/share/applications/elsky-test-client-uninstall.desktop"
chmod 644 "$PACKAGE_DIR/usr/share/applications/elsky-test-client-uninstall.desktop"

# 图标（使用系统通用图标占位，如无自定义图标）
mkdir -p "$PACKAGE_DIR/usr/share/icons/hicolor/256x256/apps"
# 尝试从系统复制一个通用图标，保证桌面图标可显示
for icon in /usr/share/icons/hicolor/256x256/apps/utilities-system-monitor.png \
            /usr/share/icons/hicolor/scalable/apps/utilities-system-monitor.svg; do
    if [ -f "$icon" ]; then
        cp "$icon" "$PACKAGE_DIR/usr/share/icons/hicolor/256x256/apps/elsky-test-client.png" 2>/dev/null || true
        break
    fi
done

# ---- 7. 生成 DEBIAN 控制文件 ----
echo "生成 DEBIAN 控制文件..."

# control
# 关键：不再硬依赖具体 qt 包名（不同发行版/版本包名可能不同：libqt5serialport5 vs libqt5serialport5t64 等），
# 改为只依赖最基础的运行时（python3、policykit-1），Qt 库与 python 依赖走 Recommends（推荐但不强制）。
# 首次启动时由程序自检并自动安装缺失依赖（已在 basic_tester.cpp 中实现）。
# 注意：glmark2 不列入 Recommends——统信UOS/麒麟源中无此包，声明了只会让 apt 反复警告"无法定位"，
#       实际由打包脚本离线收集进 extra-bin（可选增强，缺失自动降级）。
# [2026-11-11 卸载修复] policykit-1 在 Debian 13/Ubuntu 24.04 等新系统上已被
# polkitd 取代甚至移除。硬依赖会让包停在"未配置(half-configured)"状态，
# 软件中心 / 桌面右键调用 apt remove 时直接失败。本软件运行并不依赖 polkit，
# 降级为 Recommends 并加 polkitd 替代，任何发行版都不会因此装不上/卸不掉。
cat > "$DEBIAN_DIR/control" << EOF
Package: $PKG_NAME
Version: $PKG_VERSION
Section: utils
Priority: optional
Architecture: $DEB_ARCH
Maintainer: $PKG_MAINTAINER
Depends: python3
Recommends: policykit-1 | polkitd, libqt5core5a | libqt5core5t64, libqt5gui5 | libqt5gui5t64, libqt5widgets5 | libqt5widgets5t64, libqt5network5 | libqt5network5t64, libqt5serialport5 | libqt5serialport5t64, python3-pyqt5, python3-pyqt5.qtopengl, python3-psutil, python3-opengl, iputils-ping, ethtool, network-manager, util-linux
Suggests: libqt5serialport5-dev, python3-pyqt5.qtopengl
Description: $PKG_DESCRIPTION
 研盛测试客户端，提供电源管理、网络唤醒(WOL)、系统监控、
 设备实时监控、USB/串口/网络测试、老化测试等功能。
 支持飞腾ARM64、X86与龙芯(LoongArch/MIPS64el)平台。
 .
 首次启动会自动检测并安装缺失依赖（需联网），无需用户手动配置。
EOF

# postinst：安装后配置（免密权限 + 拨号组 + 图标缓存）
# 注意：不使用 set -e，每个步骤独立容错，避免单个失败导致后续步骤跳过
cat > "$DEBIAN_DIR/postinst" << 'EOF'
#!/bin/bash
# 安装后脚本：一次性配置权限，使软件各功能免密运行
# 注意：使用 set +e（默认），每个步骤独立处理失败，避免中断整个安装

CURRENT_USER="${SUDO_USER:-$USER}"
if [ -z "$CURRENT_USER" ]; then
    CURRENT_USER=$(logname 2>/dev/null || echo "")
fi
# 修复：双击 deb 走软件中心安装时 SUDO_USER 为空、logname 为 root，
# 旧版直接写 USERNAME_PLACEHOLDER → sudoers 免密对所有真实用户失效
# → 运行老化测试/电源管理时弹密码框（用户反馈"总要输入密码"的根因之一）。
# 改为回退枚举第一个 uid>=1000 的真实登录用户写入 sudoers。
if [ -z "$CURRENT_USER" ] || [ "$CURRENT_USER" = "root" ]; then
    CURRENT_USER=$(getent passwd 2>/dev/null | awk -F: \
        '$3>=1000 && $6!="/nonexistent" && $7!~/(nologin|false)$/ {print $1; exit}')
fi
# 实在找不到（无真实用户的纯 root 系统）才写占位符
if [ -z "$CURRENT_USER" ]; then
    CURRENT_USER="USERNAME_PLACEHOLDER"
fi

echo ""
echo "=== 配置研盛测试客户端权限 ==="

# 0. 修正安装目录权限：确保所有用户可读可执行
chmod -R a+rX /usr/lib/elsky-test-client/ 2>/dev/null
chmod 755 /usr/lib/elsky-test-client/ELSKYTestClient 2>/dev/null
chmod 755 /usr/lib/elsky-test-client/elsky-test-client.sh 2>/dev/null
chmod 755 /usr/lib/elsky-test-client/elsky-test-client.desktop 2>/dev/null

# 1. 配置 sudoers 免密：仅授权本软件需要的具体命令（最小权限原则）
# 注意：systemctl/poweroff/reboot/shutdown 必须在列（循环开关机测试依赖，否则会卡在"正在准备重启"）
# 修复：多用户机器上所有真实用户（uid>=1000）逐行授权，而非只有安装者
# 修复：补上 /usr/sbin/smartctl 与 /usr/bin/smartctl（硬盘温度 SMART 兜底会调用
#   sudo -n smartctl -A /dev/xxx）。旧白名单从未包含它，导致 sudo -n 必然报
#   "a password is required"；而代码每 2 秒重试且不缓存失败结果，造成 PAM/audit
#   日志持续刷屏、UI 线程被 waitForFinished 阻塞，最终系统卡死。
#   system_monitor.cpp 侧已加失败缓存 + 授权失败熔断；这里让需要该功能的机器
#   仍能正常读到 SMART 温度，两者配合既治标也治本。
SUDOERS_FILE="/etc/sudoers.d/elsky-test-client"
{
    echo "# 研盛测试客户端 - 最小化免密授权（所有真实登录用户）"
    getent passwd 2>/dev/null | awk -F: \
        '$3>=1000 && $6!="/nonexistent" && $7!~/(nologin|false)$/' \
        | cut -d: -f1 | sort -u \
        | while read -r suser; do
            echo "$suser ALL=(ALL) NOPASSWD: /usr/bin/apt-get, /usr/bin/apt, /usr/bin/dpkg, /usr/sbin/ethtool, /usr/bin/nmcli, /usr/bin/systemctl, /usr/sbin/systemctl, /usr/sbin/poweroff, /usr/sbin/reboot, /usr/sbin/shutdown, /usr/bin/poweroff, /usr/bin/reboot, /usr/bin/shutdown, /usr/sbin/smartctl, /usr/bin/smartctl"
        done
    # 兜底：枚举失败时至少保留 CURRENT_USER 规则
    echo "# fallback（若上方无规则则生效）"
    echo "$CURRENT_USER ALL=(ALL) NOPASSWD: /usr/bin/apt-get, /usr/bin/apt, /usr/bin/dpkg, /usr/sbin/ethtool, /usr/bin/nmcli, /usr/bin/systemctl, /usr/sbin/systemctl, /usr/sbin/poweroff, /usr/sbin/reboot, /usr/sbin/shutdown, /usr/bin/poweroff, /usr/bin/reboot, /usr/bin/shutdown, /usr/sbin/smartctl, /usr/bin/smartctl"
} > "$SUDOERS_FILE" 2>/dev/null
chmod 440 "$SUDOERS_FILE" 2>/dev/null
# 校验 sudoers 语法（visudo -c），语法错误会导致整机 sudo 失效，必须拦截
if command -v visudo >/dev/null 2>&1; then
    if ! visudo -c -f "$SUDOERS_FILE" >/dev/null 2>&1; then
        echo "  ⚠ sudoers 语法校验失败，回退为仅 $CURRENT_USER 单用户规则"
        cat > "$SUDOERS_FILE" << SUDO_EOF
# 研盛测试客户端 - 最小化免密授权
$CURRENT_USER ALL=(ALL) NOPASSWD: /usr/bin/apt-get, /usr/bin/apt, /usr/bin/dpkg, /usr/sbin/ethtool, /usr/bin/nmcli, /usr/bin/systemctl, /usr/sbin/systemctl, /usr/sbin/poweroff, /usr/sbin/reboot, /usr/sbin/shutdown, /usr/bin/poweroff, /usr/bin/reboot, /usr/bin/shutdown, /usr/sbin/smartctl, /usr/bin/smartctl
SUDO_EOF
        chmod 440 "$SUDOERS_FILE" 2>/dev/null
    fi
fi
echo "  ✓ sudoers 免密已配置（含关机/重启/依赖安装/硬盘SMART检测命令）: $SUDOERS_FILE"

# 2. 加入 dialout 组（串口访问权限）
if command -v usermod >/dev/null 2>&1 && [ "$CURRENT_USER" != "USERNAME_PLACEHOLDER" ]; then
    usermod -aG dialout "$CURRENT_USER" 2>/dev/null
    echo "  ✓ 已加入 dialout 组（串口访问，重新登录后生效）"
fi

# 3. 刷新桌面图标缓存（不强制）
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database /usr/share/applications 2>/dev/null
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache /usr/share/icons/hicolor 2>/dev/null
fi

# 3.5 往所有真实用户的桌面放图标（DDE/KDE 双击即启动）
# 修复：旧版只给 $CURRENT_USER（SUDO_USER/logname）放图标 —— 双击 deb 走软件中心
# 安装时 postinst 环境里 SUDO_USER 常为空、logname 为 root → 拿不到真实用户
# → 桌面永远没有图标（用户反馈的核心问题）。
# 改为枚举所有 uid>=1000 的真实登录用户，逐个放置；桌面目录按
# 桌面/Desktop/XDG user-dirs.dirs 顺序探测。
place_desktop_icon() {
    local uname_="$1" uhome="$2" ddir=""
    # 探测桌面目录：中文名"桌面" > "Desktop" > XDG 配置声明
    if [ -d "$uhome/桌面" ]; then
        ddir="$uhome/桌面"
    elif [ -d "$uhome/Desktop" ]; then
        ddir="$uhome/Desktop"
    elif [ -f "$uhome/.config/user-dirs.dirs" ]; then
        local xdg_desktop
        xdg_desktop=$(grep 'XDG_DESKTOP_DIR' "$uhome/.config/user-dirs.dirs" 2>/dev/null \
            | head -1 | sed 's/.*="\?\$HOME\/\?//; s/"\?$//' | tr -d '\r')
        [ -n "$xdg_desktop" ] && [ -d "$uhome/$xdg_desktop" ] && ddir="$uhome/$xdg_desktop"
    fi
    if [ -n "$ddir" ]; then
        # 桌面图标用符号链接指向 /usr/share/applications/ 下的标准 .desktop，
        # 保证改图标/改菜单项时桌面与启动器始终一致，也便于包管理器识别归属。
        # [2026-11-11 二次现场修正] 启动器原生「卸载」是否可用由 DDE/lastore 的
        # 应用商店安装记录决定（deepin Wiki 明示"仅对部分应用有效"），与本链接
        # 是否符号链接无关；可靠卸载请走 .desktop 的「卸载研盛测试客户端」Action。
        rm -f "$ddir/elsky-test-client.desktop" 2>/dev/null
        if ln -s /usr/share/applications/elsky-test-client.desktop \
                "$ddir/elsky-test-client.desktop" 2>/dev/null; then
            chown -h "$uname_" "$ddir/elsky-test-client.desktop" 2>/dev/null
            echo "  ✓ 桌面图标已创建(符号链接): $ddir/elsky-test-client.desktop"
        elif cp /usr/share/applications/elsky-test-client.desktop \
                "$ddir/elsky-test-client.desktop" 2>/dev/null; then
            chown "$uname_" "$ddir/elsky-test-client.desktop" 2>/dev/null
            chmod 755 "$ddir/elsky-test-client.desktop"
            echo "  ✓ 桌面图标已创建(实体副本): $ddir/elsky-test-client.desktop"
        fi
        # 独立的「卸载研盛测试客户端」桌面入口：启动器原生「卸载」不可靠，
        # 这个图标一定会被 dde-launcher 枚举，双击即可卸载（会开终端显示结果）。
        rm -f "$ddir/elsky-test-client-uninstall.desktop" 2>/dev/null
        if ln -s /usr/share/applications/elsky-test-client-uninstall.desktop \
                "$ddir/elsky-test-client-uninstall.desktop" 2>/dev/null; then
            chown -h "$uname_" "$ddir/elsky-test-client-uninstall.desktop" 2>/dev/null
            echo "  ✓ 桌面卸载入口已创建: $ddir/elsky-test-client-uninstall.desktop"
        elif cp /usr/share/applications/elsky-test-client-uninstall.desktop \
                "$ddir/elsky-test-client-uninstall.desktop" 2>/dev/null; then
            chown "$uname_" "$ddir/elsky-test-client-uninstall.desktop" 2>/dev/null
            chmod 755 "$ddir/elsky-test-client-uninstall.desktop"
        fi
    fi
}
ICON_PLACED=0
while IFS=: read -r f_name f_pw f_uid f_gid f_gecos f_home f_shell; do
    # 只要真实登录用户：uid>=1000、有家目录、非 nologin/false shell
    if [ "$f_uid" -ge 1000 ] 2>/dev/null && [ -d "$f_home" ] \
        && case "$f_shell" in */nologin|*/false|"") false;; *) true;; esac; then
        place_desktop_icon "$f_name" "$f_home" && ICON_PLACED=$((ICON_PLACED+1))
    fi
done < <(getent passwd 2>/dev/null)
if [ "$ICON_PLACED" -eq 0 ]; then
    echo "  ℹ 未找到任何真实用户的桌面目录，可从应用菜单启动"
fi

# 3.6 安装时依赖自检（只检查并报告，不在 dpkg 锁内嵌套安装）
APP_DIR="/usr/lib/elsky-test-client"
if [ -x "$APP_DIR/ELSKYTestClient" ]; then
    SYS_GLIBC=$(ldd --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+$' | head -1)
    # 第1级：主程序本身与本系统 glibc（致命级，装了也跑不起来）
    BIN_GLIBC_BAD=$(ldd "$APP_DIR/ELSKYTestClient" 2>/dev/null | grep 'version .GLIBC_' | head -3)
    if [ -n "$BIN_GLIBC_BAD" ]; then
        echo "  ✗✗ [致命] 主程序与本系统 glibc(${SYS_GLIBC:-未知}) 不兼容，本安装包无法在本系统运行:"
        echo "$BIN_GLIBC_BAD" | sed 's/^/      /'
        echo "      根因: 安装包在更高 glibc 的系统上编译（glibc 无法向下降级）。"
        echo "      解决: 请改用与本系统相同版本（如统信1070/麒麟V10=2.28）编译机产出的安装包。"
    else
        # 第2级：仅包内库不兼容（警告级，启动脚本会自动回退系统库运行）
        LIB_GLIBC_BAD=$(LD_LIBRARY_PATH="$APP_DIR/lib:$APP_DIR/lib/gl-fallback" ldd "$APP_DIR/ELSKYTestClient" 2>/dev/null \
            | grep 'version .GLIBC_' | head -3)
        if [ -n "$LIB_GLIBC_BAD" ]; then
            echo "  ⚠ 包内 lib/ 与本系统 glibc(${SYS_GLIBC:-未知}) 不匹配（包与系统版本不一致），"
            echo "      启动时会自动改用系统库运行；建议用与本机相同系统的编译机重新打包。"
        fi
    fi
    MISSING_DEPS=$(LD_LIBRARY_PATH="$APP_DIR/lib:$APP_DIR/lib/gl-fallback" ldd "$APP_DIR/ELSKYTestClient" 2>/dev/null \
        | grep "not found" | grep -v 'version .GLIBC_' | awk '{print $1}' | sort -u | head -5)
    if [ -z "$MISSING_DEPS" ]; then
        echo "  ✓ 依赖自检通过：打包的 Qt 库完整可用"
    else
        echo "  ⚠ 依赖自检发现缺库（首次启动时会自动尝试离线/在线修复）:"
        echo "$MISSING_DEPS" | sed 's/^/      /'
    fi
    # 检查 Qt 平台插件
    if [ -f "$APP_DIR/lib/platforms/libqxcb.so" ]; then
        echo "  ✓ Qt xcb 平台插件已就位"
    else
        echo "  ⚠ Qt xcb 平台插件缺失（GUI 可能无法启动）"
    fi
fi

# 3.7 ethtool 兜底安装（WOL 网络唤醒功能依赖）
# 系统缺失时从包内 extra-bin/ 复制到 /usr/local/bin（不覆盖系统已有版本）
# ★ 记录标记文件，卸载(postrm)时据此清理我们装的 /usr/local/bin/ethtool，
#   避免误删用户自行安装的版本
if ! command -v ethtool >/dev/null 2>&1; then
    if [ -x "/usr/lib/elsky-test-client/extra-bin/ethtool" ]; then
        cp "/usr/lib/elsky-test-client/extra-bin/ethtool" /usr/local/bin/ethtool 2>/dev/null
        chmod 755 /usr/local/bin/ethtool 2>/dev/null
        if command -v ethtool >/dev/null 2>&1; then
            mkdir -p /var/lib/elsky-test-client 2>/dev/null
            echo "installed-by-elsky-postinst" > /var/lib/elsky-test-client/ethtool.marker 2>/dev/null
            echo "  ✓ 系统缺少 ethtool，已自动安装（WOL 网络唤醒功能可用）"
        fi
    else
        echo "  ℹ 系统缺少 ethtool 且包内未携带（WOL 设置功能需联网安装: sudo apt install ethtool）"
    fi
fi

# 3.7b glmark2 兜底安装（老化测试显卡压测项依赖，2026-09-15 新增；
#                          2026-11-04 重构：数据/二进制分离安装 + 破损现场自动修复）
# 背景：glmark2 的场景数据(shader/模型/纹理)路径编译期硬编码为 /usr/share/glmark2，
#       二进制与数据必须配套。飞腾 D2000 现场出现过"二进制在 /usr/local/bin、
#       /usr/share/glmark2 数据丢失"的破损状态 → 窗口全黑但 FPS 照常输出。
#       旧逻辑的缺陷：只要系统已有任一 glmark2 二进制就整体跳过 → 数据永远修不上。
# 本次修复(三级保障)：
#   A) 数据修复(独立于二进制执行)：包内带数据 且 系统 /usr/share/glmark2 缺失或不
#      完整(shaders/models/textures 任一缺失/为空) → 逐子目录补齐，不覆盖已有文件。
#      修复后即使机器上残留"以前手工编译的旧版 glmark2"也能找到数据正常出画面。
#   B) 二进制安装(按变体查缺)：系统缺哪个变体(glmark2 / glmark2-es2 / wayland)
#      就补装哪个，不覆盖系统已有版本。
#   C) 标记文件仅在确实安装过内容时写入，卸载(postrm)据此清理，防误删用户自装版本。
GLMARK_PKG_ROOT="/usr/lib/elsky-test-client/extra-bin/glmark2"
glmark_data_complete() {
    [ -d /usr/share/glmark2 ] || return 1
    local sub
    for sub in shaders models textures; do
        if [ ! -d "/usr/share/glmark2/$sub" ] \
           || [ -z "$(ls -A "/usr/share/glmark2/$sub" 2>/dev/null)" ]; then
            return 1
        fi
    done
    # [2026-11-06 加固] 只查"目录在且非空"会把半残目录误判为完整而跳过补齐:
    # 现场系统 shaders 目录在、但 light-basic.vert / desktop.vert 缺失 → 全黑。
    local _nv
    _nv=$(find /usr/share/glmark2/shaders -maxdepth 1 -type f -name '*.vert' 2>/dev/null | wc -l)
    [ "$_nv" -ge 5 ] || return 1
    return 0
}
GLMARK_PKG_BINS=$(find "$GLMARK_PKG_ROOT" -type f -name 'glmark2*' -perm -u+x 2>/dev/null)
# [2026-11-06] 不再用 find|head -1 猜数据目录(可能命中旧布局的不完整副本),
# 按确定优先级逐个校验,取第一个真正成套的 shaders/models/textures 目录。
GLMARK_PKG_SHARE=""
for _cand in "$GLMARK_PKG_ROOT/usr/share/glmark2" \
             "$GLMARK_PKG_ROOT/share/glmark2" \
             "$GLMARK_PKG_ROOT/usr/local/share/glmark2"; do
    if [ -d "$_cand/shaders" ] && [ -d "$_cand/models" ] \
       && [ -d "$_cand/textures" ]; then
        GLMARK_PKG_SHARE="$_cand"
        break
    fi
done
if [ -z "$GLMARK_PKG_SHARE" ]; then
    GLMARK_PKG_SHARE=$(find "$GLMARK_PKG_ROOT" -type d -name glmark2 -path '*/share/*' 2>/dev/null | head -1)
fi
GLMARK_INSTALLED_ANY=""

# A) 数据修复/补齐（最优先：能救活已黑屏的 glmark2，含历史手工编译残留）
if [ -n "$GLMARK_PKG_SHARE" ] && ! glmark_data_complete; then
    echo "  ℹ 检测到 /usr/share/glmark2 缺失或不完整，从包内补齐场景数据..."
    mkdir -p /usr/share/glmark2 2>/dev/null
    _ds=""
    for _ds in shaders models textures; do
        # [2026-11-06] 数据不完整时整体替换三个子目录。旧逻辑仅在"目标缺失
        # 或为空"时才复制,半残目录(非空但缺 light-basic.vert 等)会被跳过,
        # 补齐后 glmark2 依旧全黑 —— 这正是现场黑屏没被 postinst 修好的原因。
        if [ -d "$GLMARK_PKG_SHARE/$_ds" ] \
           && [ -n "$(ls -A "$GLMARK_PKG_SHARE/$_ds" 2>/dev/null)" ]; then
            rm -rf "/usr/share/glmark2/$_ds" 2>/dev/null
            cp -r "$GLMARK_PKG_SHARE/$_ds" "/usr/share/glmark2/" 2>/dev/null
            GLMARK_INSTALLED_ANY="1"
        fi
    done
    if glmark_data_complete; then
        echo "  ✓ 已补齐 glmark2 场景数据 /usr/share/glmark2（shader/模型/纹理）"
        echo "    （修复'窗口全黑但有 FPS'的数据缺失问题；对不支持 --data-path 的"
        echo "      老版 glmark2(2014.03)同样有效，因其只读这个编译期硬编码路径）"
    else
        echo "  ⚠ glmark2 数据补齐后仍不完整（包内数据可疑），glmark2 压测可能黑屏"
    fi
elif [ -z "$GLMARK_PKG_SHARE" ] && [ -n "$GLMARK_PKG_BINS" ]; then
    echo "  ⚠ 包内有 glmark2 二进制但未找到成套场景数据，目标机压测将黑屏"
    echo "    处理：按打包脚本提示放置 offline_debs/glmark2-data*.deb 或 offline_src/ 源码包"
fi

# B) 二进制安装：按变体查缺补装（不覆盖系统已有同名变体）
if [ -n "$GLMARK_PKG_BINS" ]; then
    _gb=""
    _bn=""
    for _gb in $GLMARK_PKG_BINS; do
        _bn=$(basename "$_gb")
        if ! command -v "$_bn" >/dev/null 2>&1; then
            cp "$_gb" "/usr/local/bin/$_bn" 2>/dev/null
            chmod 755 "/usr/local/bin/$_bn" 2>/dev/null
            GLMARK_INSTALLED_ANY="1"
        fi
    done
fi

# C) 标记（仅在确实安装过内容时写）+ 就绪报告
if [ -n "$GLMARK_INSTALLED_ANY" ]; then
    mkdir -p /var/lib/elsky-test-client 2>/dev/null
    echo "installed-by-elsky-postinst" > /var/lib/elsky-test-client/glmark2.marker 2>/dev/null
fi
if command -v glmark2 >/dev/null 2>&1 || command -v glmark2-es2 >/dev/null 2>&1; then
    if glmark_data_complete; then
        echo "  ✓ glmark2 就绪（老化测试 3D+glmark2 合并压测项可用）"
    else
        echo "  ⚠ glmark2 二进制可用但 /usr/share/glmark2 数据缺失，glmark2 压测将黑屏"
        echo "    处理：联网机 apt-get download glmark2* 放入 offline_debs/ 重新打包安装"
    fi
elif [ -z "$GLMARK_PKG_BINS" ]; then
    echo "  ℹ 系统缺少 glmark2 且包内未携带（老化 glmark2 压测项将自动跳过，不影响其它项）"
fi

# 3.8 Python 离线依赖就绪性报告（开箱即用模式 + dpkg 兜底双形态）
PY_DEB_COUNT=$(ls -1 /usr/lib/elsky-test-client/python_modules/apt_packages/*.deb 2>/dev/null | wc -l)
if [ -d /usr/lib/elsky-test-client/python_modules/PyQt5 ] \
   && [ -d /usr/lib/elsky-test-client/python_modules/psutil ] \
   && [ -d /usr/lib/elsky-test-client/python_modules/OpenGL ]; then
    echo "  ✓ 老化测试依赖已按'开箱即用'形态随包（同发行版目标机零安装零密码零联网）"
elif [ "$PY_DEB_COUNT" -gt 0 ]; then
    echo "  ✓ 老化测试离线依赖已打包（$PY_DEB_COUNT 个 .deb，运行时自动安装，跨发行版兜底）"
else
    echo "  ⚠ 老化测试依赖未打包（首次运行需联网安装）"
fi

# 4. 如果是 USERNAME_PLACEHOLDER 状态，提示用户手动配置
if [ "$CURRENT_USER" = "USERNAME_PLACEHOLDER" ]; then
    echo ""
    echo "  ⚠ 未检测到当前用户，请手动配置免密权限："
    echo "    sudo sed -i 's/USERNAME_PLACEHOLDER/\$USER/' $SUDOERS_FILE"
fi

# [2026-11-11 三次修正] 包归属自检：仅确认 .desktop 已登记进 dpkg 数据库。
# 注意：dde-launcher 原生「卸载」= lastore D-Bus RemovePackage，即使本项通过
# 也可能点击无反应（属桌面环境限制）。自带独立卸载图标不依赖本项结果。
# 刷新桌面数据库，让「卸载研盛测试客户端」图标立刻出现在启动器里。
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi
if dpkg -S /usr/share/applications/elsky-test-client.desktop >/dev/null 2>&1; then
    echo "  ✓ 应用已注册到包管理器（dpkg -S 可反查本包归属）"
else
    echo "  ⚠ 未能在包管理器中定位 .desktop（不影响自带卸载入口；也可终端执行 sudo apt purge ${DPKG_MAINTSCRIPT_PACKAGE:-elsky-test-client}）"
fi

echo "=== 研盛测试客户端安装完成 ==="
echo "  桌面已生成'研盛测试客户端'图标，双击启动"
echo "  各功能已配置免密权限，无需重复输入密码"
echo "  卸载方式（推荐从上到下）："
echo "    1) 启动器 / 桌面 →「卸载研盛测试客户端」独立图标（双击，开终端显示结果）"
echo "    2) 桌面主图标右键 →「卸载研盛测试客户端」"
echo "    3) 终端执行 sudo apt purge elsky-test-client"
echo "  注意：启动器里对主图标右键弹出的「卸载」是 DDE 自带菜单，走应用商店"
echo "        (lastore) 链路，对 dpkg -i 手工安装的包常点击无反应，属桌面环境"
echo "        限制，本软件无法修复 —— 请改用上面的独立卸载图标。"
echo "  诊断：/usr/lib/elsky-test-client/diagnose_uninstall.sh（只读，输出定位信息）"
exit 0
EOF
chmod 755 "$DEBIAN_DIR/postinst"

# prerm：卸载前温和收尾（2026-11-11 新增）
# 统信/麒麟"右键卸载"走 apt remove。若主程序或老化测试 python 进程仍在运行，
# 占用包内文件会让卸载卡住或报"设备忙"。仅结束本软件自己的进程，
# 全程容错且必然 exit 0，绝不阻断卸载。
cat > "$DEBIAN_DIR/prerm" << 'EOF'
#!/bin/bash
set +e
pkill -TERM -f '/usr/lib/elsky-test-client/ELSKY_test_client' 2>/dev/null
pkill -TERM -f '/usr/lib/elsky-test-client/aging_test_gui.py' 2>/dev/null
sleep 1
exit 0
EOF
chmod 755 "$DEBIAN_DIR/prerm"

# postrm：卸载后全面清理
# 修复：旧版只删 sudoers，桌面图标/ethtool/用户配置/菜单缓存全部残留。
# 新版按用户要求"所有关联文件通通卸载干净"：
#   ① 所有真实用户桌面上的 elsky-test-client.desktop 与 -uninstall.desktop
#   ② /etc/sudoers.d/elsky-test-client 免密配置
#   ③ postinst 兜底安装的 /usr/local/bin/ethtool（有标记才删，防误删用户自装版本）
#   ④ 各用户 ~/.config/ELSKY 配置目录
#   ⑤ 菜单/图标缓存刷新 + /var/lib/elsky-test-client 标记目录
#   ⑥ 各用户 ~/.config/systemd/user/ 下的开机自启单元（auto_start_manager 创建，
#      不清理则卸载后 systemctl --user 仍尝试启动已删除的程序，journal 持续报错）
# remove 与 purge 都执行全量清理（本包无需要保留的配置）
cat > "$DEBIAN_DIR/postrm" << 'EOF'
#!/bin/bash
# 卸载后清理：所有关联文件（remove/purge 均全量清理）
set +e

echo "=== 清理研盛测试客户端关联文件 ==="

# 1. 删除所有真实用户桌面上的图标
while IFS=: read -r f_name f_pw f_uid f_gid f_gecos f_home f_shell; do
    if [ "$f_uid" -ge 1000 ] 2>/dev/null && [ -d "$f_home" ]; then
        for ddir in "$f_home/桌面" "$f_home/Desktop"; do
            # [2026-11-11] 桌面图标可能是符号链接；卸载时目标文件已被移除，
            # 单纯 [ -f ] 会返回 false 导致死链接残留，故同时判断 [ -L ]。
            for ico in elsky-test-client.desktop elsky-test-client-uninstall.desktop; do
                if [ -e "$ddir/$ico" ] || [ -L "$ddir/$ico" ]; then
                    rm -f "$ddir/$ico" 2>/dev/null
                    echo "  ✓ 已删除桌面图标: $ddir/$ico"
                fi
            done
        done
    fi
done < <(getent passwd 2>/dev/null)

# 2. 删除 sudoers 免密配置
SUDOERS_FILE="/etc/sudoers.d/elsky-test-client"
if [ -f "$SUDOERS_FILE" ]; then
    rm -f "$SUDOERS_FILE" 2>/dev/null
    echo "  ✓ 已删除免密配置: $SUDOERS_FILE"
fi

# 3. 删除 postinst 兜底安装的 ethtool（仅当标记文件存在，防误删用户自装版本）
if [ -f /var/lib/elsky-test-client/ethtool.marker ] && [ -f /usr/local/bin/ethtool ]; then
    rm -f /usr/local/bin/ethtool 2>/dev/null
    echo "  ✓ 已删除兜底安装的 /usr/local/bin/ethtool"
fi

# 3b. 删除 postinst 兜底安装的 glmark2（仅当标记文件存在，防误删用户自装版本）
if [ -f /var/lib/elsky-test-client/glmark2.marker ]; then
    for gbin in glmark2 glmark2-es2 glmark2-wayland glmark2-es2-wayland; do
        if [ -f "/usr/local/bin/$gbin" ]; then
            rm -f "/usr/local/bin/$gbin" 2>/dev/null
            echo "  ✓ 已删除兜底安装的 /usr/local/bin/$gbin"
        fi
    done
    # 场景数据目录（postinst 兜底复制时才创建；用户自装版本会被系统包管理器管理，
    # 此处仅在无系统包声明归属时删除——dpkg -S 查不到归属说明是本包复制的）
    if [ -d /usr/share/glmark2 ] && ! dpkg -S /usr/share/glmark2 >/dev/null 2>&1; then
        rm -rf /usr/share/glmark2 2>/dev/null
        echo "  ✓ 已删除兜底安装的 /usr/share/glmark2"
    fi
fi

# 4. 删除各用户的配置目录（老化测试配置等）+ 开机自启 systemd 用户单元
while IFS=: read -r f_name f_pw f_uid f_gid f_gecos f_home f_shell; do
    if [ "$f_uid" -ge 1000 ] 2>/dev/null && [ -d "$f_home" ]; then
        # 4a. 开机自启单元：unit 文件 + enable 生成的 wants 符号链接
        #     （auto_start_manager.cpp 创建 elsky-test-client.service；
        #     postrm 环境无法对各用户 session 执行 systemctl --user disable，直接删文件）
        if [ -f "$f_home/.config/systemd/user/elsky-test-client.service" ]; then
            rm -f "$f_home/.config/systemd/user/elsky-test-client.service" 2>/dev/null
            echo "  ✓ 已删除自启服务: $f_home/.config/systemd/user/elsky-test-client.service"
        fi
        for wants_dir in "$f_home"/.config/systemd/user/*.wants; do
            [ -d "$wants_dir" ] || continue
            if [ -e "$wants_dir/elsky-test-client.service" ] || [ -L "$wants_dir/elsky-test-client.service" ]; then
                rm -f "$wants_dir/elsky-test-client.service" 2>/dev/null
                echo "  ✓ 已删除自启链接: $wants_dir/elsky-test-client.service"
            fi
        done
        # 4b. ELSKY 配置目录（含 elsky-autostart.sh 自启脚本、老化测试配置）
        if [ -d "$f_home/.config/ELSKY" ]; then
            rm -rf "$f_home/.config/ELSKY" 2>/dev/null
            chown "$f_name" "$f_home/.config" 2>/dev/null
            echo "  ✓ 已删除用户配置: $f_home/.config/ELSKY"
        fi
    fi
done < <(getent passwd 2>/dev/null)

# 5. 清理标记目录
rm -rf /var/lib/elsky-test-client 2>/dev/null

# 6. 刷新菜单与图标缓存（让开始菜单条目立即消失）
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database /usr/share/applications 2>/dev/null
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache /usr/share/icons/hicolor 2>/dev/null
fi
# KDE 的 ksycoca 缓存
if command -v kbuildsycoca5 >/dev/null 2>&1; then
    kbuildsycoca5 --noincremental 2>/dev/null
fi

echo "=== 卸载清理完成 ==="
exit 0
EOF
chmod 755 "$DEBIAN_DIR/postrm"

# ---- 7.5 GLIBC 兼容性门禁 v2（2026-10-09：从"一刀切中止"升级为"分级处置"）----
# 背景：v1 门禁对任何 GLIBC 超标文件一律 exit 1。实际场景中编译机（glibc 2.31）的
# 系统库（libsystemd/libudev/libwebp/libxml2/libvpx 等）经依赖收集进入 lib/，
# 全部被 v1 拦停——但这类库恰恰【不该带】也【不必带】：
#   1) 包内 lib/ 是 RUNPATH 优先查找路径，带超标库 → 目标机优先加载必报
#      "version `GLIBC_2.xx' not found"，连回退系统库的机会都没有；
#   2) 剔除后目标机加载器自动回退到系统自带同名库（glibc 2.28 编译，必能加载）。
#      统信1070/麒麟V10 桌面版对这些基础库全部自带（systemd/udev 为系统底座）。
# 因此 v2 分级处置：
#   A. lib/ 下的超标收集库     → 自动剔除 + 汇总提示（目标机用系统版本，严格更优）
#   B. extra-bin/ 下可选产物   → 保留文件 + 警告（glmark2 等可选增强项超标则对应
#                                功能在目标机自动降级，不影响主程序）
#   C. 主程序/自编译产物超标   → 仍然 exit 1（真没救，必须换低 glibc 编译机重编）
# 源头拦截已在收集阶段完成（collect_deps_of/Qt 强制收集/Qt 插件复制三处调用
# lib_compat_ok），本门禁是最后防线 + 分级汇总。MIN_TARGET_GLIBC/ver_gt/
# elf_max_glibc/is_elf 已提前到依赖收集段定义。
BUILD_GLIBC=$(ldd --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+$' | head -1)

echo ""
echo "========================================="
echo "GLIBC 兼容性门禁（目标基线 glibc >= $MIN_TARGET_GLIBC）"
echo "编译机 glibc: ${BUILD_GLIBC:-未知}"
echo "========================================="
GLIBC_REPORT="$(mktemp /tmp/elsky_glibc.XXXXXX)"
while IFS= read -r -d '' f; do
    is_elf "$f" || continue
    maxv=$(elf_max_glibc "$f")
    [ -z "$maxv" ] && continue
    need="${maxv#GLIBC_}"
    if ver_gt "$need" "$MIN_TARGET_GLIBC"; then
        echo "$f|GLIBC_$need" >> "$GLIBC_REPORT"
    fi
done < <(find "$INSTALL_DIR" -type f \
    \( -name '*.so' -o -name '*.so.*' -o -name 'ELSKYTestClient' -o -path '*extra-bin*' \) -print0 2>/dev/null)

if [ -s "$GLIBC_REPORT" ]; then
    # 分级：A 类 lib/ 收集库（自动剔除）/ B 类 extra-bin 可选产物（警告保留）/ C 类核心产物（硬失败）
    GLIBC_REMOVED_LIST="$(mktemp /tmp/elsky_glibc_rm.XXXXXX)"
    GLIBC_WARN_LIST="$(mktemp /tmp/elsky_glibc_warn.XXXXXX)"
    GLIBC_FATAL_LIST="$(mktemp /tmp/elsky_glibc_fatal.XXXXXX)"
    while IFS='|' read -r f ver; do
        case "$f" in
            "$LIB_DIR"/*)
                # A 类：lib/ 下收集的编译机系统库 → 剔除，目标机回退系统自带版本
                rm -f "$f" 2>/dev/null
                echo "$ver  $(basename "$f")" >> "$GLIBC_REMOVED_LIST"
                ;;
            "$EXTRA_BIN_DIR"/*|*/extra-bin/*)
                # B 类：extra-bin 可选产物（glmark2 等）→ 警告保留，目标机对应功能自动降级
                echo "$ver  ${f#$INSTALL_DIR/}" >> "$GLIBC_WARN_LIST"
                ;;
            *)
                # C 类：主程序/其它核心产物 → 硬失败（无法自救）
                echo "$ver  ${f#$INSTALL_DIR/}" >> "$GLIBC_FATAL_LIST"
                ;;
        esac
    done < "$GLIBC_REPORT"

    # A 类汇总：已剔除
    if [ -s "$GLIBC_REMOVED_LIST" ]; then
        REMOVED_COUNT=$(wc -l < "$GLIBC_REMOVED_LIST")
        echo "  ⚠ 已自动剔除 $REMOVED_COUNT 个 GLIBC 超标的收集库（目标机将使用系统自带版本，功能不受影响）:"
        sort -Vu "$GLIBC_REMOVED_LIST" | head -20 | sed 's/^/      /'
        [ "$REMOVED_COUNT" -gt 20 ] && echo "      ...（其余 $((REMOVED_COUNT - 20)) 个见完整警告）"
        echo "    说明: 这些库是编译机(glibc ${BUILD_GLIBC:-?})的系统库，随依赖收集带入；"
        echo "          统信1070/麒麟V10 桌面版全部自带同名库，剔除后回退加载系统版本（更老但兼容）。"
        # 收集阶段源头拦截记录也一并提示
        if [ -s "$GLIBC_SKIP_LIST_FILE" ]; then
            SKIP_COUNT=$(sort -u "$GLIBC_SKIP_LIST_FILE" | grep -vc '^$' 2>/dev/null || echo 0)
            [ "$SKIP_COUNT" -gt 0 ] && echo "    另有 $SKIP_COUNT 个超标库在收集阶段已被拦截在包外。"
        fi
        echo ""
    fi
    # B 类汇总：警告保留
    if [ -s "$GLIBC_WARN_LIST" ]; then
        WARN_COUNT=$(wc -l < "$GLIBC_WARN_LIST")
        echo "  ⚠ $WARN_COUNT 个包内可选产物（extra-bin/）GLIBC 超标，保留打包但在目标机对应功能将自动降级:"
        sort -Vu "$GLIBC_WARN_LIST" | head -10 | sed 's/^/      /'
        echo ""
    fi
    # C 类：核心产物超标
    if [ -s "$GLIBC_FATAL_LIST" ]; then
        FATAL_COUNT=$(wc -l < "$GLIBC_FATAL_LIST")
        echo "  ✗ $FATAL_COUNT 个核心产物（主程序）要求的 GLIBC 高于默认目标基线 $MIN_TARGET_GLIBC:"
        sort -Vu "$GLIBC_FATAL_LIST" | head -15 | sed 's/^/      /'
        echo ""
        # [2026-10-16] 自动基线降级：直接运行（未显式指定 MIN_TARGET_GLIBC）时，
        # 主程序超标说明编译机 glibc 高于默认基线（如兆芯/麒麟2503 = glibc 2.31 编译）。
        # 此时包仍可在与编译机相同或更新 glibc 的系统上正常运行 → 自动以编译机
        # glibc 为基线继续打包，并醒目警告适用范围；不再强制 exit 1。
        # 统信1070/麒麟V10(glibc 2.28) 编译机上主程序永不超标，此分支不会触发，
        # 原打包链路零变化。显式指定 MIN_TARGET_GLIBC 时仍按显式值严格拦截。
        if [ -z "${MIN_TARGET_GLIBC_EXPLICIT:-}" ] && [ -n "$BUILD_GLIBC" ]; then
            echo "  ⚠⚠⚠ 自动基线降级 ⚠⚠⚠"
            echo "  主程序在本编译机(glibc $BUILD_GLIBC)上编译，其 GLIBC 需求不会超过编译机。"
            echo "  已自动以编译机 glibc($BUILD_GLIBC) 为目标基线继续打包。"
            echo ""
            echo "  *** 重要适用范围提示 ***"
            echo "  本包仅保证在与编译机相同或更新 glibc 的系统上运行"
            echo "  （编译机: $(grep -m1 PRETTY_NAME /etc/os-release 2>/dev/null | cut -d\" -f2), glibc $BUILD_GLIBC）。"
            echo "  如需部署到统信UOS 1070 / 麒麟V10 SP1(glibc 2.28)，请在这些系统上重新编译打包，"
            echo "  或显式指定基线让本脚本强制拦截: MIN_TARGET_GLIBC=2.28 ./cross_platform_package_deb.sh"
            echo ""
            echo "  主程序超标符号明细（前 10 个）:"
            _FATAL_ELF=$(head -1 "$GLIBC_FATAL_LIST" | awk '{print $1}')
            if [ -n "$_FATAL_ELF" ] && [ -f "$_FATAL_ELF" ] && command -v objdump >/dev/null 2>&1; then
                objdump -T "$_FATAL_ELF" 2>/dev/null | grep -oE 'GLIBC_2\.(2[89]|3[0-9])[0-9.]*' | sort -Vu | head -10 | sed 's/^/      /'
            fi
            echo ""
        else
            # 显式指定了基线仍超标（或无法检测编译机 glibc）→ 保持硬失败保护
            echo "  根因: 编译机 glibc(${BUILD_GLIBC:-未知}) 高于显式指定的目标基线 $MIN_TARGET_GLIBC。"
            echo "  glibc 无法随包携带/降级，主程序在低于编译机 glibc 的目标机上必报 \"version 'GLIBC_x.xx' not found\"。"
            echo "  修复方法（三选一）:"
            echo "    1)【推荐】在与目标机相同或更低 glibc 的系统上重新编译+打包:"
            echo "       统信UOS 1070 / 银河麒麟V10 SP1 / Debian10（glibc 2.28）"
            echo "    2) 目标机与编译机同款系统（如麒麟2503 glibc ${BUILD_GLIBC:-未知}）时，一键以编译机为基线:"
            echo "       MIN_TARGET_GLIBC=host ./cross_platform_package_deb.sh"
            echo "    3) 确认目标机基线确实更高时，显式指定基线重新打包:"
            echo "       例: 目标为 Ubuntu22.04(glibc 2.35): MIN_TARGET_GLIBC=2.35 ./cross_platform_package_deb.sh"
            if [ "${ALLOW_INCOMPATIBLE_GLIBC:-0}" = "1" ]; then
                echo "  （ALLOW_INCOMPATIBLE_GLIBC=1，已知不兼容仍继续打包——目标机主程序将无法运行！）"
            else
                rm -f "$GLIBC_REPORT" "$GLIBC_REMOVED_LIST" "$GLIBC_WARN_LIST" "$GLIBC_FATAL_LIST" "$GLIBC_SKIP_LIST_FILE"
                exit 1
            fi
        fi
    elif [ ! -s "$GLIBC_REMOVED_LIST" ] && [ ! -s "$GLIBC_WARN_LIST" ]; then
        echo "  ✓ 包内所有 ELF 的 GLIBC 依赖均 <= $MIN_TARGET_GLIBC（目标机可正常加载）"
    fi
    rm -f "$GLIBC_REPORT" "$GLIBC_REMOVED_LIST" "$GLIBC_WARN_LIST" "$GLIBC_FATAL_LIST"
else
    if [ -s "$GLIBC_SKIP_LIST_FILE" ]; then
        SKIP_COUNT=$(sort -u "$GLIBC_SKIP_LIST_FILE" | grep -vc '^$' 2>/dev/null || echo 0)
        if [ "$SKIP_COUNT" -gt 0 ]; then
            echo "  ✓ 包内所有 ELF 的 GLIBC 依赖均 <= $MIN_TARGET_GLIBC（目标机可正常加载）"
            echo "  ℹ 收集阶段已源头拦截 $SKIP_COUNT 个 GLIBC 超标的编译机系统库（目标机使用系统自带版本）:"
            sort -u "$GLIBC_SKIP_LIST_FILE" | head -20 | sed 's/^/      /'
        else
            echo "  ✓ 包内所有 ELF 的 GLIBC 依赖均 <= $MIN_TARGET_GLIBC（目标机可正常加载）"
        fi
    else
        echo "  ✓ 包内所有 ELF 的 GLIBC 依赖均 <= $MIN_TARGET_GLIBC（目标机可正常加载）"
    fi
fi
rm -f "$GLIBC_SKIP_LIST_FILE"

# ---- 7.6 GLIBCXX/CXXABI 包内自洽门禁（S3：libstdc++ 已随包，验证包内自给自足）----
# 原理：libstdc++.so.6 已随包携带（is_system_lib 不再排除），目标机系统
# libstdc++ 版本高低不再影响启动——RUNPATH/LD_LIBRARY_PATH 优先命中包内副本。
# 但必须保证：包内全部 ELF 声明的 GLIBCXX_/CXXABI_ 需求 <= 包内 libstdc++
# 自身提供的上限，否则目标机必报 "version `GLIBCXX_3.4.xx' not found"。
# 与 GLIBC 门禁的区别：GLIBC 对的是"目标机系统能力"（无法随包），
# 本门禁对的是"包内自洽"（不依赖目标机任何东西）。
echo ""
echo "========================================="
echo "GLIBCXX/CXXABI 包内自洽检查"
echo "========================================="
LIBSTDCXX_PACKED=$(ls -1 "$LIB_DIR"/libstdc++.so.6* 2>/dev/null | head -1)
if [ -z "$LIBSTDCXX_PACKED" ] || [ ! -f "$LIBSTDCXX_PACKED" ]; then
    # libstdc++ 未随包属于异常（is_system_lib 已放行，ldd 收集应已带走），
    # 目标机将退回依赖系统 libstdc++，统信1070 上大概率无法启动——按致命处理。
    echo "  ✗ [致命] lib/ 下未找到 libstdc++.so.6——libstdc++ 未随包！"
    echo "    目标机将依赖系统 libstdc++，若其 GLIBCXX 低于编译机（统信1070=3.4.24），"
    echo "    程序必报 \"version 'GLIBCXX_3.4.xx' not found\" 无法启动。"
    echo "    请检查 is_system_lib 是否误排除 libstdc++，或编译机 libstdc++ 路径异常。"
    if [ "${ALLOW_INCOMPATIBLE_GLIBC:-0}" != "1" ]; then
        exit 1
    fi
else
    # 包内 libstdc++ 能提供的最高的 GLIBCXX/CXXABI（排除 *UND* 未定义行）
    CXX_PROVIDE_GLIBCXX=$(objdump -T "$LIBSTDCXX_PACKED" 2>/dev/null | grep -v '\*UND\*' \
        | grep -oE 'GLIBCXX_[0-9]+(\.[0-9]+)+' | sed 's/^GLIBCXX_//' | sort -Vu | tail -1)
    CXX_PROVIDE_CXXABI=$(objdump -T "$LIBSTDCXX_PACKED" 2>/dev/null | grep -v '\*UND\*' \
        | grep -oE 'CXXABI_[0-9]+(\.[0-9]+)+' | sed 's/^CXXABI_//' | sort -Vu | tail -1)
    echo "  随包 libstdc++: $(basename "$LIBSTDCXX_PACKED")"
    echo "  包内提供上限: GLIBCXX_${CXX_PROVIDE_GLIBCXX:-未知} / CXXABI_${CXX_PROVIDE_CXXABI:-未知}"

    CXX_REPORT="$(mktemp /tmp/elsky_cxx.XXXXXX)"
    while IFS= read -r -d '' f; do
        is_elf "$f" || continue
        [ "$f" = "$LIBSTDCXX_PACKED" ] && continue   # libstdc++ 自身跳过
        # 只取 *UND*（未定义=对外需求）行的符号版本；定义行是该 ELF 自身提供能力
        # 注意：objdump -T 的未定义标记是 *UND*（带星号），grep ' UND ' 匹配不到
        need_gxx=$(objdump -T "$f" 2>/dev/null | grep '\*UND\*' \
            | grep -oE 'GLIBCXX_[0-9]+(\.[0-9]+)+' | sed 's/^GLIBCXX_//' | sort -Vu | tail -1)
        need_cabi=$(objdump -T "$f" 2>/dev/null | grep '\*UND\*' \
            | grep -oE 'CXXABI_[0-9]+(\.[0-9]+)+' | sed 's/^CXXABI_//' | sort -Vu | tail -1)
        if [ -n "$need_gxx" ] && [ -n "$CXX_PROVIDE_GLIBCXX" ] \
           && ver_gt "$need_gxx" "$CXX_PROVIDE_GLIBCXX"; then
            echo "GLIBCXX_$need_gxx > 包内提供 GLIBCXX_$CXX_PROVIDE_GLIBCXX  ${f#$INSTALL_DIR/}" >> "$CXX_REPORT"
        fi
        if [ -n "$need_cabi" ] && [ -n "$CXX_PROVIDE_CXXABI" ] \
           && ver_gt "$need_cabi" "$CXX_PROVIDE_CXXABI"; then
            echo "CXXABI_$need_cabi > 包内提供 CXXABI_$CXX_PROVIDE_CXXABI  ${f#$INSTALL_DIR/}" >> "$CXX_REPORT"
        fi
    done < <(find "$INSTALL_DIR" -type f \
        \( -name '*.so' -o -name '*.so.*' -o -name 'ELSKYTestClient' -o -path '*extra-bin*' \) -print0 2>/dev/null)

    if [ -s "$CXX_REPORT" ]; then
        CXX_OFFENDER_COUNT=$(wc -l < "$CXX_REPORT")
        echo "  ✗ 检测到 $CXX_OFFENDER_COUNT 个文件的 GLIBCXX/CXXABI 需求超过包内 libstdc++ 提供能力:"
        sort -Vr "$CXX_REPORT" | head -15 | sed 's/^/      /'
        echo ""
        echo "  根因: 这些文件编译时链接的 libstdc++ 比随包副本更新（编译环境不一致）。"
        echo "  修复方法: 用同一套 g++/libstdc++ 重新编译全部组件后重新打包。"
        if [ "${ALLOW_INCOMPATIBLE_GLIBC:-0}" = "1" ]; then
            echo "  （ALLOW_INCOMPATIBLE_GLIBC=1，已知不自洽仍继续打包——目标机可能无法运行！）"
        else
            rm -f "$CXX_REPORT"
            exit 1
        fi
    else
        echo "  ✓ 包内全部 ELF 的 GLIBCXX/CXXABI 需求 <= 随包 libstdc++ 提供上限（包内自洽）"
    fi
    rm -f "$CXX_REPORT"
fi

# ---- 8. 打包前自检：离线依赖完整性验证 ----
# 模拟目标环境（LD_LIBRARY_PATH 指向 lib/，忽略系统 Qt），
# 对主程序和所有插件跑 ldd，发现 not found 则收集报告。
# 任何缺失都会导致目标机功能不可用（无网络无法补救），必须在此拦截。
echo ""
echo "自检：离线依赖完整性验证..."
SELF_CHECK_REPORT="$(mktemp /tmp/elsky_selfcheck.XXXXXX)"
FATAL_PROBLEM=false

# 硬性检查1：主程序需要的每一个 Qt 库都必须在 lib/ 中
echo "  检查主程序 Qt 依赖是否全部打包..."
REQUIRED_QT_LIBS=$(ldd "$INSTALL_DIR/ELSKYTestClient" 2>/dev/null \
    | awk '{print $1}' | grep '^libQt5' | sort -u)
if [ -n "$REQUIRED_QT_LIBS" ]; then
    for lib in $REQUIRED_QT_LIBS; do
        if [ ! -e "$LIB_DIR/$lib" ]; then
            echo "  ✗ [致命] 主程序依赖的 $lib 未打包进 lib/！目标机将无法启动" >> "$SELF_CHECK_REPORT"
            FATAL_PROBLEM=true
        fi
    done
    echo "    主程序 Qt 依赖: $(echo $REQUIRED_QT_LIBS | tr '\n' ' ')"
else
    echo "  ✗ [致命] ldd 无法解析主程序依赖（编译产物损坏？）" >> "$SELF_CHECK_REPORT"
    FATAL_PROBLEM=true
fi

# 硬性检查2：Qt xcb 平台插件必须存在（GUI 启动的必要条件）
if [ ! -f "$LIB_DIR/platforms/libqxcb.so" ]; then
    echo "  ✗ [致命] Qt 平台插件缺失: lib/platforms/libqxcb.so" >> "$SELF_CHECK_REPORT"
    echo "    （GUI 双击启动的必要条件，缺失则海光统信等目标机无法启动）" >> "$SELF_CHECK_REPORT"
    FATAL_PROBLEM=true
else
    echo "  ✓ lib/platforms/libqxcb.so 已就位"
fi

# 硬性检查3：ethtool（WOL 功能，警告级——缺失仅影响 WOL 不影响其它功能）
if [ -f "$EXTRA_BIN_DIR/ethtool" ]; then
    echo "  ✓ extra-bin/ethtool 已就位（WOL 功能离线可用）"
else
    echo "  ⚠ ethtool 未打包（仅影响无 ethtool 目标机上的 WOL 设置功能）"
fi

# 硬性检查3b：glmark2（老化显卡压测项，警告级——缺失该项自动降级跳过）
# 场景数据（shader/纹理）与二进制同等重要：二进制在但 /share/glmark2 缺失
# → glmark2 能启动但所有场景报错，等同不可用 → 一并检查
GLMARK_SELF_BIN=$(find "$EXTRA_BIN_DIR/glmark2" -type f -name 'glmark2*' -perm -u+x 2>/dev/null | head -1)
GLMARK_SELF_SHARE=$(find "$EXTRA_BIN_DIR/glmark2" -type d -name glmark2 -path '*/share/*' 2>/dev/null | head -1)
if [ -n "$GLMARK_SELF_BIN" ] && [ -n "$GLMARK_SELF_SHARE" ]; then
    echo "  ✓ extra-bin/glmark2 已就位（老化测试显卡压测项离线可用）"
elif [ -n "$GLMARK_SELF_BIN" ]; then
    echo "  ⚠ glmark2 二进制已打包但场景数据缺失（postinst 无法兜底安装数据，该项将降级）" | tee -a "$SELF_CHECK_REPORT"
else
    echo "  ⚠ glmark2 未打包（老化测试 glmark2 压测项在目标机将自动跳过，不影响其它项）"
fi

# 硬性检查4：Python 离线依赖（4.5 节已硬拦截，此处复核状态写入报告）
PY_DEB_TOTAL=$(ls -1 "$APT_PKG_DIR"/*.deb 2>/dev/null | wc -l)
echo "  Python 离线依赖: $PY_DEB_TOTAL 个 .deb（apt_packages/）"

# 硬性检查5：★老化测试"开箱即用"完整性★
# 用户硬性要求：安装 deb 后除连接服务器/网络唤醒外全部功能离线可用。
# （网络测速已改为内网双机 TCP 对测，端口 18964/18965，零外部工具依赖，离线可用）
# PyQt5/psutil/OpenGL 三件套必须以解包形态就位，否则同发行版全新系统上
# 首次运行老化测试会走"密码→联网→失败弹框"旧路径。缺一即中止打包。
echo "  检查老化测试开箱即用完整性（PyQt5/psutil/OpenGL 解包形态）..."
OOO_MISSING=""
for pkgdir in PyQt5 psutil OpenGL; do
    if [ ! -d "$PY_MODULES_DIR/$pkgdir" ]; then
        OOO_MISSING="$OOO_MISSING $pkgdir"
    fi
done
if [ -n "$OOO_MISSING" ]; then
    echo "  ✗ [致命] 开箱即用依赖缺失:$OOO_MISSING —— 全新系统首次运行老化测试将需要密码/联网！" >> "$SELF_CHECK_REPORT"
    FATAL_PROBLEM=true
else
    # 进一步验证 PyQt5 核心模块 .so 存在（防解包不完整）
    PYQT5_CORE_SO=$(find "$PY_MODULES_DIR/PyQt5" -name 'QtCore*.so' 2>/dev/null | head -1)
    if [ -z "$PYQT5_CORE_SO" ]; then
        echo "  ✗ [致命] PyQt5 解包不完整（找不到 QtCore .so）" >> "$SELF_CHECK_REPORT"
        FATAL_PROBLEM=true
    else
        echo "  ✓ 老化测试依赖开箱即用（PyQt5/psutil/OpenGL 已解包就位）"
        # 隔离环境验证 PyQt5 .so 能否从包内 lib/ 解析全部依赖
        # （apt 版 PyQt5 链接系统 libQt5*，包内 lib/ 必须能提供同版本库）
        PYQT5_MISSING=$(LD_LIBRARY_PATH="$LIB_DIR:$LIB_DIR/gl-fallback" ldd "$PYQT5_CORE_SO" 2>/dev/null \
            | grep "not found" | awk '{print $1}' | sort -u)
        if [ -n "$PYQT5_MISSING" ]; then
            echo "  ✗ [致命] 包内 PyQt5 缺库: $PYQT5_MISSING —— 老化测试开箱即用会失败！" >> "$SELF_CHECK_REPORT"
            FATAL_PROBLEM=true
        else
            echo "  ✓ 包内 PyQt5 依赖在 lib/ 下全解析（开箱即用运行时可用）"
        fi
    fi
fi

# 用隔离环境检查单个 ELF：LD_LIBRARY_PATH 只指向打包 lib/
check_elf_deps() {
    local elf="$1"
    [ -f "$elf" ] || return 0
    local missing
    missing=$(LD_LIBRARY_PATH="$LIB_DIR:$LIB_DIR/gl-fallback" ldd "$elf" 2>/dev/null | grep "not found" | awk '{print $1}' | sort -u)
    if [ -n "$missing" ]; then
        {
            echo "  ✗ [缺库] $(basename "$elf") 缺少:"
            echo "$missing" | sed 's/^/      /'
        } >> "$SELF_CHECK_REPORT"
    fi
}

# 检查主程序
check_elf_deps "$INSTALL_DIR/ELSKYTestClient"

# 检查所有打包的库和插件（用 for + 进程替换避免管道子 shell）
while IFS= read -r -d '' so; do
    check_elf_deps "$so"
done < <(find "$LIB_DIR" -name '*.so*' -type f -print0 2>/dev/null)

if [ ! -s "$SELF_CHECK_REPORT" ]; then
    echo "  ✓ 自检通过：所有依赖在离线环境下均可解析"
else
    echo ""
    echo "  ⚠ 自检发现缺库！以下库未打包，目标机（无网络）将无法使用对应功能。"
    cat "$SELF_CHECK_REPORT"
    echo ""
    if [ "$FATAL_PROBLEM" = true ]; then
        echo "  ✗✗✗ 存在致命缺失，禁止打包！（否则目标机安装后无法启动）✗✗✗"
        echo "  修复方法:"
        echo "    1) 确认编译机已安装完整 Qt 运行库: sudo apt install libqt5core5a libqt5gui5 libqt5widgets5 libqt5network5 libqt5serialport5 qtbase5-dev"
        echo "    2) 确认编译用 qmake 可用: qmake -v && qmake -query QT_INSTALL_LIBS"
        echo "    3) 修复后重新运行本打包脚本"
        rm -f "$SELF_CHECK_REPORT"
        exit 1
    fi
    echo "  常见原因：编译机未安装对应开发/运行库。"
    echo "  建议：在编译机安装缺失库后重新打包，例如："
    echo "    sudo apt-get install libgl1-mesa-dev libegl1-mesa-dev libxcb-xinerama0-dev ..."
fi
rm -f "$SELF_CHECK_REPORT"

# ---- 8.5 生成依赖矩阵清单（随 deb 发布，供安装后核对离线完整性）----
echo ""
echo "生成依赖矩阵清单 DEPENDENCY_CHECK.txt ..."
QT_LIB_COUNT=$(find "$LIB_DIR" -maxdepth 1 -name 'libQt5*.so*' | wc -l)
TOTAL_LIB_COUNT=$(find "$LIB_DIR" -name '*.so*' -type f | wc -l)
PLUGIN_DIRS=$(ls -d "$LIB_DIR"/*/ 2>/dev/null | xargs -n1 basename 2>/dev/null | tr '\n' ' ')
PY_MAIN_DEBS=$(ls -1 "$APT_PKG_DIR"/*.deb 2>/dev/null | xargs -n1 basename 2>/dev/null | grep -E '^python3-(pyqt5|psutil|opengl)' | tr '\n' ' ')
{
    echo "=============================================="
    echo " 研盛测试客户端 离线依赖矩阵清单"
    echo "=============================================="
    echo "生成时间:   $(date '+%Y-%m-%d %H:%M:%S')"
    echo "目标架构:   $ARCH ($PLATFORM_NAME) -> $DEB_ARCH"
    echo "编译 Qt:    ${QT_VER_FROM_QMAKE:-未知} ($QT_LIB_FROM_QMAKE)"
    echo ""
    echo "【功能 → 依赖 → 打包位置】"
    echo "----------------------------------------------"
    echo "GUI 主程序:"
    echo "  Qt5 运行库       $QT_LIB_COUNT 个        lib/"
    echo "  Qt 平台插件      ${PLUGIN_DIRS:-无}          lib/"
    echo "  xcb/X11/GL 图形栈  已全量收集          lib/"
    echo "  动态库总计        $TOTAL_LIB_COUNT 个文件  lib/"
    echo ""
    echo "老化测试(Python):"
    echo "  python3          依赖目标机预装（桌面系统标配）"
    echo "  PyQt5/psutil 等  $PY_DEB_TOTAL 个 .deb    python_modules/apt_packages/"
    echo "  （主包: $PY_MAIN_DEBS）"
    echo "  首次运行老化测试时自动离线 dpkg -i 安装，无需联网"
    echo ""
    echo "WOL 网络唤醒:"
    if [ -f "$EXTRA_BIN_DIR/ethtool" ]; then
        echo "  ethtool          已打包              extra-bin/（系统缺失时 postinst 自动装到 /usr/local/bin）"
    else
        echo "  ethtool          未打包（系统缺失时 WOL 需联网安装）"
    fi
    echo "  nmcli/ip         依赖目标机预装（NetworkManager/iproute2 标配）"
    echo ""
    echo "电源管理/开机自启/非法关机检测:"
    echo "  systemctl/journalctl/last/sync  依赖目标机预装（systemd/util-linux/coreutils 标配）"
    echo ""
    echo "提权:"
    echo "  pkexec(policykit-1)/sudo        deb Depends 已声明，桌面系统标配"
    echo ""
    echo "跑分测试:"
    echo "  glmark2/glxgears/stream/iozone/fio  可选外部工具，缺失时自动回退内置同源算法（不影响离线运行）"
    echo ""
    echo "【不打包项（按设计）】"
    echo "  glibc/gcc 运行时: 版本绑定编译机内核接口，目标机必有"
    echo ""
    echo "【离线自愈机制】"
    echo "  1. 启动脚本: 缺库时 离线deb→在线apt→ld.so 三级自愈"
    echo "  2. qt.conf: 即使环境变量被清空也能定位 lib/ 插件"
    echo "  3. 老化测试: 检测模块缺失时自动 dpkg -i 离线包"
    echo "=============================================="
} > "$INSTALL_DIR/DEPENDENCY_CHECK.txt"
echo "  ✓ 已生成: $INSTALL_DIR/DEPENDENCY_CHECK.txt"
echo "    （安装后可查看: cat /usr/lib/elsky-test-client/DEPENDENCY_CHECK.txt）"

# ---- 9. 打包 ----
echo ""
echo "开始打包 deb..."
RELEASE_DIR="$PROJECT_ROOT/release"
mkdir -p "$RELEASE_DIR"
DEB_FILE="$RELEASE_DIR/${PKG_NAME}_${PKG_VERSION}_${DEB_ARCH}.deb"

dpkg-deb --build --root-owner-group "$PACKAGE_DIR" "$DEB_FILE"
DPKG_RC=$?

echo ""
echo "=== deb 打包完成 ==="
if [ $DPKG_RC -ne 0 ] || [ ! -f "$DEB_FILE" ]; then
    echo "✗ deb 打包失败 (dpkg-deb 退出码: $DPKG_RC)"
    echo "  请检查脚本上方是否有其他错误信息"
    exit $DPKG_RC
fi
echo "安装包: $DEB_FILE"
echo "大小: $(du -h "$DEB_FILE" | cut -f1)"

# 检查离线 .deb 数量，提示用户
DEB_COUNT=$(ls -1 "$APT_PKG_DIR"/*.deb 2>/dev/null | wc -l)
# [2026-11-11] 旧版只要没有 .deb 就打印"目标机需联网安装"，但同一份日志
# 前面已确认"零网络开箱即用模块已就位"，属自相矛盾的误导告警。
# 改为同时判断开箱即用形态：两者皆无才告警。
PY_READY=0
if [ -d "$INSTALL_DIR/python_modules/PyQt5" ] \
   && [ -d "$INSTALL_DIR/python_modules/psutil" ] \
   && [ -d "$INSTALL_DIR/python_modules/OpenGL" ]; then
    PY_READY=1
fi
if [ "$DEB_COUNT" -eq 0 ] && [ "$PY_READY" -eq 0 ]; then
    echo ""
    echo "⚠ 警告: 离线 Python 依赖未打包（既无开箱即用模块，也无离线 .deb）"
    echo "  目标机首次执行老化测试时需要联网安装："
    echo "    sudo apt install python3-pyqt5 python3-pyqt5.qtopengl python3-psutil python3-opengl"
    echo "  或在另一台同架构 Linux 联网后执行："
    echo "    apt-get download python3-pyqt5 python3-pyqt5.qtopengl python3-psutil python3-opengl"
    echo "    将 *.deb 复制到目标机 /usr/lib/elsky-test-client/python_modules/apt_packages/"
    echo "    sudo dpkg -i /usr/lib/elsky-test-client/python_modules/apt_packages/*.deb"
    echo "    sudo apt-get install -f -y"
elif [ "$PY_READY" -eq 1 ]; then
    echo "离线 Python 依赖: 开箱即用模块已随包（零安装零密码零联网，无需 .deb 兜底）"
else
    echo "离线 Python 依赖: $DEB_COUNT 个 .deb"
fi
echo ""
echo "安装方法（目标机）："
echo "  1. 双击 .deb 文件用软件中心安装"
echo "  2. 或命令安装: sudo dpkg -i $(basename "$DEB_FILE")"
echo "  3. 或命令安装: sudo apt install ./$(basename "$DEB_FILE")"
echo ""
echo "安装后："
echo "  - 桌面出现'研盛测试客户端'图标，双击启动"
echo "  - 首次安装已配置免密权限，各功能不再重复弹密码框"
