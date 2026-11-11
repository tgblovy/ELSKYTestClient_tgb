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
elif [[ "$ARCH" =~ ^(loongarch64|loong64|loongarch|mips64el|mips64|mipsel)$ ]]; then
    # 龙芯 LoongArch / 老龙芯 MIPS64el：产物统一 ELSKY_test_client_loong
    CURRENT_PLATFORM="loong"
    CURRENT_PLATFORM_NAME="龙芯"
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

# 1.5 复制Python老化测试脚本
echo "复制Python老化测试脚本..."
if [ -f "$PROJECT_ROOT/aging_test_gui.py" ]; then
    cp "$PROJECT_ROOT/aging_test_gui.py" .
    echo "✓ 复制 aging_test_gui.py"
else
    echo "⚠ 警告: 未找到 aging_test_gui.py"
fi
if [ -f "$PROJECT_ROOT/aging_auto_start.py" ]; then
    cp "$PROJECT_ROOT/aging_auto_start.py" .
    echo "✓ 复制 aging_auto_start.py"
else
    echo "⚠ 警告: 未找到 aging_auto_start.py"
fi
if [ -f "$PROJECT_ROOT/complex_scene.py" ]; then
    cp "$PROJECT_ROOT/complex_scene.py" .
    echo "✓ 复制 complex_scene.py"
else
    echo "⚠ 警告: 未找到 complex_scene.py"
fi
# aging_selftest.py: 跨架构自检工具(显存检测/停止时序/依赖完整性),
# 部署到目标机后执行 `python3 aging_selftest.py --with-gui` 即可验证
if [ -f "$PROJECT_ROOT/aging_selftest.py" ]; then
    cp "$PROJECT_ROOT/aging_selftest.py" .
    echo "✓ 复制 aging_selftest.py"
else
    echo "⚠ 警告: 未找到 aging_selftest.py"
fi

# 1.7 收集 Python 依赖（用于离线安装老化测试所需模块）
# 核心策略: 直接从编译机系统复制已安装的 Python 模块
# 优点: 完全离线、架构匹配、与编译机一致、目标机开箱即用
echo ""
echo "=========================================="
echo "收集 Python 依赖（从编译机系统复制）"
echo "=========================================="

PYTHON_DEPS_DIR="$DEPLOY_DIR/python_modules"
mkdir -p "$PYTHON_DEPS_DIR"

# 1.7.1 检测编译机是否已安装所需 Python 模块
# 如果未安装,先在编译机执行 apt install
check_python_module() {
    python3 -c "import $1" 2>/dev/null
    return $?
}

PYTHON_DEPS_ALL_READY=true
echo "检查编译机 Python 模块状态:"
for mod in PyQt5 PyQt5.QtOpenGL psutil OpenGL; do
    if check_python_module "$mod"; then
        echo "  ✓ $mod 已安装"
    else
        echo "  ✗ $mod 未安装"
        PYTHON_DEPS_ALL_READY=false
    fi
done

# 1.7.2 如果编译机缺包,自动安装（编译机有网络）
if [ "$PYTHON_DEPS_ALL_READY" = false ]; then
    echo ""
    echo "编译机缺少部分 Python 模块，尝试自动安装..."
    if command -v apt-get >/dev/null 2>&1; then
        echo "  使用 apt 安装: python3-pyqt5 python3-pyqt5.qtopengl python3-psutil python3-opengl"
        sudo apt-get update -qq 2>&1 | tail -3
        sudo apt-get install -y python3-pyqt5 python3-pyqt5.qtopengl python3-psutil python3-opengl 2>&1 | tail -5
        
        # 重新检查
        PYTHON_DEPS_ALL_READY=true
        for mod in PyQt5 PyQt5.QtOpenGL psutil OpenGL; do
            if ! check_python_module "$mod"; then
                PYTHON_DEPS_ALL_READY=false
                break
            fi
        done
    elif command -v yum >/dev/null 2>&1; then
        echo "  使用 yum 安装: python3-qt5 python3-psutil python3-PyOpenGL"
        sudo yum install -y python3-qt5 python3-psutil python3-PyOpenGL 2>&1 | tail -5
    fi
fi

# 1.7.3 检测 Python 版本和模块路径
PY_VERSION=""
if command -v python3 >/dev/null 2>&1; then
    PY_VERSION=$(python3 -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')" 2>/dev/null)
fi
echo "  Python 版本: ${PY_VERSION:-未知}"

# dist-packages 路径列表（不同发行版不同）
DIST_PACKAGES_PATHS=""
if [ -n "$PY_VERSION" ]; then
    DIST_PACKAGES_PATHS="
        /usr/lib/python3/dist-packages
        /usr/lib/python${PY_VERSION}/site-packages
        /usr/local/lib/python${PY_VERSION}/dist-packages
        /usr/lib/python3/site-packages
    "
fi

# 找到实际包含 PyQt5 的路径
PYQT5_SOURCE_PATH=""
for path in $DIST_PACKAGES_PATHS; do
    if [ -d "$path/PyQt5" ]; then
        PYQT5_SOURCE_PATH="$path"
        echo "  ✓ 找到 Python 包路径: $PYQT5_SOURCE_PATH"
        break
    fi
done

# 1.7.4 复制已安装的 Python 模块到部署包
COPY_SUCCESS=false
if [ -n "$PYQT5_SOURCE_PATH" ] && [ -d "$PYQT5_SOURCE_PATH" ]; then
    echo ""
    echo "复制 Python 模块到部署包..."
    
    # 要复制的模块列表（与老化测试所需完全对应）
    MODULES_TO_COPY="PyQt5 PyQt5 sip psutil OpenGL OpenGL_accelerate"
    
    for mod in $MODULES_TO_COPY; do
        # 模块可能是目录或单文件
        if [ -d "$PYQT5_SOURCE_PATH/$mod" ]; then
            # 目录形式（如 PyQt5/）
            cp -r "$PYQT5_SOURCE_PATH/$mod" "$PYTHON_DEPS_DIR/" 2>/dev/null
            echo "  ✓ 复制模块目录: $mod"
        elif [ -f "$PYQT5_SOURCE_PATH/$mod.py" ]; then
            # 单文件形式（如 OpenGL.py）
            cp "$PYQT5_SOURCE_PATH/$mod.py" "$PYTHON_DEPS_DIR/" 2>/dev/null
            echo "  ✓ 复制模块文件: $mod.py"
        elif [ -f "$PYQT5_SOURCE_PATH/${mod}.so" ]; then
            # 编译的 .so 形式
            cp "$PYQT5_SOURCE_PATH/${mod}.so" "$PYTHON_DEPS_DIR/" 2>/dev/null
            echo "  ✓ 复制编译模块: $mod.so"
        else
            echo "  - 跳过 $mod (不存在或不是模块)"
        fi
    done
    
    # 复制 PyQt5 的 .so 文件（在 PyQt5 目录外有时也有相关 .so）
    for so_file in "$PYQT5_SOURCE_PATH"/PyQt5.so \
                   "$PYQT5_SOURCE_PATH"/sip*.so \
                   "$PYQT5_SOURCE_PATH"/sip*.py; do
        if [ -f "$so_file" ]; then
            cp "$so_file" "$PYTHON_DEPS_DIR/" 2>/dev/null
            echo "  ✓ 复制辅助文件: $(basename $so_file)"
        fi
    done
    
    # 复制 .pth 文件和 .dist-info 元数据
    for pth_file in "$PYQT5_SOURCE_PATH"/PyQt5-*.pth \
                    "$PYQT5_SOURCE_PATH"/PyQt5*.dist-info; do
        if [ -e "$pth_file" ]; then
            cp -r "$pth_file" "$PYTHON_DEPS_DIR/" 2>/dev/null
        fi
    done
    
    # 检查复制结果
    PYQT5_OK=false
    PSUTIL_OK=false
    OPENGL_OK=false
    
    [ -d "$PYTHON_DEPS_DIR/PyQt5" ] && PYQT5_OK=true
    [ -d "$PYTHON_DEPS_DIR/psutil" ] || [ -f "$PYTHON_DEPS_DIR/psutil.py" ] || [ -f "$PYTHON_DEPS_DIR/psutil.so" ] && PSUTIL_OK=true
    [ -d "$PYTHON_DEPS_DIR/OpenGL" ] || [ -f "$PYTHON_DEPS_DIR/OpenGL.py" ] && OPENGL_OK=true
    
    if [ "$PYQT5_OK" = true ] && [ "$PSUTIL_OK" = true ] && [ "$OPENGL_OK" = true ]; then
        COPY_SUCCESS=true
    fi
    
    # PyQt5.QtOpenGL 子模块检查（在 PyQt5 目录内）
    if [ "$PYQT5_OK" = true ] && [ -d "$PYTHON_DEPS_DIR/PyQt5/QtOpenGL" ]; then
        echo "  ✓ PyQt5.QtOpenGL 子模块已包含"
    else
        echo "  ⚠ PyQt5.QtOpenGL 子模块未找到"
    fi
    
    # PyQt5 依赖的 Qt 库（PyQt5 内部使用的 .so 文件）
    if [ -d "$PYTHON_DEPS_DIR/PyQt5" ]; then
        QT_LIBS_DIR="$PYTHON_DEPS_DIR/PyQt5/Qt5"
        if [ -d "$PYQT5_SOURCE_PATH/PyQt5/Qt5" ]; then
            echo "  ✓ PyQt5 已包含 Qt5 库目录"
        else
            # 某些发行版 Qt5 库在系统路径，PyQt5 通过软链接
            echo "  ℹ PyQt5 使用系统 Qt5 库"
        fi
    fi
fi

# 1.7.5 备用方案: apt-get download .deb 包（如果上面复制失败）
if [ "$COPY_SUCCESS" = false ] && command -v apt-get >/dev/null 2>&1; then
    echo ""
    echo "--- 备用方案: apt-get download .deb 包 ---"
    APT_DEPS_DIR="$PYTHON_DEPS_DIR/apt_packages"
    mkdir -p "$APT_DEPS_DIR"
    cd "$APT_DEPS_DIR"
    
    # 下载 .deb 包及其所有依赖
    APT_PACKAGES="python3-pyqt5 python3-pyqt5.qtopengl python3-psutil python3-opengl"
    for pkg in $APT_PACKAGES; do
        echo "  下载 $pkg 及其依赖..."
        apt-get download "$pkg" 2>&1 | tail -2
        # 递归下载依赖（最多2层）
        for dep in $(apt-cache depends --recurse --no-recommends --no-suggests --no-conflicts --no-breaks --no-replaces --no-enhances "$pkg" 2>/dev/null | grep "^\w" | sort -u | head -50); do
            apt-get download "$dep" 2>/dev/null
        done
    done
    cd "$DEPLOY_DIR"
    
    DEB_COUNT=$(ls -1 "$APT_DEPS_DIR"/*.deb 2>/dev/null | wc -l)
    if [ "$DEB_COUNT" -gt 0 ]; then
        echo "✓ 成功下载 $DEB_COUNT 个 .deb 包到 apt_packages/"
        COPY_SUCCESS=true
    fi
fi

# 1.7.6 创建 Python 路径加载脚本（部署时使用）
cat > "$DEPLOY_DIR/python_path.sh" << 'EOF'
#!/bin/bash
# Python 模块路径加载脚本
# 让老化测试脚本能够找到打包的 Python 模块

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PY_MODULES_DIR="$SCRIPT_DIR/python_modules"

if [ -d "$PY_MODULES_DIR" ]; then
    # 将打包的 Python 模块目录加入 PYTHONPATH
    export PYTHONPATH="$PY_MODULES_DIR:${PYTHONPATH}"
    
    # 如果有 Qt 库目录，也加入 LD_LIBRARY_PATH
    if [ -d "$PY_MODULES_DIR/PyQt5/Qt5/lib" ]; then
        export LD_LIBRARY_PATH="$PY_MODULES_DIR/PyQt5/Qt5/lib:${LD_LIBRARY_PATH}"
    fi
    
    # 如果有 .so 文件在 python_modules 目录下
    if ls "$PY_MODULES_DIR"/*.so 1>/dev/null 2>&1; then
        export LD_LIBRARY_PATH="$PY_MODULES_DIR:${LD_LIBRARY_PATH}"
    fi
fi
EOF
chmod +x "$DEPLOY_DIR/python_path.sh"

# 1.7.7 显示收集结果
echo ""
echo "=== Python 依赖收集结果 ==="
if [ "$COPY_SUCCESS" = true ]; then
    PYTHON_SIZE=$(du -sh "$PYTHON_DEPS_DIR" 2>/dev/null | awk '{print $1}')
    echo "✓ Python 依赖已打包 (总大小: $PYTHON_SIZE)"
    echo "  目录: $PYTHON_DEPS_DIR"
    echo "  内容:"
    ls -la "$PYTHON_DEPS_DIR" | head -20 | awk '{print "    " $9 " " $5}' | grep -v "^    $"
    
    if [ -d "$PYTHON_DEPS_DIR/PyQt5" ]; then
        echo "  ✓ PyQt5 (含 QtOpenGL 子模块)"
    fi
    if [ -d "$PYTHON_DEPS_DIR/psutil" ] || [ -f "$PYTHON_DEPS_DIR/psutil.py" ]; then
        echo "  ✓ psutil"
    fi
    if [ -d "$PYTHON_DEPS_DIR/OpenGL" ] || [ -f "$PYTHON_DEPS_DIR/OpenGL.py" ]; then
        echo "  ✓ PyOpenGL"
    fi
    if [ -d "$PYTHON_DEPS_DIR/apt_packages" ]; then
        DEB_COUNT=$(ls -1 "$PYTHON_DEPS_DIR"/apt_packages/*.deb 2>/dev/null | wc -l)
        echo "  ✓ apt 包 ($DEB_COUNT 个 .deb 文件)"
    fi
    echo ""
    echo "  目标机使用方式:"
    echo "    1. 自动: 启动脚本会自动设置 PYTHONPATH"
    echo "    2. 手动: source ./python_path.sh && python3 aging_test_gui.py"
else
    echo "⚠ 未能收集 Python 依赖"
    echo "  目标机需手动执行:"
    echo "    sudo apt install -y python3-pyqt5 python3-pyqt5.qtopengl python3-psutil python3-opengl"
fi

# 总是生成 REQUIREMENTS.txt
cat > "$PYTHON_DEPS_DIR/REQUIREMENTS.txt" << 'REOF'
# 老化测试 Python 依赖
# 部署包已包含离线模块,启动时自动加载
# 如自动加载失败,可手动安装:
#   sudo apt install -y python3-pyqt5 python3-pyqt5.qtopengl python3-psutil python3-opengl

PyQt5>=5.12.0
psutil>=5.4.0
PyOpenGL>=3.1.0
REOF

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
    echo "检测到飞腾ARM64平台"
    APP_NAME="ELSKY_test_client_ft"
elif [[ "$ARCH" =~ "x86_64" ]]; then
    echo "检测到X86_64平台"
    APP_NAME="ELSKY_test_client_x86"
elif [[ "$ARCH" =~ "i386" || "$ARCH" =~ "i686" ]]; then
    echo "检测到X86平台"
    APP_NAME="ELSKY_test_client_x86"
elif [[ "$ARCH" =~ ^(loongarch64|loong64|loongarch|mips64el|mips64|mipsel)$ ]]; then
    echo "检测到龙芯平台 ($ARCH)"
    APP_NAME="ELSKY_test_client_loong"
else
    echo "错误: 不支持的平台架构: $ARCH"
    exit 1
fi

# 检查可执行文件是否存在
if [ ! -f "$SCRIPT_DIR/$APP_NAME" ]; then
    echo "错误: 找不到对应平台的可执行文件: $APP_NAME"
    exit 1
fi

# GLIBC 兼容性预检（版本冲突无法通过安装依赖解决，必须明确诊断）
GLIBC_ERR=$(ldd "$SCRIPT_DIR/$APP_NAME" 2>/dev/null | grep 'version .GLIBC_' | head -3)
if [ -n "$GLIBC_ERR" ]; then
    echo "✗ [致命] 程序依赖的库与本系统 glibc 版本不兼容:"
    echo "$GLIBC_ERR"
    echo "  根因: 本部署包在更高 glibc 的系统上编译/打包（glibc 无法向下降级）。"
    echo "  解决: 请在与本系统相同版本（如统信UOS 1070/麒麟V10 = glibc 2.28）的系统上重新编译打包。"
    exit 1
fi

# 自动检测并安装缺失依赖
echo "检查依赖库..."
MISSING=$(ldd "$SCRIPT_DIR/$APP_NAME" 2>/dev/null | grep "not found")
if [ -n "$MISSING" ]; then
    echo "检测到缺失依赖:"
    echo "$MISSING"
    echo "尝试自动安装依赖..."
    DEPS_OK=false
    if command -v apt-get >/dev/null 2>&1; then
        pkexec apt-get update -qq && pkexec apt-get install -y libqt5core5a libqt5gui5 libqt5widgets5 libqt5network5 libqt5serialport5 2>&1 && DEPS_OK=true
    elif command -v yum >/dev/null 2>&1; then
        pkexec yum install -y qt5-qtbase qt5-qtbase-gui qt5-qtnetwork qt5-qtserialport 2>&1 && DEPS_OK=true
    fi
    if [ "$DEPS_OK" = true ]; then
        echo "依赖安装完成"
        STILL_MISSING=$(ldd "$SCRIPT_DIR/$APP_NAME" 2>/dev/null | grep "not found")
        if [ -n "$STILL_MISSING" ]; then echo "$STILL_MISSING"; fi
        else echo "所有依赖已满足"; fi
    else
        echo "警告: 自动安装失败，请手动安装缺失的依赖"
        echo "  Debian/Ubuntu/统信/麒麟: sudo apt install libqt5core5a libqt5gui5 libqt5widgets5 libqt5network5 libqt5serialport5"
        echo "  CentOS/RHEL:             sudo yum install qt5-qtbase qt5-qtbase-gui qt5-qtnetwork qt5-qtserialport"
    fi
else
    echo "依赖检查通过"
fi

# 串口权限设置（确保用户可访问 /dev/ttyUSB* /dev/ttyS*）
echo "检查串口权限..."
CURRENT_USER=$(whoami)
if ! groups "$CURRENT_USER" | grep -q dialout; then
    echo "提示: 当前用户不在 dialout 组，串口设备可能无法访问"
    echo "  如需使用 COM 测试功能，请执行:"
    echo "    sudo usermod -aG dialout $CURRENT_USER"
    echo "  然后重新登录生效"
fi

# 配置 sudo 免密（用于 LAN 唤醒 Magic 模式设置）
SUDOERS_FILE="/etc/sudoers.d/elsky_wol"
if [ ! -f "$SUDOERS_FILE" ]; then
    echo "配置 LAN 唤醒免密权限（需要一次性授权）..."
    CURRENT_USER=$(whoami)
    pkexec bash -c "echo '$CURRENT_USER ALL=(ALL) NOPASSWD: /usr/sbin/ethtool, /usr/bin/nmcli' > $SUDOERS_FILE && chmod 440 $SUDOERS_FILE" 2>/dev/null
    if [ -f "$SUDOERS_FILE" ]; then
        echo "LAN 唤醒免密权限配置完成"
    fi
fi

# 设置执行权限
chmod +x "$SCRIPT_DIR/$APP_NAME"

echo "启动客户端..."
echo "平台: $ARCH"
echo "程序: $APP_NAME"
echo "================================="

# 加载打包的 Python 模块（开箱即用，老化测试用）
if [ -d "$SCRIPT_DIR/python_modules/PyQt5" ]; then
    export PYTHONPATH="$SCRIPT_DIR/python_modules:${PYTHONPATH}"
    if [ -d "$SCRIPT_DIR/python_modules/PyQt5/Qt5/lib" ]; then
        export LD_LIBRARY_PATH="$SCRIPT_DIR/python_modules/PyQt5/Qt5/lib:${LD_LIBRARY_PATH}"
    fi
fi

cd "$SCRIPT_DIR"
exec "./$APP_NAME" "$@"
EOF
    chmod +x start_client.sh
fi

# 4. 创建库目录并收集依赖库
# 使用绝对路径避免 cd 后相对路径问题
DEPLOY_LIB_DIR="$DEPLOY_DIR/lib"
mkdir -p "$DEPLOY_LIB_DIR"
echo "收集依赖库到: $DEPLOY_LIB_DIR"

# 4.1 检查可执行文件是否启用了串口支持（链接了 Qt5SerialPort）
SERIALPORT_ENABLED=false
if [ -f "$DEPLOY_DIR/ELSKY_test_client_${CURRENT_PLATFORM}" ]; then
    if ldd "$DEPLOY_DIR/ELSKY_test_client_${CURRENT_PLATFORM}" | grep -qi "Qt5SerialPort"; then
        SERIALPORT_ENABLED=true
        echo "✓ 检测到可执行文件已链接 Qt5SerialPort"
    else
        echo "⚠ 警告: 可执行文件未链接 Qt5SerialPort (串口功能可能不可用)"
        echo "  提示: 编译时需安装 libqt5serialport5-dev 并确保 .pro 启用了 serialport 模块"
    fi
fi

# 4.2 通过 ldd 自动收集动态链接库（使用绝对路径）
EXE_PATH="$DEPLOY_DIR/ELSKY_test_client_${CURRENT_PLATFORM}"
if [ -f "$EXE_PATH" ]; then
    echo "分析依赖库: $EXE_PATH"
    # 使用临时文件收集库路径，避免子 shell 变量丢失
    TMP_LIBS=$(mktemp)
    # 提取格式 "libname.so => /path/to/lib (0x地址)" 中的路径。
    # 修复（S1）：原 awk 用 gawk 专有 match($0, re, arr) 三参数写法，
    # 在 mawk（Debian/Ubuntu/统信默认 awk）下直接报错，依赖收集静默为空。
    # 改用 POSIX sed（与 deb 打包脚本 collect_deps_of 同款），全 awk 实现无涉。
    # not found 行无 (0x 地址 自然不匹配；linux-vdso/ld-linux 无 => 也被排除。
    ldd "$EXE_PATH" 2>/dev/null \
        | sed -nE 's/.*=> ([^ ]+) \(0x[0-9a-f]+\)/\1/p' > "$TMP_LIBS"
    
    COPIED_COUNT=0
    while read lib; do
        # 跳过空行和 not found
        if [ -z "$lib" ] || [ "$lib" = "not" ] || [[ "$lib" == *"not found"* ]]; then
            continue
        fi
        # 清理路径
        lib=$(echo "$lib" | tr -d ' ')
        if [ -f "$lib" ]; then
            lib_name=$(basename "$lib")
            if [ ! -f "$DEPLOY_LIB_DIR/$lib_name" ]; then
                cp "$lib" "$DEPLOY_LIB_DIR/" && COPIED_COUNT=$((COPIED_COUNT + 1))
            fi
        fi
    done < "$TMP_LIBS"
    rm -f "$TMP_LIBS"
    echo "✓ 基础依赖库收集完成 (复制了 $COPIED_COUNT 个库)"
    
    # 显示收集的库文件数量
    LIB_COUNT=$(ls -1 "$DEPLOY_LIB_DIR"/*.so* 2>/dev/null | wc -l)
    echo "  lib/ 目录共 $LIB_COUNT 个 .so 文件"
    if [ "$LIB_COUNT" -gt 0 ]; then
        echo "  前 5 个: $(ls -1 "$DEPLOY_LIB_DIR"/*.so* 2>/dev/null | head -5 | xargs -I {} basename {})"
    fi
else
    echo "⚠ 警告: 未找到可执行文件 $EXE_PATH，跳过依赖库收集"
fi

# 4.3 强制收集 Qt5SerialPort 运行时库（即使 ldd 未列出）
# 原因: 某些系统下 Qt5SerialPort 可能以插件形式加载，ldd 检测不到
if [ "$SERIALPORT_ENABLED" = true ]; then
    echo "强制检查 Qt5SerialPort 运行时库..."
    
    # 扩展的 Qt5SerialPort 库路径列表（含版本号与无版本号）
    SERIALPORT_LIB_PATHS="
        /usr/lib/libQt5SerialPort.so
        /usr/lib/libQt5SerialPort.so.5
        /usr/lib/libQt5SerialPort.so.5.5
        /usr/lib/libQt5SerialPort.so.5.12
        /usr/lib/libQt5SerialPort.so.5.15
        /usr/lib/aarch64-linux-gnu/libQt5SerialPort.so
        /usr/lib/aarch64-linux-gnu/libQt5SerialPort.so.5
        /usr/lib/aarch64-linux-gnu/libQt5SerialPort.so.5.5
        /usr/lib/x86_64-linux-gnu/libQt5SerialPort.so
        /usr/lib/x86_64-linux-gnu/libQt5SerialPort.so.5
        /usr/lib/x86_64-linux-gnu/libQt5SerialPort.so.5.5
        /usr/lib/x86_64-linux-gnu/libQt5SerialPort.so.5.12
        /usr/lib/x86_64-linux-gnu/libQt5SerialPort.so.5.15
        /usr/lib/arm-linux-gnueabihf/libQt5SerialPort.so
        /usr/lib/arm-linux-gnueabihf/libQt5SerialPort.so.5
        /usr/lib/loongarch64-linux-gnu/libQt5SerialPort.so
        /usr/lib/loongarch64-linux-gnu/libQt5SerialPort.so.5
        /usr/lib/loongarch64-linux-gnu/libQt5SerialPort.so.5.15
        /usr/lib/loong64-linux-gnu/libQt5SerialPort.so
        /usr/lib/loong64-linux-gnu/libQt5SerialPort.so.5
        /usr/lib/mips64el-linux-gnuabi64/libQt5SerialPort.so
        /usr/lib/mips64el-linux-gnuabi64/libQt5SerialPort.so.5
    "
    
    SERIALPORT_FOUND=false
    for path in $SERIALPORT_LIB_PATHS; do
        if [ -f "$path" ]; then
            lib_name=$(basename "$path")
            if [ ! -f "$DEPLOY_LIB_DIR/$lib_name" ]; then
                cp "$path" "$DEPLOY_LIB_DIR/" && echo "✓ 复制 Qt5SerialPort 库: $lib_name"
            else
                echo "✓ Qt5SerialPort 库已存在: $lib_name"
            fi
            SERIALPORT_FOUND=true
        fi
    done
    
    if [ "$SERIALPORT_FOUND" = false ]; then
        echo "⚠ 警告: 未找到 Qt5SerialPort 库文件"
        echo "  请在编译机手动查找: find / -name 'libQt5SerialPort*' 2>/dev/null"
    fi
fi

# 4.4 创建依赖检查报告
cat > "$DEPLOY_DIR/DEPS_CHECK.txt" << DEPS_EOF
依赖库检查报告
==============
生成时间: $(date +"%Y-%m-%d %H:%M:%S")
平台: $ARCH ($CURRENT_PLATFORM_NAME)

串口支持状态: $([ "$SERIALPORT_ENABLED" = true ] && echo "✓ 已启用" || echo "✗ 未启用")

已收集的库文件:
$(ls -1 lib/ 2>/dev/null | sed 's/^/  - /')

Qt5SerialPort 库:
$(ls -1 lib/ 2>/dev/null | grep -i serialport | sed 's/^/  - /' || echo "  无")

如目标机器仍提示缺少串口库，请手动安装:
  Debian/Ubuntu/统信/麒麟: sudo apt install libqt5serialport5
  CentOS/RHEL:             sudo yum install qt5-qtserialport
DEPS_EOF
echo "✓ 依赖检查报告已生成: DEPS_CHECK.txt"

# 4.9 创建 Python 依赖加载脚本（开箱即用，无需安装）
# 核心策略: 通过 PYTHONPATH 让目标机直接使用打包的 Python 模块
# 仅在打包模块不可用时才降级为 apt 安装
cat > "$DEPLOY_DIR/install_python_deps.sh" << 'EOF'
#!/bin/bash
# Python 依赖加载脚本（开箱即用）
# 优先使用打包的 python_modules 目录（无需安装，无需 root）
# 降级方案: .deb 安装 → 在线 apt 安装

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PY_MODULES_DIR="$SCRIPT_DIR/python_modules"
APT_DIR="$PY_MODULES_DIR/apt_packages"

echo "=== Python 依赖检查 ==="

# 方式1: 使用打包的 python_modules 目录（推荐，开箱即用）
if [ -d "$PY_MODULES_DIR/PyQt5" ]; then
    echo "✓ 检测到打包的 Python 模块: $PY_MODULES_DIR"
    
    # 设置 PYTHONPATH 让 Python 找到打包的模块
    export PYTHONPATH="$PY_MODULES_DIR:${PYTHONPATH}"
    
    # 设置 LD_LIBRARY_PATH（PyQt5 内部可能依赖 Qt 库）
    if [ -d "$PY_MODULES_DIR/PyQt5/Qt5/lib" ]; then
        export LD_LIBRARY_PATH="$PY_MODULES_DIR/PyQt5/Qt5/lib:${LD_LIBRARY_PATH}"
    fi
    # 处理 .so 文件
    if ls "$PY_MODULES_DIR"/*.so 1>/dev/null 2>&1; then
        export LD_LIBRARY_PATH="$PY_MODULES_DIR:${LD_LIBRARY_PATH}"
    fi
    
    # 验证打包模块是否可用
    echo "  验证打包模块..."
    PACKAGED_OK=true
    for module in PyQt5 PyQt5.QtOpenGL psutil OpenGL; do
        if python3 -c "import ${module}" 2>/dev/null; then
            echo "    ✓ $module"
        else
            echo "    ✗ $module (打包模块缺失或不兼容)"
            PACKAGED_OK=false
        fi
    done
    
    if [ "$PACKAGED_OK" = true ]; then
        echo "✓ 打包模块全部可用"
        # 将环境变量写入临时文件供其他脚本使用
        echo "export PYTHONPATH=\"$PY_MODULES_DIR:\${PYTHONPATH}\"" > /tmp/elsky_python_env.sh
        if [ -d "$PY_MODULES_DIR/PyQt5/Qt5/lib" ]; then
            echo "export LD_LIBRARY_PATH=\"$PY_MODULES_DIR/PyQt5/Qt5/lib:\${LD_LIBRARY_PATH}\"" >> /tmp/elsky_python_env.sh
        fi
        chmod +x /tmp/elsky_python_env.sh
        exit 0
    fi
    echo "⚠ 打包模块不完整，尝试降级方案..."
fi

# 方式2: 使用 .deb 文件安装
NEED_INSTALL=false
for module in PyQt5 PyQt5.QtOpenGL psutil OpenGL; do
    if ! python3 -c "import ${module}" 2>/dev/null; then
        NEED_INSTALL=true
        break
    fi
done

if [ "$NEED_INSTALL" = true ] && [ -d "$APT_DIR" ]; then
    echo ""
    echo "--- 方式2: 使用 .deb 文件安装 ---"
    DEB_FILES=$(ls "$APT_DIR"/*.deb 2>/dev/null)
    if [ -n "$DEB_FILES" ]; then
        echo "找到 .deb 文件:"
        ls -1 "$APT_DIR"/*.deb 2>/dev/null | while read f; do echo "  - $(basename $f)"; done
        
        # dpkg 安装需要 root 权限
        echo "使用 dpkg 安装（需要 root 权限）..."
        pkexec dpkg -i "$APT_DIR"/*.deb 2>&1 | tail -10
        pkexec apt-get install -f -y 2>&1 | tail -5
    fi
fi

# 方式3: 在线安装 (最后兜底)
NEED_ONLINE=false
for module in PyQt5 PyQt5.QtOpenGL psutil OpenGL; do
    if ! python3 -c "import ${module}" 2>/dev/null; then
        NEED_ONLINE=true
        break
    fi
done

if [ "$NEED_ONLINE" = true ]; then
    echo ""
    echo "--- 方式3: 在线安装 (apt) ---"
    if command -v apt-get >/dev/null 2>&1; then
        pkexec apt-get update -qq && \
        pkexec apt-get install -y python3-pyqt5 python3-pyqt5.qtopengl python3-psutil python3-opengl 2>&1 | tail -10
    elif command -v yum >/dev/null 2>&1; then
        pkexec yum install -y python3-qt5 python3-psutil python3-PyOpenGL 2>&1 | tail -10
    fi
fi

# 最终验证
echo ""
echo "=== 最终验证 ==="
DEPS_OK=true
for module in PyQt5 PyQt5.QtOpenGL psutil OpenGL; do
    if python3 -c "import ${module}" 2>/dev/null; then
        echo "  ✓ $module"
    else
        echo "  ✗ $module"
        DEPS_OK=false
    fi
done

if [ "$DEPS_OK" = false ]; then
    echo ""
    echo "========================================="
    echo "⚠ Python 依赖未完全安装"
    echo "请手动执行:"
    echo "  sudo apt install -y python3-pyqt5 python3-pyqt5.qtopengl python3-psutil python3-opengl"
    echo "========================================="
    exit 1
fi

echo ""
echo "✓ 所有 Python 依赖已就绪"
EOF
chmod +x "$DEPLOY_DIR/install_python_deps.sh"

# 5. 创建动态库加载脚本
cat > "$DEPLOY_DIR/load_libraries.sh" << 'EOF'
#!/bin/bash
# 动态库加载脚本

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIB_DIR="$SCRIPT_DIR/lib"

# 设置库路径（优先使用本地 lib 目录）
if [ -d "$LIB_DIR" ]; then
    export LD_LIBRARY_PATH="$LIB_DIR:${LD_LIBRARY_PATH}"
    export QT_PLUGIN_PATH="$LIB_DIR:${QT_PLUGIN_PATH}"
fi

echo "=== 研盛测试客户端 - 库加载 ==="
echo "脚本目录: $SCRIPT_DIR"
echo "库目录: $LIB_DIR"

# 检查库文件
if [ -d "$LIB_DIR" ]; then
    echo "找到库文件:"
    ls "$LIB_DIR"/*.so* 2>/dev/null | while read lib; do
        echo "  - $(basename $lib)"
    done
else
    echo "警告: 库目录不存在"
fi

# 自动加载 Python 模块（开箱即用，无需安装）
# 通过 PYTHONPATH 加载打包的 python_modules 目录
PY_MODULES_DIR="$SCRIPT_DIR/python_modules"
if [ -d "$PY_MODULES_DIR/PyQt5" ]; then
    # 设置环境变量（当前进程及子进程生效）
    export PYTHONPATH="$PY_MODULES_DIR:${PYTHONPATH}"
    
    # PyQt5 内部依赖的 Qt 库
    if [ -d "$PY_MODULES_DIR/PyQt5/Qt5/lib" ]; then
        export LD_LIBRARY_PATH="$PY_MODULES_DIR/PyQt5/Qt5/lib:${LD_LIBRARY_PATH}"
    fi
    if ls "$PY_MODULES_DIR"/*.so 1>/dev/null 2>&1; then
        export LD_LIBRARY_PATH="$PY_MODULES_DIR:${LD_LIBRARY_PATH}"
    fi
    echo "✓ 已加载打包的 Python 模块: $PY_MODULES_DIR"
fi

# 验证 Python 依赖是否可用（含打包模块）
PYTHON_DEPS_READY=true
for module in PyQt5 PyQt5.QtOpenGL psutil OpenGL; do
    if ! python3 -c "import ${module}" 2>/dev/null; then
        PYTHON_DEPS_READY=false
        break
    fi
done

if [ "$PYTHON_DEPS_READY" = false ]; then
    echo ""
    echo "⚠ 检测到 Python 老化测试依赖未完全可用"
    if [ -f "$SCRIPT_DIR/install_python_deps.sh" ]; then
        echo "  尝试通过 install_python_deps.sh 修复..."
        bash "$SCRIPT_DIR/install_python_deps.sh"
        # 重新检查
        PYTHON_DEPS_READY=true
        for module in PyQt5 PyQt5.QtOpenGL psutil OpenGL; do
            if ! python3 -c "import ${module}" 2>/dev/null; then
                PYTHON_DEPS_READY=false
                break
            fi
        done
    fi
    
    if [ "$PYTHON_DEPS_READY" = false ]; then
        echo "  ⚠ 部分依赖仍缺失，老化测试可能无法运行"
        echo "  手动安装: sudo apt install python3-pyqt5 python3-pyqt5.qtopengl python3-psutil python3-opengl"
    fi
else
    echo "✓ Python 老化测试依赖已就绪"
fi

# 检测平台
ARCH=$(uname -m)
APP_NAME=""
if [ "$ARCH" = "aarch64" ]; then
    APP_NAME="ELSKY_test_client_ft"
elif [[ "$ARCH" =~ "x86_64" || "$ARCH" =~ "i386" || "$ARCH" =~ "i686" ]]; then
    APP_NAME="ELSKY_test_client_x86"
elif [[ "$ARCH" =~ ^(loongarch64|loong64|loongarch|mips64el|mips64|mipsel)$ ]]; then
    APP_NAME="ELSKY_test_client_loong"
fi

# 自动检测并安装缺失依赖
if [ -n "$APP_NAME" ] && [ -f "$SCRIPT_DIR/$APP_NAME" ]; then
    echo "检查依赖库..."
    MISSING=$(ldd "$SCRIPT_DIR/$APP_NAME" 2>/dev/null | grep "not found")
    if [ -n "$MISSING" ]; then
        echo "检测到缺失依赖:"
        echo "$MISSING"
        echo "尝试自动安装依赖..."
        DEPS_OK=false
        if [ "$ARCH" = "aarch64" ]; then
            # 飞腾平台
            echo "检测到飞腾ARM64平台，安装依赖..."
            if command -v apt-get >/dev/null 2>&1; then
                pkexec apt-get update -qq && pkexec apt-get install -y libqt5core5a libqt5gui5 libqt5widgets5 libqt5network5 libqt5serialport5 2>&1 && DEPS_OK=true
            elif command -v yum >/dev/null 2>&1; then
                pkexec yum install -y qt5-qtbase qt5-qtbase-gui qt5-qtnetwork qt5-qtserialport 2>&1 && DEPS_OK=true
            fi
        elif [[ "$ARCH" =~ "x86_64" || "$ARCH" =~ "i386" || "$ARCH" =~ "i686" ]]; then
            # X86平台
            echo "检测到X86平台，安装依赖..."
            if command -v apt-get >/dev/null 2>&1; then
                pkexec apt-get update -qq && pkexec apt-get install -y libqt5core5a libqt5gui5 libqt5widgets5 libqt5network5 libqt5serialport5 2>&1 && DEPS_OK=true
            elif command -v yum >/dev/null 2>&1; then
                pkexec yum install -y qt5-qtbase qt5-qtbase-gui qt5-qtnetwork qt5-qtserialport 2>&1 && DEPS_OK=true
            fi
        elif [[ "$ARCH" =~ ^(loongarch64|loong64|loongarch|mips64el|mips64|mipsel)$ ]]; then
            # 龙芯平台（LoongArch/MIPS64el），Qt5 包名与 X86 一致
            echo "检测到龙芯平台，安装依赖..."
            if command -v apt-get >/dev/null 2>&1; then
                pkexec apt-get update -qq && pkexec apt-get install -y libqt5core5a libqt5gui5 libqt5widgets5 libqt5network5 libqt5serialport5 2>&1 && DEPS_OK=true
            elif command -v yum >/dev/null 2>&1; then
                pkexec yum install -y qt5-qtbase qt5-qtbase-gui qt5-qtnetwork qt5-qtserialport 2>&1 && DEPS_OK=true
            fi
        fi
        if [ "$DEPS_OK" = true ]; then
            echo "依赖安装完成"
        else
            echo "警告: 自动安装失败，请手动安装缺失的依赖"
            echo "  飞腾平台: sudo apt-get install libqt5core5a libqt5gui5 libqt5widgets5 libqt5network5 libqt5serialport5"
            echo "  X86平台: sudo apt-get install libqt5core5a libqt5gui5 libqt5widgets5 libqt5network5 libqt5serialport5"
            echo "  CentOS/RHEL: sudo yum install qt5-qtbase qt5-qtbase-gui qt5-qtnetwork qt5-qtserialport"
        fi
        # 再次检查
        STILL_MISSING=$(ldd "$SCRIPT_DIR/$APP_NAME" 2>/dev/null | grep "not found")
        if [ -n "$STILL_MISSING" ]; then
            echo "仍有缺失依赖:"
            echo "$STILL_MISSING"
        else
            echo "所有依赖已满足"
        fi
    else
        echo "依赖检查通过"
    fi
fi

# 配置 sudo 免密（用于 LAN 唤醒 Magic 模式设置）
SUDOERS_FILE="/etc/sudoers.d/elsky_wol"
if [ ! -f "$SUDOERS_FILE" ]; then
    echo "配置 LAN 唤醒免密权限（需要一次性授权）..."
    CURRENT_USER=$(whoami)
    pkexec bash -c "echo '$CURRENT_USER ALL=(ALL) NOPASSWD: /usr/sbin/ethtool, /usr/bin/nmcli' > $SUDOERS_FILE && chmod 440 $SUDOERS_FILE" 2>/dev/null
    if [ -f "$SUDOERS_FILE" ]; then
        echo "LAN 唤醒免密权限配置完成"
    fi
fi

echo "启动主程序..."
# 启动主程序（start_client.sh 会自动加载 Python 模块）
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
elif [[ "$ARCH" =~ ^(loongarch64|loong64|loongarch|mips64el|mips64|mipsel)$ ]]; then
    PLATFORM_NAME="龙芯"
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
Exec=sh -c "./load_libraries.sh"
Path=$INSTALL_DIR
Icon=utilities-system-monitor
Categories=Utility;
Terminal=false
StartupNotify=true
DESKTOP_EOF

# 复制桌面快捷方式
if [ -d "$DESKTOP_DIR" ]; then
    cp "$INSTALL_DIR/ELSKYTestClient.desktop" "$DESKTOP_DIR/"
    # .desktop 文件必须有可执行权限才能被桌面环境双击运行
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
    chmod +x "$INSTALL_DIR/ELSKYTestClient.desktop"
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
if [ -f "$INSTALL_DIR/ELSKY_test_client_loong" ]; then
    echo "  ✓ 龙芯版本"
fi

# 检查当前平台对应的版本是否存在（龙芯单独映射，避免被三元式误判为 x86）
case "$ARCH" in
    aarch64) _CUR_SUFFIX="ft" ;;
    loongarch64|loong64|loongarch|mips64el|mips64|mipsel) _CUR_SUFFIX="loong" ;;
    *) _CUR_SUFFIX="x86" ;;
esac
CURRENT_APP="ELSKY_test_client_${_CUR_SUFFIX}"
if [ -f "$INSTALL_DIR/$CURRENT_APP" ]; then
    echo "  ✓ 当前平台版本: $CURRENT_APP"
else
    echo "  ⚠ 当前平台版本不存在: $CURRENT_APP"
fi

echo ""
echo "安装完成！"
echo "支持的平台: 飞腾ARM64, X86(兆芯/海光/Intel/AMD), 龙芯(LoongArch/MIPS64el)"
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
• 龙芯平台 (loongarch64, loong64, mips64el)
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
• ELSKY_test_client_loong - 龙芯版本 (LoongArch/MIPS64el)
• start_client.sh        - 跨平台启动脚本
• load_libraries.sh      - 库加载脚本
• install.sh            - 安装脚本
• lib/                  - Qt依赖库目录（含SerialPort等）
• python_deps/          - Python老化测试离线依赖(.whl)
• install_python_deps.sh - Python依赖自动安装脚本
• DEPS_CHECK.txt        - 依赖检查报告

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
     sudo apt-get install libqt5core5a libqt5gui5 libqt5widgets5 libqt5network5 libqt5serialport5

   X86平台:  
     sudo apt-get install libqt5core5a libqt5gui5 libqt5widgets5 libqt5network5 libqt5serialport5

   CentOS/RHEL:
     sudo yum install qt5-qtbase qt5-qtbase-gui qt5-qtnetwork qt5-qtserialport

3. 串口(COM)测试无法使用
   ① 检查 Qt5SerialPort 库是否已安装:
      ldd ELSKY_test_client_* | grep SerialPort
      (有输出表示库已就绪)
   
   ② 安装串口库(若缺失):
      sudo apt install libqt5serialport5
      (CentOS: sudo yum install qt5-qtserialport)
   
   ③ 配置串口访问权限:
      sudo usermod -aG dialout \$USER
      (需要注销后重新登录生效)
   
   ④ 验证串口设备:
      ls -l /dev/ttyUSB* /dev/ttyS*
      (确认用户对设备有读写权限)

4. 老化测试(aging_test_gui.py)启动失败
   ① 首次启动时会自动检测 Python 依赖
   ② 若自动安装失败,手动执行:
      ./install_python_deps.sh
   ③ 或直接用 apt 安装:
      sudo apt install -y python3-pyqt5 python3-pyqt5.qtopengl python3-psutil python3-opengl
   ④ 验证:
      python3 -c "import PyQt5, PyQt5.QtOpenGL, psutil, OpenGL; print('OK')"

技术支持: 唐贵宝
版本: $VERSION
平台: $CURRENT_PLATFORM_NAME

打包时间: $(date +"%Y-%m-%d %H:%M:%S")
EOF

# 8. 创建版本信息
cat > VERSION << EOF
PRODUCT=ELSKYTestClient
VERSION=2.1
BUILD_DATE=$(date +%Y-%m-%d)
BUILD_USER=$CURRENT_USER
BUILD_PLATFORM=$CURRENT_PLATFORM_NAME
SUPPORTED_PLATFORMS=ft,x86,loong
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

# 10.5 部署包自检（关键！确保 lib/ 目录被正确包含）
echo ""
echo "========================================="
echo "部署包自检"
echo "========================================="
SELF_CHECK_OK=true

# 检查 lib 目录是否存在
if [ ! -d "lib" ]; then
    echo "✗ 严重错误: lib/ 目录不存在！"
    SELF_CHECK_OK=false
else
    LIB_FILE_COUNT=$(ls -1 lib/ 2>/dev/null | wc -l)
    echo "✓ lib/ 目录存在 (共 $LIB_FILE_COUNT 个文件)"
    
    # 检查 Qt5SerialPort 是否在 lib 目录中
    if ls lib/libQt5SerialPort* 1> /dev/null 2>&1; then
        echo "✓ lib/ 中包含 Qt5SerialPort 库"
        ls lib/libQt5SerialPort*
    else
        if [ "$SERIALPORT_ENABLED" = true ]; then
            echo "✗ 严重错误: 启用了串口但 lib/ 中没有 Qt5SerialPort!"
            echo "  原因可能是: 编译机未安装 libqt5serialport5-dev"
            SELF_CHECK_OK=false
        else
            echo "⚠ lib/ 中无 Qt5SerialPort (可执行文件未链接串口)"
        fi
    fi
fi

# 检查可执行文件
if [ -f "ELSKY_test_client_${CURRENT_PLATFORM}" ]; then
    echo "✓ 可执行文件存在: ELSKY_test_client_${CURRENT_PLATFORM}"
    # 检查可执行文件实际依赖
    if [ -f "ELSKY_test_client_${CURRENT_PLATFORM}" ]; then
        NEEDED_SERIAL=$(ldd "ELSKY_test_client_${CURRENT_PLATFORM}" | grep -i serialport || true)
        if [ -n "$NEEDED_SERIAL" ]; then
            echo "  可执行文件需要的串口库:"
            echo "$NEEDED_SERIAL" | sed 's/^/    /'
        fi
    fi
else
    echo "✗ 严重错误: 缺少当前平台可执行文件"
    SELF_CHECK_OK=false
fi

if [ "$SELF_CHECK_OK" = false ]; then
    echo ""
    echo "========================================="
    echo "⚠ 自检失败！请先解决问题再打包"
    echo "========================================="
    exit 1
fi

# 检查 Python 依赖收集情况
if [ -d "python_modules" ]; then
    PYMOD_SIZE=$(du -sh python_modules 2>/dev/null | awk '{print $1}')
    if [ -d "python_modules/PyQt5" ]; then
        echo "✓ Python 模块已打包 (大小: $PYMOD_SIZE)"
        # 详细检查
        [ -d "python_modules/PyQt5/QtOpenGL" ] && echo "  ✓ PyQt5.QtOpenGL"
        [ -d "python_modules/psutil" ] || [ -f "python_modules/psutil.py" ] && echo "  ✓ psutil"
        [ -d "python_modules/OpenGL" ] || [ -f "python_modules/OpenGL.py" ] && echo "  ✓ PyOpenGL"
    elif [ -d "python_modules/apt_packages" ]; then
        DEB_COUNT=$(ls -1 python_modules/apt_packages/*.deb 2>/dev/null | wc -l)
        echo "✓ Python apt 包已打包 ($DEB_COUNT 个 .deb, 大小: $PYMOD_SIZE)"
    else
        echo "⚠ Python 依赖目录存在但内容不完整"
    fi
fi

# 检查 install_python_deps.sh 和 python_path.sh
if [ -f "install_python_deps.sh" ]; then
    echo "✓ Python 依赖加载脚本: install_python_deps.sh"
fi
if [ -f "python_path.sh" ]; then
    echo "✓ Python 路径脚本: python_path.sh"
fi

echo "✓ 部署包自检通过"

# 10.8 GLIBC 兼容性门禁（与 deb 打包脚本同策略）
# 包内 lib/ 复制自编译机，编译机 glibc 高于目标机时，目标机必报
# "version 'GLIBC_2.xx' not found" 且无法自愈 → 打包时直接拦截。
# [2026-10-16] 与 deb 打包脚本同步：记录是否显式指定基线 + host 快捷方式 +
# 自动基线降级（兆芯/麒麟2503 等编译机=目标机的场景直接运行脚本即可完成打包）。
MIN_TARGET_GLIBC_EXPLICIT="${MIN_TARGET_GLIBC:-}"
MIN_TARGET_GLIBC="${MIN_TARGET_GLIBC:-2.28}"   # 统信UOS 1070 / 麒麟V10 SP1 / Debian10 基线
if [ "$MIN_TARGET_GLIBC" = "host" ]; then
    _HOST_GLIBC=$(ldd --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+' | head -1)
    if [ -n "$_HOST_GLIBC" ]; then
        MIN_TARGET_GLIBC="$_HOST_GLIBC"
        echo "ℹ MIN_TARGET_GLIBC=host: 目标基线取编译机 glibc(${MIN_TARGET_GLIBC})"
        echo "  注意: 本包仅保证在与编译机相同或更新 glibc 的系统上正常运行。"
    else
        echo "⚠ 无法识别编译机 glibc 版本，回退默认基线 2.28"
        MIN_TARGET_GLIBC="2.28"
    fi
fi
BUILD_GLIBC=$(ldd --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+$' | head -1)
ver_gt() { [ "$1" != "$2" ] && [ "$(printf '%s\n' "$1" "$2" | sort -V | head -1)" = "$2" ]; }
elf_max_glibc() {
    local out
    out=$(objdump -T "$1" 2>/dev/null | grep -oE 'GLIBC_[0-9]+(\.[0-9]+)+' | sort -Vu | tail -1)
    [ -z "$out" ] && out=$(readelf -V "$1" 2>/dev/null | grep -oE 'GLIBC_[0-9]+(\.[0-9]+)+' | sort -Vu | tail -1)
    echo "$out"
}
is_elf() { [ "$(head -c 4 "$1" 2>/dev/null | od -An -tx1 | tr -d ' \n')" = "7f454c46" ]; }
echo ""
echo "GLIBC 兼容性门禁（目标基线 glibc >= $MIN_TARGET_GLIBC，编译机 glibc: ${BUILD_GLIBC:-未知}）..."
GLIBC_REPORT="$(mktemp /tmp/elsky_glibc.XXXXXX)"
while IFS= read -r -d '' f; do
    is_elf "$f" || continue
    maxv=$(elf_max_glibc "$f")
    [ -z "$maxv" ] && continue
    need="${maxv#GLIBC_}"
    if ver_gt "$need" "$MIN_TARGET_GLIBC"; then
        echo "GLIBC_$need  ${f#./}" >> "$GLIBC_REPORT"
    fi
done < <(find . -type f \( -name '*.so' -o -name '*.so.*' -o -name 'ELSKY_test_client_*' \) -print0 2>/dev/null)
if [ -s "$GLIBC_REPORT" ]; then
    echo "  ✗ 检测到 $(wc -l < "$GLIBC_REPORT") 个文件要求的 GLIBC 高于默认目标基线 $MIN_TARGET_GLIBC:"
    sort -Vr "$GLIBC_REPORT" | head -15 | sed 's/^/      /'
    echo ""
    # [2026-10-16] 自动基线降级（与 deb 打包脚本同步）:
    # 直接运行（未显式指定 MIN_TARGET_GLIBC）时，超标说明编译机 glibc 高于默认
    # 基线（如兆芯/麒麟2503 = glibc 2.31）。包仍可在与编译机相同或更新 glibc
    # 的系统上运行 → 自动以编译机 glibc 为基线继续打包 + 醒目警告；
    # 统信1070/麒麟V10(2.28) 编译机上不会触发此分支，原行为零变化。
    # 显式指定 MIN_TARGET_GLIBC 时仍严格拦截。
    if [ -z "$MIN_TARGET_GLIBC_EXPLICIT" ] && [ -n "$BUILD_GLIBC" ]; then
        echo "  ⚠⚠⚠ 自动基线降级 ⚠⚠⚠"
        echo "  已自动以编译机 glibc($BUILD_GLIBC) 为目标基线继续打包。"
        echo ""
        echo "  *** 重要适用范围提示 ***"
        echo "  本包仅保证在与编译机相同或更新 glibc 的系统上运行"
        echo "  （编译机: $(grep -m1 PRETTY_NAME /etc/os-release 2>/dev/null | cut -d\" -f2), glibc $BUILD_GLIBC）。"
        echo "  如需部署到统信UOS 1070 / 麒麟V10 SP1(glibc 2.28)，请在这些系统上重新编译打包，"
        echo "  或显式指定基线让本脚本强制拦截: MIN_TARGET_GLIBC=2.28 ./cross_platform_package.sh"
    else
        echo "  根因: 编译机 glibc(${BUILD_GLIBC:-未知}) 高于显式指定的目标基线 $MIN_TARGET_GLIBC。"
        echo "  修复: 在与目标机相同/更低 glibc 的系统上重新编译+打包；"
        echo "        或目标机基线确实更高时: MIN_TARGET_GLIBC=2.35 ./cross_platform_package.sh"
        echo "        （或编译机=目标机时一键:   MIN_TARGET_GLIBC=host ./cross_platform_package.sh）"
        if [ "${ALLOW_INCOMPATIBLE_GLIBC:-0}" != "1" ]; then
            rm -f "$GLIBC_REPORT"
            exit 1
        fi
        echo "  （ALLOW_INCOMPATIBLE_GLIBC=1，忽略不兼容继续打包——目标机安装后将无法运行！）"
    fi
else
    echo "  ✓ 包内所有 ELF 的 GLIBC 依赖均 <= $MIN_TARGET_GLIBC"
fi
rm -f "$GLIBC_REPORT"

# 11. 打包成压缩文件
cd ..
PACKAGE_NAME="ELSKYTestClient_Universal_${VERSION}.tar.gz"
tar -czf "$PACKAGE_NAME" "$(basename $DEPLOY_DIR)"

# 11.5 验证压缩包内容
echo ""
echo "压缩包内容预览:"
tar -tzf "$PACKAGE_NAME" | grep -E "\.so|ELSKY_test_client|wheel|REQUIREMENTS|install_python_deps" | head -25

echo ""
echo "=== 跨平台打包完成 ==="
echo "部署包: $(pwd)/$PACKAGE_NAME"
echo "大小: $(du -h $PACKAGE_NAME | cut -f1)"
echo ""
echo "包含的平台版本:"
if [ -f "$BIN_DIR/ELSKY_test_client_ft" ]; then echo "  ✓ 飞腾ARM64版本"; fi
if [ -f "$BIN_DIR/ELSKY_test_client_x86" ]; then echo "  ✓ X86版本"; fi
if [ -f "$BIN_DIR/ELSKY_test_client_loong" ]; then echo "  ✓ 龙芯版本"; fi
echo ""
echo "串口支持状态:"
if [ "$SERIALPORT_ENABLED" = true ]; then
    echo "  ✓ 已启用串口功能 (Qt5SerialPort)"
    echo "  ✓ lib/ 目录已包含 Qt5SerialPort 库"
    echo "  ⚠ 目标机器如无该库, 启动脚本会自动安装 libqt5serialport5"
else
    echo "  ✗ 未启用串口功能 (可执行文件未链接 Qt5SerialPort)"
    echo "  ⚠ 请检查编译时是否安装 libqt5serialport5-dev"
fi
echo ""
echo "详细依赖检查报告: $DEPLOY_DIR/DEPS_CHECK.txt"
echo ""
echo "Python 依赖 (老化测试):"
if [ -d "$DEPLOY_DIR/python_modules/PyQt5" ]; then
    PYMOD_SIZE=$(du -sh "$DEPLOY_DIR/python_modules" 2>/dev/null | awk '{print $1}')
    echo "  ✓ 已打包完整 Python 模块 (大小: $PYMOD_SIZE)"
    echo "  ✓ 目标机开箱即用，无需联网安装"
    echo "  ✓ 启动时通过 PYTHONPATH 自动加载"
elif [ -d "$DEPLOY_DIR/python_modules/apt_packages" ]; then
    DEB_COUNT=$(ls -1 "$DEPLOY_DIR/python_modules"/apt_packages/*.deb 2>/dev/null | wc -l)
    echo "  ✓ 已打包 $DEB_COUNT 个 .deb 离线包"
    echo "  ✓ 启动时自动 dpkg 安装"
else
    echo "  ⚠ 未收集到 Python 依赖"
    echo "  ⚠ 目标机需手动执行: sudo apt install python3-pyqt5 python3-pyqt5.qtopengl python3-psutil python3-opengl"
fi
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
echo "      包含串口库和 Python 老化测试离线依赖"
