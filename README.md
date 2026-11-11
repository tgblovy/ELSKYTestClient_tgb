# 国产平台 ELSKY 研盛测试软件（ELSKYTestClient）

**版本**：V2.1　|　**框架**：Qt 5.12+（兼容 Qt 6.x）　|　**平台**：飞腾 / 龙芯 / 兆芯 / 海光 / Intel / AMD
**适用系统**：银河麒麟 V10 SP1 / 2503、统信 UOS 1070 / V20 / V25、Ubuntu 18.04+、Debian 10+（**dpkg 系**）
**配套文档**：[《整改优化回归分析报告_ELSKYTestClient_V2.1.md》](./整改优化回归分析报告_ELSKYTestClient_V2.1.md) —— 全量异常 Bug、优化内容、回归验证与发版铁律

一款面向**国产化硬件平台**的综合测试与硬件评分客户端，覆盖电源管理、网络唤醒、设备监控、循环压力测试、串口 / USB / 网络测速、系统综合跑分、老化测试、非法关机检测、远程统一控制等场景。所有功能在麒麟 / 统信 / Ubuntu 等国产 Linux 桌面环境**离线运行**，Qt 库、Python 依赖、外部工具与 glmark2 场景数据随包携带。

> **CentOS / RHEL / Fedora 为 rpm 系，当前 `.deb` 包无法安装**。如需支持须单独出 RPM 包与 RHEL 构建脚本；源码编译方式不受此限制。

---

## 发版与部署铁律（先读这一段）

| # | 铁律 | 原因 |
|---|---|---|
| 1 | **编译机与目标机 glibc 同源**，且编译机 glibc **不得高于**目标机 | glibc 只能向上兼容。在麒麟2503 / 新 Ubuntu 上编译的包拿到统信1070（glibc 2.28）安装，必报 `GLIBC_2.29/2.30 not found`，任何打包技巧都救不回来。打包脚本已内置 GLIBC 门禁，超标直接中止 |
| 2 | **任何源码改动后必须全量重编译 + 重打包** | 历史多次出现"源码已修、现场仍是旧逻辑"的发布产物漂移（见整改报告 A1） |
| 3 | 打包日志必须全绿：零网络 Python 依赖、glmark2 二进制 + 场景数据、GLIBC 门禁、`visudo -c` 校验；**统信 / 麒麟源无 glmark2，须先放好 `glmark2_offline_<架构>.tar.gz` 资产包再打包** | 四项任一失败，产出的包在目标机必有功能缺失。现场多次出现「打包成功但 glmark2 仍缺失」，根因就是漏放资产包 |
| 4 | 目标机装包后先跑 `sudo bash regression_test_elsky.sh`，全绿再测功能 | 回归脚本已覆盖 GLIBC、Python 依赖、外部命令、sudoers、glmark2 数据、循环休眠等专项 |
| 5 | **拔网线复测一遍**老化测试 / WOL / 跑分内置模式 | 验证离线自足，不依赖现场 apt 源 |
| 6 | 卸载验证：点「卸载研盛测试客户端」独立图标或 `sudo apt purge elsky-test-client` 后，两个桌面图标、`~/.config/ELSKY/`、sudoers、兜底二进制应清理干净 | postrm 已重写，用 marker 区分"本包安装"与"系统自装"，不误删系统组件；**启动器里对主图标右键弹出的那个「卸载」是 DDE 硬编码菜单，走 lastore 应用商店 D-Bus，第三方 `dpkg -i` 包修不好，请勿使用** |

---

## 目录

- [功能总览](#功能总览)
- [运行环境与安装](#运行环境与安装)
  - [deb 包安装（推荐）](#deb-包安装推荐)
  - [从源码编译](#从源码编译)
  - [支持的硬件架构](#支持的硬件架构)
  - [离线依赖清单](#离线依赖清单)
- [各功能详解](#各功能详解)
  - [基础测试（含跑分测试）](#基础测试含跑分测试)
  - [老化测试（3D + glmark2 交替压测）](#老化测试3d--glmark2-交替压测)
  - [串口（COM）测试](#串口com测试)
  - [系统实时监控](#系统实时监控)
  - [网络唤醒 (WOL)](#网络唤醒-wol)
  - [设备实时监控 / 循环控制器 / 开机自启](#设备实时监控--循环控制器--开机自启)
  - [非法关机检测](#非法关机检测)
  - [服务器连接 / 标签管理 / 日志](#服务器连接--标签管理--日志)
- [常见问题与排错](#常见问题与排错)
  - [编译打包问题](#编译打包问题)
  - [安装与启动问题](#安装与启动问题)
  - [glmark2 / 3D 老化问题](#glmark2--3d-老化问题)
  - [硬盘温度 / 系统卡顿问题](#硬盘温度--系统卡顿问题)
  - [COM 串口问题](#com-串口问题)
  - [跑分与依赖问题](#跑分与依赖问题)
  - [其它常见问题](#其它常见问题)
- [开发说明](#开发说明)
- [版本历史](#版本历史)

---

## 功能总览

| 功能模块 | 主要用途 |
|---|---|
| **基础测试** | 关机 / 重启 / 休眠 / 串口测试 / USB 测速 / 网络测速 / 老化测试 / 系统综合跑分 |
| **老化测试** | CPU / 内存 / 磁盘 / 网络 / 2D / **3D + glmark2 交替压测**，多实例独立窗口 |
| **网络唤醒 (WOL)** | 通过局域网发送魔术包远程开机 |
| **系统实时监控** | CPU 温度 / 占用、内存、磁盘、**硬盘温度（三级兜底）**、开机时间 |
| **设备实时监控** | 局域网存活设备扫描与状态显示 |
| **循环控制器** | 自动循环执行开 / 关机 / 休眠 / 老化任务（休眠自带 RTC 唤醒） |
| **开机自启管理** | systemd user service + .desktop autostart 双通道 |
| **非法关机检测** | 标志文件 + journalctl + last-x + suspend.marker 多层判定，累加计数与历史记录 |
| **服务器连接 / 统一控制** | 连接测试服务器，多节点集中控制 |
| **标签管理 / 日志查看** | 设备标签维护；操作日志滚动、过滤、导出 |

---

## 运行环境与安装

### deb 包安装（推荐）

```bash
# 1. 安装（按本机架构选包）
sudo dpkg -i elsky-test-client_2.1_amd64.deb      # x86_64（海光 / 兆芯 / Intel / AMD）
sudo dpkg -i elsky-test-client_2.1_arm64.deb      # aarch64（飞腾 / 鲲鹏）
# 龙芯（LoongArch / MIPS64el）不要用 arm64 包，架构名以本机 dpkg 输出为准：
sudo dpkg -i elsky-test-client_2.1_$(dpkg --print-architecture).deb

# 2. 若 dpkg 报缺依赖，自动补齐
sudo apt-get install -f -y

# 3. 启动
elsky-test-client          # 或桌面双击"研盛测试客户端"
```

安装过程（postinst）会自动完成：

1. **写入 sudoers 免密**（`/etc/sudoers.d/elsky-test-client`），覆盖 `apt-get / apt / dpkg / ethtool / nmcli / systemctl / poweroff / reboot / shutdown / **smartctl**`；对 `/etc/passwd` 中所有 uid ≥ 1000 的真实用户逐行授权，并用 `visudo -c -f` 校验，失败则丢弃写入以防整机 sudo 失效；
2. **补齐 glmark2 场景数据**到 `/usr/share/glmark2`（shaders / models / textures），并校验关键文件（如 `shaders/light-basic.vert`）；
3. **兜底安装** `glmark2` 二进制、`ethtool` 到 `/usr/local/bin`，用 marker 标记归属，卸载时只删自己装的；
4. **为所有真实用户放桌面图标**（桌面 / Desktop / XDG user-dirs 三级探测，覆盖中英文系统）；
5. GLIBC / Python 依赖自检，异常时给出中文诊断而非英文 symbol 报错。

> **免密不是"全局 sudo"**：只授权上述具体命令路径。`smartctl` 在 V2.1 才补进白名单 —— 旧版包缺这条会引发硬盘温度兜底反复认证失败、日志刷屏直至系统卡死，详见[硬盘温度 / 系统卡顿问题](#硬盘温度--系统卡顿问题)。

### 从源码编译

**环境准备**：

| 系统 | 命令 |
|---|---|
| Ubuntu / Debian / 麒麟 / 统信 | `sudo apt-get install build-essential qtbase5-dev libqt5serialport5-dev qttools5-dev-tools` |
| rpm 系（仅源码编译） | `sudo dnf install gcc-c++ make qt5-qtbase-devel qt5-qtserialport-devel qt5-qttools-devel` |

```bash
cd ELSKYTestClient_tgb
./cross_platform_build.sh                 # 编译，产物 bin/ELSKY_test_client_{ft|x86|loong}
./cross_platform_package_deb.sh           # 打 deb；或 cross_platform_package.sh 打 tar 版
```

编译脚本会**强制校验 `qmake -query QT_VERSION` 必须为 5.\*** 并导出 `QT_SELECT=5`。统信1070 的 `/usr/bin/qmake` 是 qtchooser 链接且默认指向 Qt4，旧版脚本会误选导致编译崩溃；现会跳过失效链接并打印明确的安装指引。

**打包机要求**：

- glibc ≤ 目标机（见[发版铁律](#发版与部署铁律先读这一段)），可用 `MIN_TARGET_GLIBC=2.28 ./cross_platform_package_deb.sh` 显式声明基线；
- 若编译机已安装 `PyQt5 / psutil / OpenGL`，打包走**「零网络」策略**：直接从 dist-packages 复制进包并验证 import，同机同 ABI，完全不依赖 apt 源；
- glmark2 获取顺序：`apt-get download` → `offline_debs/*.deb` → **`glmark2_offline_<架构>.tar.gz` 资产包自动解包** → `offline_bin/glmark2/` 预置二进制 + 数据 → 编译机本地已装 → 源码编译（末级兜底）。**麒麟 / 统信官方源不收录 glmark2 属正常现象**；把网盘提供的 `glmark2_offline_amd64.tar.gz` / `glmark2_offline_arm64.tar.gz` 放在项目根目录（桌面 / 下载目录也会自动搜索）即可自动收进包，**无需编译、无需联网、无需 root**。确实缺失时该项才自动降级，不影响其它功能。

### 支持的硬件架构

| 架构 | 厂商 / 型号 | 适用 deb |
|---|---|---|
| **x86_64** | Intel / AMD、海光 7000/8000、兆芯 KH-30000 / KX-6000 / KX-U6780 | `*_amd64.deb` |
| **aarch64** | 飞腾 D2000 / D3000M、鲲鹏 | `*_arm64.deb` |
| **loongarch64 / loong64 / mips64el** | 龙芯 3A5000 / 3A6000 / 3C5000（LoongArch）、3A3000 / 3A4000（MIPS64el） | `*_<本机 dpkg 架构>.deb` |

> 脚本默认本机架构编译；交叉编译需对应工具链与 Qt 库。飞腾 FT-2000/2500 的产物名 `_ft2000/_ft3000` 已做「精确名 → 通配」两级查找。
> 龙芯产物统一为 `ELSKY_test_client_loong`，且**不加任何 `-march`**（龙芯 g++ 不识别 ARM / X86 的 `-march`，硬加会直接编译失败），由本机编译器按原生架构编译。deb 的 `Architecture:` 取本机 `dpkg --print-architecture` 真实值，避免 `package architecture does not match system` 拒绝安装。

### 离线依赖清单

deb 包自包含以下运行时依赖（`dpkg -L elsky-test-client` 或安装后查看 `/usr/lib/elsky-test-client/DEPENDENCY_CHECK.txt`）：

| 路径 | 内容 |
|---|---|
| `lib/` | 编译机同版本 Qt 5/6 全家桶 + xcb 平台插件 + xcbglintegrations |
| `python_modules/` | PyQt5 / psutil / OpenGL（解包或零网络复制形态，**直接 import，无需 dpkg -i、无需密码、无需联网**） |
| `python_modules/apt_packages/*.deb` | 第二层保险：跨发行版 import 失败时回退离线安装 |
| `extra-bin/glmark2/` | glmark2 二进制 + `/usr/share/glmark2` 场景数据（shaders / models / textures） |
| `extra-bin/ethtool` | WOL 依赖 |

启动脚本保留三级自愈（离线 dpkg → 在线 apt → ld.so 兜底），但**基础库缺失与 GLIBC 不兼容是两回事**：后者会在启动/安装时直接给出中文根因说明，不会盲目走无效的自愈链。

---

## 各功能详解

### 基础测试（含跑分测试）

下拉框选择测试项 → 点击"执行"：

| 测试项 | 作用 | 所需环境 |
|---|---|---|
| 关机 / 重启 / 休眠 | 电源管理 | root/sudo（deb 已配免密） |
| 串口测试 | 检测 / 发送 / 接收串口数据 | 串口设备，支持多窗口 |
| USB 速度测试 | USB 存储读写速度 | 至少 1 个 USB 设备 |
| 网络测速 | 内网 / 外网测速 | 至少 1 个对端 |
| **跑分测试** | CPU / GPU / 内存 / 磁盘综合评分 | 见下 |
| 老化测试 | 长时间压力测试 | 独立后台进程 |

#### 跑分测试

- **离线 / 内置模式**：内置算法评分，标注"内置测评"，分数仅用于本工具内横向对比；
- **联网 / 专业模式**：自动询问是否安装 glmark2 / fio / iozone / x11perf 等权威工具，给出可与公开评测对比的分值。

| 项目 | 权重 | 内置模式 | 专业模式 |
|---|---|---|---|
| CPU | 30% | 整数 / 浮点 / 素数加权 | 优先 UnixBench |
| GPU | 25% | OpenGL 离屏渲染 | 优先 glmark2 → glxgears |
| 内存 | 25% | STREAM Triad | 优先官方 STREAM |
| 磁盘 | 20% | 256MB 顺序 + 4K 随机 | 优先 iozone + fio |

评分采用 P50/P99 双点对数插值，报告自带"业界对标"与"数据可比性分级"。**测速失败只报错，不再上报伪造数据**（V2.0 修复 C1）。内置模式约 1~2 分钟，专业模式约 15~30 分钟。

### 老化测试（3D + glmark2 交替压测）

V2.1 起 **3D 测试与 glmark2 合并为同一项**，界面上不再有独立的 glmark2 勾选项：

```
原有 3D 画面（约 120s） → 销毁 GL 画布 → glmark2 跑完一轮 → 重建 3D 画面 → …… 循环，
直到老化时长结束或手动停止
```

实现要点：

- `aging_test_gui.py` 的 `_gpu3d_merged_worker()` 负责交替循环；两者**不同时持有 GL 上下文**，避免早期"3D 画面色块 / 花屏"的上下文冲突；
- `_make_glmark_env()` 为 glmark2 子进程净化 `LD_LIBRARY_PATH`（包内 Qt 自带的 GL 库会污染景嘉微等国产驱动），必要时切软件渲染（`LIBGL_ALWAYS_SOFTWARE=1` / `GALLIUM_DRIVER=llvmpipe`）；
- 四级启动策略 + 多变体候选（X11 / Wayland / es2），`select` 防管道死锁，`stdbuf -oL` 保证 FPS 实时可见；
- **场景数据自愈**：`_glmark_ensure_system_data()` 在 `/usr/share/glmark2` 缺/残时，先尝试直接补齐，再尝试 `sudo -n` 非交互提权，都不行则在日志与弹窗给出精确命令。数据本来就在包内 `extra-bin/glmark2/usr/share/glmark2`；
- 找不到 glmark2 时该项安静降级，**不阻塞其它老化项**。

> **glmark2 版本差异**：新版支持 `--data-path` 重定向；麒麟2503 自带的 `2014.03+git20150611.fa71af2d` **不支持** `--data-path`（连 `--version` 都不认），只读编译期硬编码的 `/usr/share/glmark2`。`GLMARK2_DATA_PATH` 环境变量上游全系不读取。因此 V2.1 的统一解法是**把数据铺到系统路径**，而不是依赖参数重定向。

### 串口（COM）测试

- 支持**同时打开多个独立串口测试窗口**（非模态、多实例）；
- V2.1 起每个窗口是 **`Qt::Window` 独立顶层窗口**：主界面最小化时 COM 窗口**不会跟着最小化**，任务栏有各自条目；
- 接收区 `QTextEdit` 限制 2000 行（`setMaximumBlockCount(2000)`）+ 批量 append，长时间大量收发（RX/TX 数十 MB）不会内存无限增长导致 5 个界面同时卡死；
- 静默循环收发路径已移除 UI 线程的 `waitForBytesWritten(1000)` 同步阻塞；
- 停止位三档映射已校正：1 → OneStop、1.5 → OneAndHalfStop、2 → TwoStop（旧版 1.5 与 2 互换）。

### 系统实时监控

显示 CPU 温度 / 占用、内存、磁盘、开机时间、**硬盘温度**。硬盘温度采用三级兜底：

1. `sysfs`（`/sys/class/hwmon`、`/sys/block/.../temperature`）；
2. `hwmon` 属性；
3. `smartctl -A <设备>` 解析 SMART 194 / NVMe Temperature（需免密 sudo；root 运行时直接执行，不经 sudo）。

V2.1 对该兜底做了限流：成功结果缓存 60s、失败也缓存 60s、授权失败熔断 5 分钟、单轮最多 2 个设备 / 单次 800ms / 总预算 2s，并删除了无效的 `smartctl --scan /dev/xxx` 调用。读不到时界面显示「硬盘温度: 未检测到」，不影响其它监控项。

### 网络唤醒 (WOL)

填入目标 MAC → UDP 广播魔术包。依赖 `ethtool`（包内携带，postinst 兜底安装到 `/usr/local/bin`）；Magic 模式设置不再限定麒麟 / 统信，Ubuntu / Debian 同样生效（ethtool + nmcli 双保险）。

### 设备实时监控 / 循环控制器 / 开机自启

- **设备监控**：`ip link` 只解析形如 `^\d+:\s*(\S+):` 的主行，MAC 续行不再被误报为"网络设备插入"；
- **循环控制器**：循环休眠使用 `rtcwake -m mem -s N` 把"休眠 N 秒 + RTC 自唤醒"做成原子操作，interval 在循环语义下真正表示休眠时长；无 rtcwake / 无权限时自动降级为普通 suspend，不阻塞用户；
- **开机自启**：DISPLAY / XAUTHORITY 动态探测，X11 与 Wayland 均可用；systemd service 与 .desktop autostart 双通道。

### 非法关机检测

多层判定（开机自动运行）：

1. **标志文件**：运行时创建 `running.flag`，正常退出删除；启动时存在 = 非法关机；
2. **suspend.marker 第零点五层**：suspend 前 fsync 落盘，若 `bootTime - markerTime < 60s` 判定为"休眠期间被立即断电"的非法关机；≥ 60s 视为正常使用残留并清除；
3. **journalctl**：上次启动日志中是否有正常 shutdown 记录；
4. **last -x**：系统 shutdown 记录。

支持长按电源 / 直接断电识别、连续多次累加计数、历史记录全部保留。报告保存在 `~/.config/ELSKY/illegal_shutdown/`。

> **已知局限**：休眠十几小时后再断电，marker 时序启发式无法覆盖（gap 远大于 60s 阈值），依赖 dmesg 文件系统恢复证据；建议同时开启开机自启 + dmesg 监控。`interval < 25s` 的循环休眠后立刻正常关机可能误报，实际使用建议 interval ≥ 25s。

### 服务器连接 / 标签管理 / 日志

连接测试服务器（IP / 端口），支持多节点集中控制；标签维护设备型号 / 批次 / 编号；日志实时滚动、搜索 / 过滤 / 导出。

---

## 常见问题与排错

### 编译打包问题

#### ❌ `could not exec '/usr/lib/aarch64-linux-gnu/qt4/bin/qmake'`（统信1070）

`/usr/bin/qmake` 是 qtchooser 链接且默认指向 Qt4。**新版脚本已强制校验 Qt5**：

```bash
./cross_platform_build.sh 2>&1 | grep -i "找到可用的 Qt5 qmake"
# 应看到 "... (Qt5, 校验通过)"，不再出现 qt4 字样
```

若系统确实没有 Qt5 开发包，脚本会打印对应发行版的安装指引而不是半路崩在 make。

#### ❌ 打包日志提示"没有 glmark2"，统信 / 麒麟 apt 也下载不到

UOS / 麒麟官方源不收录 glmark2，**不是打包失败**，该项属可选增强；但要让目标机真正跑起 glmark2，按下面做（V2.1 二次修复后的推荐路径）：

```bash
# 1. 从网盘取对应架构的资产包（二选一）
#    x86_64 / 兆芯 / D2000 / D3000M  → glmark2_offline_amd64.tar.gz
#    飞腾 ARM64                      → glmark2_offline_arm64.tar.gz
# 2. 放到与 cross_platform_package_deb.sh 同目录即可（放桌面 / 下载目录也能自动找到）
#    不需要手工解压、不需要 root、不需要联网
# 3. 重新打包，日志出现下面两行即成功：
#    ℹ 发现离线资产包 ...，自动解包到项目根目录...
#    ✓ glmark2 已从 offline_bin/ 收进包（二进制 + 场景数据成套）
```

资产包内是 Ubuntu bionic `glmark2 2014.03+git20150611.fa71af2d-0ubuntu7`，最大只要求 `GLIBC_2.27`，兼容统信1070 / 麒麟V10 SP1 的 glibc 2.28；内含 `usr/bin/glmark2` + `glmark2-es2` 与成套 `shaders / models / textures`（79 / 7 / 48 个文件）。

> **不要**直接抓 Debian 12+ / Ubuntu 22.04+ 的 `glmark2-x11 2023.01`：它要求 `GLIBC_2.34`，装到 glibc 2.28 的机器上必报 `GLIBC_2.34 not found`。也不要再把 `offline_debs/` 当首选方案 —— 源里根本没有包时这条路走不通。

**打包脚本不会自动安装 meson / ninja 或任何系统包**（V2.1 二次修复）：早前版本在打包里跑 `apt-get install`，统信 UOS 现场会触发 `SIGTTIN` 使进程进入 `T(stopped)`，`timeout` 的 `SIGTERM` 杀不掉 stopped 进程，整个打包永久卡住。现在工具只读探测，缺失时源码编译级直接快速失败并给出离线资产指引，打包继续、不再挂起。

#### ❌ 打包中止："不满足全功能离线运行" / "产物非本机编译"

- **离线依赖缺失**：优先走零网络方案 —— 在编译机 `sudo apt install python3-pyqt5 python3-pyqt5.qtopengl python3-psutil python3-opengl` 后重新打包，脚本直接从系统复制，无需下载；
- **外来二进制**：`bin/` 里的主程序是在别的（更高 glibc）机器编译的。必须在目标基线系统本机清理重编：

```bash
rm -rf build bin/ELSKY_test_client_*
./cross_platform_build.sh && ./cross_platform_package_deb.sh
```

#### ❌ 打包被 GLIBC 门禁中止

门禁扫描包内**全部 ELF** 的最大 GLIBC 符号版本需求，超过 `MIN_TARGET_GLIBC`（默认 2.28）即中止并列出违规文件。**不要用 `ALLOW_INCOMPATIBLE_GLIBC=1` 掩盖** —— 产出的包在目标机必死。正确做法是换到同基线系统编译。

### 安装与启动问题

#### ❌ 安装后启动报 `version 'GLIBC_2.29/2.30' not found`

包是在更高 glibc 系统上编译的，**缺的不是某个库，而是库要求的 glibc 版本超出本机**。启动 / 安装脚本会给出中文根因（含本机 glibc 版本与解决办法）。解决：在统信1070 / 麒麟V10 SP1 / Debian10（glibc 2.28）**本机重新编译打包**，卸旧装新。

#### ❌ 桌面双击无反应 / 终端报缺 Qt 库

```bash
ls /usr/lib/elsky-test-client/lib/libQt5Core.so*
/usr/lib/elsky-test-client/elsky-test-client.sh   # 终端直跑看真实错误
```

麒麟 V10 / 统信的 `.desktop` 嵌套引号问题已修（用 `Path=` 字段替代 `cd "..."`）；安全中心拦截 launcher 的问题已通过 `QProcess::startDetached` 绕开。老版本 deb 才有这些现象。

#### ❌ `elsky-test-client: command not found`

```bash
hash -r && elsky-test-client
# 或 /usr/bin/elsky-test-client
```

#### ❌ 统信 / deepin 启动器右键「卸载」点击无反应

**这是桌面环境限制，本软件无法修复，也不再尝试修复。** 查 `dde-launcher` 源码确认：启动器右键菜单由 `MenuWorker` 硬编码生成，那个「卸载」固定走

```
appId → .desktop → dpkg -S 取包名 → org.deepin.dde.Lastore1.PackageExists / RemovePackage
```

即依赖应用商店（lastore）记录，对 `dpkg -i` 手工安装的第三方包常点击无反应。deepin Wiki 原文：「启动器卸载仅对部分应用有效，请勿依赖于该卸载方式」；官方 issue 亦注明它只在装了 deepin-store 的环境生效。另外 freedesktop 标准的 `Actions=` 只有 **dde-file-manager 桌面图标右键**才读，启动器右键菜单不读——所以「给 `.desktop` 加 Desktop Action 让启动器多一个卸载项」这条路是走不通的。

V2.1 第三轮改为注册一个**普通应用图标**「卸载研盛测试客户端」（`Terminal=true`），只要是应用，启动器必然枚举显示：

- 启动器 / 应用菜单 / 桌面 →「卸载研盛测试客户端」→ 双击，打开终端显示卸载全过程与结果；
- 桌面主图标右键 →「卸载研盛测试客户端」（文件管理器右键，读 `Actions=`）；
- 终端 `sudo apt purge -y elsky-test-client`。

卸载脚本优先用 postinst 已配置的 **sudoers 免密 `sudo -n`**（绕开 `pkexec` / polkit / 统信安全中心可能的拦截），`pkexec` 兜底，root 直跑；成功失败都明确输出并写 `/tmp/elsky-test-client-uninstall.log`。

**已装旧包、不想重新打包**：直接在目标机跑 `sudo bash fix_uninstall_on_site.sh`（网盘提供），幂等补装独立卸载图标 + 卸载脚本 + 诊断脚本，不改 dpkg 数据库。定位问题跑 `bash /usr/lib/elsky-test-client/diagnose_uninstall.sh`，把 `/tmp/elsky-uninstall-diagnose.txt` 发回即可。

#### ❌ 桌面没有图标 / 卸载后有残留

postinst 枚举所有 uid ≥ 1000 的真实用户逐个放图标（覆盖软件中心安装拿不到 `SUDO_USER` 的场景）；postrm 清理所有用户桌面图标、`~/.config/ELSKY/`、sudoers 与兜底二进制。验证：

```bash
sudo apt purge elsky-test-client
# 核对：桌面无图标、~/.config/ELSKY 不存在、/etc/sudoers.d/ 无 elsky 文件
```

#### ❌ 跑分专业模式 / 电源管理仍弹密码

```bash
sudo cat /etc/sudoers.d/elsky-test-client
# 应包含当前用户与下列命令路径（V2.1 含 smartctl）：
#   /usr/bin/apt-get, /usr/bin/apt, /usr/bin/dpkg, /usr/sbin/ethtool, /usr/bin/nmcli,
#   /usr/bin/systemctl, /usr/sbin/systemctl, /usr/sbin/poweroff, /usr/sbin/reboot,
#   /usr/sbin/shutdown, /usr/bin/poweroff, /usr/bin/reboot, /usr/bin/shutdown,
#   /usr/sbin/smartctl, /usr/bin/smartctl
```

若文件不存在或缺少当前用户，重新用新包安装即可；手工写入后务必 `sudo chmod 440` 并 `sudo visudo -c -f` 校验。

### glmark2 / 3D 老化问题

#### ❌ 原有 3D 画面跑完后 glmark2 黑屏（但日志有 FPS）

**典型根因：场景数据缺失**，不是 GL 驱动问题。glmark2 会正常开窗、正常输出 FPS，但 `shaders / models / textures` 加载全部失败 → 所有场景 `Set up failed` → 全黑。日志特征：

```
Failed to open models directory '/usr/share/glmark2/models'
Failed to open ".../shaders/light-basic.vert"
```

**现场一键修复**（数据就在本机包里，无需联网、无需重新打包、无需重启软件）：

```bash
sudo mkdir -p /usr/share/glmark2
sudo cp -r /usr/lib/elsky-test-client/extra-bin/glmark2/usr/share/glmark2/{shaders,models,textures} /usr/share/glmark2/
# 补完直接重跑老化即可出画面
```

V2.1 已在两处根治：postinst 安装时校验 `.vert ≥ 5` 并整体替换残缺目录；运行时 `_glmark_ensure_system_data()` 自愈。

#### ❌ 为什么 D3000M 能跑、D2000 / 兆芯不能跑

D3000M 那台 `/usr/share/glmark2` 本身是完整的，程序走"系统数据完整 → 裸跑"路径；D2000 / 兆芯手工编译残留或新装系统该目录为空/半残。程序想用 `--data-path` 指向包内数据，但麒麟自带 `2014.03` 老版本**不支持**该参数（环境变量 `GLMARK2_DATA_PATH` 也无效），只能读硬编码路径 —— 所以统一解法是把数据铺到 `/usr/share/glmark2`。

#### ❌ 3D 与 glmark2 同时跑时画面有色块 / 花屏

早期版本两者并行持有 GL 上下文导致。V2.1 已改为**交替循环**：3D 画面 → 销毁 GL 画布 → glmark2 一轮 → 重建 3D，两者不同时占用 GL 上下文。

#### ❌ glmark2 跑完一轮后没有回到 3D，隔一会儿又跑 glmark2

先确认是否为**目标机 GL/3D 运行环境问题**（该现象在部分机型上由系统层引起）。代码侧准备的窗口显示回执 / 销毁时序修复**尚未合入 V2.1**（经确认为系统问题，暂不优化），详见整改报告「遗留问题」章节。

#### ❌ 老化测试 3D 整体黑屏 / 缺 OpenGL

```bash
echo $DISPLAY                      # 空 = 无 X11，需在物理机本地桌面运行
glxinfo | grep "OpenGL renderer"   # llvmpipe = 软渲染，能跑但慢
glxgears                           # 60+ fps 说明 3D 正常
```

依赖安装（Python 侧与系统库）：

```bash
sudo apt-get install python3-pyqt5 python3-pyqt5.qtopengl python3-opengl \
  python3-psutil freeglut3 libgl1 libglu1-mesa libegl1 mesa-utils
```

包内已带 PyQt5 / psutil / OpenGL 离线形态，正常安装无需上述命令。顶层 import 有降级守卫，缺依赖时 2D 老化仍可用并给出安装指引。

### 硬盘温度 / 系统卡顿问题

#### ❌ 软件一直在"获取硬盘信息"，最终系统卡死 / 死机

**这是 V2.1 修复的重点故障**。现象：终端 / journal 里每 1~2 秒出现

```
sudo: pam_unix(sudo:auth): auth could not identify password for [it0]
sudo:   it0 : a password is required ; TTY=unknown ; PWD=... ; USER=root ;
        COMMAND=/usr/sbin/smartctl -A /dev/nvme0n1
```

**根因链**：主界面「硬盘温度」第三级兜底调 `sudo -n smartctl` → 打包脚本的 sudoers 白名单**从未包含 smartctl**（旧版包）→ 授权失败 → 旧代码失败不缓存、不退避，2 秒定时器每轮重跑整条兜底链，还额外拉一次无效的 `smartctl --scan /dev/xxx` → PAM/audit 日志持续刷屏写满 `/var`；同时 `waitForFinished()` 在 **UI 主线程同步阻塞**（每设备 3s + 2s）→ 界面假死。叠加 phytium_pstate 调频注册失败等平台驱动告警，系统容错下降，最终表现为死机。

> 注意：代码用的是 `sudo -n`（非交互），**本来就不会弹密码框**；问题不是"没处理密码交互"，而是**没处理授权失败的返回结果**。

**现场止血**（旧包已装机器，不用等重打包）：

```bash
# 补免密规则（把 it0 换成实际用户名）
echo 'it0 ALL=(ALL) NOPASSWD: /usr/sbin/smartctl, /usr/bin/smartctl' | \
  sudo tee /etc/sudoers.d/elsky-smartctl
sudo chmod 440 /etc/sudoers.d/elsky-smartctl && sudo visudo -c -f /etc/sudoers.d/elsky-smartctl
# 清理被刷满的日志
sudo journalctl --vacuum-size=200M
```

**永久修复（V2.1 已合入）**：失败也缓存 60s、SMART 成功缓存 60s、root 直跑不经 sudo、非 root 授权失败熔断 5 分钟且只提示一次、限 2 设备 / 单次 800ms / 总预算 2s、删除无效 `--scan`、sudoers 三条规则补 smartctl。仿真 2 秒定时器跑 10 分钟，sudo 调用从 **600 次降到 2 次（未配免密）/ 10 次（已配免密）/ 0 次（root）**。

### COM 串口问题

#### ❌ 5 个 COM 界面收发到 28~29MB 后全部卡死

接收区 `QTextEdit` 无限增长 + 逐行 append + UI 线程 `waitForBytesWritten(1000)` 同步阻塞。V2.1 已改为 `setMaximumBlockCount(2000)` + 批量 append + 移除静默循环的同步等待。

#### ❌ 多个 COM 窗口随主界面最小化一起最小化

`QDialog` 默认带 `Qt::Dialog` 标志，X11 下即使无 parent 也会被 Qt 写入 `WM_TRANSIENT_FOR` 并标记为 DIALOG 类型；麒麟 / 统信的 KWin / UKUI 系窗口管理器把这类窗口视为主窗口的附属对话框组，最小化主窗口时整组收起。V2.1 改为 **`Qt::Window` 独立顶层窗口**（`dialogs/com_test_dialog.cpp` 覆盖式设置 + `core/basic_tester.cpp` 删除 `| Qt::Dialog`）。可见变化：每个 COM 窗口在任务栏有独立条目 —— 这本就是"多个独立测试界面"应有的行为。

#### ❌ 串口 1.5 / 2 停止位通信乱码

旧版 `setStopBits(currentIndex()+1)` 把 1.5 与 2 恰好互换。V2.1 用显式映射表修正。改过档位，历史"碰巧能通"的用例需重新标定。

### 跑分与依赖问题

| 现象 | 处理 |
|---|---|
| 跑分时其它功能卡顿 | 已修：只降工作线程优先级（nice 19），升级到最新版 |
| GPU 报"不可测" | 无 GL 工具 / headless；装 `glmark2` 或 `x11perf`，离线场景内置离屏渲染自动接管 |
| 分数偏高 / 偏低 | 内置模式仅本工具内横向对比，**不能对标业内 benchmark**；看报告的"数据可比性分级" |
| 磁盘 IOPS 跳动 10 倍 | page cache + SSD GC 正常波动；测试前 `sync && echo 3 > /proc/sys/vm/drop_caches`（需 root），多跑取中位 |
| UnixBench 跑十几分钟 | 全套 12 项需 30~60 分钟，本软件默认跑 5 项子集约 10 分钟 |
| 网络测速内网必失败 / 报告出现 8.5 / 3.2 | 已修：ping 目标改为"默认网关 → 配置服务器 → 8.8.8.8"级联；失败只报错不再伪造数据 |
| 老化测试首次点击无反应 | 已修：依赖安装成功后不再提前 return |
| `ImportError: ... GLIBCXX_3.4.xx not found` | 系统 libstdc++ 过旧，`sudo apt-get install libstdc++6` |

离线环境下除"外网测速 / 跑分专业模式下载工具"（本身需要网络，有明确文案）外全部功能可用。

### 其它常见问题

| 现象 | 处理 |
|---|---|
| 首次启动窗口空白 / 慢 | 首次创建 `~/.config/ELSKY/` 并初始化状态文件，第二次启动正常 |
| 中文乱码 | `export LANG=zh_CN.UTF-8` 后启动 |
| CPU 占用高 | 后台跑分 / 老化 / 测速属预期，均工作在低优先级线程 |
| 日志文件位置 | 操作日志 `~/.config/ELSKY/elsky-test-client.log`；非法关机 `~/.config/ELSKY/illegal_shutdown/`；跑分报告 `~/.config/ELSKY/benchmark/`；依赖清单 `/usr/lib/elsky-test-client/DEPENDENCY_CHECK.txt`；glmark2 阶段日志 `/tmp/glmark2_aging.log` |
| 彻底卸载 | 启动器 / 桌面 →「卸载研盛测试客户端」**独立图标**（双击，开终端显示结果）；或桌面主图标右键 →「卸载研盛测试客户端」；或终端 `sudo apt purge -y elsky-test-client`。诊断：`bash /usr/lib/elsky-test-client/diagnose_uninstall.sh` |

---

## 开发说明

### 项目结构

```
ELSKYTestClient_tgb/
├── core/                          # 核心模块（C++）
│   ├── client_window.*             # 主窗口（V2.1 窗口标题）
│   ├── basic_tester.*              # 基础测试分发 / COM 窗口创建
│   ├── benchmark_tester.*          # 跑分测试（双模式）
│   ├── system_monitor.*            # 系统监控 / 硬盘温度三级兜底
│   ├── system_controller.*         # 电源管理 / rtcwake 休眠
│   ├── cycle_controller.*          # 循环控制器
│   ├── illegal_shutdown_detector.* # 非法关机多层检测
│   ├── network_speed_tester.*      # 网络测速
│   ├── wake_on_lan.*               # 网络唤醒
│   ├── device_manager.*            # 设备监控
│   ├── privilege_helper.*          # 提权辅助
│   └── server_connector.*          # 统一控制
├── dialogs/                       # 弹窗模块（com_test_dialog 等）
├── common/                        # 通用协议
├── aging_test_gui.py              # 老化测试 GUI（3D + glmark2 交替压测）
├── aging_auto_start.py / complex_scene.py
├── cross_platform_build.sh        # 编译（Qt5 强校验 / GLIBC 检测）
├── cross_platform_package_deb.sh  # deb 打包（GLIBC 门禁 / 零网络 / postinst / postrm）
├── cross_platform_package.sh      # tar 版打包
├── regression_test_elsky.sh       # 目标机自动回归脚本
├── ELSKYTestClient.pro / main.cpp
└── README.md
```

### 技术栈

- **GUI**：Qt 5.12+ / Qt 6.x（C++11）；**后端测试**：Python 3 + PyQt5
- **打包**：qmake + GNU Make + dpkg-deb；**架构**：x86_64 / aarch64 / loongarch64 / loong64 / mips64el
- **通信**：TCP 自定义协议（`common/protocol.cpp`）

### 添加新功能

1. `core/` 下新增模块（.h/.cpp）；2. 更新 `ELSKYTestClient.pro` 的 `SOURCES/HEADERS`；3. 同步 `cross_platform_build.sh` 的 .pro 模板；4. 重新 `build → package`；5. 跑 `regression_test_elsky.sh`。

### 目标机回归

```bash
sudo bash regression_test_elsky.sh
```

脚本覆盖：安装结构、离线 ldd、Python 依赖、外部命令（含 rtcwake / glmark2 数据）、GLIBC 兼容性（2.5 节）、sudoers、GUI 冒烟、循环休眠与断电检测（7.5 节）、卸载残留。**真机矩阵**（统信1070 / 麒麟V10 SP1 / 麒麟2503 / Ubuntu / Debian × x86_64 / aarch64，含拔网线场景）仍需在每台目标机执行并留档，详见整改报告。**龙芯平台真机编译 / 打包 / 安装回归 ⏳ 待执行**：本轮已在 x86 环境完成 6 段架构分支的 60 组模拟对照与真实 qmake 解析验证，龙芯实机验证须在龙芯机器上补做。

---

## 维护团队

**作者**：唐贵宝、曾周　|　**单位**：研盛（ELSKY）
**适用硬件**：国产化平台（飞腾 / 龙芯 / 兆芯 / 海光 / Intel / AMD；x86_64 / aarch64 / LoongArch / MIPS64el）

---

## 版本历史

### V2.1（当前，2026-11）

**编译打包与离线分发**
- ✅ 统信1070 三问题整改：qmake 误选 Qt4 → 双脚本 Qt5 强校验；glmark2 转"可选增强"口径 + `offline_debs/` 离线兜底；新增 GLIBC 兼容性门禁（打包 / 启动 / postinst / 回归四道防线）
- ✅ 追加整改：零网络 Python 依赖复制、精简 apt 清单 + 预过滤 + 超时重试、双形态完整性校验、外来二进制检测、GLIBC 预检分级（致命 / 自动改用系统库）
- ✅ glmark2 场景数据成套离线携带：`glmark2-data` / `offline_debs` / `offline_src` 三级提取，postinst 校验关键文件并整体替换残缺目录
- ✅ **统信 UOS 二次现场修复 · 打包卡死**：构建工具改为只读探测 meson / ninja，打包脚本绝不自动 `apt-get` / `sudo -n` / `pip install`（现场 `SIGTTIN` → `T(stopped)` → `timeout` 失效导致永久挂起）；工具缺失时源码编译级快速失败，打包继续
- ✅ **统信 UOS 二次现场修复 · glmark2 真正可用**：新增 `glmark2_offline_<架构>.tar.gz` 资产包自动解包通道（项目根目录 / 桌面 / 下载目录自动搜索，架构精确匹配、`../` 路径逃逸校验）；资产包采用 Ubuntu bionic 老版本（最大 `GLIBC_2.27`）+ 成套场景数据，提供 amd64 / arm64 两个包，替代不兼容的 Debian 2023.01（`GLIBC_2.34`）
- ✅ 统信 UOS 二次现场修复 · 右键卸载（**部分有效**）：`.desktop` 新增 `Desktop Action Uninstall` 与 `X-Deepin-AppID`，随包提供 `uninstall.sh`。后经现场回归与 dde-launcher 源码确认：`Actions=` 只对文件管理器桌面右键有效，启动器右键菜单是硬编码的，该入口在启动器里不显示，问题未真正解决 —— 见下条第三轮
- ✅ **统信 UOS 三次现场修复 · 卸载入口（最终方案）**：确认启动器原生「卸载」= `org.deepin.dde.Lastore1.RemovePackage`，依赖应用商店记录，第三方包无法修复（deepin Wiki 明示勿依赖）；改为注册普通应用图标 `elsky-test-client-uninstall.desktop`（`Terminal=true` + `TryExec`，启动器必然枚举）并在桌面同步放置；卸载脚本改为 `sudo -n` 免密 → `pkexec` → root 三路径提权，成功失败均可见并写日志；新增只读 `diagnose_uninstall.sh` 现场诊断与 `fix_uninstall_on_site.sh` 免重打包补装脚本；postinst 刷新桌面数据库、postrm 清理两个桌面图标
- ✅ 全项目版本号统一为 2.1（窗口标题、`setApplicationVersion`、两个打包脚本、README 徽标与历史）

**老化测试 / glmark2**
- ✅ 3D 与 glmark2 合并为交替循环，去掉独立 glmark 勾选项，消除 GL 上下文冲突导致的花屏
- ✅ `_make_glmark_env()` 净化 `LD_LIBRARY_PATH`、四级启动策略、多变体候选、`select` 防死锁、行缓冲实时 FPS
- ✅ 修复场景数据缺失黑屏：postinst 补齐 + 运行时 `_glmark_ensure_system_data()` 自愈（老版本 glmark2 不支持 `--data-path`，统一铺数据到 `/usr/share/glmark2`）

**COM 串口测试**
- ✅ 修复 5 窗口大数据（RX/TX 28~29MB）全部卡死：`setMaximumBlockCount(2000)` + 批量 append + 移除 UI 线程同步等待
- ✅ 修复多窗口随主界面最小化联动：改为 `Qt::Window` 独立顶层窗口

**系统监控 / 稳定性**
- ✅ 修复 `sudo -n smartctl` 授权失败引发的认证日志风暴与系统卡死：缓存 / 熔断 / 降频 / 删无效 `--scan` / sudoers 补 smartctl

**平台兼容 / 龙芯**
- ✅ 恢复龙芯（LoongArch / 老龙芯 MIPS64el）兼容：编译、deb / tar 打包、启动、回归脚本补全架构分支；产物 `ELSKY_test_client_loong`；库目录 / Qt 插件 / 动态链接器追加龙芯 multiarch 候选
- ✅ 既有 x86_64 / aarch64 / i386 / i686 分支行为一字未改：60 组 `uname -m` 模拟对照输出逐字一致，真实 qmake 解析验证通过；纠正文档把龙芯误归为 aarch64 的描述

### V2.0（2026-09）

- ✅ 新增**系统综合跑分测试**（双模式：内置离线 / 联网专业）
- ✅ **非法关机检测**三层机制 + 累加计数 + 历史记录；追加 rtcwake 循环休眠与 suspend 断电检测
- ✅ 全功能**离线运行**：Qt 库 / PyQt5 / ethtool / glmark2 随包；零安装零密码零联网的老化依赖方案
- ✅ deb 包**硬拦截**坏包出厂（缺 Qt 库 / 缺 Python 主包 / GLIBC 不兼容直接打包失败）
- ✅ 启动脚本**三级自愈**；桌面图标全平台双击启动修复；postinst 多用户图标 + postrm 全面清理
- ✅ 20 项功能与稳定性修复：3D 场景切换崩溃、CPU 满载 fork 死锁、跨发行版 Python ABI、飞腾产物名、网络测速伪造数据、串口停止位互换、提权命令注入、`ip link` MAC 误报、GUI 线程阻塞、GLU 泄漏 / 除零等
- ✅ glmark2 集成与准确性核查：行缓冲实时 FPS、Wayland 变体自适应、二进制 + 场景数据双校验
- ✅ 跑分只降工作线程优先级，不影响主进程其它功能

### V1.9

- 基础测试 / 网络唤醒 / 循环控制 / 系统监控 / 统一控制

---

> **文档维护说明**：本 README 于 2026-11-09 按 V2.1 实际代码重写，同步修正了旧版中「CentOS 7+ 可用 deb」「首次运行老化测试自动 dpkg -i」「sudoers 示例缺 smartctl」等过时描述。全量异常 Bug 清单、根因分析、优化内容、回归验证结果与遗留风险，见配套文档 **《整改优化回归分析报告_ELSKYTestClient_V2.1.md》**（整合原《优化分析报告_ELSKYTestClient_V2.0》《优化总结与回归报告_ELSKYTestClient_V2.0》《整改报告_统信1070三问题_GLIBC兼容与离线打包_V2.1》）。
