# ELSKYTestClient 整改报告 — 统信1070 编译/打包/运行三问题（V2.1）

日期：2026-09-21　|　涉及版本：V2.0 → V2.1　|　截图来源：it0（飞腾ARM64 / 统信UOS 1070）

---

## 一、三个问题的根因

### 问题1：统信1070 编译报错 `could not exec '/usr/lib/aarch64-linux-gnu/qt4/bin/qmake'`

**根因**：统信1070 上 `/usr/bin/qmake` 是 **qtchooser 符号链接**，其 default 配置指向 **Qt4**（且 qt4 的 qmake 二进制缺失）。而两个脚本的 qmake 查找逻辑存在缺陷：

| 脚本 | 缺陷 |
|---|---|
| cross_platform_build.sh | 候选列表末位兜底 `/usr/bin/qmake`，**从不校验 Qt 大版本**，会误选 Qt4/失效链接（截图1 编译阶段即崩溃） |
| cross_platform_package_deb.sh | 更严重：把 `command -v qmake` 排在候选**第一位**，同样不校验版本（截图2 实测选中 `Qt 4.8.7` 后继续跑，Qt 库目录定位到错误路径） |

### 问题2：打包提示没有 glmark2，统信系统 apt 也无法下载

**根因**：`apt-get download glmark2` 依赖**编译机的 apt 源**里有这个包。统信UOS/麒麟的**官方源本身就不收录 glmark2**（`无法定位软件包` 属正常现象，不是系统坏了），编译机=统信时必然下载失败。该功能本是"可选增强项"（缺失时老化测试自动降级跳过），但旧提示语把它写成了近乎报错的口径，造成误解。

### 问题3：deb 安装后无法运行，终端运行报 `version 'GLIBC_2.30' not found`

**根因（本轮最核心）**：deb 包内 `lib/` 里的 127+ 个 Qt/X11/GL 库是从**编译机**原样复制的，这些二进制的符号绑定**编译机的 glibc 版本**。本次安装包是在 glibc ≥ 2.30 的系统上编译打包的，而统信1070 的 glibc 只有 **2.28** → 目标机加载包内库时必报 `GLIBC_2.29/2.30 not found`。

关键结论：**glibc 只能向上兼容、无法向下降级，也无法随包携带**。启动脚本的三级自愈（离线deb→在线apt→ld.so兜底）对此完全无效——缺的不是"某个库"，而是"库要求的 glibc 版本"超出目标系统。这是"在高版本系统上编译、拿到低版本系统安装"这条路线的必然结果，**任何打包技巧都救不回来**，必须从流程上治理。

---

## 二、整改内容（全部已落盘）

| # | 文件 | 改动 |
|---|---|---|
| 1 | cross_platform_build.sh | `find_real_qmake` 重写：glob 覆盖所有架构的 `qt5/bin/qmake` 路径，**强制校验 `qmake -query QT_VERSION` 必须为 5.\***，Qt4/失效链接直接跳过并打印原因；后续再加一道 Qt5 版本硬校验（失败给出统信/麒麟对应的安装指引）；导出 `QT_SELECT=5` |
| 2 | cross_platform_package_deb.sh | 同样重写 `find_real_qmake`（Qt5 校验 + glob 架构覆盖，PATH 中的 qmake 仅作兜底且同样校验） |
| 3 | cross_platform_package_deb.sh | **新增"GLIBC 兼容性门禁"（7.5节）**：用 objdump/readelf 扫描包内**全部 ELF**（lib/、插件、extra-bin、python_modules 的 .so、主程序）的最大 GLIBC 符号版本需求，超过基线（默认 `MIN_TARGET_GLIBC=2.28`）即**中止打包**并列出清单与修复指引；可用 `ALLOW_INCOMPATIBLE_GLIBC=1` 强行放行、`MIN_TARGET_GLIBC=x.xx` 调整基线 |
| 4 | cross_platform_package.sh | tar 版打包同样加入 GLIBC 门禁 |
| 5 | deb 启动脚本 elsky-test-client.sh | 新增 **GLIBC 预检**：检测到 `version 'GLIBC_' not found` 直接给出中文诊断（根因+本机 glibc 版本+解决办法）后退出，不再盲目走无效的自愈链 |
| 6 | deb postinst | 安装时依赖自检增加 GLIBC 不兼容检测（致命级，明确提示"安装包与系统版本不匹配"） |
| 7 | tar 包内嵌 start_client.sh（2处） | 同样加入 GLIBC 预检 |
| 8 | cross_platform_package_deb.sh | glmark2 改为**可选增强**口径：apt 下载失败后增加 `offline_debs/` 离线 deb 目录兜底；提示语明确"UOS/麒麟源无此包属正常、缺失自动降级、不影响其它功能"；**从 control 的 Recommends 中移除 glmark2**（避免 apt 在无此包的系统上反复告警） |
| 9 | regression_test_elsky.sh | 新增 **2.5 GLIBC 兼容性**检测节：在目标机上核验包内所有库/二进制的 glibc 符号需求 ≤ 本系统 glibc |

以上脚本均通过 `bash -n` 语法检查；版本比较逻辑已单测验证（2.30>2.28 拦截、2.28=2.28 放行、2.17 放行）。

---

## 三、优化后的优缺点

**优点**
1. **错误前移**：glibc 不兼容从"目标机黑盒报错"前移为"打包机明确拦截+清单"，打包阶段就能发现问题，不再浪费真机排障时间。
2. **诊断可读**：即使带了不兼容的旧包到目标机，启动脚本/安装脚本也会给出中文根因说明（"请用同版本系统重新编译打包"），一线人员可自助定位。
3. **qmake 选择健壮**：不再依赖 qtchooser 的系统配置状态，Qt4/损坏链接永远不会被误选；glob 候选天然覆盖 aarch64/x86_64/mips64el/loongarch64 等国产架构。
4. **glmark2 定位清晰**：不再误导为打包失败；新增 offline_debs/ 离线通道，在无源系统上也能携带该功能；缺失时全功能自动降级，不影响其它老化项。
5. **多一层验收防线**：目标机回归脚本可自动核验 glibc 兼容性，直接对应本次翻车场景。

**缺点 / 代价**
1. **门禁是"拦"不是"解"**：要出统信1070可用的包，**必须**在 glibc ≤ 2.28 的系统（统信1070 本机、麒麟V10 SP1、Debian10）上编译打包，无法绕过——这是 Linux 二进制分发的硬约束，脚本只能保证不再"假装能行"。
2. 打包时间略增：全量 ELF 扫描约增加数秒~十几秒（百级文件），可忽略。
3. `MIN_TARGET_GLIBC=2.28` 是保守基线：如果某条产品线只需跑 Ubuntu 22.04+，需手动调高基线，否则个别场景会过度拦截（但这属于"宁严勿错"）。

---

## 四、注意事项（务必执行）

1. **编译机与目标机同源**（最高优先级）：
   - 目标统信1070 / 麒麟V10 SP1（glibc 2.28）→ 编译打包必须在这类系统上做；
   - 不要再在麒麟2503/新Debian/新Ubuntu上编译后拿去装统信1070；
   - 一个系统出的 deb 只承诺同基线系统，跨基线发布用 `MIN_TARGET_GLIBC` 显式声明并重新验证。
2. 若编译机不满足基线，门禁会**中止打包并列出违规文件**——按提示换编译机即可，不要用 `ALLOW_INCOMPATIBLE_GLIBC=1` 掩盖（产出的包装机必死）。
3. glmark2 若需要离线携带：在联网的 Debian/Ubuntu 上 `apt-get download glmark2`，把 .deb 放入项目 `offline_debs/` 目录后重新打包；不需要时忽略该提示即可。
4. 每次换编译机/换系统后，务必先跑一轮 `./cross_platform_build.sh && ./cross_platform_package_deb.sh`，确认门禁全绿再发版。

---

## 五、优化清单（本轮改动汇总）

- [x] P0：qmake 误选 Qt4 → build/package_deb 双脚本 Qt5 版本强校验（问题1根治）
- [x] P0：GLIBC 不兼容 → 打包门禁 + 启动预检 + postinst 检测 + 回归脚本核验（问题3根治，四道防线）
- [x] P1：glmark2 → 可选增强口径 + offline_debs/ 离线兜底 + 移出 Recommends（问题2整改）
- [x] P1：tar 版打包脚本同步加 GLIBC 门禁与启动预检
- [x] P2：回归脚本新增 2.5 GLIBC 兼容性检测节
- [x] 全部脚本 bash -n 语法校验通过、版本比较逻辑单测通过

---

## 六、各架构各 Linux 系统回顾测试（必须真机执行）

> 说明：我在 Windows 开发机上完成了脚本整改与静态校验（语法+逻辑单测），**无法替代目标机真机验证**。请按下表逐台执行，每台装包后先跑 `sudo bash regression_test_elsky.sh`，全绿再测功能。

### 测试矩阵

| 目标系统 | 架构 | glibc | 编译打包机要求 | 重点验证项 | 状态 |
|---|---|---|---|---|---|
| 统信UOS 1070 | ARM64（飞腾） | 2.28 | **必须本系统或同级** | 三个截图问题全回归：编译、打包(glmark2提示)、安装后启动 | ⏳ 待真机 |
| 统信UOS 1070 | AMD64（海光/兆芯） | 2.28 | 同上 | 同上 | ⏳ 待真机 |
| 银河麒麟 V10 SP1 | ARM64/AMD64 | 2.28 | 同上 | 全功能离线安装运行 | ⏳ 待真机 |
| 银河麒麟 2503 | 按机型 | ≥2.31 | 可用其编译，产包可向下？**否**——只能装 ≥其 glibc 的目标机 | 2.28 基线包在本系统应正常运行（向上兼容） | ⏳ 待真机 |
| Ubuntu 20.04 | x86_64 | 2.31 | — | 2.28 基线包运行 | ⏳ 待真机 |
| Ubuntu 22.04 / Debian 11+ | x86_64 | ≥2.35 | — | 2.28 基线包运行 | ⏳ 待真机 |
| Debian 10 | x86_64 | 2.28 | 可作统信1070 的替代编译机 | 同统信1070 | ⏳ 待真机 |

### 每台目标机的标准回归步骤

1. **装包**：`sudo dpkg -i elsky-test-client_2.0.0_<架构>.deb`（或双击安装）；
2. **跑回归**：`sudo bash regression_test_elsky.sh` —— 重点看新增 **2.5 GLIBC 兼容性**节是否全 PASS；
3. **启动**：双击桌面图标 + 终端直跑 `/usr/lib/elsky-test-client/elsky-test-client.sh` 两条路径都要能起 GUI；
4. **功能抽测**：网络唤醒（WOL 设置+魔术包）、电源管理（关机/重启/循环休眠）、COM 测试（插串口）、老化测试（勾选 CPU/GPU/3D/glmark2 项，确认缺失项优雅降级、其余项正常跑满）；
5. **离线验证**：拔网线重复 1~4，确认 Qt 库/Python 依赖全部离线自足；
6. **卸载验证**：`sudo dpkg -r elsky-test-client`，确认桌面图标/sudoers/兜底二进制清理干净。

### 对应本次三个问题的专项确认

- **统信1070 编译**：在编译机上跑 `./cross_platform_build.sh`，应看到 "找到可用的 Qt5 qmake: ... (Qt5, 校验通过)"，不再出现 qt4 字样；若系统确实没有 Qt5 开发包，应打印明确的 apt 安装指引而不是半路崩在 make。
- **glmark2**：打包日志应显示 "ℹ glmark2 未随包（可选增强项，非打包失败）"；若放入 offline_debs/ 后重打包，应显示 "✓ glmark2 已…解包"，目标机老化测试 glmark2 项可用。
- **安装后运行**：新包在统信1070 上安装后直接双击应能启动；若误装旧高版本包，终端应看到中文的 GLIBC 诊断（含本机 glibc 版本与解决办法），而不是一屏英文 symbol 错误。

### 门禁自验（打包机上，发版前最后一关）

```bash
# 应当 FAIL/中止（模拟用高版本库混入）：把一个高 glibc 库拷进包内路径再打包，确认门禁拦截并列清单
# 正常发版：确认打包日志出现
#   "GLIBC 兼容性门禁（目标基线 glibc >= 2.28）"
#   "✓ 包内所有 ELF 的 GLIBC 依赖均 <= 2.28（目标机可正常加载）"
```

---

## 七、结论

- 问题1/2/3 根因均已定位并在脚本层根治：qmake 选择不再可信 qtchooser、glmark2 转为可持续维护的可选增强、GLIBC 兼容性从"碰运气"变为"四道防线"（打包门禁→启动预检→安装时检测→回归脚本核验）。
- **发版铁律**：统信1070/麒麟V10 的包，必须在 glibc ≤ 2.28 的系统上编译打包——这是 Linux 平台的硬约束，本套脚本已把该约束变成自动强制检查。
- 真机回归矩阵（第六节）请在每台目标机上执行并留档，全绿后方可发版。

---

## 八、2026-09-22 追加整改（打包中止 + 旧包安装后仍报 GLIBC）

### 现象

- 打包机（统信1070）：Python 离线依赖只下载到 2 个 .deb（qtsvg/opengl），`python3-pyqt5/python3-pyqt5.qtopengl/python3-psutil` 缺失 → "打包中止：不满足全功能离线运行"。
- 目标机：安装旧包后运行仍报 GLIBC_2.29/2.30 not found。

### 追加根因

1. **闭包下载整 体失败**：`apt-get install --download-only` 的清单里含统信源**不存在**的包（python3-pyqt5.qtwebkit/qtmultimedia/qtsql/qtxmlpatterns），apt 因"无法定位软件包"直接放弃**整个**依赖树下载；回退的逐包下载超时仅 10 秒，统信 CDN 实测 ~75KB/s，大包（pyqt5 数 MB）必然中途失败。
2. **外来二进制**：本次新装的包主程序自身就要求 GLIBC_2.29 —— 说明 bin/ 里的产物是在更高版本系统上编译的（工程目录从别的机器拷来未重新编译），这样的包在本机 2.28 上装什么依赖都没用。
3. 完整性校验只认 .deb 形态，即使"开箱即用"复制形态成功也不放行，逻辑过严。

### 追加整改（cross_platform_package_deb.sh）

| # | 改动 | 效果 |
|---|---|---|
| 1 | **新增"零网络"策略（最优先）**：检测编译机 python3 已安装 PyQt5/psutil/OpenGL 时，直接从 dist-packages 复制进包并验证 import——同机同 ABI 100% 兼容，**完全不依赖网络**（编译机既然能跑老化 GUI 就必有这些模块） | 统信源缺包/CDN 再慢也挡不住打包 |
| 2 | apt 下载清单**精简为实际 import 的包**并**逐个预过滤**（apt-cache 探测，缺包跳过不再拖垮整体）；闭包下载超时 90s→600s、失败重试 1 次；逐包下载 10s→60s×3 次重试；新增 `offline_debs/` 人工离线包兜底 | 慢速源也能下全 |
| 3 | 完整性校验改为**双形态达标**：开箱即用（PyQt5/psutil/OpenGL 齐备）或 4 主包 .deb 齐备，任一满足即放行 | 与实际离线能力对齐 |
| 4 | **打包入口新增"外来二进制检测"**：主程序要求的最大 GLIBC 超过本机 glibc → 立即中止，提示"产物非本机编译，请先 rm -rf build bin/* 后重新 ./cross_platform_build.sh" | 杜绝把别处编译的旧产物混入打包 |
| 5 | 启动脚本 GLIBC 预检**分级**：主程序本身不兼容→致命退出+指引；仅包内库不兼容→**自动改用系统库启动**（装完即可运行），并提示重新打包 | 旧兼容性问题不再"一键必死" |
| 6 | postinst 检测同步分级（致命/警告） | 安装时即给出准确结论 |
| 7 | regression_test_elsky.sh 2.5 节同步分级措辞 | 目标机验收口径一致 |

### 本次问题的正确操作序列（在 it0 上）

```bash
# 1) 确保在 it0（统信1070）本机重新编译（不要用别处拷来的 bin/）
rm -rf build bin/ELSKY_test_client_*
./cross_platform_build.sh          # 应显示 "找到可用的 Qt5 qmake ... (Qt5, 校验通过)"

# 2) 重新打包（PyQt5 本机已装 → 走零网络复制，不再依赖下载）
./cross_platform_package_deb.sh    # 应看到 "✓ 零网络方案成功" 与 GLIBC 门禁全绿

# 3) 目标机卸旧装新
sudo dpkg -r elsky-test-client 2>/dev/null
sudo dpkg -i release/*.deb         # 或 release/deb_build/ 下的 .deb

# 4) 验收
elsky-test-client                  # 终端直跑应正常起 GUI
sudo bash regression_test_elsky.sh # 2.5 GLIBC 节 + 第3节 Python 依赖应全 PASS
# 拔网线再跑一遍老化测试，确认全功能离线可用
```

