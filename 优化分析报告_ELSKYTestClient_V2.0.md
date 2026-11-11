# ELSKYTestClient V2.0 优化分析报告

- **分析日期**: 2026-09-08
- **分析范围**: `core/` `dialogs/` `common/` C++ 源码、Python 老化测试脚本、`cross_platform_*.sh` 构建/打包脚本、`release/` 已发布产物（deb + deb_build）
- **目标要求**: ① 各架构各 Linux 发行版正确稳定运行；② 功能漏洞完善；③ deb 离线双击安装、依赖完整无遗漏；④ 优化清单（含好处/隐患/优化后注意点）；⑤ 回归测试与优化前后对比清单

---

## 一、总体结论

项目整体架构成熟：Qt 库全量离线打包 + qmake 精确定位 + `qt.conf`/`rpath`/`LD_LIBRARY_PATH` 三层保障 + 启动三级自愈 + deb 打包前 ldd 自检拦截，这套"离线 GUI 依赖"方案是扎实的，**Qt 库、xcb 平台插件、xcbglintegrations 均已正确随包携带**。

但经逐文件审查，发现以下 **4 类关键问题**，其中前两类会直接导致"发布的功能与源码不符 / 现场功能不可用或崩溃"：

1. **🔴 打包产物与源码漂移（最危险）** — 已发布的 deb（amd64 09-01、arm64 09-03）与打包暂存目录 `release/deb_build`（09-05）里的老化测试脚本是**旧版**：`aging_test_gui.py` 打包版仅 675 行，而当前源码 1829 行；`complex_scene.py` 内容也不同；`aging_selftest.py` 未打包。此外 09-08 上午又有 5 个 core C++ 文件（wake_on_lan / network_speed_tester / basic_tester / system_controller / privilege_helper）被修改，**所有现存 deb 均早于当前源码**。
   → 后果：源码里已修复的崩溃/缺陷根本没进发布包；现场用户测试的仍是旧逻辑。
2. **🔴 老化测试在多数目标机无法启动/崩溃** — 打包时把含 `cpython-38` C 扩展的 `psutil` 当"纯 Python"复制进 `python_modules/`，而启动器又无条件把该目录加到 `PYTHONPATH` 最前面 → 在 python3 ≠ 3.8 的目标机（UOS V20=3.7、Ubuntu 22.04=3.10、Debian 12=3.11、Ubuntu 18.04=3.6）上 `import psutil` 必然失败；`complex_scene.py` 的 QTimer 无父对象 + 场景切换复用已删除部件 → 3D 老化长时间运行崩溃。
3. **🟠 多个功能级 bug** — 老化测试依赖安装成功后提前 return 不启动；网络测速失败仍上报伪造数据、内网 ping 8.8.8.8 必失败；串口 1.5/2 停止位映射互换；`ip link` 解析把 MAC 误认成网卡；TCP 控制通道无分帧（粘包/拆包丢命令）等。
4. **🟠 打包/权限配置隐患** — FT-2000/FT-2500 机型产物名 `_ft2000/_ft3000` 与打包脚本硬编码 `_ft` 不一致（打包直接失败）；`apt_packages` 内是**麒麟专属**离线 deb（跨发行版 `dpkg -i` 会失败）；`ethtool` 本次未打包（`extra-bin` 为空，WOL 依赖落空）；sudoers 存在 `USERNAME_PLACEHOLDER` 失效与 `NOPASSWD dpkg/apt` 提权面过大的问题；README 声称支持 CentOS 但 deb 在 CentOS 上根本无法安装。

---

## 二、优化清单（含位置 / 问题 / 隐患 / 好处 / 优化后注意点）

> 严重度：🔴 P0 必修（功能不可用/崩溃/安全）｜🟠 P1 建议修（逻辑错误/跨平台隐患）｜🟡 P2 打磨

### A. 发布流程与版本一致性（P0）

| 编号 | 位置 | 问题与隐患 | 优化方案 | 好处 | 优化后注意点 |
|---|---|---|---|---|---|
| **A1** | `release/*.deb`、`release/deb_build` vs 仓库源码 | **发布产物漂移**：打包版 `aging_test_gui.py`=675 行旧版（源码 1829 行，md5 不同），`complex_scene.py` 不同，`aging_selftest.py` 缺失；两个 deb（09-01/09-03）早于 09-03 之后的老化脚本与 09-08 的 C++ 修改。现场跑的代码 ≠ 源码，已修缺陷仍会在目标机复现 | 1) 发布前必须从**当前源码全量重编译 + 重打包**；2) 在 `cross_platform_package_deb.sh` 末尾加"内容一致性校验"：对打包进 deb 的 py 脚本逐个 `md5sum` 并与 `release/source_manifest.md5` 比对，不一致即中止；3) 记录源码 commit/时间戳到 `DEPENDENCY_CHECK.txt` | 现场功能=源码功能；已修复问题真正生效；可追溯 | 每次改源码后养成"先 build→package→跑校验"习惯；校验失败禁止发布 |
| **A2** | `core/basic_tester.cpp:764-768、816、832` | 老化测试依赖检查：离线 deb 安装成功或联网安装成功两个分支都**提前 `return`**，跳过后续脚本查找与启动 → **首次运行老化测试（需装依赖）时点击无任何反应**，用户必须再点一次才启动 | 安装成功、`env.clear()` 后**不要 return**，改为 fall-through 继续执行启动逻辑（与 `onAgingTestAutoStart` 保持一致）；或安装成功后递归调用自身一次 | 首次点击即可启动，消除"点了没反应"的假死体验 | 递归/回跳要注意防止死循环（可加"已安装过"标志）；启动前再次校验脚本存在 |
| **A3** | `cross_platform_package_deb.sh:398`（cp psutil/OpenGL）+ `core/basic_tester.cpp:875-883`（launcher `export PYTHONPATH=python_modules`） | `python_modules/psutil/` 含 `_psutil_linux.cpython-38-x86_64-linux-gnu.so`（C 扩展，绑定 **Python 3.8/x86_64**），却被当"纯 Python"复制；launcher 无条件把 `python_modules` 前置到 PYTHONPATH → **python3 ≠ 3.8 的目标机（UOS/Ubuntu 22.04/Debian 12/Ubuntu 18.04）`import psutil` 失败**，且遮蔽 dpkg 离线安装的正确 psutil，老化测试启动即崩/报错 | 1) deb 打包**删除 psutil/OpenGL 目录的 `cp`**，只依赖 `apt_packages/*.deb`（目标机首次运行自动 dpkg -i，按目标机 ABI 匹配）；2) launcher 仅在 `python_modules/PyQt5` 目录**确实存在**时才 export PYTHONPATH；3) tar.gz 打包（`cross_platform_package.sh:183`）同样移除 psutil，改用 `pip download --platform` 生成 manylinux wheel 或声明联网安装 | 老化测试在 UOS/Ubuntu/Debian 全系列可用；消除包内冗余（含 46 个测试/__pycache__ 文件） | 修改后重新验证 3.7/3.8/3.10/3.11 各 python 版本下 `import psutil` 均走系统 dist-packages；删除 `python_modules/psutil` 与 `OpenGL` 目录 |
| **A4** | `cross_platform_build.sh:14-24` vs `cross_platform_package_deb.sh:67`、`cross_platform_package.sh:43`、`bin/start_client.sh:12-14` | 构建脚本在 FT-2000/FT-2500 机型产出 `ELSKY_test_client_ft2000/_ft3000`，而打包/启动脚本**硬编码查找 `ELSKY_test_client_ft`** → 飞腾 D2000/D3000 上**打包直接报"未找到编译产物"退出**，或安装后启动找不到二进制 | 统一命名策略：打包/启动脚本改为按 `uname -m` + 实际产物通配（`ELSKY_test_client_ft*` / 直接用统一名 `ELSKYTestClient`），或构建脚本不再细分 ft2000/ft3000 均用 `ft` | FT-2000/FT-2500 机型可正常打包安装 | 修改后需在真机（非 x86）上验证一次完整 build→package→install 链路 |
| **A5** | `complex_scene.py:22` + 打包版（旧版）`aging_test_gui.py` 场景切换 | ① `self.timer = QTimer()` **无 parent**——场景部件销毁后悬空 timer 仍触发 `self.update()`，对已删除的 C++ 对象调用致崩溃；② 打包进 deb 的旧版在 2D/3D 与简单/复杂场景切换时 `setCentralWidget` 会删除旧部件，但引用未置空，**3D 老化约 60~120s 切换即崩**，无法支撑 7 天长稳老化 | ① `QTimer(self)`；② 场景切换后显式把被替换的引用置 `None` 或每次无条件重建，彻底避免复用已删除部件（当前源码仅靠 try/except 兜底重建，仍会每轮抛异常，应改掉） | 3D 老化长时间运行不崩；日志干净无 traceback | 在带 GL 的机器上做 >1 小时、覆盖场景切换的老化验证 |

### B. 打包与离线依赖完整性（P0/P1）

| 编号 | 位置 | 问题与隐患 | 优化方案 | 好处 | 优化后注意点 |
|---|---|---|---|---|---|
| **B1** | `cross_platform_package_deb.sh:351-367` 实测 `apt_packages/*kylin*` | 离线 Python deb 是从**编译机发行版源**（麒麟 kylin）下载的：`python3-psutil_5.5.1-1kylin4k1`、`python3-pyqt5_5.14.1-3kylin1k0.1` 等。目标机若为 UOS/Ubuntu/Debian（Python/Qt 版本、依赖包名不同，如 Debian 12 的 `t64` 改名），`dpkg -i` 会因依赖版本冲突**失败**；脚本无发行版识别与降级 | 1) 明确"**同发行版同架构**"打包矩阵：在麒麟上打麒麟版、在 UOS 上打 UOS 版、在 Ubuntu 上打 Ubuntu 版；2) deb 脚本开头检测 `/etc/os-release` ID 并打印"本包适配发行版"，安装侧（basic_tester）离线 dpkg 失败时给出明确中文提示而非静默；3) 将 `libqt5core5a | libqt5core5t64` 这类 Recommends 别名在所有目标发行版验证 | 离线老化测试在各发行版真正可装可用；现场报错可定位 | 打包脚本发布前用"发行版×架构"矩阵各跑一次离线安装冒烟 |
| **B2** | README（适用系统 CentOS 7+）、`cross_platform_build.sh:60-72` | ① CentOS/RHEL **无 dpkg**，.deb 根本装不上；② 构建脚本 qmake 探测列表不含 `qmake-qt5`、SerialPort 头文件路径不含 `/usr/lib64` → 声明支持但实际不支持 | 选项一：README 删除 CentOS/RHEL 支持声明（推荐，.deb 形态不支持 rpm 系是客观限制）；选项二：如需支持则另建 RPM 打包与 RHEL 构建脚本 | 需求/交付一致，避免现场到 CentOS 才发现装不了 | 对外文档与 release 说明同步修订 |
| **B3** | `cross_platform_package_deb.sh` postinst（脚本内 644-670 段）+ 启动脚本自愈1（504-517） | ① `CURRENT_USER` 取 `SUDO_USER`，root 直装（无 SUDO_USER）时写成字面量 `USERNAME_PLACEHOLDER`，**sudoers 规则对所有真实用户不生效**（免密失效）；② sudoers 放行 `NOPASSWD: /usr/bin/dpkg, /usr/bin/apt, /usr/bin/apt-get` → 被授权用户可免密 root 装/卸任意包，**提权面过大**；③ 自愈1 在普通用户无 sudo/pkexec 时直接跳过 | ① 用 `logname`/`who` 取真实登录用户；取不到时建议用 `%` 组授权或在桌面首启时由程序自行写用户级授权；② sudoers 白名单收窄为具体命令+参数（如 `apt-get install -y python3-pyqt5* python3-psutil python3-opengl libqt5serialport5`、`dpkg -i /usr/lib/elsky-test-client/python_modules/apt_packages/*`），不给裸 `dpkg/apt`；③ 多用户场景用组（如 `%elsky-users`） | 权限最小化，符合安全基线；免密稳定生效；多用户覆盖 | 修改后分别在 root 直装、sudo 安装两种方式下验证电源管理/依赖安装不弹密码 |
| **B4** | `cross_platform_package_deb.sh:246-261`（ethtool 收集）+ postinst 3.7 | 本次 `deb_build` 中 **`extra-bin/` 目录为空**（编译机未装 ethtool）→ WOL 在目标机缺失 ethtool 时离线不可用（与 README"已自动处理"矛盾） | 打包机先 `apt-get install ethtool` 再打包；并在打包自检中把"extra-bin 为空且系统无 ethtool"升级为**中止打包**（同 python 主包缺失策略，或 ALLOW 开关） | WOL 离线可用与文档一致 | 校验 `lib/` 收集了 ethtool 依赖（如 libmnl 等），不能只拷二进制 |
| **B5** | `DEBIAN/control` Depends/Recommends | 外部命令依赖声明不足：`ping`（iputils-ping）、`ethtool`、`nmcli`（network-manager）、`speedtest-cli` 均未打包也不在 Depends/Recommends | Depends 保持最小（python3、policykit-1）+ Recommends 补 `iputils-ping, ethtool, network-manager`；speedtest-cli 明确文档化（离线不可用，报错文案已指向内网测速替代）；Debian 12 `t64` 改名用 `|` 别名已在 Recommends 处理 | 功能依赖可预期；apt 安装时提示自动补 | 在 Ubuntu 18.04 上验证 Recommends 里含 `t64` 别名的语法兼容（旧 apt 解析 `|` 别名没问题，但要验证 t64 包名不存在时不报错） |
| **B6** | `release/deb_build` 打包体积 173/193MB | `python_modules/psutil/` 内含 46 个测试与 `__pycache__/*.cpython-38.pyc`；Qt 全家桶含用不到的模块（Multimedia/Qml/Quick…） | 删除 psutil/OpenGL 冗余复制后（A3）再 `find -name __pycache__ -o -name tests` 清理；裁剪 libQt5Qml/Quick/Multimedia 等未用 Qt 库（需在自检白名单中排除） | 包体积预计可降 20%~40%，安装更快 | 裁剪后必须重新跑离线 ldd 自检 + GUI 启动冒烟，防误删运行期 dlopen 需要的库 |
| **B7** | `cross_platform_build.sh:50-54` | 清理规则 `ELSKY_test_client*` 匹配不到无下划线的 `ELSKYTestClient`/`ELSKY_test_client`，bin/ 残留多个旧二进制（3.9MB~6.7MB，Dec-Jan 产物），存在"打包到旧程序"风险 | 清理/打包统一按"当前架构对应产物名"精确清理 + 打包前校验二进制 mtime 晚于全部源码 mtime | 杜绝"新源码+旧二进制"混装 | 每次构建输出产物名与时间戳日志，便于审计 |

### C. C++ 功能与稳定性（P1）

| 编号 | 位置 | 问题与隐患 | 优化方案 | 好处 | 优化后注意点 |
|---|---|---|---|---|---|
| **C1** | `core/network_speed_tester.cpp:56、93、145-149` | ① 连通性硬编码 `ping 8.8.8.8`，文件顶部已实现 `defaultGatewayIp()`（读 `ip route`）却**从未调用** → 内网/离线环境连通性必判失败，测速入口进不去；② `:93` 错误提示引用**从未赋值**的 `pingTarget`（显示空字符串）；③ `:145-149` 在 speedtest-cli 失败时仍 `emit testFinished(8.5, 3.2)` **上报伪造网速**，与该文件 `:113-118` 声称"不再伪造数据"自相矛盾，污染测试报告 | ① ping 目标按顺序：默认网关 → 配置服务器 IP → 8.8.8.8（仅外网）；② `pingTarget` 使用实际值；③ 失败路径只 `testError`，不发 `testFinished` | 内网环境功能可用；报告不再出现假数据 | 与"统一控制的内网测速"入口打通验证 |
| **C2** | `core/privilege_helper.cpp:208-209` | `ensureAuthForPrivilege` 验证记忆密码时用 `echo '%1' | sudo -S true` 拼进 shell：密码含 `'` 命令破裂、含 `$()`/`;` 可**命令注入**；且密码明文出现在 `/proc/<pid>/cmdline`（同机任意用户 `ps` 可见）——与文件内 `:107-109` 注释宣称已修复的漏洞**同款回退** | 与 `:115`/`:176` 一致：`QProcess::start("sudo", {"-S","-p","","bash","-c","true"})`，密码走 stdin | 消除注入与密码泄露两条安全漏洞 | 覆盖"密码含单引号/特殊字符"用例回归 |
| **C3** | `core/server_connector.cpp:60、99-109` | ① `waitForConnected(5000)` 在 GUI 线程同步阻塞；② `onReadyRead` 把一次 `readAll()` 当一条消息，**无缓冲/无分帧**——TCP 粘包时多条指令粘成一条、拆包时指令被截断，统一控制下发的 `SHUTDOWN/START_CYCLE` 等可能丢命令；`common/protocol.cpp` 已有 `NetworkMessage` 分帧序列化却未使用 | 改用信号驱动异步连接；接收侧维护 QByteArray 缓冲，按 `NetworkMessage` 帧格式（长度头/分隔符）切包重组，事件循环逐条 emit | 远程控制指令可靠不丢；UI 不卡 | 需与服务器端联调粘包/拆包用例 |
| **C4** | `dialogs/com_test_dialog.cpp:660`（配合 `:275` combo：`"1","1.5","2"`） | `setStopBits(currentIndex()+1)`：选 "1.5"→2(TwoStop)、选 "2"→3(OneAndHalfStop)，**1.5 与 2 恰好互换** → 串口通信失败/乱码 | 用映射表：index 0→OneStop(1)、1→OneAndHalfStop(3)、2→TwoStop(2) | 1.5/2 停止位正确，可正常联调 | 用示波器/对端回环实测三档停止位 |
| **C5** | `core/device_manager.cpp:728-741` | `ip link` 输出按 `:` 切分，续行 `link/ether 00:11:22:33:44:55` 被切出 `parts[1]="11"`，通过非空/非 lo 校验 → **把 MAC 段 "11" 误报为网卡**，产生虚假的"网络设备插入"事件 | 只解析行首匹配 `^\d+:\s*(\S+):` 的主行，其余行跳过 | 设备监控准确，无虚报事件 | 在双网卡/多接口机器验证增删事件 |
| **C6** | `core/auto_start_manager.cpp:337-338` | systemd 服务写死 `DISPLAY=:0`、`XAUTHORITY=$HOME/.Xauthority` → Wayland、多 seat、XWayland 下图形自启失效 | 由 launcher 启动脚本动态探测（`loginctl`/`$XDG_RUNTIME_DIR`），或同时保留 .desktop autostart 作为跨 DE 兜底 | 各桌面环境自启可靠 | 在麒麟 V10（X11）与 UOS（Wayland）各验证一次开机自启 |
| **C7** | `core/benchmark_tester.cpp:289-294、807-812、1007-1020` | ① UnixBench/STREAM 路径硬编码 `/usr/local/bin`、`/opt/...`，未与 `which` 结果合并 → 国产发行版找不到/结果不可复现；② `dropCachesBeforeDiskTest` 在工作线程执行、进程无 root，`echo 3 > /proc/sys/vm/drop_caches` **静默失败**，"冷盘"测试实际读热缓存，**磁盘跑分虚高、跨机不可比**；③ `:989` 固定 256MB + STREAM 大数组，瘦客户端 OOM 风险 | ① 探测路径合并 `which` 结果并按发行版增补候选；② drop_caches 走 sudo/免密（或明确报告"热缓存模式"并标注分数等级，不要静默）；③ 文件大小按可用内存自适应 | 跑分可复现、可比、不误导 | 多次跑分取中位数对比验证波动收敛 |
| **C8** | `dialogs/usb_speed_dialog.cpp:352`、`dialogs/com_test_dialog.cpp:571`、`core/wake_on_lan.cpp:211/243` | 临时脚本/结果文件写死固定 `/tmp/xxx.sh`：多实例/并发时互相覆盖；析构删除可能删掉另一实例文件 | 改用 `QTemporaryFile` 或路径带 PID/随机串 | 多开安全 | 无 |
| **C9** | `core/privilege_helper.cpp:94/124/183` 等 | `waitForFinished(-1)` 无限等待（注释为避免误杀休眠命令）——GUI 线程调用时**界面冻结**；suspend/hibernate 期间长阻塞 | 拆分：休眠类命令异步信号驱动；常规命令设合理超时（如 60s） | UI 不冻结 | 区分"需等待完成"与"异步"两类命令分别处理 |
| **C10** | 多文件 | GUI 线程多处 `waitForFinished(3000)` 执行外部命令（device_manager、client_window 等）→ 扫描期短卡顿 | 大文件/慢命令改异步（QProcess 信号 + 结果回调） | 监控页滚动更流畅 | 防止并发扫描重入 |

### D. Python 老化测试（P1/P2）

| 编号 | 位置 | 问题与隐患 | 优化方案 | 好处 | 优化后注意点 |
|---|---|---|---|---|---|
| **D1** | `aging_test_gui.py:1112-1152` | CPU 压力 worker **在已有多线程的进程内 `fork`** `multiprocessing.Process` → fork 瞬间其它线程持锁则子进程**死锁**，CPU 满载项起不来（触发"满载失败"误报） | ① 主线程启动压力前预创建子进程池；② 或显式 `multiprocessing.set_start_method('spawn')`（`_cpu_worker` 已是模块级可 pickle）；③ 或纯线程实现 CPU 压力 | CPU 满载稳定启动 | 多核大核数机器验证满载曲线 |
| **D2** | `aging_test_gui.py:1682`、`complex_scene.py:112` | `resizeGL`/透视 `width/height` **除以零未保护**（窗口 0 尺寸/离屏 resize）→ ZeroDivisionError 从 Qt 回调冒出 | 入口 `if width==0 or height==0: return` | 抗异常 resize | 最小化/还原窗口循环测试 |
| **D3** | `complex_scene.py:181-185` | 每帧 `gluSphere(gluNewQuadric(),…)` 泄漏一个 GLU quadric，7 天老化累积约 1.8 千万次分配 | `__init__` 创建 `self._quadric`，析构 `gluDeleteQuadric` | 长稳内存平稳 | 7 天级老化验证 RSS 曲线 |
| **D4** | `aging_test_gui.py:1537/1548` | 老化网络自检写死外网 `8.8.8.8` 与希腊测速站 → **离线环境每轮误报"网络满载失败"**，与"离线运行"产品定位矛盾 | 网络目标可配置；离线/无外网自动降级为内网或跳过 | 离线老化不再误报 | 确认降级逻辑在日志中可见 |
| **D5** | `aging_test_gui.py:4/13-14` | 顶层 `import psutil/OpenGL.GL` 无降级：缺依赖时整个 GUI 直接 ImportError，无友好提示 | try/except 包裹，缺失时 QMessageBox 给出安装指引（与 aging_selftest 提示一致） | 现场可自助定位 | — |

### E. 其它打磨（P2）

- `ELSKYTestClient.pro:35` TARGET `ELSKY_test_client` vs README/bin 名混乱 → 统一一个对外名。
- `main.cpp:16` `app.setApplicationVersion("1.5")` 落后于实际 2.0.0 → 与 deb 版本统一。
- 桌面图标使用系统 `utilities-system-monitor` 占位图 → 建议提供自有图标资源。
- 打包脚本版本号建议 `PKG_VERSION` 从 git tag/参数传入，避免手改遗漏。
- deb 安装到 `/usr/lib/elsky-test-client` 的 `elsky-test-client.desktop` 内同时放于 `/usr/share/applications`（规范位置），桌面副本由 postinst 放置，逻辑正确但建议卸载时清理桌面副本（postrm 目前只删 sudoers，不删桌面图标与 `~/.config/ELSKY`）。
- `common/protocol.cpp` 分帧代码与 `server_connector` 不一致（见 C3）——统一收敛。

---

## 三、外部命令依赖矩阵（目标机需预装 / 是否随包）

> 结论：**Qt 运行库/插件、Python 离线 deb、启动脚本自愈均已随包**，体系完整；缺口集中在 `extra-bin` 实际为空（ethtool 未打包）与若干命令未声明。

| 命令 | 调用位置 | 用途 | deb 是否自带/声明 | 状态 |
|---|---|---|---|---|
| `ethtool` | wake_on_lan | WOL 网卡唤醒 | 设计上随包（extra-bin）→ **本次实际为空** | 🔴 缺失 |
| `ping` | network_speed_tester / aging_test_gui | 连通性 | 未打包、未 Declares | 🟠 缺声明 |
| `nmcli` | wake_on_lan | WOL 持久化 | 未打包、未 Declares | 🟠 缺声明 |
| `speedtest-cli` | network_speed_tester | 网速测试 | 未打包（离线不可用，报错已提示） | 🟡 文档化即可 |
| `python3` | 老化测试 | 解释器 | Depends: python3（桌面标配） | ✅ |
| `systemctl/journalctl/last` | 电源/非法关机 | systemd 全家桶 | 依赖系统标配 | ✅ |
| `ip/lsblk/sync/pkill/xset…` | 多模块 | 网络/磁盘/进程 | util-linux/iproute2/procps 标配 | ✅ |
| `sensors/smartctl/nvidia-smi/glxinfo` | 监控/跑分 | 温度/健康/GPU | 未打包但有逐级降级 | ✅ 降级 |
| `glmark2/fio/iozone/x11perf/stream` | 跑分 | 权威基准 | 未打包，回退内置算法 | ✅ 回退 |

---

## 四、优化前后对比清单（回归基线）

| 功能/模块 | 优化前（现状与隐患） | 优化后（预期结果） |
|---|---|---|
| 老化测试（依赖安装后首启） | 安装完依赖直接 return，首次点击无反应（A2） | 首次点击即启动老化 GUI |
| 老化测试 3D（跨发行版） | psutil cpython-38 .so 遮蔽 → python≠3.8 崩溃（A3）；场景切换约 120s 崩/旧版必崩（A5） | 各发行版 python 版本下稳定启动；>1h 3D 老化不崩不闪退 |
| 老化测试 CPU 满载 | 多线程内 fork 可能死锁（D1） | 满载稳定、停止即时回收 |
| 老化测试长稳 | GLU quadric 每帧泄漏；resizeGL 除零（D2/D3） | 7 天级 RSS 平稳；任意 resize 不崩 |
| 发布包与源码 | deb/暂存目录内 py 为旧版 675 行（A1） | deb 内文件 md5 与源码一致，含版本清单 |
| 飞腾 FT-2000/2500 打包 | 产物名 `_ft2000/_ft3000` 与打包脚本 `_ft` 不符，打包失败（A4） | 全飞腾型号 build→package→install 成功 |
| 网络测速 | 内网 ping 8.8.8.8 必失败；失败报伪造 8.5/3.2（C1） | 内网走网关探测；失败只报错不出假数据 |
| 串口 1.5/2 停止位 | 档位互换，通信错乱（C4） | 三档位物理回环正确 |
| 设备实时监控 | MAC 段被误报为网卡（C5） | 设备清单准确、无虚报增删事件 |
| 统一控制（远程命令） | TCP 无分帧，粘包/拆包丢命令（C3） | 指令逐条可靠送达 |
| 非法关机检测/电源管理/开机自启 | 检测逻辑本身健壮（已确认）；自启 DISPLAY 写死（C6） | X11+Wayland 自启均生效；非法关机三层不误判 |
| 跑分测试 | 外部工具路径硬编码；清缓存静默失败致分数虚高（C7） | 分数可复现、等级标注真实 |
| 提权/密码 | 记忆密码验证含命令注入+ /proc 泄露（C2） | 密码全走 stdin，无注入面 |
| sudoers 配置 | root 安装写 USERNAME_PLACEHOLDER 失效；NOPASSWD dpkg/apt 提权面大（B3） | 免密稳定生效且最小权限 |
| WOL | 本包未带 ethtool，离线不可用（B4） | extra-bin 非空校验纳入打包自检，WOL 离线可用 |
| 离线 python 依赖 | 麒麟专属 deb 跨发行版 dpkg 失败（B1） | 按发行版出包/失败提示明确 |
| 多开/并发 | /tmp 固定文件名互相覆盖（C8） | QTemporaryFile 无竞争 |
| 卸载 | postrm 不清理桌面图标/配置文件（E） | 卸载干净可选 |

---

## 五、回归测试方案（优化实施后执行）

### 5.1 测试矩阵

| 维度 | 覆盖 |
|---|---|
| 架构 | x86_64（Intel/AMD/海光/兆芯）、aarch64（飞腾 D2000/D3000、鲲鹏） |
| 发行版 | 麒麟 V10/V11、统信 UOS V20/V25、Ubuntu 20.04/22.04、Debian 11/12 |
| Python | 3.7 / 3.8 / 3.10 / 3.11 |
| 安装方式 | ① 全新系统 `sudo dpkg -i *.deb`（模拟离线双击）；② `apt install ./xxx.deb`；③ 已装旧版升级覆盖 |
| 场景 | 有网 / 完全离线（断开网线）双环境 |

### 5.2 每机必测功能清单（配合根目录 `regression_test_elsky.sh` 自动项）

1. 安装：离线 `dpkg -i` 成功 → postinst 权限配置 → 桌面图标出现 → `elsky-test-client` 命令可启动（`DEPENDENCY_CHECK.txt` 无警告）。
2. GUI 启动：桌面双击 + 终端启动均正常，无缺库报错（lib/platforms/libqxcb.so 就位）。
3. 基础测试：关机/重启/休眠/串口回环（三档停止位）/USB 测速/网络测速（内网网关 + 有外网）/老化测试（首次运行即启动，2D/3D 场景连续切换 2h+）/跑分（内置模式，报告完整、无假数据项）。
4. 网络唤醒：ethtool 存在；魔术包广播目标机可唤醒。
5. 监控：CPU 温度/占用、内存、磁盘、开机时间、设备增删事件无虚报。
6. 循环控制器：循环 N 次开关机任务完整执行不卡死。
7. 非法关机：断电/长按电源 3 次，标志文件+journalctl+last-x 三层判定一致、计数累加、历史保留。
8. 开机自启：X11 与 Wayland 各验证一次。
9. 统一控制：服务器下发关机/启动循环指令 100 条，逐条确认无丢失（粘包/拆包压力）。
10. 提权：免密执行电源管理（root 直装与 sudo 安装两种方式都不弹密码）。
11. 日志与标签：滚动、过滤、导出正常。
12. 卸载重装：`purge` 后桌面图标/配置文件清理，重装后一切正常。

### 5.3 通过标准

- 所有架构×发行版组合：功能 1-12 全绿；老化 2h+ 无崩溃、无 traceback、RSS 平稳；
- 离线环境下除"跑分专业模式/外网测速"外全部功能可用（这两项有明确文案说明）；
- 报告/日志无伪造数据（无 8.5/3.2 之类的魔数回填）。

---

---

## 六、实施状态跟踪（2026-09-09 更新）

> 本节记录上述清单的实际修复状态。✅=已实施并语法校验通过；⚠️=部分实施；📋=流程性事项/需真机环境；❌=未实施。

### 6.1 已实施（✅）

| 编号 | 修复内容 | 落点文件 |
|---|---|---|
| **A2** | 依赖安装成功后不再提前 return，首次点击老化测试即启动 | `core/basic_tester.cpp` |
| **A3** | 打包不再复制 psutil/OpenGL_accelerate（含 C 扩展，ABI 绑定）；仅保留纯 Python 的 OpenGL/pyserial；清除残留与 `__pycache__`/tests | `cross_platform_package_deb.sh` |
| **A4** | 飞腾 D2000/D3000 产物名 `_ft2000/_ft3000` 两级查找（精确→通配），打包/启动脚本均兼容 | `cross_platform_package_deb.sh`、`bin/start_client.sh` |
| **A5①** | `QTimer(self)` 传父对象，杜绝悬空 timer 崩溃 | `complex_scene.py` |
| **A5②** | 3D 场景切换改为无条件重建部件，绝不复用已删除引用 | `aging_test_gui.py` |
| **B4** | 编译机无 ethtool 时回退 `apt-get download` + `dpkg-deb -x` 解包携带，依赖库并入 lib/ | `cross_platform_package_deb.sh` |
| **B5** | Recommends 补 `iputils-ping, ethtool, network-manager` | `cross_platform_package_deb.sh`（control） |
| **C1** | 网络测速 ping 网关优先、失败只报错不再伪造 8.5/3.2 数据 | `core/network_speed_tester.cpp` |
| **C2** | sudo 密码全部走 stdin（`sudo -S -p ""`），消除命令注入与 /proc 泄露 | `core/privilege_helper.cpp` |
| **C4** | 串口停止位显式映射（1→OneStop、1.5→OneAndHalfStop、2→TwoStop） | `dialogs/com_test_dialog.cpp` |
| **C5** | `ip link` 只解析 `^\d+:\s*(\S+):` 主行，MAC 续行不再误报为网卡 | `core/device_manager.cpp` |
| **C6** | 自启 DISPLAY 动态探测（早前实施） | `core/auto_start_manager.cpp` |
| **C8** | /tmp 临时文件全部带 PID：WOL 脚本/结果文件、USB 测速脚本、串口权限脚本 | `core/wake_on_lan.cpp`、`dialogs/usb_speed_dialog.cpp`、`dialogs/com_test_dialog.cpp` |
| **C9（部分）** | 休眠/关机命令异步化（修复栈对象 deleteLater UB）；USB 测速改信号驱动，GUI 不再阻塞 5 分钟；dd 脚本 `set -e` 与 locale 小数点兼容 | `core/system_controller.cpp`、`core/basic_tester.cpp:1280`、`dialogs/usb_speed_dialog.cpp` |
| **D1** | CPU 压力 worker 模块级 + `set_start_method('spawn')` + PDEATHSIG 兜底（父进程被 kill -9 时内核回收 worker，杜绝孤儿满载） | `aging_test_gui.py` |
| **D2** | 两处 `resizeGL` 除零保护 | `aging_test_gui.py`、`complex_scene.py` |
| **D3** | GLU quadric 惰性创建复用，不再每帧泄漏 | `complex_scene.py` |
| **D4** | 网络老化外网 ping 失败自动回退默认网关，离线机房不再误报 | `aging_test_gui.py` |
| **D5** | 顶层 `psutil/OpenGL` import 降级守卫（HAS_PSUTIL），缺依赖时 2D 老化仍可用 | `aging_test_gui.py` |
| **E** | `main.cpp` 版本号 1.5→2.0.0，与 deb PK_VERSION 统一 | `main.cpp` |

### 6.2 部分实施 / 流程性事项（⚠️📋）

| 编号 | 状态 | 说明 |
|---|---|---|
| **A1** 发布产物漂移 | 📋 | 代码已修，但**现存 release/*.deb 均为旧版**。必须重新 build→package 后发布；打包脚本已有 ldd 自检与 DEPENDENCY_CHECK.txt，发布前逐项核对 |
| **B1** 跨发行版依赖 | ⚠️ | control 已用 `t64` 别名；但 apt_packages 内 deb 仍取自编译机发行版，需按"麒麟版/UOS 版/Ubuntu 版"分发出包 |
| **B3** sudoers | ⚠️ | 免密白名单仍含裸 apt/dpkg（程序自愈装依赖依赖它）。收窄需与 `basic_tester.cpp` 安装命令做参数级联调，建议列入下一迭代；回归脚本已对此出 WARN |
| **B6/B7** 体积与 bin 清理 | 📋 | psutil 删除后需重打包观察体积；build 前手动清理 bin/ 旧产物 |

### 6.3 未实施（❌，列入后续迭代）

- **C3** TCP 分帧（`server_connector.cpp`）：需与服务器端协议联调，风险较大单独迭代；
- **C7** 跑分工具路径/drop_caches 权限：功能可用但有跨机可比性问题；
- **C10** 多处 GUI 线程 `waitForFinished(3000)`：卡顿属体验问题非功能缺陷；
- **B2** README 的 CentOS 声明修订：文档事项。

### 6.4 优化后必须注意的事项（重打包前必读）

1. **必须全量重编译重打包**：本报告所有 C++/Python 修复都要重新 build→package 才会进入 deb；
2. **打包机需联网**（或预装 ethtool）：A3/B4 的 apt 下载回退依赖网络；
3. **回归测试入口**：目标机安装新 deb 后运行根目录 `regression_test_elsky.sh`，按第五节矩阵逐机过 12 项功能清单；
4. **验证 python≠3.8 目标机**（UOS V20=3.7、Ubuntu 22.04=3.10、Debian 12=3.11）：老化测试首次运行会自动 `dpkg -i` 离线 deb，确认 `import psutil` 走系统路径；
5. **串口停止位**：修改了档位映射，需用对端回环重新验证 1.5/2 档；
6. **3D 老化**：需在带 GL 的机器上做 ≥1 小时含场景切换的长稳验证。

---

*说明：本报告基于静态源码审查 + 打包产物比对（2026-09-08），实施状态更新于 2026-09-09。第六节为最终实施状态，发布前请以最新源码重新编译打包后再按第五节执行回归。*
