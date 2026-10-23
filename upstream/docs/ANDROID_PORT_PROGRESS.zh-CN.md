# Android 移植进度

更新日期：2026-10-23。此文件随工作更新，是中断后继续移植的入口。

English: [ANDROID_PORT_PROGRESS.md](ANDROID_PORT_PROGRESS.md)

## 当前状态（继续开发先看这里）

**2026-10-23 最新进度（第五轮）：Mali（天玑 / Immortalis）真机跑通，3D 场景"全黑"的根因定位并修好，两台小米设备实测通过。** ① **平台**：新增 **Mali-G925-Immortalis MC12**（小米 25079RPDCC 平板，Android 16，Vulkan 1.3.278，**无 root**）支持。实测限制：`subgroupSize` 只有 16（`EXT_subgroup_size_control` 的 min=max=16，数据包的 `host-subgroup-size.txt` 必须改 16）、**所有 BC 格式 `optimalTilingFeatures=0`**（游戏贴图几乎全是 BC，改走 `dm/gfx/tex/nvn_bcdec.c` 在 CPU 上解成非压缩格式，与 Metal 后端同源）、GPU timestamp 不支持（回退 CPU 时钟）、`maxComputeWorkGroupInvocations=1024`。**重译着色器到 16 档是无效功**（同一 shader 在 64/128 档译出的 SPIR-V 逐字节相同，Ryujinx 生成的是运行时自适应代码），换 GPU 只要改那个 `host-subgroup-size.txt`。
② **崩溃一：BC 贴图** → 驱动收到后在自己代码里 SIGSEGV，已在端口里加无 BC 通路绕开。
③ **崩溃二：曾以为"3D storage image 必崩"** → 2026-10-23 用独立最小程序证伪（同��� SPIR-V 在 3D/2D、任意 LocalSize Z、带不带 requiredSubgroupSize 下都建得出）；当年真因是**诊断代码在持 `queue_lock` 时重复加锁**导致全进程自锁。**`local_z_fold`（把 compute 的 LocalSize.z 折小、dispatch.z 同步放大）确实是必需的**，关掉仍复现标志性崩溃（`addr=0x18`、寄存器里 `x5=0x7367616c662e656c` = ASCII `le.flags`）。
④ **本轮真正的 bug：3D 场景近乎全黑，而界面/血球/物品栏完全正常。** 量化：营地场景区平均 sRGB **5.2**、68% 像素纯黑（Adreno 同场景 43.4）。**不是分辨率、不是游戏内时间、也不是"被跳过的 compute"**（`552f45c348a61b86` 实测已建出并派发）。根因是 **LocalSize Z 折叠让着色器手算的 z 索引失配**：折叠只改 `OpExecutionMode LocalSize.z`（8→4）、dispatch.z 同步 ×2，驱动算出的 global z 区间不变，但这些 compute **不用 `gl_GlobalInvocationID`**，而是自己手算 `(WorkgroupID[2] << log2(LocalSize.z)) + LocalInvocationID[2]`（移位量 3 硬编码）。折叠后手算 z 只落在 {0-3, 8-11, 16-19, 24-27} —— **一半 z 切片永远不写**（其余越界被着色器自己的边界检查丢掉），写 3D 贴图的 compute（32×8×32 光照网格、32³ 调色表）因此只填一半 → 3D 世界几乎没有环境光，而界面在色调映射之后合成所以不受影响。**原代码里那段"乘积不变语义天然等价"的论证只对用 `gl_GlobalInvocationID` 的着色器成立**，这是这次排查最大的弯路。修法：折叠时把 z 那条移位改成 `<<log2(MALI_LOCAL_Z_TARGET)`（新增 `dm/android/native/nvn_vulkan_zfold.inc`），手算 z 即等于驱动 global z，每个切片恰好写一次，**等价于没有折叠**；x/y 的 LocalSize 没变，移位不动。独立测试 `dm/android/tests/zfold_test.c`（宿主可跑，3/3 PASS）。**修后**：营地场景区平均 sRGB **45.4**、纯黑 **0%**（Adreno 参考 43.4），与参考图基本一致。
⑤ **两个必须记的坑**：`gl_WorkGroupId` 要按 **BuiltIn 装饰**（`OpDecorate %var BuiltIn 26`）认，**不能靠 OpEntryPoint 接口列表的位置**（不同着色器里 WorkgroupId/LocalInvocationId 先后不一致，`2988a1dc` 的接口列表是反的）；会被折叠的 compute 共 **6 个**（用数据包 `*.res.json` 的 `local` 字段筛 `z>4`：`1188eaed`(8,1,8)、`2988a1dc`、`552f45c3`、`73a87ffb`、`890de4b2`、`de7267a5`），**只修到 4 个会漏**（那两个恰好在营地没派发）。
⑥ **可复用的定位手法**：先拿同项目**其它设备的同场景历史截图**当基线（一比就分出"设备差异"和"时间回归"，这次直接推翻了"用户改分辨率导致"的猜测）→ 反汇编真实 SPIR-V（`spirv-dis`，别信代码注释里的等价性论证）→ **零构建 A/B**（着色器是运行时从数据包读的，包不做逐文件摘要校验、字节数不变就不会被察觉，直接替换设备上的 `.spv` 就能验证假设）→ 设备日志 + `dumpsys` + `screencap` 三个加起来才是完整证据。
⑦ **Windows 构建链修好**：`build_gpu_driver.py` / `build_gpu_fixtures.py` 原本直接 `import fcntl`（Linux 独有），加了 ImportError 兜底；构建脚本读 UTF-8 文件需要 `PYTHONUTF8=1`；`--version` 传 `0.6.57`（脚本自己加 `-android-port` 后缀，`versionCode` = 0×10000+6×100+57）。
⑧ **已测设备**：小米 25079RPDCC 平板（Mali-G925-Immortalis MC12，3008×1880，Android 16，无 root）与小米 M2007J1SC 手机（骁龙 865 / Adreno 650，Android 12）各跑通完整流程 —— 导入数据包 → 启动 → 主菜单 → 角色选择 → 营地渲染。

**2026-10-17 最新进度（第二轮）：启动界面与「数据包导入」已真机验证（电脑端用 `make_package.py` 或 `.claude/skills/d2r-package` 生成 `d2r-package.zip` → 手机端选包 → 解包 → 点启动 → 进主菜单并渲染，`vulkan-perf` 60.01–61.44 FPS）；本轮把用户报告的三个问题全部修好并逐条真机验证，另给出分辨率的实测档位。① 方向键（Xbox）：手柄把十字键报成 `AXIS_HAT_X/Y`，而原生输入只读摇杆轴、完全不读 HAT 轴，于是一整块被静默丢弃——现已按 ±0.5 阈值映射成按钮位；接入实体手柄时虚拟摇杆自动隐藏。② 切后台存档：原顺序是"先挂起图形、再请存盘"，游戏线程被挡在 NVN 入口外，3 秒内 0 次提交（游戏自己 5 分钟才存一次）；改成"先请存盘（实测 241–262 ms 内落盘 2 次）→ 冻结客体时钟 → 挂起图形"后正常。③ 切后台再回来：进程保留、窗口/交换链重建、继续出图（59.9 FPS，PID 全程不变）；过程中查出安卓给窗口的时机在 `onResume` **之后**，重建交换链时必须自己先挡住出图线程（已修），并从 iOS 补齐了客体时钟冻结（实测后台 18 秒未被算进游戏时间：这段时间只出 420 帧 ≈ 7.0 秒）与 `dm_3d_scale_resumed`。④ 分辨率：营地实测 1600×900 → 1920×1080 基本免费（FPS 58.64 → 58.07、fence_wait 0.81 → 0.86 ms），2560×1440 约 −2 FPS（56.61）且 fence_wait 涨到 2.15 ms，三档都没有触发 3D 内部比例降档。⑤ 手柄 ZL/ZR（同样报成模拟轴）已修，四条输入路径全部按 Switch 位序真机验收；⑥ 按返回键不再整局退出，改成呼出游戏内菜单（帧率显示 / 虚拟摇杆 / 画质 / 退出游戏）；⑦ 调试浮层改为**全局默认关闭**；⑧ 存档可**导入/导出**（启动界面与游戏内菜单都能用，导出为 zip 走系统文件选择器，导入是"临时目录 → 校验 → 原子换入"，游戏内导入后不存盘退出、重开加载）；⑨ 应用名改为**「暗黑破坏神2」**并配上随源码发布的**矢量自适应图标**（`dm/android/local/` 可换成本机自己的图）；⑩ GitHub Actions 自动产出**通用 APK + 打包套件**两个制品（推 `v*` tag 即发 Release，玩家下载后交给 AI 助手用技能打包并 adb 装好）。以下本节为当前状态，后面的旧版本记录仅供追溯。**

**2026-10-17 最新进度（第三轮）：逐绘制 CPU 成本大幅下降，并把「掉帧」拆成两件事。** 用户报告「帧数不稳定、掉到 30 多帧、渲染有问题」。本轮先加**只读分段计时**（`nvn_vulkan_cpu_phases.inc`：每个出图边界取一次各阶段墙钟，60 帧汇总一行 `[vulkan-cpu-phases]`，只累加计数与纳秒、不改任何行为），把移植自己的开销逐段量出来再改。营地/野外 1268 次绘制实测：整帧 17.3–17.6 ms、线程 CPU 14.1–14.6 ms、GPU 约 12 ms；移植的逐绘制翻译 `draw` 9.7–10.8 ms，其中 `bind_shader`（每次绘制绑 VS/FS 两个着色器）5.4–5.7 ms。**先实测排除了三个原本的怀疑对象**：UBO 重读客体内存（6415 次/帧）只要 0.87–1.1 ms；布局互斥锁加哈希（每次绘制都做）0.05 ms；整个出图函数 `mtl_present` 只有 0.2 ms（`提交+出图` 那 10 ms 是 NVN 层回放命令流，不是出图）。真凶是**每帧 1131 次描述符集重建**（`descriptor_allocate` + `vkUpdateDescriptorSets`，2.63–2.77 ms）：游戏每个物体的 UBO 内容每帧都变 → 端口给它换 arena 槽位 → 描述符内容跟着变。改法是把这类 UBO 声明为**动态偏移**（`VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`）：描述符只留 arena 块基址加固定 65536 范围，位置在绑定时用偏移给，同一个块内换多少槽位描述符都不再变。**实测：描述符未命中 1131 → 538 次/帧（−52%），该段 2.63 → 0.81 ms，`bind` 5.4 → 2.77 ms，`draw` 9.7 → 8.24 ms，线程 CPU 14.2 → 13.2 ms，营地帧间隔 p50 16.87 → 15.93 ms；同一野外场景浮层 45–47 → 60–62 FPS。** 改这个必须同步动五处，漏一处都静默出错且编译不报：`dependencies.inc` 靠描述符类型判断「读 UBO 前要不要冲刷在途写入」、描述符 payload 编码（动态 buffer 会掉进「当图像」分支）、描述符池配额（动态是类型 8，只列 0–7 会分配失败）、工作区选择（漏了会拿到错误指针）、图形探针的偏移记录。设备动态 uniform 上限用两级回退兜住（先退驱动表槽位、再整批退回静态），实测最高用到 9 个、未触发回退。主机端 `uniform_arena_test.inc` 同步更新，并新增一个「让 GPU 真正消费动态偏移」的计算着色器用例。**另加独立异常帧日志**（用户要求后续排查直接看文件）：帧间隔或线程 CPU 达到 20 ms 才写进 `files/abnormal-frames.log`（与 `game.log` 同目录），记该帧的间隔/CPU/GPU/绘制数/通道数/贴图上传/UBO 读取字节数/UBO 内容变更/描述符未命中/本核与最快核频率，第二行是该帧自身的分段耗时；正常帧只做十几次减法、不写盘。**用它收的 1096 条异常帧把「掉帧」拆成两类**：① A 类（移植翻译占大头，128 帧）每次绘制从 9.6 涨到 20.0 µs；② B 类（移植不是瓶颈，924 帧）`draw` 只有 0.15–1.04 ms 而 CPU 高达 120–180 ms，是加载与流式载入。**A 类逐项对比确认不是代码干了更多活**：绘制数 0.95×、UBO 读取字节/绘制 0.98×、UBO 内容变更次数 0.95×、描述符未命中率 0.99× —— 全部一样，只有耗时 2.08×，同时**整颗 SoC 的最快核频率 p50 从 2400 压到 1997 MHz**。两个大核的 `scaling_max_freq` 都是 4320000（上限没被压），调速器是 `walt`，热区里有 `pm8550-bcl-lvl0`（电池电流限制）—— 所以是**游戏最忙、功耗最高（8.5–9.3 W）的瞬间被功耗策略临时削频**，同一份活干了两倍时间。**这一条解释了本项目此前一直「未解释」的 21–30 ms 轻超帧**（见下「实际效果和限制」旧记录）。结论：平稳帧率已修好（野外 60–62 FPS、帧间隔 p50 16.3 ms、单个 120 帧窗口 `over33ms=0`），剩余尖峰在设备侧功耗策略；代码侧还能做的是**继续压低单次绘制的绝对工作量**，让削频时也能塞进 16.67 ms。

**2026-10-18 最新进度（第四轮）：光照"方块"定因 —— 是系统驱动侧，内置 Turnip 解决，并在启动界面加了驱动切换。** 真机探针（`DM_VK_PROBE_FRAME` / `gpu-probe.request`）把光照的实际载体抓了出来：游戏把光照算进一张 **32×8×32 的 RGBA16F 三维贴图**（计算着色器以存储图像写入，片元着色器每帧采样约 1882 次，采样器是**线性过滤**）；可见范围内正好约 32 个格子，与实测的 **106 屏幕像素方块**完全对上。同一份着色器与数据在 **Turnip** 上渲染正常、在**系统 Adreno 驱动**（`driver_id=8`）上出现成块亮度不一致（人物附近最明显、逐帧跳变），帧率两者都是 60 上下 → **问题在驱动对这张三维存储图像的处理，不在移植的读数、上传或过滤**。本轮先按"是不是移植的锅"逐条实测排除了：贴图上传覆盖 GPU 内容（游戏内稳态 `uploads=0.00/帧`）、采样器过滤（与 Metal 后端逐项一致）、未登记贴图退回 1×1 替身（`unregistered_tex=0.00/帧`）。**因此把用户提供的 Turnip T30 内置进 APK（`dm/android/prebuilt/turnip/`，Mesa，MIT，构建者 @Mr_Purple_666；`build.py --turnip` 默认打包、`--no-turnip` 出通用包），并在启动界面加了「驱动：系统驱动 / 内置 Turnip」一行**：选择记在启动界面自己的 `launcher-state.conf`，启动时把 `assets/turnip/vulkan.purple.so` 复制到内部 `files/temp/driver/` 并写 `gpu-driver.conf`（含 `subgroup_size`＝数据包里 `host-subgroup-size.txt` 的值 64 —— T30 物理默认 128 而游戏着色器按 64 构建，端口会逐项校验、不一致直接拒绝启动），切回系统驱动则删掉该配置。真机双向验证通过（`requested=Turnip selected=Turnip identity verified` / `requested=system selected=system`）。**README 的"不含预编译驱动"那句已按用户决定改写**：Release 会带这个 MIT 许可的第三方驱动，仓库仍不含任何游戏内容。

### 源码和功能

- 私有仓库：[sydeEvans/d2r-android](https://github.com/sydeEvans/d2r-android)。保留上游许可与来源说明，不包含游戏资源、密钥或 APK。
- 最新已构建和安装的提交：本轮提交 `Fix the three reported issues and make the resolution selectable`（三个报告问题修复 + 分辨率可选，提交号见 `git log` 最近一条），分支 `main`；设备上的 APK 确实是这一版源码构建的（核对过：`dm/` 下没有比 APK 更新的源文件）。此前提交 `fa758de`（启动界面导入数据包并加载）、`0a1c0c1`（记录导入轮与四个根因）。更早基线 `891f596172042a7d2db702e66fb425ca0b6d6102`（`Reduce Android Vulkan frame-time overhead`）；本轮之前同批落地：`e392a90` 启动界面、`6461a5d` 浮层显示电池功耗、`d99f46a` 上下文入口测试改为当前契约、`c2298ca` 数据包格式与汇编器、`7b8bf06` 打包 CLI 与 skill、`13f7e7d` 启动画面随源码发布。
- 本轮新增（已真机验证）：数据包导入（`docs/ANDROID_PACKAGE.zh-CN.md` 是格式契约）。电脑端 `make_package.py` 从玩家自己的游戏文件与密钥解包/改写/翻译，产出一个 zip；手机端启动界面选它、解包、再启动游戏。**三个必须记住的约束**：① 模块解到**内部**目录 —— 外部存储是 `noexec` 挂载，从那里 `dlopen` 会失败（实测 `/storage/emulated` 带 `noexec`）；大资源仍在外部目录。② 包里的 RomFS 必须**整个 `data/` 目录**都打进去、不按扩展名筛选 —— 曾经漏掉隐藏文件 `.build.config` 与 `build_key.txt`，游戏报 "TACT Failed to load build config" 后直接返回 `nnMain`，症状像"游戏无故退出"；`ls` 不带 `-a` 对比还会显示"文件完全一致"。③ 分组档位文件 `host-subgroup-size.txt` 必须**随包发布**，运行时在加载驱动前会读它，缺了直接中止启动。
- 启动界面（`LauncherActivity`）：已导入才允许启动（未导入时入口置灰）、导入时显示百分比、导入是原子的（旧 `manifest.json` 先删、新的最后写，中途失败不会让游戏读到半个包）。调试浮层开关、画面档位与**分辨率**写进 `files/temp/launch-settings.conf`，由 native 严格解析（键白名单、失败即中止启动、不接受任意路径）。分辨率只给三档预设（跟随档位 / 1920×1080 / 2560×1440），空值表示"跟随档位"、不写这个键；真机步骤验证：点一下 → `分辨率：1920×1080` → 再点 → `2560×1440` → 点启动 → 配置写出 `resolution=2560x1440` → native 读到 `DM_GAME_RES=2560x1440`。**注意改动界面标签的函数绝不重新读盘**（点完又读盘会把刚改的内存值覆盖回去，症状是"点了没反应"，两行都曾如此）。
- 方向键与扳机（用户报告）：**同一类问题有两个**。① 十字键：Xbox 手柄在安卓上把它报成 `AXIS_HAT_X/Y` 而不是 `DPAD_*` 键码，而原生输入只读摇杆轴、**完全不读 HAT 轴**（改动前全仓 `grep AXIS_HAT` 零命中），于是一整块被静默丢弃。现在按 ±0.5 阈值映射成 `DM_BTN_LEFT/RIGHT/UP/DOWN`（`dm_input.h` 的 `dm_pad_hat_buttons`），并用**独立的 `hat_buttons` 位掩码**，免得 HAT 归零时把同时按住的 `DPAD_*` 键码一起清掉；源判断也放宽到 `JOYSTICK|GAMEPAD`，纯 GAMEPAD 源的方向键不会再掉进触摸分支被当成手指坐标。② 扳机 ZL/ZR：同样报成**模拟轴**（`AXIS_LTRIGGER`/`AXIS_RTRIGGER`，0–1，本机 flat=0.062）而不是 L2/R2 键码，键码分支永远收不到；现在读这两个轴（`AXIS_BRAKE`/`AXIS_GAS` 别名作兜底），阈值 0.5，同样用自己的位掩码（`dm_pad_trigger_buttons`）。三条输入路径（HAT 轴 / 扳机轴 / 按键码）现在**都在变化时各记一行** `[android-pad]`，出问题时不用靠猜。
- **真机逐键验收（Xbox Wireless Controller，Switch 位序）**：游戏的 `GetNpadState` 收样文件里实际返回的位值与按下的一一对应 —— 左 `0x1000`、右 `0x4000`、上 `0x2000`、下 `0x8000`、ZL `0x100`、ZR `0x200`、LB `0x40`、RB `0x80`、A `0x1`、B `0x2`、START `0x400`、SELECT `0x800`；阈值也按设计工作（`LT=0.41/0.47` 不触发、`0.66/1.00` 触发）。虚拟摇杆接入手柄后自动隐藏、拔出恢复，同一轮截图确认。宿主单测 `t_padmap` 36 项（含 9 条 HAT + 7 条扳机断言）。
- 启动界面承诺「只写自己的键、不动别人的」：`launch-settings.conf` 里比它管的三个键更多的键（如诊断用的 `input_receipts`）原先会被**静默抹掉** —— 实测就这样丢过一次 `input_receipts=1`（配置里写着、native 读到「未设」，HID 收样一直没生成）。现在重写时把不归它管的行原样带上（注释与空行仍由它自己写），修复后同一路径实测 `input_receipts=1` 保留并被 native 读到。
- **游戏内菜单（按返回键呼出，用户要求照 Eden 那样）**：返回键原先直接结束游戏页面，而本移植里结束游戏页面 = **结束整个进程**（一个进程只跑一个游戏），横屏手势导航下「边缘内滑」极易误触、一下就整局退出（实测：按一次 → 存盘 241 ms / 2 次提交 → 退出进程 → 回到桌面）。现在返回键改成呼出右侧菜单，四项：**帧率显示**（运行时开关，并写回配置让重启后记得）、**虚拟摇杆**（屏幕按键显隐；接入实体手柄时的自动隐藏照旧）、**画质**（3D 内部渲染比例四档：自动 / 高质量（配置满值）/ 平衡（0.85）/ 性能（0.70），**立即生效**；注意**输出分辨率仍在启动界面**，因为游戏的窗口贴图按启动时的参数一次建好、运行中只能缩不能放）、**退出游戏**（走 `finish()`，与原来的退出同一条路：先请游戏存盘、再释放窗口、再结束进程）。再按一次返回或点菜单外面收起。菜单打开期间手柄与按键被挡在游戏外（否则角色会在菜单后面继续动），**返回键与触摸放行**（返回键要能走到菜单窗口去收起它）。**真机验证（2026-10-17）**：uiautomator 读到四个真实控件（`游戏菜单` / `帧率显示：关` / `虚拟摇杆：开` / `画质：自动` / `退出游戏`，面板在右侧 x 3024–3340），两次返回键 = 开/关且 **PID 全程不变**（修前同一次返回就换进程），截图确认右侧深色面板。逐项实测：**画质**四档依次生效并真实改内部分辨率（`3D 渲染比例 0.85 → 3D 目标 1360x765`、`0.70 → 1120x630`、回自动 `1.00 → 1600x900`，输出始终 1600x900）；**帧率显示**开/关标签随之切换且调试线程继续产出；**虚拟摇杆**关掉后屏幕按键隐藏；**退出游戏**：存盘 241 ms / 2 次提交 → `activity destroyed` → `优雅退出完成（0.000 秒）` → 进程结束，logcat 留下 `[menu] exit row tapped` → `[menu] hidden`。**注意：验证过程中曾出现一次"第二次按返回后进程消失"，查到原因是前台被系统设置抢走（我上一条被中断的滑动测试把设置的任务卡片点开了），后台活动被系统回收后按设计结束进程 —— 不是菜单的问题；清掉那个状态后重跑即通过。**
- **GitHub Actions 自动流水线（用户要求）**：`.github/workflows/build.yml` 每次 push / PR 跑「可移植检查 → 构建通用 APK → 组装打包套件」，产出两个可下载制品：① `D2R-Android-port.apk` —— 宿主与前端完整、**不含游戏代码**（15.8 MB；游戏模块由手机端导入的数据包提供），真机验证过：装上它 + 导入数据包即可进游戏（日志 `imported package in use` + guest 正常引导）；② `d2r-package-kit-<版本>.zip` —— 给玩家电脑用的套件：源码 + `.claude/skills/d2r-package` 技能 + 上面那个 APK + 一页说明（10.9 MB；只打 git 已跟踪文件，构建产物与本机覆盖目录不进包）。推 `v*` tag 会建 Release 并把两者挂上去。**CI 不构建"带游戏代码"的 APK**：那部分从玩家自己的游戏文件改写而来，仓库与 CI 都不该有。为了让这套成立，`build.py` 放开了「`--game-run` 必须带 `--game-source`」这条限制 —— 不带时明确打印 `Package-only build` 再产出通用包。检查脚本 `dm/android/tools/ci_checks.py` 本地与 CI 跑同一组（中文字面量、中英表、方向键/扳机映射 36 项、启动设置普通 53 + 故障注入 59、存档包 19 项、仓库卫生），macOS 专有的 `t_thread_leak` / `t_i18n` 如实列为"跳过"而不是假装全绿。
- **所有构建一律壳模式（用户决定）**：三张游戏派生表（编年史仓库物品表 / 经验表 / 关卡亮度表）的内容**不再编进程序**。仓库里提交的是**运行时替身**：`dm/host/dm_vault_tab.h`、`dm_hud_exp.h`（空表 + 条数 + 启动时填表），`dm_bright_levels.inc`（只有 `DM_BRIGHT_COUNT`，`dm_bright.c` 运行时填）。数据由数据包里的 `derived.tsv` 提供（标签 `EXP` / `VTPART` / `VTROW` / `VTSET` / `BRT`）：`make_package.py` 把三张真实表生成到工作目录（**不写源码树**）→ `dm/tools/shell_stubs.py data` 拆成 `derived.tsv` → `build_package.py assemble --derived` 放进包根；前端在导入的包里找到它就把路径经 `DM_DERIVED` 交给宿主（`dm_derived_rt.h`，Mac/iOS 仍按可执行文件旁的 `dm/derived.tsv`）。**时序要点**：库加载时的构造器早于前端设置 `DM_DERIVED`，所以 Android 由前端在 `boot()` 里显式调 `dm_hud_exp_load()` / `dm_vault_load()` / `dm_bright_load()` 填一次。导入白名单新增的是**精确文件名** `derived.tsv`（不放松成"任意根文件"）。`shell_stubs.py` 从 `dm/ios/tools/` 移到平台中立的 `dm/tools/`（它服务的是共用宿主）。CI 新增检查：三张表必须是替身（缺标记或体积过大即失败），防止真实游戏数据被误提交。**这样 CI 不需要任何游戏文件就能构建可玩 APK** —— 这正是上一条流水线失败的根因。
- **应用名与图标（用户要求）**：系统里的应用名改为 **「暗黑破坏神2」/「Diablo II」**（`dmAndroidTitle`）。图标是随源码发布的**纯矢量自适应图标**（`dm/android/res/`）：近黑底 + 两层低透明度金色圆做微光 + 金色菱框 + 四面暗红宝石 + 中心金点（"灵魂石"式几何图形，**项目自画、不含游戏美术**）；另有 Android 13 主题图标的单色层（`mipmap-anydpi-v33`）。minSdk 已是 29，所以不需要为更老的系统准备 PNG 位图。`dm/android/local/ic_launcher.*`（该目录已 gitignore）可以把图标换成本机自己的图，与启动背景图同一套约定；构建时 `res/` 会被复制进构建目录再交给 aapt2。
- **存档导入 / 导出（启动界面 + 游戏内菜单）**：导出把内部 `files/save/`（角色 / 共享仓库 / Settings 三个目录，本机实测 17 个文件）整棵树打成 zip，走系统的「新建文档」选择器，玩家自己选存到哪（下载目录、根目录都行，应用不需要存储权限）；导入走「打开文档」选择器，流程是**解到临时目录 → 校验 → 原子换入**（换入前旧目录还在，失败回滚；越界路径 `..` / 绝对路径 / 反斜杠、空包、超限都会被拒）。压缩 / 解压 / 替换做成纯 Java 的 `SaveArchive`，**宿主机 19 项测试**覆盖往返、越界、空包与回滚。**游戏内导入必须退出游戏**：当前这局的内存状态不能覆盖刚导入的存档（退出路径"请游戏存盘"那一步会跳过，native 侧 `skip_save_once`），重开后加载导入的存档 —— 游戏宿主进程无法原地重启，所以这是"导入 → 退出 → 重开"而不是就地换档，确认框里写明了这一点。
- **调试浮层改为全局默认关闭**（用户要求）：只有配置里明确写 `debug_overlay=1` 才显示 —— 启动界面的开关初值、游戏内菜单的初值、native 启动时的读取三处统一成同一语义（写 0 或没写都是关）。native 那条「运行状态」生命周期日志不受浮层开关影响，仍然每 10 秒一行，方便诊断。
- **切后台后从桌面点图标再进入会「终止这一局」（用户报告）**：根因不在后台保留那条（它已经好着：进程保留、窗口/交换链重建、客体时钟不跳），而在**任务栈结构**。启动界面是本应用的入口活动，切后台后点桌面图标，安卓会在游戏**上面**再开一个启动界面实例 —— 任务栈于是变成「启动界面(根) / 游戏 / 启动界面(新)」，玩家看到的是启动界面，像是「游戏没了」；再点「启动游戏」就开第三个活动并把中间那个**游戏活动销毁**，而销毁游戏活动在本项目里等于**结束整个进程**（一个进程只跑一个游戏），新开的那个也一起死，安卓随后重新起进程从头加载 —— 体感就是「应用终止、这一局没了」（存档安全：切后台时已经存过）。真机复现：点之前 PID 26997、点之后 PID 28545（换了进程，日志被新实例截断）。修法：只要游戏活动还在（同进程静态标记 `D2RActivity.hostRunning`），启动界面在 `onCreate`/`onResume` 里立刻 `finish()` 让位，把下面的游戏露出来，走已验证的「回前台重建交换链」那条路。**代价与用法：游戏跑着时启动界面不再可达 —— 要改设置（分辨率 / 换数据包）请先把应用从最近任务里划掉，那才是真正退出。**
- 接入实体手柄时隐藏虚拟摇杆（用户要求）：用 `InputManager.InputDeviceListener`（设备增删级监听）+ 启动时扫一次，**不能用窗口事件分发** —— NativeActivity 把 `AInputQueue` 直接挂到主 Looper 消费手柄事件，窗口那层根本看不到。拔出时按同一路径恢复显示。
- 切后台的存档与进程保留（用户报告"切后台存档可能丢""切后台再打开会重启"）：两个问题的根因都在同一处顺序。窗口销毁不再等于退出进程（真正的退出走 `destroyed()`），并且**必须先请游戏存盘、再挂起图形**：请存盘要等游戏自己跑完一次定时存盘并提交，而图形一挂起游戏线程就被挡在 NVN 入口外，提交永远等不到（真机实录：3 秒内 0 次提交）。`mtl_screen_attach` 重建交换链前自己也挂起（安卓给窗口的时机在 `onResume` **之后**，此时游戏线程已在跑、可能正读着旧交换链），重建完由前端按"当前是否在前台"放开。退出路径上重复的第二次请存盘已去掉（那时图形已挂起，注定等不到，只会白等 3 秒再打一条假红字）。
- 调试浮层新增电池行：电流、电压、按二者算出的功率、电量，来自框架（应用不需要 root）；浮层可关，关掉后调试线程跳过整块显示工作，但首帧卡死检测照常。
- 仓库自带测试 `test_context_entry` 长期失败（140 子用例 100 失败）。根因是一次提速改动把入口快路从"序号校验"简化成"只比 TP"，测试没同步改；测试已改成断言当前契约（并加强：命中路径一条 `msr` 都不许有）。
- 本轮新增（已真机验证）：Vulkan 管线录制 + 预热（`dm/android/native/nvn_vulkan_warm.inc`）。每次真实创建图形管线后把完整状态（vs/fs 键、通道键、管线键）追加写入应用私有目录的 `pso_vk_user.tsv`；下次启动在初始化末尾开后台线程按清单逐条重建管线（建后即毁，编译结果留在驱动管线缓存），并在第一次进局读取画面处最多等待 15 秒让预热收尾，进局后不再现编。清单带版本与结构尺寸头，不符整份忽略；预热线程只做非致命加载，任何可疑记录跳过，不中断游戏。开关：`DM_PSO_MANIFEST=off` 关预热、`DM_PSO_USER=off` 关记录；与运行期共用同一份创建函数，动态状态（视口/剪裁/混合常量/深度偏置/模板掩码）一律不参与创建，保证预热与游戏命中同一驱动缓存条目。预热线程不记账（`resource_create_quiet`），长帧账目与慢创建日志只反映游戏线程；长帧"新建管线 N 条 + 着色器库 M 个共 X ms"的累计值修复后显示真实值。
- 当前工作目录：`~/d2r`（2026-10-16 由 `~/dm-work/publish/d2r-android` 迁回；旧开发现场归档于 `~/dm-work/archive/d2r-old-site-20261016`）。源码只在 `~/d2r`，`~/dm-work` 仅存放工具、资源和运行产物；继续开发前应确认目录与分支。
- 已具备 Vulkan 渲染、AAudio 声音、虚拟摇杆和按键、系统键盘输入、横屏与屏幕适配、帧率及 CPU/GPU 调试信息。
- 本轮远端优化包括：减少无效差异导致的重复管线状态，缩小待处理内存范围和已知复制范围的查找，复用着色器资源绑定规划，调整缓存写入重试频率，触控浮层仅在状态变化时重绘。
- 维护约定：本文件顶部「当前状态」永远反映最新进展与可重建步骤；每次工作先读此处，收工前更新并提交推送，任何电脑 clone 后即可继续。

### 构建和部署

- 干净源码首次构建缺少 `dm_vault_tab.h`、`dm_hud_exp.h`、`dm_bright_levels.inc`。已通过原 `dm/host/Makefile` 从本地 RomFS 生成，随后生产 APK 构建和签名验证成功；中英文 Android 构建说明（`docs/ANDROID_BUILD*.md`）已补充生成步骤。
- 可重建性验证（2026-10-16）：在全新 `git clone`（HEAD `891f596`）中按 `docs/ANDROID_BUILD.zh-CN.md` 全流程重建成功——三张生成表与既有产物字节级一致，重写后的 `libdmguest_main/sdk/subsdk0.so` 与既有构建产物逐字节一致，生产 APK 构建成功（约 4 分 47 秒）。证据：`~/dm-work/research/android-repro-verify/`。
- 已覆盖安装到 OPPO `OPD2409`（`adb install -r` 均返回 `Success`，本轮三次重建各约 1.4-1.5 s，应用数据保留）；实际进入已有角色的营地并走动。未重新传送游戏资源或覆盖存档。
- 最近一次观察无线 ADB 在线。端口会变化，连接时使用 `adb devices -l` 和 `adb mdns services` 获取实际状态。
- 管线磁盘缓存已确认重启加载：`load=accepted bytes=1739385`。受控对照（删除驱动缓存 blob、移开清单）证明预热把菜单 826 ms、进营地 562 ms 的现编时间挪到了后台（见下表）。
- 本轮未新增或运行自动化测试；完成了生产构建、安装、实际启动、进营地走动、用户实战打怪日志和受控 A/B 对照检查。本轮仅更新 OPPO 平板，新手机尚未安装验证此提交。

### 实际效果和限制

| 观察场景 | 已记录结果 |
| --- | --- |
| 安装后静止营地 | 截图 60.0 FPS，CPU 7.7 ms、GPU 7.4 ms；两个连续 120 帧窗口无超过 33 ms 的帧，最大间隔 23.640、24.435 ms |
| 后续营地移动 | 截图 58.2 FPS；随后十个 60 帧窗口为 59.38–60.69 FPS，对应五个 120 帧窗口无超过 33 ms 的帧，窗口最大间隔 22.826–31.482 ms |
| 新管线编译卡顿（修复前） | 根因已确认：整段会话 26 条 `[vulkan-create-slow]` 全部是首次使用的管线在 `vkCreateGraphicsPipelines` 内现编，最高 111.987 ms（frame 109872，同帧间隔 133.958 ms、线程 CPU 126.224 ms） |
| 预热对照 A（冷驱动缓存、无清单） | create-slow 26 条（最高 117.9 ms）；菜单最坏帧 957.2 ms（新建 30 条 826.0 ms）与 561.9 ms；进入营地 633.6 ms（新建 32 条 562.0 ms）；营地走动 140.5/89.3 ms |
| 预热对照 B（冷驱动缓存 + 109 条预热） | 渲染线程图形管线现编 0 条；菜单 141.8 ms（新建 30 条 52.1 ms）、进营地 98.0 ms（新建 28 条 2.5 ms 全命中）、营地走动无 >46.8 ms 帧；预热线程后台建完 109 条 2114.0 ms（帧 1 仅 26.2 ms 未被阻塞） |
| 用户实战打怪（预热局） | 首次击杀特效 18 条新管线现编，卡顿簇 119.1/103.3/102.7/102.1/99.4 ms；已全部录制，下一局 `built=127 wall_ms=447.7` 预热成功，开局唯一 create-slow 为 1 条计算管线（26.7 ms） |
| 其他长帧 | 原 91.010 ms 长帧已结案：frame 110574 为两次管线编译（38.5+31.8 ms）加 4 ms 着色器加载；会话共 3126 条长帧，其中 95% 是 21–30 ms 轻超帧（线程 CPU 偶发冲到 18–19 ms 错过一次垂直同步），表情轮盘簇（248/193 ms，提交+出图仅 4.2/22.8 ms）与恢复/切场景后簇仍未解释 |
| **21–30 ms 轻超帧已定因（2026-10-17 第三轮）** | 用新增的独立异常帧日志（`files/abnormal-frames.log`）收 1096 条异常帧做逐项对比：移植翻译占大头的 128 帧里，绘制数 0.95×、UBO 读取字节/绘制 0.98×、UBO 内容变更 0.95×、描述符未命中率 0.99×，**操作次数与字节数全部相同，只有每次绘制耗时 2.08×（9.59 → 19.96 µs）**，同时整颗 SoC 的最快核频率 p50 由 2400 降到 1997 MHz。大核 `scaling_max_freq` 仍是 4320000（上限未压，调速器 `walt`），热区存在 `pm8550-bcl-lvl0`（电池电流限制）→ 结论是**功耗策略在高负载瞬间削频**，不是代码效率退化。另一类 924 帧是加载/流式载入（`draw` 0.15–1.04 ms 而 CPU 120–180 ms），与渲染无关 |
| 逐绘制翻译成本（2026-10-17 第三轮，野外 1268 次绘制） | 改前 `draw` 9.7–10.8 ms（`bind` 5.4–5.7、`dep` 1.2–1.4、`ubo` 0.9–1.1、`descset` 2.63–2.77）；改后 `draw` 8.24 ms（`bind` 2.77、`dep` 0.46–0.98、`ubo` 0.96、`descset` 0.81、`resolve` 2.1–3.5），线程 CPU 14.1–14.6 → 13.2 ms，帧间隔 p50 16.87–17.89 → 15.93–16.31 ms |
| 出图分辨率（营地，2026-10-17，16 个 60 帧窗口，剔除探针帧） | 1600×900：FPS 均 58.64（49.42–60.23）、`vulkan-time` GPU 6.48 ms、fence_wait 0.81 ms；1920×1080：58.07（44.11–60.42）、6.55 ms、0.86 ms；2560×1440：56.61（48.13–60.11）、8.14 ms、2.15 ms。三档都没有出现 `3D 比例控制` 降档记录，即 3D 内部比例保持满值。注意 `GPU=` 这一项只统计游戏批次、**不含最后的缩放合成**，所以分辨率代价主要体现在帧率与 fence_wait 上，不能只看它 |
| 切后台 / 回前台 / 正常退出（营地，2026-10-17） | 按 HOME：`退出前等游戏存盘 241 ms，期间存档提交 2 次` → 冻结客体时钟 → 图形挂起 → 窗口销毁，进程保留（PID 不变）。移回前台：`图形恢复 → 图形挂起 → 窗口重建（game surface 3392×2400 images=5）→ 图形恢复`，PID 仍不变，回来后新出 23 条出图记录。客体时钟在后台期间停住：后台前 frame 2075 记 18015.2 s，回来后 frame 2495 记 18022.2 s——即那段时间只按真实出图走了 7.0 秒，没有被算进游戏时间（否则会多出十几秒）。正常退出（清任务栈）：存盘 241 ms / 2 次提交、无重复请求、无红字，老进程 1.30 秒退出 |

当前截图中人物、营地、地面、界面和触控按键正常，未出现此前的黑屏。以上只覆盖已观察的营地场景，不能证明全部地图、技能、战斗和光照正确，也不能宣称全程稳定 60 FPS。FPS 是应用统计，不能等同于每帧都准时显示。

本机证据：`~/dm-work/research/android-latest-891f596/`。成功构建见 `build.log`，生成表头见 `generate-tables.log`，安装后日志见 `game.log`，后续观察见 `current-game.log`；截图为 `launch.png`、`camp.png`、`current.png`，摘要为 `RESULT.md`。本轮日志分析、预热实现与真机验证记录：`~/dm-work/research/android-pipeline-prewarm-20261016/`（含 `live-game.log` 基线、`session2a.log` 预热局、`logA.log`/`logB.log` 对照、`session_user.log` 实战打怪、`session_verify.log` 127 条验证、截图与 `RESULT.zh-CN.md`）。本机产物不随源码发布。

### 下一步

用户报告的三个问题、手柄方向键与 ZL/ZR、切后台再进入、游戏内菜单、存档导入/导出、调试浮层默认关闭，本轮都已修好并真机验证（根因、改法与实测数字见上「源码和功能」与「实际效果和限制」）。仍未做完、需要继续的：

1. 计算管线仍未录制（至今首次现编 26.7–56.6 ms，游戏中首次用到会卡一下）：在录制端加 compute 挂点并扩展清单格式后可一并预热。
2. 把本机积累的 `pso_vk_user.tsv`（139 条）整理为仓库种子清单随构建发布，让驱动更新/清数据/新设备直接受益。**发放前的崩溃风险已核查并加固**：预热路径上唯一一处"让驱动调用决定进程生死"的 `vkCreateRenderPass` 已改成与 `graphics_pipeline_create` 同一约定（预热传 `fatal=0`，驱动拒绝就跳过该记录）；**但这台设备上没复现出退出，所以按"防御性加固"记录，不要写成修好了一个已复现的崩溃**。另注意仓库的「派生信息」政策（`docs/DERIVED.zh-CN.md` / `CONTRIBUTING.zh-CN.md`）：往仓库里加这类由游戏文件派生的东西，先开 Issue 商量。
3. **剩余长帧的后续（2026-10-17 第三轮已定因一部分）**：21–30 ms 轻超帧已查明是设备功耗策略在高负载瞬间削频（同工作量耗时 2.08×、SoC 最快核 2400 → 1997 MHz），不是移植效率问题；代码侧只能继续压低单次绘制的绝对工作量（野外 `draw` 8.24 ms / 1268 次 ≈ 6.5 µs 每次，仍有下降空间：`resolve` 逐绑定解析 2.1–3.5 ms 里每次绑定都走一次全局互斥锁的采样器/贴图查找是最大单项）。**待继续**：表情轮盘簇（248/193 ms）、恢复/切场景后簇、以及削频本身的设备侧实验（把本应用加入厂商「游戏空间」白名单后是否放宽 `pm8550-bcl-lvl0`）。异常帧日志已就位，复现时直接读 `files/abnormal-frames.log`。
4. 分辨率再往上（2560×1440 以上）必须先解决取舍：营地 1440p 已经吃掉约 2 FPS、fence_wait 涨到 2.15 ms，再往上越过 16.7 ms 预算就会触发 3D 内部比例降档，那时 UI 更清但 3D 更糊。游戏内菜单的「画质」现在可以直接切 3D 内部比例（自动 / 高质量 / 平衡 / 性能），**输出分辨率仍在启动界面**（游戏的窗口贴图按启动参数一次建好，运行中只能缩不能放）。
5. **超分（FSR / SGSR）是第三条画质杠杆，用户已决定先不接**：结论、许可边界（AMD 头 MIT 可用 / Eden 胶水 GPL-3.0 不可用 / 高通 SGSR 是 BSD-3）与「先离线截图对照、再上机 A/B」的步骤见 [FSR 超分调研](ANDROID_UPSCALING_RESEARCH.zh-CN.md)。**尚未实施、也没在 Adreno 上测过开销。**
6. 在新手机部署同一提交，确认显示、触控和实际帧率。
7. 任何电脑 clone 后可按 `docs/ANDROID_BUILD.zh-CN.md` 重建、按 `.claude/skills/d2r-package` 生成数据包；主线保持 Android 优化，LASRecomp 和 Eden 作为渲染实现参考（Eden 的着色器翻译是 GPL-3.0，不能并入本 MIT 项目）。

## 历史进度（以下版本、端口与待办按当时状态保留）

**最新帧稳定性版本（2026-10-16）：** `adpf-cycle-20261016.apk` 已正常编译9.970秒、安装并采集4200帧，源文件与构建冻结一致。实际同4009,4929营地2400–4200帧均35.82387FPS（33.11–43.16）。本轮相同长度诊断基线均34.68774FPS；15个120帧窗口里超过33.333ms从295/1800降为207/1800，超过50ms前后均1帧，最大50.141→50.634ms。没有温度匹配或重复A/B，不能宣称稳定提升，卡顿及60FPS目标仍未解决。画面正常，未写或运行测试。

已修复安卓性能提示ADPF计量契约：单线程会话不再混加后台worker耗时，改为同window/index/tid的acquire等待后至present后完整周期，单调墙钟扣WSI完整调用（含少量驱动CPU），保留GPU依赖等待。暂停清周期，恢复允许新周期，错配丢弃。实际4320周期上报全成功。分项证实部分44ms长帧含32ms线程CPU、取图/呈现/worker均低于1ms；后续深入CPU工作波动，不凭频率样本断言热降频。证据 `~/dm-work/research/android-adpf-cycle-20261016/handoff.zh-CN.md`，基线 `android-stability-20261016/`。ADB<TABLET_ADB_SERIAL>，720p/T30/subgroup64不变。以下为历史。

**第七版最新（2026-10-16）：** `binding-plan-20261016.apk` 已编译、安装并进入同4009,4929营地，4200–6000帧31窗均 **36.54258FPS**（33.49–40.63），人物和场景截图正常。保持720p/T30/subgroup64，未写或运行测试。新实现将不变的着色器绑定规划移到初始化，并让上下文入口命中路径不修改条件标志，省去NZCV保存恢复；完整资源同步与线程缓存检查保留。正常build exit0/10.908秒、install成功，源文件与安装构建冻结一致。没有温度匹配A/B，不能把相比第六版的差额全归因于优化，60FPS仍未达到。

本轮证据 `~/dm-work/research/android-binding-plan-20261016/`，截图 `camp/device.png`，详细 `handoff.zh-CN.md`；ADB目前<TABLET_ADB_SERIAL>。frame6000提交CPU10.946ms、wall18.914ms；整块GPU clone0、原位patch0.0218MiB/frame、UBO快读4278/frame且fallback/errors0。实际CPU采样12900样本/lost0（3000–3780帧，已排除在最终FPS区间外），入口仍占用户周期10.09%，绑定4.82%；这些比例不是绝对耗时。以下第六版及更早状态为历史。

**最新用户约束：不要写测试，专注生产实现和真机运行。** 不再新增或执行测试/fixture。当前第六版`word-hash-20261016.apk`已正常构建/安装并进入同4009,4929营地，2400–4200帧均35.09129FPS（32.37–41.88），材质正常，GPU均10.970ms。每帧GPU整块clone为0，小UBO内容复用与4319次权限租约快读全命中，页面权限43次/0.46ms、fault约24次；99.98%必败复制搜索已跳过。没有温度匹配A/B，不报告稳定收益百分比；CPU提交仍约11.6ms并有真实等待，60FPS未达到。当前入口：[增量实现记录](ANDROID_GPU_SNAPSHOT_DELTA_PLAN.zh-CN.md)“本轮当前交接入口”；证据`~/dm-work/research/android-word-hash-20261016/`，图片`camp/device.png`。查无端口固定30FPS上限、时钟/精度未改，ADPF仅只读核查未改调度。以下旧APK/待采集/旧端口状态均按历史保留。

继续运行4800–9480帧共79窗均35.04101FPS（30.75–47.09），clone继续0、fallback0；生产源与已安装APK freeze逐项一致。最新真实profile12110样本/lost0，主要下一目标是bind_shader、调用入口与真实依赖等待；ADPF分账问题仅记录待实际会话证据，不修改调度。以下首版和早期记录保留为历史，不作为继续运行测试或恢复PC路线的指令。

**用户路线决定：已有 Android 版提高帧率，LASRecomp 仅作渲染借鉴，停止 PC 静态重编译扩张。** 本轮入口：[2026-10-16 帧率优化记录](ANDROID_FPS_OPTIMIZATION_20261016.zh-CN.md)。已定位18.667MiB vertex只读SSBO因6–8页写标记整块重读（约5.2–8.8ms），NVN尺寸传递正确；另一1MiB texel全页标记但仅约0.06ms。Eden按页更新已借鉴；LAS原heap导入在系统/T30均实测缺扩展，未移植该路径。首版增量之后的原位更新、post0合批、小UBO内容复用与权限租约快读、复制范围负筛选和图形hash已合入当前第六版；具体实现和实际结果见[当前交接](ANDROID_GPU_SNAPSHOT_DELTA_PLAN.zh-CN.md)。最新ADB实际`<设备地址>`、transport12；以下旧地址与状态段保留为历史。

移植版已在安卓真机用 `AndroidProbe` 实际进入第一幕营地、移动并响应技能请求，声音已由用户确认恢复；原先黑剪影的材质源刷新已修复。T30明确64模式的同APK/同位置对照已完成：T30脚下矩形斑块消失，切回系统驱动再次出现；尚不能宣称全部渲染正确。接入CPU精确索引后，同坐标掌机720p的T30约21.1–22.5FPS（均21.805），GPU均16.08ms；此前同APK系统对照均20.026FPS/GPU24.463ms，60FPS目标仍未达到。显示接口已真机修正为原NVN固定60Hz节拍；没有改游戏时钟。虚拟摇杆与完整按键第一版已安装，16项离线行为及真机双指/独立抬手/取消通过，新HID返回也已证实游戏实际收到杆与A并独立释放。

用户亲自切换 Eden 为 Turnip T30 后已进入同版游戏营地，三次截图显示30.0FPS。Eden与移植场景不同，不能直接把帧率差全归因于驱动。合法真实Maxwell测试发现T30默认128的ballot只有64位，高64的ALL失败。**明确通过Vulkan选择64后，真实22列×128调用全部PASS**，生产选择已正式接入；新system默认/strict T30 required64完整GPU回归与profile均exit0，32/48部分活跃Maxwell及256片元API真像素也PASS。完整合法full64/full128缓存均6931全val/ABI，去未使用能力声明外全部执行指令与旧缓存一致。四路光照CS原输入交叉已完成。当前安装 `pending-index-reader-buckets-mapbench-calibrated.apk`，game部分与正常桶APK一致，strict T3064、probe0关闭，营地均22.074FPS/GPU16.172ms。实际4次大读取21.917MiB/wall12.510ms是主要目标；独立真实映射基准已排除更慢的中转复制。heap权限读取租约与首版owned snapshot接入已在源码实现，正在真实并发/GPU测试，尚未构建或安装含快路的game APK，60FPS仍未达成。

游戏 Activity 为 `org.d2r.android.port/.D2RActivity`，ADB序列号 `<设备序列号>`。当前 ADB 在线，最新地址 `<设备地址>`、transport5，未重配对。所有修改仍未提交，游戏代码/素材/着色器缓存保持 git 忽略。后文为历史记录，以文末和各专项说明的最新证据为准。

## 目标和用户决定

- 将当前仓库的 Switch ARM64 原生执行兼容层移植到 Android，进行实际构建和真机尝试。
- 使用 `$HOME/Downloads/dd.zip`，不再做游戏包完整性/版本校验。
- 使用 `$HOME/Downloads/ProdKeys.zip` 在本机解包。密钥内容不得写进日志、仓库或文档。
- 用户已提供无线 ADB 配对信息并要求配对；已配对且自动连接。
- 用户要求将关键工作信息保存到 Markdown，并持续更新。

## 工作区与设备

- 仓库：`<LOCAL_WORKTREE>`；起点 `main`，提交 `2eb5758`，上游 `jhaizhou-ops/d2r-mobile`。
- 主机：Linux x86_64；宿主目录同时挂在 `/home/<用户>` 与 `/var/home/<用户>`。
- Android 设备：`OPD2409`，SoC `SM8750P`，`arm64-v8a`，Android 16 / API 36，4 KB 页，内存约 12 GB。
- ADB：`~/dm-work/tools/android/platform-tools/adb`。
- 已连接序列号：`<设备序列号>`。
- 最新连接地址：`<设备地址>`。无线端口可能变化，应先用 `adb devices -l` 和 `adb mdns services`。
- 不保存临时配对码。
- Vulkan 查询得到 Adreno 830 / Vulkan 1.3；窗口实际为 3392×2400，是大屏设备，后续按实际尺寸适配。

## 本机产物（不进 git）

- 工具根目录：`~/dm-work/tools/android`。
- Python 环境：`~/dm-work/tools/android-venv`；已安装 lz4、capstone、unicorn、zstandard、nsz。
- 密钥：`~/dm-work/data/private/prod.keys`、`title.keys`；目录 0700，文件 0600。
- 原始游戏包已从 ZIP 提取到：`~/dm-work/data/android-input/`。
  - 本体 NSZ，约 7.7 GB。
  - `v-diablo_ii_resurrected_v2228224.nsp` 更新。
  - `Reign_of_the_Warlock` DLC NSZ。
- hactool 已从官方仓库克隆并编译：`~/dm-work/tools/hactool/hactool`。
- 工具安装日志：`~/dm-work/tools/android-bootstrap.log`。
- 已将完整 RomFS 的 46 个文件（8,973,547,258 字节）传到设备 `/sdcard/Android/data/org.d2r.android.port/files/romfs/`。
- 设备启动探针目录：`/data/local/tmp/d2r-port/`；真实模块放 `game/`，临时目录 `temp/`，探针存档 `probe-save/`。
- 真实宿主：`dm/android/build/host/dmhost-android`；源码构建命令 `python dm/android/tools/host_link.py`。
- 日志：`~/dm-work/tools/android-{build,tests,host-boot,romfs-push}.log`。

## 已实现与验证

- 新建 `dm/android/`，构建产物和本机配置已忽略。
- `tools/bootstrap.py`：在仓库外安装 NDK r28c、API 35 平台、build-tools 35、ADB、JDK 17。采用分段下载；Google 工具包按官方公布摘要核验。这不是游戏文件校验。
- `dm/tools/patch_nso.py` 增加 `DM_TARGET=android`：补丁通过保留完整寄存器的入口取得动态 TLS 上下文；默认 Apple 路线保留。
- 改写器增加显式 `DM_ALLOW_PREPATCHED=1`：允许字节恰好等于同一登记补丁的新字节，不接受未知字节。
- `tools/guest_elf.py`：将改写后的 NSO 打包成 16 KB 对齐的 ELF 动态库，保持模块相对布局，导出 `dm_guest_image`。
- `native/runtime.[ch]`：宿主线程上下文、ELF 模块加载/键页填写，以及四线程隔离测试。
- `native/activity.c`：真正的 Android NativeActivity，生命周期、多点输入、原项目的纯 C 触屏状态机。
- `native/vulkan_front.c`：真实 Vulkan 交换链与诊断绘制；尚非游戏 NVN 回放。
- `native/game_modules.c`：加载真实游戏模块，解析客体动态符号/重定位表，统计尚需接入的导入；不调用游戏入口。
- `tools/build.py`：无需 Gradle 的 APK 构建、对齐、签名、安装流程。
- `tests/test_rewriter.py`：Unicorn 对拍算术、条件分支、位测试、标志、PC 相对寻址、栈、原子窗口、TLS 八类程序，每类四组输入，全部通过。
- 真机：四线程各 10000 次线程上下文隔离测试通过。
- 真机和 APK 沙箱：四线程各 1000 次实际改写指令执行通过，覆盖 x18、TLS/TLR、svc、计时器、宿主线程指针、浮点寄存器、NZCV 标志。
- 真机：Adreno 830，Vulkan 1.3，3392×2400 交换链，已呈现超过 120 帧。
- 真机：触屏摇杆输出已观察到非零轴值，抬手恢复零；日志保存在设备 logcat。
- 真实模块已在 APK 中装载：main 55,170,816 字节、sdk 15,578,476 字节、subsdk0 8,563,932 字节；共 38,837 个符号、116,034 项重定位需求。
- APK 的检查器统计 1326 个导入中 14 个没有客体定义。**这不是“只剩 14 个接口”**：SDK 内已有定义的平台服务仍需要宿主强制接管。
- 上游宿主 C 文件 **71/71** 均在 Android 适配环境下编译成功。`host_link.py` 已将原宿主、平台适配、显示/驱动胶水层和第三方解压库链接成真实 Android 可执行程序。
- 真实宿主在设备 shell 下已实际完成三个模块的 **116,034 项重定位**，**264 条强制接管全部命中**。
- 真实 SDK：`nnosInitialize → nninitInitializeSdkModule → nninitStartup` 完成；约 3720 MB **虚拟堆**配置成功（不等于实测物理 RAM 占用）。
- 真实全局构造：main 852 项、sdk 115 项、subsdk0 44 项，之后执行到游戏 `nnMain`。
- 当前真实启动边界：程序请求 `nvnDeviceGetProcAddress`，无窗口探针明确报 `Android game Vulkan backend is pending` 并中止（退出码 134）。这是预期的未实现图形入口，**没有游戏画面**。
- 原 HLE 音频队列接 AAudio 的真机测试通过：48 kHz PCM16、六声道转立体声矩阵、实际静音回调、缓冲释放事件、暂停恢复及关闭。
- 原 HLE 生命周期的真机测试通过：SIGTERM 交给普通线程有序清理，模拟卡住时 1.5 秒兜底退出。
- 真机真实写保护缺页测试通过：Linux ucontext PC/SP、ESR WnR 写入位、恢复执行。
- 资源 MD5/SHA1/SHA256 适配对照 Python 标准库 36 个边界/多块用例通过。
- `tools/test.py` 是后续复跑这些关键测试的统一入口。

首轮 APK 已成功安装并启动；**游戏尚未运行**。界面明确显示 `GAME BACKEND PENDING`。

## 设计决策

- 保留上游的游戏逻辑、资源工具、触屏纯 C 状态机和 NVN 命令记录，新增 Android 平台与 Vulkan 后端。
- 不依赖 Bionic 私有 pthread TLS 数组布局，使用动态 ELF TLS。最初的 `initial-exec` 方案已放弃：Android 的 dlopen/JNI 库没有足够静态 TLS 余量。当前键页保存上下文内部槽偏移，以及全寄存器保留的上下文入口地址。
- 游戏程序在电脑上改写，打成 ELF 库后嵌入 APK；运行时不修改客体指令。
- Vulkan 从已有翻译链的 `.spv` 和资源反射接入；Metal 后端不能直接使用。
- 构建、游戏生成物、密钥全部放仓库外或已忽略目录。
- 未完成的图形/系统服务必须明确报告，不用假实现宣称已进入游戏。

## 正在进行与下一步

1. 工具链和游戏解包均已完成。游戏文件位于 `~/dm-work/data/switch/3.3.94012/Update/{ExeFS,RomFS}`。
2. 三个模块已用 Android 规则重写并包装为 ELF，位于 `~/dm-work/build/android/game/`。
3. 当前 APK：`dm/android/build/D2R-Android-port.apk`；包名 `org.d2r.android.port`，仅安装了我们自己的调试包。
4. **当前优先任务：完整 NVN→Vulkan 后端**。复用 `dm/gfx/nvn/hle_nvn.c` 与 `nvn_obj.h`，替换约 5900 行 Metal 执行层。现在 `frontend_headless.c` 在 `nvnBootstrapLoader` 明确停止，不能作为游戏后端使用。
5. 准备 `.NET 10` 着色器工具和 SPIR-V 缓存；当前仅准备了 CPU 运行时工具，尚未翻译游戏着色器，也未做 Android 贴图格式适配。
6. 将真实宿主接进 Activity，替换诊断呈现循环，接资源、触屏游戏输入与原版 UI/覆盖层；当前 APK 只做装载检查和子系统测试，真实 SDK 启动仍是独立 shell 探针。
7. 随后验证真实游戏画面、进菜单/进局、真实存读档、后台恢复和持续性能。

## 已排除的问题

- ZIP 提取需恢复 NDK 符号链接；`bootstrap.py` 已处理，否则 `clang` 会成为只有链接目标文字的普通文件。
- Android 的 ELF 链接器要求合法节表；`guest_elf.py` 已补齐 `.dynsym/.dynstr/.dynamic/.hash` 等节。
- 原改写器在 `msr tpidr_el0,x18` 时会取宿主 x18；已修正为取虚拟 x18，补充了 `mrs x18,...` 的写槽处理。
- Android 上下文入口增加代码长度，原有 br x18 跳转表的固定落地垫空间需扩大；已调整。
- hactool 在解密失败时仍可能返回 0；提取脚本已识别工具自身的错误输出。初次成功退出并不代表文件真的提取成功。
- 新版 prod.keys 含旧 hactool 不接受的额外字段；本机已生成最小 `~/dm-work/data/private/hactool.keys`，不输出密钥。
- hactool 的 `--titlekey` 要加密票据密钥；已改为直接读取本机票据中的加密字段。
- `extract_packages.py` 已自动生成私有最小密钥集，并检查解包是否实际产生 ExeFS/RomFS；这不是游戏版本或摘要校验。
- HLE 的 Mach-O 节登记改为 Android ELF 登记，并加 `retain` 防止链接器删除；`--gc-sections` 下的真实登记表测试通过。
- Linux 信号现场不是 Darwin 的指针结构，`dm/host/dm_ucontext.h` 统一访问；已用真实缺页验证。
- 首次不带显示/驱动胶水层且资源传送未完成的探针出现 `nnMain returned`；加入 `gfx/hle_nv.c`、`gfx/hle_vi.c` 后，完整资源下明确到达 NVN 图形入口。不要把首轮正常返回当作游戏运行成功。

## 续跑命令

```bash
cd <LOCAL_WORKTREE>
git status --short
tail -30 ~/dm-work/tools/android-bootstrap.log
python3 dm/android/tools/bootstrap.py
~/dm-work/tools/android/platform-tools/adb devices -l
~/dm-work/tools/android/platform-tools/adb mdns services
source ~/dm-work/tools/android-venv/bin/activate
python dm/android/tests/test_rewriter.py
python dm/android/tools/test.py
python dm/android/tools/host_compile.py --compat
python dm/android/tools/host_link.py
python dm/android/tools/build.py --game-source ~/dm-work/build/android/game --install
~/dm-work/tools/android/platform-tools/adb logcat -d -s D2RAndroid:I AndroidRuntime:E
```

真实启动探针（已部署可执行程序、游戏库与 TSV 后；失败于图形入口是当前预期结果）：

```bash
~/dm-work/tools/android/platform-tools/adb shell \
  'DM_ROMFS=/sdcard/Android/data/org.d2r.android.port/files/romfs DM_SAVE=/data/local/tmp/d2r-port/probe-save DM_TEMP=/data/local/tmp/d2r-port/temp DM_BOOTPROF=1 DM_TACT_PREFETCH=0 DM_DBG=1 timeout 30 /data/local/tmp/d2r-port/dmhost-android /data/local/tmp/d2r-port/game main sdk subsdk0'
```

## 风险和验证边界

- Metal 执行层约 5900 行，Vulkan 内存、屏障、同步和贴图适配是最大工作量。
- 部分 NSO 原指令用栈下临时槽，Android 信号/宿主调用边界需要实际验证。
- 上下文入口每次保存全部通用和 SIMD 寄存器，正确性已验证，开销较高；游戏性能尚未知。
- 4 KB 真机通过，16 KB ELF 对齐已配置，但没有 16 KB 真机运行结果。
- APK 沙箱已通过运行时、真实代码测试、存档事务与安全内存读取；实际游戏 SDK 启动尚未在 APK 中验证。
- 其他线程暂停/寄存器读取的 Mach 诊断功能返回明确“不支持”。Android 音频输出的热插拔/断线重建尚未验证。
- 当前全部修改在工作区，尚未提交；继续开发前先 `git status`，不要覆盖已有修改。
- 没有完整游戏运行、帧率或稳定性结果。不要把成功配对、编译或清屏当作游戏已运行。

## 第二阶段：NVN → Vulkan（进行中）

- 用户要求继续移植，已移除无窗口宿主在 `nvnBootstrapLoader` 的人为中止，接入原有 523 个 NVN 函数跳板、ELF 语义注册表和原命令记录层。
- 新增 `dm/android/native/nvn_vulkan.c`：真实 Vulkan 设备/队列、内存池范围管理、GPU 缓冲复制、完成后事件/时间戳发布、栅栏等待、命令 CALL/JUMP/更新/屏障回放。首版同步提交优先保证正确性。未实现的绘制命令仍明确报错。
- 原宿主加 NVN 图形接口和 Vulkan 后端已在 NDK 下完整链接成功。
- `dm/android/tests/vulkan_test.c` 已在电脑 Mesa llvmpipe Vulkan 软件实现上通过：复制前结果不可见、提交完成后事件和数据可见、先写后读重叠依赖、嵌套命令、栅栏等待及池地址边界。此结果不等于 Adreno 真机验证。
- 设备原无线调试端口拒绝连接，ADB 无已连接设备；已请求用户提供当前无线调试连接端口，电脑侧实现继续。
- 正在安装 .NET 10 编译原有 Maxwell → SPIR-V 翻译器，随后处理贴图、清除和图形/计算管线。

### 首画面优先（用户新要求）

- 设备重新在线，mDNS 连接端口变为 `<设备地址>`；无需重新配对。
- Vulkan NVN 复制/事件/重叠依赖/栅栏测试已在 Adreno 830 真机通过；GPU 实测 BC=1、ASTC=1。
- `tools/build_shaders.py` 从 162 个原版 CASC 着色器包及 main 内嵌图形着色器提取 6927 个唯一作业，全部成功转为 SPIR-V，生成 JSON 和 C 可读二进制 `.meta` 反射。223 个优化器失败作业使用 DebugMode 翻译路径重试后成功。缓存约 480 MB，已部署至设备 `/data/local/tmp/d2r-port/shaders/`。
- 已接入贴图创建/解排布/多 mip 多层上传、CPU 读回、缓冲到贴图复制、完整贴图/渲染目标清除、计算管线、图形管线、描述符绑定、绘制和索引绘制、深度预绘制与遮挡计数器。模板状态正在接入。首版每条绘制同步回收临时资源，性能未优化。
- 游戏真实执行皮肤预积分计算着色器 `a1217a07d94c1184`，随后加载角色资源并执行多条真实图形绘制。尚未成功 present 或捕获完整游戏画面。
- 启动崩溃的精确回溯是 `clone_node -> mount_ex -> MountSave`：递归存档复制每层栈帧携带 64 KB 文件缓冲，游戏工作线程的小栈溢出。已把缓冲与子路径移到堆上；后续真机启动通过此位置。前面关于 Vulkan 初次加载的猜测并非这次崩溃的真实根因。
- 后端头文件拆为 `nvn_vulkan_tex.inc` 和 `nvn_vulkan_shader.inc`；`host_link.py` 添加编译依赖缓存。宿主游戏入口使用独立 64 MiB pthread 栈，驱动提前初始化。
- 当前测试命令在上一条启动命令基础上增加 `DM_VK_SHADERS=/data/local/tmp/d2r-port/shaders DM_VK_CAPTURE=/data/local/tmp/d2r-port/captures`，日志 `~/dm-work/tools/android-first-draw.log`；捕获目标 `frame-00001.ppm` 等。
- 尚缺 Android NativeWindow 游戏呈现；现有已安装 APK 仍为上一阶段诊断版，不能宣称能玩。新增 GPU 图形功能还需像素读回验证及回归检查。

### 首画面已在安卓应用中实际显示

- 新增 `frontend_activity.c`，实际运行原宿主与 nnMain；应用包已从诊断前端改为真实游戏运行模式，已有 APK 安装更新并在设备启动。
- 新增 Vulkan Android surface/swapchain、获取/呈现信号量、交换链布局转换、实际游戏贴图缩放出图及保持宽高比的黑边。
- 修正 Vulkan 正高度视口与 Metal 裁剪坐标 Y 方向差异，改用负高度视口。修改前 60 帧全黑；修改后游戏原版火焰 Logo 已正确绘制。
- 真实屏幕截图：`dm/android/build/device-game-live.png`；GPU 读回：`dm/android/build/game-first-frame.png`。均为真机游戏自己绘制的加载画面，未使用截图贴图替代。
- 目前尚未进入主菜单；不能宣称已可玩。正在检查加载等待、缓冲更新可见性及输入。
- 已补上 main 内嵌的四个计数器计算核，着色器总数为 6931，均译成 SPIR-V。
- GPU 像素真值测试已在 Adreno 830 通过：解排布贴图采样、全屏三角形每像素 RGBA、一帧精确 256 个通过样本、计算写图像与读回。全套阶段二回归（含 64 KB 小栈存档复制）通过，日志 `~/dm-work/tools/android-tests-phase2.log`；后续同步修正需再跑 GPU 检查。
- 首版资源绑定采用 CPU 快照，已在绘制与计算前后同步提交，保证此前的 GPU uniform 更新/SSBO 写回已发布。尚未做持久 Vulkan 缓冲与异步队列优化。
- APK 路径：`dm/android/build/D2R-Android-port.apk`，约 81 MiB；包含游戏代码，仅本机使用，保持 git 忽略。安装方式：`android-venv/bin/python dm/android/tools/build.py --game-run --game-source ~/dm-work/build/android/game --install`。
- 应用日志改为私有 `files/game.log`，可用 `adb shell run-as org.d2r.android.port tail -100 files/game.log` 读。外部 `game.log` 在该设备的 adb/run-as 命名空间无法读取，不再用作主日志。
- 当前控制只支持画面点击确认、原触摸输入及键盘/手柄方向/确认，未接完整虚拟摇杆/技能覆盖层和软键盘。窗口销毁会优雅退出，尚未实现窗口重建保留进程。

### 已进入英雄职业选择界面（最新验收点）

- 设备中真实游戏自然撤下 `MainMenuLoadingPanel`，创建 `MainMenuButtonRibbon`，输入 A 后进入 `CharacterCreatePanel`，收到 `CharacterCreate Hover Barbarian`。未用隐藏遮罩/预制截图代替执行。
- 实际画面 `dm/android/build/device-hero-select.png`：中文“选择英雄职业 / 野蛮人”、职业说明、A/B 提示和 3D 角色轮廓。用户的“最快看到游戏画面”要求已达成。
- 标准启动等待较长，界面完成加载实测约 165–400 秒，原因仍需缩小；不能把早期只见 Logo 判为停止运行。增加 INTERNET 权限以支持游戏现有网络调用；未手动提交账号信息。
- 职业场景最初遇到 `Vulkan missing vertex buffer 4`：旧管线留下的顶点格式被误当成新着色器输入。离线读取 SPIR-V 的 Input 变量与 Location，仅绑定本次实际用到的属性，已越过此处并显示职业选择。
- 加入 Vulkan 驱动管线缓存和按固定状态复用的图形管线缓存；视口/剪裁/颜色常量/模板掩码及参考值动态设置。最新代码已构建、安装，全部阶段二回归通过：`~/dm-work/tools/android-tests-phase2-final.log`。
- 旧版在加载界面约 5.6 FPS（管线驱动缓存后），职业场景约 4.3 秒/帧。新固定管线复用版本的完整职业场景性能尚未测量。光照偏暗，完整游玩、命名软键盘、完整触控、窗口重建和剩余 GPU 操作仍未完成。
- 没有创建/删除用户角色，没有修改原游戏包、泄露或打包密钥。全部源代码修改尚未提交；产物与游戏缓存均保持 git 忽略。

下次从 `git status`、此段和 `adb shell run-as org.d2r.android.port tail -100 files/game.log` 开始；先确认当前 APK 可继续渲染，再检查职业场景的阴影/贴图绑定与性能。构建必须使用 `~/dm-work/tools/android-venv/bin/python`，系统 Python 未安装 lz4，不能运行 probe/改写器。

### 第三阶段：真机性能优化（继续开发中）

- 用户要求继续。ADB 再次实测在线，设备 OPD2409 / Android 16，当前 mDNS 连接端口 `<设备地址>`。
- 优化前真机职业场景连续 300 帧 p50 4339 ms，约 0.23 FPS；原图保存在 `dm/android/build/device-before-opt.png`。
- 增加完成后复用的 Vulkan 缓冲缓存（64 MiB 上限），最多 32 次绘制或 32 MiB 临时缓冲后提交。普通 GPU 复制的已知输入转发给后续快照，客体内存仍在 GPU 完成后才发布；未知 SSBO 写回、事件/计数器保持真实完成边界。
- 遮挡计数器重置也进入完成队列，避免批处理改变 reset/report 顺序。零顶点/零实例绘制不创建零长度索引缓冲。
- 全套真机回归通过：`~/dm-work/tools/android-tests-batching-final.log`。新增像素真值检查左红右绿的两次 uniform 更新、复制结果延迟发布、复制后贴图采样、连续计算 SSBO 写回，以及 reset/report 精确 128 样本。96 次绘制只提交 3 次，实测约 28 ms。
- 新 APK 已安装。加载画面首 300 帧 p50 32.68 ms，约 30.6 FPS；复杂职业场景的稳定帧率尚待测量。正在继续检查顶点快照重复复制与光照。
- 首画面版本备份：`~/dm-work/build/android/checkpoints/first-picture.apk` 与该目录的后端源码；不进 git。
- 修正重复测试时 `adb push` 将着色器目录嵌套的问题。后续缓存推送始终用 `源目录/.` 到 `目标目录/`。

- 批量提交版本完整职业场景 300 帧 p50 1005.42 ms（约 0.99 FPS），较最初 4339 ms 提升约 4.3 倍，但仍不能流畅游玩。统计显示该段 300 帧累计读取约 192 GB 快照数据，重复顶点读取是下一处明确瓶颈。
- 新增只读顶点快照缓存（128 MiB 上限），按真实脏页变化与待发布复制失效，销池清理缓存；退役缓冲在 GPU 完成后才回收。独立真机顶点像素、脏写失效、销池测试均通过：`~/dm-work/tools/android-tests-vertex-cache.log`。正在安装后测稳定帧率。

### 原生文本输入与进一步资源复用（进行中）

- ADB 断开后已自动恢复，当前连接端口 `<设备地址>`，实测 shell 正常。
- 新增 `java/org/d2r/android/port/D2RActivity.java`，以系统输入框接原游戏 swkbd。确认/取消使用 Android 系统本地化资源，标题/规则取原游戏文本或已有 `dm_ui` 键。支持 UTF-16/UTF-8、最大字符数、角色名 15 字节规则及数量输入。Java/dex 无 Gradle 构建已通过。游戏运行组件变为 `org.d2r.android.port/.D2RActivity`，诊断模式仍用 NativeActivity。
- 真机命名请求首次触发 ART `FindStackTop > GetStackEnd` 中止：客体栈不在 pthread 登记的宿主栈范围。已改为独立标准 pthread 执行所有 JNI，游戏线程只等待文本结果；正在安装验证。不能把这一崩溃当成用户切后台造成的正常退出。
- 只读 SSBO/类型化缓冲也接入脏页缓存，读缓存上限改为 256 MiB；GPU 写入的类型化缓冲仍真实读回再发布。真机图形新增检查全部通过：`~/dm-work/tools/android-tests-read-cache-final.log`（含只读 SSBO/texel 像素、计算 texel→uniform 可见性）。
- 缓冲读取量已从复杂场景约 900 MiB/帧降至约 32 MiB/帧，但上一版复杂场景仍约 1 秒/帧，瓶颈还包括 GPU 通道及对象开销。新增持久 image view 与按批整体重用的 descriptor pool，已有真机像素回归通过，正在部署评估。

- 真机已实际弹出中文角色名输入框与系统 IME，截图 `dm/android/build/device-native-keyboard.png`。输入单个 `A` 时确认按钮禁用（UI XML 已验证）。确认回调首次暴露 `UnsatisfiedLinkError`：NativeActivity 的 dlopen 不给 Java 类加载器登记 native 库。已在 ANativeActivity_onCreate 显式 RegisterNatives；重新安装后继续确认/取消验收，尚未成功创建角色。
- 新增批量遮挡查询池与一次性结果读取，保持 reset/report 顺序；NDK 构建及真机回归正在运行。CPU 可见缓冲优先选同时 HOST_COHERENT 与 HOST_CACHED 的内存类型（该 Adreno 提供 flags=0xf 的类型 6），全部 GPU 像素/可见性检查已通过。

### 已实际进入第一幕营地（最新验收点）

- 显式 JNI 注册后确认回传已通过：创建独立角色 `AndroidProbe`，原游戏写入 .key/.map/.ma4 等文件、建立共享箱容器，并在原版 LoadScreenPanel 撤下后进入区域 1。未改游戏逻辑来伪造进局。
- 真机实际画面 `dm/android/build/device-first-town.png`：第一幕营地场景和角色、生命 55/55、法力 10/10、技能/药剂 UI 已绘制。画面明显偏暗，场景仍约 0.5 秒/帧（约 2 FPS）；不能宣称流畅或完整可玩。
- 设备私有存档 `files/save/AndroidProbe/`；没有覆盖原有用户角色。最新日志本机留存 `~/dm-work/tools/android-game-first-town.log`。
- 已显示并验证软键盘、ASCII 名称输入/返回，以及单字母输入的禁用确认。取消、汉字、数量输入及退出后读档尚待实际验收。
- 第一幕版本 APK 备份 `~/dm-work/build/android/checkpoints/first-town.apk`。源码已进一步增加批量遮挡查询和共享不可变 support/c1 缓冲，当前独立真机测试正在跑；这部分还需构建安装。

### 用户要求的屏上调试信息（正在验收）

- 用户要求 FPS 等调试信息。新增中英表键并生成表头，浮层显示平滑 FPS/帧时间、真实 NVN 提交线程 CPU 时间、每帧绘制/提交/快照数据量、缓冲容量、进程 RSS，以及原游戏发布的阶段/区域/格坐标/输入采样数。线程间使用互斥快照，不把估算当 GPU 时间。
- JNI 更新仍在标准宿主 pthread；每 0.5 秒调度 Java UI。原生 Vulkan 拥有主窗口缓冲，普通 addContentView 文字被覆盖，改成不接触摸、不抢焦点的应用内 PopupWindow 叠加；正在真机确认可见。
- 增加 WASD 左摇杆、完整手柄按钮与四轴输入，保留多键按住状态，切后台清零。完整虚拟触屏覆盖层仍未实现。
- 遮挡查询池批次复用、静态 support/c1 常量缓冲跨绘制共享已经全部真机回归通过。最新日志 `~/dm-work/tools/android-tests-debug-hud.log`。

### CPU 采样定位缓存查找瓶颈

- 真机 simpleperf（12 秒、11030 样本，无丢失）完成。报告 `~/dm-work/tools/android-cpu-profile.txt`，数据设备 `/data/local/tmp/d2r-port/android-cpu-profile.data`。使用 shell `simpleperf record --app org.d2r.android.port ... -o /data/local/tmp/...`；直接 run-as 无权调整 perf 属性，输出到应用私有目录也不适合 shell 包装器。
- user-space CPU cycles：snapshot_buffer 39.89%，image_get 18.03%，动态上下文门 14.31%，sha256_block 6.58%。因此下一步优先解决线性列表扫描，而非先推断 GPU 算力不足。
- 已改只读快照为 8192 桶哈希 + 双向 LRU（命中与逐项退役不再全表扫描），贴图为 4096 桶索引，按程序地址及 ProgramSetShaders 的真实新序号缓存 shader 解析（避免 control 地址复用时取旧 shader）。真实脏页、复制覆盖、销池和 shader 修改失效规则保留。
- 新增真机图形检查哈希碰撞、脏写、销池与程序序号/控制段地址重用。正在跑回归，之后安装比较 CPU/FPS。
- 调试 PopupWindow 已实际可见，截图 `dm/android/build/device-debug-popup.png` / `device-debug-current.png`，其文字不接触摸。原生角色 `AndroidProbe` 已跨 APK 更新重新在列表出现，后续继续验证读档、移动及光照。

### 缓存索引优化已安装并测量

- 全套回归通过，含真实 GPU 像素、可见性、哈希碰撞、脏写/销池、程序序号重用：`~/dm-work/tools/android-tests-cache-index.log`。构建安装成功：`~/dm-work/tools/android-install-cache-index.log`；`git diff --check` 通过。
- 在现有 AndroidProbe 角色选择场景中，优化前 HUD 约 1.61 FPS / 631 ms / 提交线程 CPU 472 ms；优化后连续多个 60 帧统计稳定约 2.50–2.64 FPS，300 帧 p50 396.76 ms，CPU 平均 235.899 ms。新截图 `dm/android/build/device-debug-cache-index.png` 瞬时平滑值 2.71 FPS / 376.7 ms / CPU 216.4 ms。不是营地同场景对比，也不是 30 FPS 可玩结果。
- 新版每帧约 1021 次绘制、37 次提交、24.3 MiB 快照复制；真实 GPU 时间仍未测得。CPU 时间与墙钟帧时间之差包含等待/调度，不可全部算作 GPU 时间。
- 第二次 simpleperf 11.965 秒、11189 样本、无丢失，报告 `~/dm-work/tools/android-cpu-profile-index.txt`：snapshot_buffer 从 39.89% 降至 3.27%，image_get/sha256 已低于报告 1% 门槛；上下文入口 dm_android_context_entry 36.76%，memset 7.13%，bind_shader 5.14%，image_upload 4.74%。百分比是该次所有用户态 CPU 周期占比，不是整帧墙钟占比。
- 新版游戏日志 `~/dm-work/tools/android-game-cache-index.log`。下一性能方向是上下文门/TLS 调用频率及剩余绘制提交开销；寄存器保护、Android x18 和线程上下文正确性不可为了速度跳过。光照异常、完整触控和实际读档后移动仍待处理。

### 原项目与 Eden 并行源码研究及首批落地

- 用户要求从原项目和 Eden 学习，并 clone 最新源码、多智能体并行。三个智能体分别完成原 Metal/NVN、Eden Android/Vulkan、上下文入口研究；整合报告 [Android GPU 实现对照](ANDROID_GPU_REVIEW.zh-CN.md)。
- Eden clone 到 `~/dm-work/research/eden`，master `a594c8d60ef79280adc046760561912207e16e17` 与远端核对一致。原项目已 fetch 最新 `origin/main=685d3ddef8bf75768fd4c51228566f2e8d2a5182` 供只读比较；当前工作区未 merge/reset，原有移植修改保留。
- 当前已经 Vulkan GPU 渲染/计算/复制；主要差距是同步等待、对象/参数重复处理和 CPU/GPU 数据可见性。Eden 的完成 tick、stream buffers 和 pass/object 复用值得借鉴；两者 CPU/GPU 接口架构不同，不能直接替换整个后端。
- 上下文入口改为 hidden ELF TLSDESC 直接解析：自身 25 条指令/64 字节栈，保留除返回 x16 外的全部寄存器/标志/浮点状态、Android x18/TP。原处理系统调用/观测的完整 dm_gate 保留。
- 计算 pipeline/layout 按 shader 生命周期缓存。修正 SSBO 的 Write 位（1，不是2）；fixture 改为真实flags=1，连续计算写回/读取验证通过。未绑定可写 image 和采样 texture 的 dummy 分离；新像素测试修复前实际失败、修复后主机和 Adreno 通过。脏页快照捕获按原项目先 protect→generation→read，减少漏记窗口。
- 新增实际 GPU timestamp 和 fence 等待指标；设备支持48位/52.083332ns每tick。浮层/日志显示每帧各批 GPU 区间累计，不含显示/vsync，也不是GPU利用率；CPU/GPU/等待不可相加。
- 全套真机回归 `~/dm-work/tools/android-tests-eden-final.log` 通过，含新入口 dlopen 动态 TLS 冷热4线程×1001次全寄存器/线程隔离，以及真实GPU可见性/dummy隔离/timestamp；`git diff --check` 通过。构建安装日志 `~/dm-work/tools/android-install-eden-review.log` 成功。
- 安装后第一次启动遇到设备 Asleep/keyguard，窗口销毁导致正常退出。已发 Wakeup + 系统 dismiss-keyguard、重新启动；没有把这一退出当作新入口崩溃。最新复杂场景 FPS 和光照尚待新版本实际出图测量，不能从入口指令数推算收益。
- 研究原文外部文件：`~/dm-work/research/{D2R_UPSTREAM_GRAPHICS_REVIEW.md,EDEN_ANDROID_GPU_RESEARCH.zh-CN.md,ANDROID_CONTEXT_GATE_REVIEW.md}`。未运行 Eden APK，未安装 Turnip，也未发布任何产物。

### 本轮最终真机测量与条件恢复优化

- 新版已正常启动并实际显示既有 AndroidProbe 角色选择，截图 `dm/android/build/device-debug-eden-startup.png` 约2.3FPS、CPU259.8ms、GPU121.5ms、fence等待151.1ms。连续60帧报告约2.38–2.44FPS/39–40次提交/27MiB快照。GPU指标已实际可见，光照仍偏暗。
- 专用入口首版仍占用户态周期30.93%；按sample PC核对本机ELF的真实100字节边界，53.29%的入口内周期在 `msr FPCR`，另有17.30%/17.20%在恢复序列后的指令。不是符号把后续NVN代码误计入。
- 按此证据进一步改成比较后仅在实际变化时写FPCR/FPSR；NZCV总恢复，完整寄存器语义保留。最终入口31条静态指令/64字节栈，常见路径29条；20个模拟解析器/线程状态场景验证按需写入。
- 新增确定性“read刚结束发生guest写”的脏页窗口检查；临时旧顺序版本重现失败，新版本主机/Adreno通过。最终全套 `~/dm-work/tools/android-tests-context-conditional.log` 通过，安装日志 `~/dm-work/tools/android-install-context-conditional.log` 成功，diff检查通过。
- 最终截图 `dm/android/build/device-debug-context-conditional.png`：2.51FPS、帧403.7ms、CPU238.3ms、GPU122.8ms、等待151.1ms。60帧均值2.43FPS，GPU122.84ms/等待151.70ms。不能宣称本轮有明显整体FPS提升；提交和通道开销仍占关键路径。
- 最终CPU采样11.971秒、8915样本无丢失，入口占比降到11.79%，报告 `~/dm-work/tools/android-cpu-profile-conditional.txt`。日志保存 `~/dm-work/tools/android-game-context-conditional.log`；首版保留 `android-game-eden-review.log` 与 `android-cpu-profile-eden.txt`。主要结论：GPU已在真实执行，继续做图形对象/参数复用、兼容通道合并和真实完成后的安全异步，而非寻找“开启GPU”的单个开关。

### 用户明确要求优化至真机 60 FPS：通道与参数结构改造

- 目标是实际游戏60FPS（16.7ms/帧），不是调试fixture帧率。持续优化，尚未达成；3D光照仍待修复。
- 三智能体并行实现通道/对象复用、uniform arena/descriptor合并、可见性改造设计；新增 `nvn_vulkan_draw.inc` / `nvn_vulkan_pass.inc` / `nvn_vulkan_uniform.inc`。
- 连续同附件draw沿用renderpass，graphics layout/renderpass/framebuffer持久缓存，pipeline用哈希且key加入附件槽映射。遮挡查询按真实pass累计，reset/report先结束pass，像素/样本次序保留。
- 动态UBO在持久映射arena子分配，保留完整64KiB零padding，仅清实际前缀；同批c0/c2复用，每shader统一更新descriptor。补齐compute c0 UBO地址表，真实GPU读取表字段fixture通过。
- 图像按实际stage/access跟踪风险，连续只读不重复全局屏障，真实写/读转换在pass结束后同步。批上限32→256，仍保留32MiB临时容量与unknown shader写/event/counter边界。普通复制单独链表；已知CPU输入只保留快照到真实GPU完成后发布，取消冗余临时GPUbuffer之间的复制，不提前改客体内存。
- 完整真机回归 `~/dm-work/tools/android-tests-passes-arena.log` 通过：96draw实际1submit/pass/query；257draw2批且复用对象；前后GPU像素、query、dirty窗口、SSBO写回、UBO独立版本/零padding等均通过。安装 `android-install-passes-arena.log` 成功；此前2.4FPS APK备份 `~/dm-work/build/android/checkpoints/2.4fps-before-passes.apk`。
- 同一角色选择实测约6.97–7.18FPS，300帧p50 141.81ms，CPU平均82.537ms；GPU32.1–32.8ms/帧，fencewait39.8–40.6ms。每帧仍1021draw，但约51pass、154图像barrier、12submit；uniform arena峰值149MiB。较2.4FPS约3倍提升，仍远未达到60FPS，不能停止在此或称流畅。
- 日志 `~/dm-work/tools/android-game-passes-arena.log`，截图 `dm/android/build/device-debug-passes-arena.png`。下一步基于新CPU采样继续descriptor/UBO绑定复用、真实依赖范围等待、异步context与GPU通道优化。

### 第二轮 60 FPS 优化：绑定缓存、真实依赖与深度语义

- 仍在持续处理用户的真机60FPS目标，并同步修复黑色角色/场景。当前尚未达成60FPS，不能以调试fixture或降低绘制数量冒充游戏性能。
- 编译优化改为O3，保留严格可见性/浮点语义；UBO按真实地址、范围、脏代数、待发布复制版本复用，descriptor按完整payload和layout复用。图像访问风险仍每次执行，不能因descriptor命中跳过同步。
- 依赖预检只在实际CPU读取与未发布GPU写/event/counter重叠时等待；顶点按索引最大值实际前缀检查，分行上传不把源行padding当作读取。真实GPU像素回归已覆盖相关写等待与无关范围不等待。
- 修复错误的depthRange防护：反向深度及常值深度是合法NVN状态，不能强制改成0..1。独立D32像素/遮挡回归验证旧代码会失败，新代码通过。画面发黑的原因仍需真机后续定位，尚未宣称修复。
- 全套回归日志 `~/dm-work/tools/android-tests-binding-final.log` 通过，安装 `android-install-binding-dependencies.log` 成功。同一AndroidProbe选择场景持续8.07–8.63FPS；CPU约70.8ms，GPU26.2–28.9ms，真实fence等待31.9–35.1ms；每帧1021draw、约50pass、10submit。
- 每帧UBO hits/miss=4061/1199，descriptor=847/1203，UBO读取1.27MiB，总快照24.8MiB，依赖等待5次；arena34MiB。已备份 `~/dm-work/build/android/checkpoints/8fps-binding-dependencies.apk`。
- 接下来并行集成三批次异步真实完成队列、每批独立arena/descriptor池、Android ADPF渲染线程提示；同时验证sampler border/compare语义及黑画面。异步必须由后台完成线程真实等待fence并按顺序发布客体内存/事件，不能提前宣布完成。

### 异步提交已安装：11–12 FPS，黑画面同步定位

- 三批context各自独占command/fence/query/timestamp、uniform arena和descriptor pool；后台worker实际wait fence后按FIFO发布guest buffer/event/counter并回收。环满begin等待释放queue锁，给worker公布腾slot；CPU读取未知GPU输出仍显式commit→wait→begin。读回、销池、销图像先等待真正完成。
- 真实VkEvent确定性阻塞GPU的独立回归验证：三批in-flight无提前guest/event/report发布，第4批真实阻塞，解gate后FIFO回写；跨批已知复制覆盖保持17→29次序；跨批未知GPU输出实际依赖等待；红/绿不同RT像素验证arena/descriptor不互相覆盖。全套 `~/dm-work/tools/android-tests-async-fifo.log` 通过。
- `android-install-async-sampler.log` 安装成功，ADPF logcat确认实际渲染线程session及16.667ms目标。不以session成功推断60FPS。
- 角色选择稳态约10.8–12.0FPS（启动短区间13–15FPS不当作稳定成绩）；CPU约51–56ms、GPU约28–30ms、渲染线程真实等待约21–23ms，1021draw/10submit未减少；arena三份约102MiB。备份 `checkpoints/11fps-async-sampler.apk`。
- 同frame真实截图 `dm/android/build/device-debug-async-sampler.png`：11.9FPS但3D仍黑剪影，sampler标准border和同slot重注册修复没有解决主因；UI正常。用户明确要求同步修黑，正在一次性捕获Gbuffer/HDR/LUT和draw→RT资源链，依据原Metal语义定位，不强制提亮。
- 12秒CPU采样30908样本，报告 `~/dm-work/tools/android-cpu-profile-async.txt`：context_entry10.75%、dynamicTLSDESC7.15%、draw_graphics6.96%、bind_shader4.20%、memcpy3.23%、dirty_since1.83%、async_unknown_chain1.64%。日志 `android-game-async-sampler.log`；百分比仍是所有线程用户态周期，非墙钟。
- 下一方向：参考原Metal shared guest-pool GPUbuffer，减少compute→CPU→GPU往返（当前普通快照仍24.8MiB/frame，依赖等待5/frame），保持输出版本与真实完成发布；另研究不依赖Bionic私有偏移的线程context缓存。60FPS目标仍未达成。

### 黑画面根因定位：纹理/采样器句柄反序

- 真机一次性frame180 probe已取回 `~/dm-work/research/android-gpu-probe-180/gpu-probe`：pipeline.tsv、images.tsv、typed rawbins、LUT ubo10。capture真实GPU完成后读回；诊断帧不计性能结论。
- 当前实际游戏3D/最终输出是1600×900。HDR中心接近0.0003–0.00056，最终RGB0；32³LUT15/17近identity、16含正常颜色变化。27/28是transparent targets，不可误当Gbuffer空写；volume54–57的全零也暂未定位语义。
- 对照原Metal `dm/gfx/tools/dmsh_build.py` 的 decode_tex 找到确定差异：Maxwell OR-packed texture/sampler句柄可交换两operand顺序。例如models FS5c1ca4e7c3e4657c handle0x101c015a的operand0=0x568是sampler0，operand1=0x1c0是texture11。当前Android假设operand0永远texture，误解析slot128越界并用零dummy，导致大量材质丢失。
- 全库1746 shaders/4711 bindings存在反序，涵盖models/foliage/grunge；probe的实际模型draw599确实缺材质texture slots10/11/12。正在修复统一resolver、绑定/依赖预检/诊断一致解码，以及反序真实GPU材质像素回归。此时尚未安装修复，不能宣称真机已亮。
- pinrule0.19.0安装/init/doctor成功，7条默认规则与Codex/Claude/Hermes hooks齐全；用户已继续移植，当前会话已收到pinrule规则注入。没有擅自加入新的用户自定义规则。

### 材质句柄修复已安装，但光照仍未恢复；GPU 数据转发进入实测

- 统一 `nvn_vulkan_handles.inc` 读取packed两operand实际c2 words、处理反序/正序/combined/immediate SID，并同步shader绑定、依赖read-set与probe。fixture14基于真实反序0x101b815a：旧代码同shader像素RGBA全零，新代码实际blue；五种句柄路径真机像素通过。
- 安装 `~/dm-work/tools/android-install-material-handles.log` 成功，截图 `dm/android/build/device-material-handles-first.png`：火光、部分纹理/武器高光恢复，主人物和场景仍黑；没有宣称已修完。新的 `~/dm-work/research/android-gpu-probe-materials/gpu-probe` 确认models599材质slot10/11/12正确、HDR中心从~.0003提升~.0065；原静态的offline TexSlot风险在活跃shader的实际type/reflection没有复现，暂不据此重翻译或猜修。
- 专用线程context引入公开TP键缓存、序号前后检查/读屏障、public pthread TSD清理，冷/碰撞继续TLSDESC，全寄存器/Androidx18保留。真实getter-only初始化、key失败、128线程退出/TP重用127次与4×1001寄存器probe均通过；进一步补 `cache_terminating` 防其他TSD析构晚调用getter重注册。尚未单独测量该项游戏收益。
- 图形命令状态只在每command-buffer begin清空，对相同pipeline/layout/descriptors/viewport/scissor/blend/depthbias/stencil/vertex/index省去重复driver命令。所有graphics pipeline动态声明一致；compute绑定点独立，pass/clear不取消这些状态。已有完整像素回归通过。
- GPU forwarding已接实际SSBO只读、可写初值GPUseed、vertex精确offset以及NVN GPUbuffer复制。post内记录编码完成的producer/stage/access；最新partial/未编码/event overlap必须回退真等待；CPU indices最大值/UBO/解排布仍保留CPU同步。backing引用到最后consumer真正完成才回缓存。
- 第一轮 production SSBO 同/跨批 +7→+14、red128真实像素，以及非零offset顶点同/跨批blue，均证明不CPU回读/不dependency flush并真机PASS，日志 `android-tests-gpu-forward.log`。新追加 NVNcopy→FS/vertex 与partial overlay回退另跑 `android-tests-gpu-forward-final.log`，当前仍在运行，结果待更新。
- GPU门已改仅测试宏的 timeline semaphore future host signal；不再依赖post-submit host VkEvent的便携性。生产设备feature/API不变。第一轮新增forward测试曾因误用主test同步commit宏阻塞在host gate前，已改raw commit；此为测试集成错误，最终重跑通过，不隐瞒。
- Android原版手柄UI/触屏自动导航模式不一致的原因已找到：`dm_padmode_boot` 在没有overlay时早返回而g_cur=0，触屏自动X重开列表干扰原版X/A/A读档。工作区已为Android无overlay默认controller匹配，尚未安装验证营地读档。
- 下一版将抓完整UBO/vertex/shadow depth probe继续光照根因。与此同时对照原Metal `XLATE_REDUCED=1` 默认，准备独立reduced shader变体；不覆盖full，不把Mac-26%当成Android收益，也不改CPU/game状态浮点。

### GPU 转发完整回归、实际读档及 Metal→Vulkan 调研

- `~/dm-work/tools/android-tests-gpu-forward-final.log` 已全部通过，新增九个 production 用例覆盖同/跨批 SSBO、非零偏移 GPU 顶点、GPU NVN copy→fragment/vertex，以及后续局部写入的一次真实等待与合并红/绿像素；不是模拟 GPU 输出。上下文缓存晚 TSD 析构路径也已真机通过。
- 最新安装 `~/dm-work/tools/android-install-gpu-forward.log` 成功。默认手柄模式修复后已正常进入 AndroidProbe 主菜单；截图 `dm/android/build/device-gpu-forward-start.png` 约15.5FPS，后续60帧日志约15–17FPS。这里是约970draw的主菜单，之前11–12FPS是1021draw的角色列表，不能作为严格同场景性能倍数。主菜单GPU约26–30ms、真实渲染等待12–14ms、8submit/frame、1次依赖等待/frame；快照反而增加到53.6–53.7MiB/frame，正在修复重复索引扫描。
- 已备份 `~/dm-work/build/android/checkpoints/16fps-gpu-forward.apk` 及完整 `~/dm-work/tools/android-game-gpu-forward.log`。日志累计GPU读取约242MiB/frame是多个draw重复消费量，不是CPU上传量。
- 原版A按钮实际读取既有存档并进入第一幕营地：截图 `dm/android/build/device-controller-play.png`，区域1，坐标4009,4929，Life55/55、Mana10/10。地面/帐篷/火光可见，角色/NPC/部分物件仍黑剪影；营地截图约11.1FPS、934draw。尚未验证持续移动与完整触控，尚未修完黑画面，尚未达到60FPS。
- 完整UBO、顶点与阴影探针已保存 `~/dm-work/research/android-gpu-probe-enhanced/gpu-probe`。顶点/法线/模型UBO有真实非零值，当前数据不能认定为VS归零；下一次探针补光源SSBO/texel buffer及已有pass遮挡样本，区分光照输入问题与深度拒绝，不直接改深度比较或强行提亮。
- 子智能体完成官方仓库及源码调研，报告 [Metal→Vulkan 调研](ANDROID_METAL_VULKAN_RESEARCH.zh-CN.md)。实际clone/read Indium/Iridium、MoltenMTL、msl2glsl、CrossTL，但没有构建或运行候选。现有 d2r-mobile 已经Maxwell→SPIR-V→MSL，Android保留直接SPIR-V路径；借鉴 Dawn/LLGL/bgfx 的真实完成序号、资源持有和状态跟踪，不额外引入未经游戏验证的Metal翻译层。
- full/reduced各6,931个shader现已全部通过 `spirv-val --target-env vulkan1.1`。每套仅15个真实Unknown-format ImageWrite补StorageImageWriteWithoutFormat声明、各增加8字节；执行指令/反射和其余6,916个SPV保持一致，原二进制备份完备。独立reduced变体移除非强制精确运算限制，但还未安装测量，不能宣称Android性能收益；详见 `~/dm-work/research/android-reduced-shaders-design.md`。

### 索引优化真机通过，黑剪影发现真实 GPU 分组错配

- 完整 `~/dm-work/tools/android-tests-index-shadow.log` 真机通过，包括5项索引版本/脏写/局部复制/u8-u32/flush后重取像素回归，以及D16阴影数组层/LOD/gather/六cube面/视图。索引缓存按每次backing激活的唯一整数版本、范围和宽度区分，不按guest地址或会复用的VkBuffer句柄盲缓存。
- 安装 `~/dm-work/tools/android-install-index-probe.log` 成功，同970draw主菜单稳态约18.1–18.3FPS。截图 `dm/android/build/device-index-mainmenu.png` 17.7FPS、CPU31.6ms、GPU24.8ms、等待11.5ms。每帧约1650次最大值缓存命中、11次新扫描、扫描量不足0.01MiB，但总CPU快照仍52.4MiB；下一优化不能继续把大部分快照归因于索引。日志 `~/dm-work/tools/android-game-index-probe.log`。
- 手柄模式读档后通过W长按实际移动：坐标4009,4929→4009,4926，场景与角色相对位置改变，截图 `dm/android/build/device-controller-move.png`。这是实际输入/角色移动证据，完整虚拟触控与持续游戏仍待实现。
- 新探针 `~/dm-work/research/android-gpu-probe-light-inputs/gpu-probe` 导出5份光源SSBO/texel真实数据及52个已有pass查询结果，不额外创建query。主要模型HDR通道575–830样本989301、831–922样本215464，全局遮蔽547样本360000；不能认定opaque全部被depth拒绝，局部模型仍需区别。
- 子智能体在实际models片元SPIR-V找到SHFL XOR helper；离线Accessor遗漏QueryHostSubgroupSize而继承32。独立真机Vulkan属性查询确认Adreno830实际subgroup64、min=max64、FS/CS支持，requiredSize仅CS，因此不能强制32。查询源码、arm64工具、实测日志 `~/dm-work/research/android-subgroup-query.{c,log}`。
- 原Ryujinx已提供host64下32线程guest warp的lane遮罩、shuffle真实来源及vote/ballot半组适配。正补Android离线构建host参数并在独立full精度 `shaders-subgroup64` 重译，Metal默认不改。运行时已添加真实分组日志和可选cache profile匹配检查；尚未安装64着色器，不能声称角色黑剪影已解决或60FPS已达成。
- 原项目安全3D缩放研究已完成 `~/dm-work/research/android-render-scale-review.zh-CN.md`：游戏自身0.75机制可保UI1600×900并重建3D1200×675、半深度600×337，不改Vulkan贴图尺寸或support倍率。当前先处理分组正确性，缩放尚未实施/测量。

### 2026-10-15：分组真值、类型化缓冲及显示同步，材质源与音频继续处理

- host64 full完整安装成功，日志 `~/dm-work/tools/android-install-subgroup64.log`，运行时profile64与Adreno64匹配。全6,931套SPV/meta/resJSON齐全并全部val通过；相对full32仅182个FS+5个CS变更，全量meta字节和resJSON语义不变，全部2,344个VS字节相同。主菜单截图 `dm/android/build/device-subgroup64-first.png` 19.7FPS，但人物仍黑，不能把分组错配当唯一黑根因；APK备份 `checkpoints/19fps-subgroup64.apk`。
- 已在真Adreno运行52条真实Maxwell→原Ryujinx译器的线程组fixture：old32出现542项语义错误，new64全22列/64实际线程零错误，完整真实GPU dispatch/fence/readback。四helper原字节保留，仅允许的仪表指令加入rawLane；不是GLSL镜像或模拟GPU。证据 `~/dm-work/research/subgroup-probe/{device-result.log,device-output.csv,fixture-design.md,fixture-provenance.json}`。wrong profile32也已实际被GPU64运行时检查拒绝。
- 最新CPU采样11.95秒/8611样本/丢失0，报告 `~/dm-work/tools/android-cpu-profile-subgroup64.txt`：memcpy8.31%、context5.86%、async_unknown_chain5.51%、draw4.73%、bind3.73%、snapshot2.00%。这些是用户态周期比例，不是墙钟百分比。
- 只读texel buffer接GPU完整版本转发、精确offset及写→读同步；其VkBufferView按backing/range/format在实际批次生命周期复用，完成后才清slot索引。四项新真实GPU用例（同/跨批、partial真实等待、known-copy非零offset与延迟guest发布）连同既有全套Adreno通过：`~/dm-work/tools/android-tests-texel-present.log`。
- 显示rendered semaphore改按实际swapchain image管理，acquire token按消费submit ticket真完成才重用，首布局屏障匹配TRANSFER wait；普通帧去QueueWaitIdle。最新安装 `android-install-texel-present.log` 成功，连续显示已超过1,800帧，日志明确queue_idle0及仍在途的blit票号；这是实际surface运行证据，不是完整同步验证层证明。
- CPU快照约52.4→23.3MiB/主菜单帧，texel本身CPU读取1MiB/frame，视图new2/hit385左右；后来用户进入营地，draw928、快照24.6MiB、截图 `dm/android/build/device-texel-present.png` 17.6FPS。不能把不同场景18–19FPS变化归因于某一个优化，也尚未达到60FPS。
- 电脑LVP软件Vulkan完整回归在既有forward_reference_test的gated producer等待超时，**未通过**，日志 `~/dm-work/tools/android-tests-texel-host.log`；根因尚未确定。没有跳该检查，目标Adreno完整回归实际通过，LVP失败不能掩盖或与线程组fixture的真机PASS混淆。
- 新 `guest_publish_wait` 保守等待全部post0/1/2的最新实际发布票号及上次acquire consumer，保留之后纯GPU尾部在途；不因全异步假设忽略原生guest直接CPU读或whole-buffer发布风险。两项实际timeline门/保留backing/准确green-blue像素回归通过；ReadTexels补上传前池GPU写依赖，门控真实结果必须red255不能旧red248，一并通过 `~/dm-work/tools/android-tests-guest-publish-source.log`。新游戏版 `android-install-guest-publish-source.log` 构建安装中，收益待测；证明边界 `~/dm-work/research/android-present-overlap-proof.zh-CN.md`。
- 新真实GPU材质探针导出24张BC mip0，23张raw块全零、1张噪点，离线解码图 `~/dm-work/research/android-gpu-probe-bc-materials/gpu-probe/decoded-bc/contact-sheet.png`；优先查上传/源存储，不强制提亮。诊断BC读回/解码已Host真值验证，独立Adreno BC真值准备中；mip0空不能独自证明其余mip空。源G probe将额外记录layout、stamp、dirty及CPU存储前4096字节，确认真实源与GPU差异后再改材质刷新。
- 原Metal对has_layout的预排布素材持续跟踪CPU源变化；Android GPUcopy后stamp0会永久跳过CPUdirty。独立Host实际BC红→GPU蓝→CPUdirty绿fixture已复现旧路径错误地保持蓝，`sampler-regression/pkg-refresh.log`；当前游戏是否因此黑仍待G源证据，不做每draw强制reupload。
- Adreno R16_UNORM/R32F无Dref比较支持、D16/D32F有；当前models彩色cube比较是潜在不合法路径，但真实light计数对应分支未活跃，不能据此解释现有黑剪影。保存 `~/dm-work/research/android-format-subgroup-query.log`。
- host64 reduced独立变体已完成、全6,931val/ABI通过，`~/dm-work/build/android/shaders-subgroup64-reduced/spirv`；与full64同译器，不混用旧host32 reduced。尚未真机A/B，尚未实施0.75缩放，不能先报收益。
- ADB曾临时断开，用户恢复后mDNS发现新连接端口<设备地址>，配对未重做。用户新反馈游戏无声，已指派 `/root/android_audio` 独占音频链路：检查真实PCM、AAudio状态/路由/系统音量并修复，不用独立tone测试冒充游戏有声；关键记录 `~/dm-work/research/android-game-audio-fix.zh-CN.md`（进行中）。

### 2026-10-15：声音真机恢复，重点修复空材质

- 音频根因已证实：`real_audio()` 原先只对 iOS 默认开启，Android 没有设置 `DM_AUDIO` 时走静音接收端；旧测试强制 `DM_AUDIO=1` 掩盖了默认配置错误。现 Android 默认开启真实 AAudio，保留显式 `DM_AUDIO=0`，回调只统计数据，由普通线程报告。默认配置实际真机回调、6→2 声道、释放通知、暂停恢复和退出测试通过：`~/dm-work/tools/android-audio-default-test.log`。
- 游戏 APK 已构建安装成功：`~/dm-work/tools/android-install-audio-default.log`。真实游戏非零 PCM 从440,363→3,320,173个采样持续增长，AAudio STARTED、device2扬声器、starved_frames0、xruns0；AudioFlinger 的该应用轨道 active=yes、48kHz立体声、未静音，AudioService 的 USAGE_GAME 输出也活动。用户亲自确认“声音有了”，要求后续重点处理渲染；详细证据保存于 `~/dm-work/research/android-game-audio-fix.zh-CN.md`。
- 最新 G 探针目录 `~/dm-work/research/android-gpu-probe-bc-source/gpu-probe`：24/24 预排布材质 has_layout1/stamp0/populated1/tracked1/dirty1/pending0/unknown0，CPU源前4096字节均大量非零；同帧 GPU 22张全零、2张局部垃圾，角色主要材质均全零。CPU BC块解码有正常颜色/法线，独立 Adreno BC1/BC3 实际采样及原始字节读回均通过 `sampler-regression/probe-bc-adreno.log`。已排除材质源本来为空及该独立BC读取路径不正确。
- 源数据证据与独立 packaged 红→GPU蓝→CPU新绿却仍蓝的负例吻合，正在对照原 Metal 修复独立 CPU 源更新基线；GPU 写入后保留当前内容，后来的真实源变化才重传。普通 GPU 渲染目标及已知/未知池写的依赖语义必须继续正确，不使用每次绘制强制上传。
- `guest-publish-source` 版本实际安装成功，主菜单约19.7FPS、23.3MiB CPU快照/帧、GPU23.36ms；真实 `pure_GPU_tail=0`，不能声称局部发布等待已带来跨帧重叠或额外性能收益。APK 备份 `~/dm-work/build/android/checkpoints/19fps-guest-publish-source.apk`。尚未修完渲染，尚未达到60FPS；本轮先保持host64 full精度和原尺寸验证材质修复。

### 2026-10-15：黑剪影材质修复真机闭环，Eden 对照开始

- 已实施独立 packaged CPU 源基线，GPU 写入保持当前内容和有效旧源基线，后续真实源更新重新上传；保护→代数→读取次序、检查前 seen 代数、已知复制快照和未知写实际等待保留。未跟踪源上传和内容比较使用同一份不可变快照。仅 packaged storage 补真实 CPU 初值，普通 GPU 渲染目标行为保留；部分 texture→texture 复制先消费目标的 packaged 源，避免未覆盖区域变空。
- 新真实 GPU 回归覆盖 tracked/untracked BC 红→GPU蓝保留→CPU绿、有效generation0、重复GPU标记不吞更新、CPU源别名、已知未发布复制、未知GPU源依赖、部分复制剩余红像素、读取后并发改写、普通GPU目标保留和storage imageLoad/Store读改写。完整 Adreno 回归通过 `~/dm-work/tools/android-tests-packaged-refresh.log`；脏页代数在该独立测试中由替身推进，真实游戏另外验证实际写保护链。
- APK 实际构建安装成功 `~/dm-work/tools/android-install-packaged-refresh.log`；菜单截图 `dm/android/build/device-packaged-refresh-first.png` 与读档后营地 `device-packaged-refresh-camp.png`，角色皮肤、衣服、装备、帐篷和物件已恢复。新探针 `~/dm-work/research/android-gpu-probe-packaged-refresh/gpu-probe` 的24/24 BC贴图全部非零、CPU源跟踪dirty0；角色albedo298非零40,656/65,536字节、normal299为62,887/65,536，解码是可辨识正常素材，原先同角色图像全零。
- 本次确认的是材质黑剪影修复，地面/岩石仍有方块状斑块。营地截图18.5FPS、CPU24.8ms、GPU27.8ms、等待15.0ms，尚未60FPS；没有混入缩放或reduced精度。日志 `~/dm-work/tools/android-game-packaged-refresh.log`，可回退APK `~/dm-work/build/android/checkpoints/19fps-materials-audio-restored.apk`，详细调查 `~/dm-work/research/ANDROID_LIGHTING_PROBE_NOTES.md`。
- 用户新要求用 Ryujinx 或 Eden 在安卓实际运行对照。已复用设备现有 Eden `dev.eden.eden_emulator` / versionName `1f6734c`，Vulkan系统驱动、1×、掌机、NCE；没有覆盖其配置或存档。与当前移植同版的用户提供 base NSP7.718GiB和update v2228224 0.720GiB均成功推至 `/sdcard/Download/D2R-reference`，正通过SAF添加目录、安装更新并尝试实际运行。未把准备安装包当运行成功。准备记录 `~/dm-work/research/eden-android-reference/README.zh-CN.md`。
- 电脑LVP回归失败已进一步定位：其 `vkGetQueryPoolResults` 内部无条件 DeviceWaitIdle，读取已完成批次的时间/遮挡结果也会被后续未满足timeline门的批次阻塞。独立两真实门、timestamps、precise遮挡查询实验已重现失败；改为GPU尾部CopyQueryPoolResults到独立mapped缓冲、真实fence后读取的实验通过。正实施每批独立读回，不关闭查询或跳过门控检查；尚未宣称主回归已经恢复。

### 查询同步完整回归恢复，Eden 标题进入与离线设置研究

- 每slot GPU查询读回已完成，保持原GPU时间戳、遮挡结果和FIFO发布顺序；没有生产者的旧 `post_op.query` 字段及host GetQuery分支已删除。完整 Host O3 LVP `~/dm-work/tools/android-tests-query-host.log` 与 Adreno `~/dm-work/tools/android-tests-query-subresources.log` 均实际exit0，包含原先超时的旧forward门控、前批256-counter/event发布时后批128-sample仍pending的新门控，以及全部原图形回归。没有禁查询、跳检查或把不可用当PASS。
- 新跨子资源回归在两端真实通过：2层×2mip BC纹理四色，GPU只覆盖四分之一后其他区域/mip/layer保留；后来CPU修改另一mip，按原Metal契约整源刷新并核对四个view像素。新query APK已构建成功 `~/dm-work/tools/android-build-query-readback.log`，为让Eden对照继续，尚未安装或测其真实游戏FPS；当前设备运行的移植APK仍是材质/声音修复版。
- Eden成功安装v2228224更新，日志确认title0100726014352000应用ExeFS/RomFS更新，实际游戏标题显示3.3.94012；开场动画与标题约30FPS，**还未获得营地帧率**。无需更新密钥或固件，旧全局设置和其他游戏存档均保留。网络初始化日志有大量BSD Read stub/errno89，不能单凭日志宣称其为标题无法进入的唯一原因。
- 用户建议试 `User Last Online` 并明确让试飞行模式。已浏览[近期Eden安卓用户直接实测](https://www.reddit.com/r/EdenEmulator/comments/1x1ogri/diablo_ii_with_argosy_rewrite_settingsjson/)与[2026年Eden用户的存档设置说明](https://www.reddit.com/r/yuzu/comments/1rhwnzt/diablo_2_resurrected_reign_of_the_warlock/)：在此游戏存档 `Settings/Settings.json` 添加数字字段 `"User Last Online": 9999999999999999999`（19个9）有人成功离线进入；这是玩家报告，不等于本设备已经验证。先单独试Eden游戏专用虚拟飞行模式，若不成功，正常退出并完整备份刚生成的新D2R存档，只改该JSON字段再测，不下载第三方离线补丁/他人存档，不修改其他游戏数据。
- 已通过正常设置把 D2R 的 `custom/0100726014352000.ini` 设 `airplane_mode\\use_global=false` / `airplane_mode=true`，global airplane_mode=false保留；这是模拟Switch的设置，不改安卓Wi-Fi。Eden对照智能体当前独占设备UI，root只在电脑推进代码/文档；关键阶段交还设备后再安装或捕获移植camp探针。


### Eden 营地对照已获得，虚拟手柄与驱动加载并行推进

- 用户亲自切换 Eden 为 `turnip_mrpurple_T30-toasted.adpkg.zip`（Turnip26.3.0-T30-1.4.359），创建 Titus 并进入第一幕营地。三次只读截图 `~/dm-work/research/eden-android-reference/eden-user-turnip-camp-{1,2,3}.png` 均显示30.0FPS，皮肤、衣物、帐篷、草地和火焰可见。成功组合包括该游戏虚拟飞行模式、19个9的 User Last Online 和新驱动；没有单独证明其中某一项决定进入，也没有修改用户角色。完整存档备份和操作记录见该目录 README.zh-CN.md。
- 查询读回版移植 APK 已实际构建安装。自动采集器 `~/dm-work/research/android-camp-capture.py` 在真实 frame108 按 A 读档，frame600 于营地捕获资源，frame900 后保存日志与截图；数据在 `~/dm-work/research/android-camp-query-probe`。截图18.2FPS、922draw、50pass、7submit、快照24.5MiB；后续稳态约19.2FPS、GPU25.5–26.9ms、等待13.4–13.5ms，300帧p50 51.92ms。诊断帧附近数据有额外捕获开销，尚未达到60FPS。
- 两端使用同一游戏版本，工作档仍不同：移植默认 DM_OPMODE=1、实际1600×900；Eden use_docked_mode=0、1×，其模拟 VI 报1280×720，但实际动态3D目标尚未捕获。两张营地截图位置不同，不能直接像素比较，也不能把30对19全归因于驱动。后续使用移植已有 DM_OPMODE=0 路径做同掌机档对照，不混入未经测量的 reduced 精度变体。
- 片元线程空间邻居的独立 Adreno 探针证实：完整相邻像素结果正确，但纯 Ryujinx64 绝对 shuffle 在三角形边缘的辅助线程出现16/16、10/10、18/18错误；混合 Quad 操作路径相同位置零错误。真实 models 中目前该结果的相关分支没有影响最终像素，不能把这一 API 语义问题直接当作地面方块根因。证据 `~/dm-work/research/fragment-quad-probe/device-v2.log`；TXD寄存器顺序另有独立 Host 真GPU差异，Adreno尚未运行，生产着色器未改。
- 营地光源表与菜单不同，10个网格实际会采样 R16 彩色 cube 阴影。干净替代方案的独立 Adreno 探针已通过：GPU 颜色图→设备内缓冲→D16/D32 深度图，不映射或读取源到 CPU；六面、两mip、八种比较、真实源更新与跨面线性比较共1158次像素绘制、4次内容版本复制通过。日志 `~/dm-work/research/shadow-depth-alias-probe/device-system.log`。这是独立方案验证，尚未接入生产，也未证明修复地面斑块。
- 用户要求专门子智能体处理虚拟摇杆，`/root/android_touch_controls` 已实现六块独立透明触控窗口与分源输入合并，支持摇杆与技能同时按、独立释放及取消。离线16项多指行为、Java/ARM64/JNI/i18n检查通过，真机安装与实际移动验证待 root 统一执行。驱动智能体同时实现可选、统一的 Vulkan 加载路径；不混用系统和 Turnip 对象，必须在初始化核对实际驱动及着色器线程组。当前设备仍运行用户的 Eden，未用测试输入干扰其界面。

### 虚拟手柄已经真机安装并完成双指检查

- 系统驱动 APK 构建与安装实际成功：`~/dm-work/tools/android-build-touch-controls.log` / `android-install-touch-controls.log`，备份 `~/dm-work/build/android/checkpoints/virtual-pad-system-first.apk`。六个有限大小的触控窗口确有 SPLIT_TOUCH / NOT_TOUCH_MODAL，血球、蓝球、腰带和中央画面可见，窗口实测日志 `~/dm-work/tools/android-touch-controls-windows.log`。中英表347条、中文字面量扫描与16项输入状态回归再次通过。
- 独立 Android shell 工具在 API36 检查真实输入接口后，实际发送 pointer0/1 的双指事件；全部注入返回true，并分别保存每阶段游戏截图。第一轮19.328秒：从4018,4944移动到4009,4927，双指时杆和A都高亮，真实游戏聊天出现“城镇里不行”证明技能请求已收到；只松A时杆仍高亮，最终两次中立截图都为4009,4922。走到帐篷阻挡，因此这份数据不能独自证明只松A后仍持续走满5秒。
- 第二轮反向移动、先松杆：19.325秒，BUTTON_ONLY时杆中立、A仍高亮，位置4058,4969；全松和3秒后位置一致、控件均中立。第三轮14.303秒：真实两指 ACTION_CANCEL 被系统接受，之后和3秒后均4009,4922、杆/A中立。证据目录 `~/dm-work/research/android-touch-controls/device-{sequence,release-stick-first,cancel}`；不是用纯Java状态测试冒充游戏响应。生命周期、实体手柄和所有肩键的游戏功能仍未逐项实测，不宣称完整长期游玩已验证。
- 新营地资源探针已在虚拟手柄版实际frame600捕获：`~/dm-work/research/android-camp-touch-roi-probe`，Playframe98、收集frame720。地面color/normal256×256均有正常非零草地/法线，没有全零矩形；用户移动后ROI投影在画面上方，不能据此排除脚下所有方块。此帧640网格decal仍0，实际彩色cube阴影增加为15网格，和之前固定位置10网格不混用。
- 独立 TXD Maxwell 寄存器负例已追加 Adreno 真机：`~/dm-work/research/txd-register-probe/device-result.log` exit0，原译器实际mip6/0/0，独立Eden契约参考和显式LOD真值均0/6/0；生产译器/着色器未改，当前decal0仍不能据此解释地面斑块。阴影深度别名方案暂不接生产：R16值天然[0,1]，R32颜色不能未经契约检查就复制成深度，也不能强制clamp掩盖问题。

### 显示能力、统一驱动与窄快照优化：离线验证完成，设备等待恢复

- 原已装日志明确交换间隔2、节拍60Hz、间隔模式game、被误报“无窗口”；Android `mtl_display_hz()`一直返回0，导致原NVN节拍器还有30Hz上限。当前19FPS并非全由这条上限造成，CPU/GPU仍超16.67ms。现Java只读真实屏幕支持的最高刷新率，boot宿主线程通过JNI查询并原子保存，实际surface存在时按已有前端契约报告；无窗口/未绑定前端仍0。保留原NVN display模式的固定60Hz政策，不改游戏时钟、不取消节拍器，不虚报144/60。ARM64/Java编译和既有节拍器10项通过，真机游戏速度/性能尚待测；证据 `~/dm-work/research/android-frame-limit/README.zh-CN.md`。
- 可选Turnip加载器采用固定开源adrenotools及配套hooks，系统和custom都从各自handle取得GIPA→实例表→设备表，Android不再直接链接libvulkan。正常fallback先用旧表销毁上下文再重建；strict模式不得静默落回系统，枚举后核对真实driverID及shader线程组profile。主库、属性fixture和完整GPUfixture离线编译通过，无DT_NEEDED libvulkan/undefined vk入口，五个依赖16KiB对齐、静态C++运行时及许可证均齐全。**新Android分发器的系统/Turnip/WSI实际回归均未执行**，不能用Linux结果代替。说明 [ANDROID_GPU_DRIVER.zh-CN.md](ANDROID_GPU_DRIVER.zh-CN.md)。
- API29+应用私有目录不能直接exec测试ELF，现构建把从当前源码编译的两个PIE诊断组件放进自身APK nativeLibraryDir，并打包独立GLSL测试缓存；连接恢复后用本应用UID分别运行system和strict Turnip属性/整套GPUfixture，再运行真正游戏。这些是诊断入口，不新增Activity，也不是把测试着色器放入真实游戏缓存。
- CPU窄优化仅在最新已知、不可变CPU复制完整覆盖请求，且无任何重叠GPU未知写/事件/计数器时，直接复制最终快照；部分覆盖、越界和不确定情况走原读取/顺序覆盖。逻辑快照/上传字节总量不减报，另记省掉的客体读取与重复覆盖字节；源仍由真实fence寿命保护。源码 `nvn_vulkan_async.inc` / `snapshot_read`，新回归 `known_copy_snapshot_test.inc`。
- CPU智能体两次因模型容量中断，root读取全部改动并接手测试入口及收尾。完整O3 Host Vulkan（lavapipe软件设备）回归 `~/dm-work/tools/android-tests-known-copy-host.log` 实际exit0：旧门控/查询/材质等全保留，新增真实着色器红/绿与黄色像素、非零offset、源变化顺序、跨批门控寿命、事件/计数器真实等待及GPU+7后red255均通过。这不是新的Adreno结果，也没有新的游戏FPS成绩。
- 含虚拟手柄、统一加载器及新R16六面探针的首个离线APK构建成功 `~/dm-work/tools/android-build-touch-driver-loader.log`，检查点 `virtual-pad-unified-driver-built.apk` **未安装**。最终统一候选已构建成功，增加显示/快照修复及两个从当前源码编译的诊断组件：94,345,751字节，检查点 `~/dm-work/build/android/checkpoints/virtual-pad-driver-display-known-copy-built.apk`；完整日志和实际APK审计在 `~/dm-work/research/turnip-loader-reference/final-apk-{build.log,report.json}`。12个原生ELF、16KiB对齐、签名和51个独立测试缓存文件已核对；最终Java重新编译后的16项多指行为再次PASS。设备上最后成功安装的仍是 `virtual-pad-system-first.apk`，不能把新候选称作已经在手机运行。快照优化复现说明 [ANDROID_KNOWN_COPY_SNAPSHOT.zh-CN.md](ANDROID_KNOWN_COPY_SNAPSHOT.zh-CN.md)。


### 统一系统与 T30 真机诊断通过，营地新基线与掌机对照

- 新默认Android分发完整 `test.py` 实际exit0：`~/dm-work/tools/android-tests-unified-dispatch-system.log`，全部旧运行时/图形检查及六项known-copy像素/门控检查保留。94MB统一候选实际安装成功，随后同App UID系统属性、完整GPU suite和独立profile探针实际完成；system driver8/subgroup64。
- 用户T30已复制到本应用内部temp/driver，strict属性及完整GLSL suite真实exit0，身份driver18/PurpleVK26.3.0/git62ac221a33、API1.4.359、subgroup128。R16颜色compare0/D16深度compare1，不能靠换驱动忽略此契约。完整日志 `dm/android/build/gpu-fixtures/{system,turnip}-libdmgputestprobe.so.device.log`，研究总日志在turnip-loader-reference；尚未激活正式游戏T30配置。
- 首次App UID诊断脚本因APK随机目录末尾==，被toybox env误当成赋值，返回0但未执行程序；profile轮exit126。明确保存为launch-not-executed日志，不计通过。修正为env执行固定/system/bin/sh再exec安全引号路径，属性必须有真实完成标记、GPU必须有最终真实回归标记，重新执行才取得上述PASS。run-as不能读取external shader profile，原T30 profile轮Permission denied；shell实际读取原profile为64，另存准确内部诊断副本，不改数字冒充128。
- 系统full64、1600×900新营地 `~/dm-work/research/android-camp-unified-system-900p`：实际Playframe360、probeframe1200、captureframe1500，区域1、角色4009,4929。截图20.5FPS/48.8ms，CPU24.4/GPU23.1/等待12.5ms；日志稳态约19–21FPS。boot真实查询144Hz，原NVN现在display模式/固定60Hz，误加30Hz上限已解除，实际仍受工作耗时限制。known-copy正常营地命中约0；诊断帧仅约0.02MiB省读，没有可报游戏性能收益。
- 上述探针独立命名目录避免混入旧文件。真正采样的R16cube44六面128²读回全部0；截图地面矩形斑块仍在。渲染智能体正结合原Metal生成链和源存储定位，不能只凭零值断言根因。最初frame90按A的新快节拍采集停在主菜单，已如实单独保存android-menu-unified-system-900p，不冒称营地。
- 新可复现launch-settings模块只允许现有operation_mode掌机/主机及固定路径真实HID日志；已在DM_TEMP设置后、首次模式/输入缓存前接入。Host O3普通34项/故障恢复40项通过，APK完整构建安装exit0，720p系统营地采集中。Android shell的独立测试无法创建FIFO而中止，当前不把Android单测算PASS；正准备自身App UID诊断，未跳该检查。说明见ANDROID_LAUNCH_SETTINGS.zh-CN.md。
- full128使用当前原译器与原Maxwell作业，新独立目录shaders-subgroup128，full精度，6931/6931Vulkan1.1val和6931/6931反射通过，旧full64保持。专项智能体正审核ABI并生成真正Maxwell128线程组对照probe，完成后才测试T30游戏。60FPS和地面修复均未达成。

### 掌机真营地、合法能力缓存、typed clear与T30线程组实测

- `launch_settings` 同App UID普通34/故障40项全部实际exit0，保留真实FIFO负例；shell创建FIFO失败没有跳检查，改从APK只读native目录、应用内部temp真实执行。新HID双指读回 `android-touch-controls/device-hid-receipts/real-state-changes.json` 确认实际返回：杆(102,20573)→杆+A→只杆→全部0，阶段间约5秒；不是只靠注入返回值和控件高亮。
- 正确等到原版既有角色主菜单ready再按A，系统掌机营地新基线 `android-camp-handheld-system-ready`：probe1500/capture1800、真实4009,4929、实际主色/深度/UI1280×720，截图20.0FPS/50.0ms、CPU23.1/GPU25.6/等待12.8ms。900–1800帧剔除probe后约19.93–21.52FPS，降尺寸收益有限。最大交换链1600×900是原始代码预留，裁剪720p，原ARM指令四组合4/4证明，不强改底图。
- 原cache所有6931声明未用Float64、4559 FS声明未用Interlock，却未在Android启用这些功能。Android专用译器明确不声明，profile记录实际政策并拒绝旧/缺政策cache或不匹配translator；新full64/full128独立目录全部fresh6931val、所有meta/res与原一致，除了移除两种能力/一个extension外module header和全部其余指令逐字节完全相同，NoContraction/精度保留。当前设备已完整部署合法full64，旧cache目录原样备份shaders-system64-before-capabilities。
- typed_clear视图清除已用旧代码真GPU复现R16 integer view清FFFF却得0，修后六面FFFF/0/中间位值、mip/layer/局部保留、float16/32、sRGB原字节和packaged初值均通过。完整Host LVP `android-tests-typed-clear-host.log` 与完整Adreno `android-tests-typed-clear-system.log` 均exit0，全部旧门控/查询/材质回归保留；APK构建安装成功，检查点typed-clear-system-720p-built.apk。
- 真实游戏复测 `android-camp-typed-clear-system-720p` 仍约19.45–20.49FPS、脚下方块仍在。初始化五次清除是serial388/390/391(R8)与108/109(RGBA8)，ui且原值全0，并未命中R16cube44；其六面仍0，不能把此已确认实现缺口当作本次方块原因。真实cube比较为LESS，但active Dref网格仅画面顶部7格，脚下在中央，不直接把它归为脚下色块。下一探针会读真实源地址/六面源前缀/物理范围alias。
- T30默认128独立合法Maxwellprobe实际FAIL：所有128真实lane可见，但ballot(true)XYZW为FFFFFFFF/FFFFFFFF/0/0，高64的VOTE.ALL错64项。初轮runner漏启ImageGatherExtended的非法结果保留不作驱动证据；启用真实声明所需功能后重新执行仍同样FAIL。没有改Ryu真值/算法或把physical128虚报64。
- T30已报告支持sizeControl、min64/max128、requiredStages全部；独立device实际启用功能、pipeline明确required64，真实Builtin64/两个64子组、22列×128原Maxwell全部正确，旧32对照800项错。`subgroup64-controlled-probe/device-turnip-controlled.{csv,log}`实际exit0。正在接统一loader/device/pipeline及cache有效size匹配，保留默认128日志、禁止静默fallback假过。真实计数器LocalSize32不能照搬测试的FULL标志，另有实际部分active组/FS检查准备；T30游戏性能仍待测。
- 新CPU窗口 `android-cpu-current-handheld` 实际11.9497秒、10156样本/lost0，冻结当时APK ELF/symfs、画面/前后日志/温度齐全。全局未知链+host-effect扫描自身4.62%、descriptor分配下游2.24%/更新5.70%；不能将全进程分母换成过滤样本或归到NVN全部CPU。窄精确区间索引模块已离线816552查询等价，随后已接生产，最新验证见下；不动latest-copy/overlay顺序或真实等待。

### T30同APK营地画面改善与CPU精确索引接入

- 新生产required64模式已完整构建/安装，检查点 `~/dm-work/build/android/checkpoints/subgroup-policy-probe-system720p-before-cpu-index.apk`及ELF/source tar均冻结。新默认system、App UID strict T3064全部GPU套件/实际profile均exit0；新增LocalSize32/48部分活跃Maxwell与FS256真像素actual64也PASS。物理128与实际shader64分开记录，生产computeFullSubgroups0、所有VS/FS/CS真实请求64，无FULL/VARYING；仅缺少声明时保持旧默认路径。
- T30同角色4009,4929、真实720p营地 `android-camp-turnip-required64-720p`：正常900–1800排除probe1500，15个窗口FPS20.81–22.18、均21.44；GPU均16.05ms，截图21.6FPS/CPU22.1/GPU16.3/等待11.4ms，脚下原明显矩形斑块消失。同一冻结APK改回system默认，`android-camp-new-policy-system-default-720p` 同位置矩形再次出现，FPS均20.026/GPU24.463ms。已隔离出驱动影响，不能据单一营地图宣称全部渲染正确；60FPS仍未达成。
- 新G/X/O源头证据：cube44真实CPU/GPU六面都0、只与本身image/descriptor753/754重叠，未发现当前44→45物理alias。两驱动108/109材质数据逐字相同；系统opaque光表出现分类混杂、T30类别一致，全局衰减58亦有差异。实际cs_light_bin本身无Ballot，尚不能把所有差异归票数。更精确probe仅显式frame+request第三行shaderkey生效，直接在dispatch前后复制真实bound GPU输入/输出；独立Host实测后续填99仍保留pre9/post16/pre16/post23及typed真实值，避免将帧末旧源冒充当次输入。正在准备两驱动同CS原始输入重放。
- T30新CPU数据 `android-cpu-turnip-required64-before-index`：11.9177秒、9968样本/lost0，全局cycles14,449,011,115；submit线程66.38%，未知链自身8.00%、host-effect1.49%、draw5.17%、bind3.05%。首轮CLI -f1000被拒绝未记录，保留失败输出；正确参数另跑取得上述真实数据。此基线不含CPU索引或新精准CSprobe。
- 精确UNKNOWN/HOST_EFFECT区间索引已集成：每个真实batch独立持有，实际post链顺序决定前缀、字段完成才登记、typed初值读之后登记、真实fence/FIFO发布后才退役。保留原guest_publish copies||host_sync、latest/overlay/barrier/retain顺序。O3/O0各816552查询和动态scope7036对照PASS；完整Host GPU与新默认Adreno `android-tests-pending-index-system.log` 均exit0，151次原链oracle及每commit登记审计PASS，所有旧timeline/像素/材质/查询回归保留。统一APK已安装，真实营地仅小幅改善，不能用离线节点数声称大幅加速，最新数值见下。
- 新System默认Maxwell扩展观察probe有actualexit1，主要观察SubgroupId逐row异常，按其重建的ballot真值级联失败；实际device SPV已核与冻结测试shader完全同字节。保留原CSV/log/exit，正做独立Builtin最小验证，不把此观测直接当32个原Maxwell投票错误或据此改helper。

### 索引实测、真实内核CPU与同输入光照重放

- 索引版T30营地 `~/dm-work/research/android-camp-index-cs-turnip-720p`，同4009,4929、真实720p、900–1800帧剔除1500探针：15窗FPS21.12–22.48，均21.80467，GPU均16.08067ms；脚下矩形仍消失。对比此前T30均21.44只能称小幅改善，未达到60FPS。实际每帧UNKNOWN/HOST查询约29197/30116，平均树访问1.15/.29；三个batch槽保留总计10.5KiB，不再逐项扫描整个旧链。
- 新紧凑采样 `android-cpu-index-turnip-tight`：真实用户态perf11.9232秒、9516样本/lost0；相邻proc窗口14.58秒，主线程30405用户CPU3.31秒、内核CPU6.68秒、实际运行9.99005秒，排队仅0.0583秒。不能把这些内核CPU叫GPU睡眠，不能用不同窗口相除伪造每帧耗时。前后位置4020,4940且截图中立，但HID日志已到1024行上限，未证明全窗无输入；此前before-index的截图跨度约98秒，角色也发生移动，不作为严格静态FPS A/B。
- 本设备内核采样事件 `cpu-cycles:k` 实际失败为unsupported event，保留失败日志，没有修改权限或声称获得内核调用栈。真实本线程 `getrusage(RUSAGE_THREAD)` 提交统计已构建/安装/真机验证；替换原兼容层每次读proc的统计实现，缺失读数明确unavailable，frame300建立同窗基线，600起每300帧输出wall/CPU/kernel/yield。ARM64定向编译与Host真实用户/内核/别线程隔离自校验通过，源码、独立patch与证据在 `android-cpu-performance-review/android-submit-stats/`，最新实际数值见下。
- 精准CS探针在T30真实frame1500只命中一次原 `b54d192f7c620ded`，groups4×5×1，8份即时pre和2份即时post；没有虚构第二次dispatch。冻结实际UBO、真实RGBA32_FLOAT texel-ROM和输出缓冲，原着色器在Host LVP重放，与游戏post的counts5120字节、indices1310720字节完全一致，640格独立类别/子集/重复检查均0错误。
- **同一T30即时pre、同一原CS** 在真机两个驱动交叉执行：T30 required64再次逐字节一致且独立oracle exit0；System driver8执行完成exit0，但counts44字节、indices53字节不同，独立oracle实际exit1，31格子集错误和31格分类错误、0重复。执行成功不是数值PASS。证据在 `~/dm-work/research/cs-light-bin-replay/turnip-input-cross-driver-summary.json` 与原始输出目录。这隔离了本样本计算差异，尚未锁定具体错误指令，也不泛化成所有系统驱动均有问题。
- 该分类CS没有ballot/shuffle/原子/屏障，468个CFG块均可达，同调用者写后读有明确契约；不能把此结果归到另一条默认子组Builtin诊断。System同APK即时pre/post已采集，另一半交叉闭环：System pre→System逐字复现自身坏gamepost、31格分类/子集错；System pre→T30和Host逐字一致且规则全0。四路汇总 `cs-light-bin-replay/fourway-summary.json`；简单bitcast小例System/T30均实际PASS，尚不能把复杂原CS错误归为FTZ。前驱depthbounds两次输入有大量差异，下一步独立定位，未据此直接判值为0就是错；不修改游戏精度、光照数据或着色器来制造通过。

### 提交内核瓶颈已在新APK实测

- 正常统一构建/安装 `~/dm-work/build/android/checkpoints/pending-index-submit-stats.apk`，95,050,957字节；新增统计之外图形策略与当前索引不变。真实T30冷启动、probe0关闭，营地4009,4929，`android-camp-submit-stats-turnip-720p`：900–1800共16窗FPS21.49–23.15、均22.173125，GPU均15.9825ms；不把不同诊断/温度窗口的小幅波动当显著加速。
- 每个真实300帧窗口，frame1200/1500/1800的提交CPU22.176/21.845/22.011ms，kernel16.788/16.758/16.902ms；wall33.158/32.911/32.980ms。kernel已经位于提交内部，但仍不能预先将其归给readv或mprotect。yield wall约1.76–1.91ms是单独原有累计，不能与GPU/CPU简单相加。
- 相邻冷启动紧凑采样 `android-cpu-submit-stats-turnip-tight`：实际11.9517秒、8809样本/lost0、record exit0；proc窗口14.48秒，主TID12364用户3.2秒/内核6.82秒/runtime10.0557秒/runqueue0.0571秒。HID日志39→59行未到cap，窗口持续读回零按键/零摇杆，前后真实位置4009,4929；相较上轮已补齐输入见证。
- 继续添加当前线程实际safe-reader调用/真实返回字节/失败/耗时及既有全线程页面保护低频分账。读数必须分别标明线程和wall口径，不从逻辑snapshot MB推syscall成本、不把blocked墙钟叫内核CPU。子智能体审查已知写范围内连续解保护合批的并发协议，生产优化尚未实施。

### 真正读取14ms已定位，尺寸桶和权限实验继续

- 完整新标准Android回归 `~/dm-work/tools/android-tests-reader-stats-system.log` 实际exit0，正常统一reader/dirty统计APK构建/安装成功：`pending-index-reader-dirty-stats.apk`，95,346,126字节；源tar/ELF/hash清单独立留存。当前线程safe-reader仍是真正process_vm_readv，返回/errno/partial语义未变，没有把已登记pool直接视为可安全memcpy。
- 实际营地 `android-camp-reader-dirty-stats-turnip-720p` 同4009,4929/1280×720，**frame900仍混加载画面**，不能用它抬高FPS；稳态960–1800共15窗FPS20.73–22.21、均21.49333，GPU均15.94333ms，脚下矩形仍未出现。截图和完整原日志/thermal在同目录。新增统计没有证明性能收益，60仍未达成。
- frame1500/1800真实submit CPU22.113/22.035ms、kernel16.955/16.621ms。当前TID19651所有实际safe-reader用途合计每帧1308.5次，requested/returned各24.664MiB，全full、partial/error0，**read wall14.245/14.230ms**。尺寸次数0/371.5/101/611/约9.4/211.6/4，当前尚不能从次数断言4个大读取占多少耗时。全线程页面保护约1004次/1273页/wall7.0ms、直接fault377次、宿主准备259页每帧。两个wall统计线程与范围不同，不能相加当互斥内核总量。
- Reader真实AppUID独立ARM夹具实际exit0：完整/zero/invalid源和目的/真实partial4096of8192/errno保留/clock不可用/TLS隔离全部通过。后续同一对clock的七桶bytes/wall细分也已Host和ARM验证，新API352B/线程、不增加syscall；当前95,329,742字节的`pending-index-reader-buckets.apk`已安装，新版AppUID19call检查再次exit0，营地1800正在采集。新版source tar/freeze在checkpoints，旧诊断不得混作新版证据。
- 原函数和合批候选的真实页面权限/信号夹具也均AppUID实际exit0、各109观察：8页8→1调用、断段、池边界、重叠BUSY、同页/邻页缺页、静止销池复用；**可写而旧代数窗口1→8页的反例也真机复现**。这不证明任意异步写安全，未接入通用prep合批。持queue_lock的单post0复制事务存在更窄可证明边界，设计在 `dirty-range-batch-review/gpu-copy-transaction-design.zh-CN.md`，尚未实施；FS完整写前/写后协议另列。
- HLE映射只读审查发现pool登记不验证可读页、当前heap数值检查无读取期租约，不能直接解引用未知地址。优先独立测实际Vulkan映射目标是否使kernel copy昂贵，并评估安全源读取到普通owned scratch后复制到已持有映射。第一版独立基准短窗口CPU量化不足，未安装/执行，保留为旧产物；研究基准将延长实际batch后再真机运行，不靠不可靠CPU0读数下结论。
- 原b54三个合法独立SSA候选已真机System/T30对照：exact局部与single-block仍44/53byte差异，fullSSA变10/36byte差异但仍31错类；T30三份均完整0byte差异。未修改游戏cache/生产译器，不能把这些仍失败的候选当渲染修复。

### 四次大读取、独立映射基准与heap读取租约实现中

- 尺寸桶APK真实营地 `android-camp-reader-buckets-turnip-720p`，960–1800共15窗均22.074FPS、范围21.43–22.49，GPU均16.172ms；仅统计，无加速声明。frame1800全部1290.4次/24.606MiB/wall14.094ms，**>65536字节的4次占21.917MiB/wall12.510ms**；其余读合计约1.584ms。四大读的具体caller还未由这个尺寸统计证明，不能直接假设全来自snapshot_buffer。
- 校准版独立Vulkan映射基准已真实AppUID两驱动执行：System driver8实际1type/126row、T30 driver18实际3type/252row，均exit0、全字节/partial4096of8192/errno/zero合同PASS。T30生产同选择规则type1 flags0xf，8MiB direct readv中位1021.827µs、中转1307.794µs、fixture自有两端memcpy239.807µs；中转更慢，不接生产。普通单源VMA的独立测量不能等同游戏12.5ms，未获取设备内核调用栈，不推定特定kernel函数。研究 `safe-reader-diagnostics/vulkan-readv-benchmark/device-comparison.zh-CN.md`。
- 当前设备安装 `pending-index-reader-buckets-mapbench-calibrated.apk`，game部分与正常桶APK逐条相同，仅多独立PIE。系统与T30基准环境分别显式指定，command.json的execution_driver及实际DRIVER输出为准，record.mode只表示prepare阶段。第一版短窗口基准APK没有执行，保留不混用。设备已恢复T30营地，`android-camp-before-heap-lease-resumed`实际frame900截图/日志已保存。最近用户连接检查ADB实际device/transport5、shell回应与游戏PID均存在，没有重配对。
- 正在实现 `dm_android_guest_heap_copy_owned(source,destination,size)`：仅内部已持有、完整可写且不重叠的目的；完整heap源验证和实际复制在与SetHeapSize共用的tryrdlock内，忙/heap外/alias均未写入返回0，继续原safe readv；不取消dirty保护或改变known-copy/overlay/FIFO。SetHeapSize成功才发布cur，失败可能部分改权限所以永久拒绝快路并返回真实非零Result。Android初始化once/ready、cur单次原子观察；不新增2MiB接受策略。非Android保留原行为。
- CPU智能体独占 `dm/host/hle_svc.c` / `hle.h`，真实源Host九组页权限/初始化/缩堆并发/partial失败/errno/真实dirty写fault初轮PASS，另有故意丢读锁的负对照确实被拒；ARM PIE待复核冻结，**还未在设备运行或安装含快路的game APK**。后端智能体仅改 `native/nvn_vulkan.c`，首版只给snapshot_buffer刚创建的完整自有vk_buffer、>64KiB尝试；mtl_copy/forward_seed/stack/shadow继续原路，单独报告large candidate和实际heap命中字节。Host旧GPU回归PASS只走无heap弱符号fallback，不冒充正向快路验证。
- root已把 `diag.c::tk_rd` 唯一Android旧heap布尔检查后memcpy改为相同封闭租约helper+原vm_read_overwrite fallback，非Android旧路径保留。其目的仍为原有内部栈/结构，不扩展任意目的地址许可；Android布尔range getter仅信息观察，不再批准随后直接读取。待双方冻结后root统一实际GPU/ARM测试、构建、安装与同营地fresh FPS/画面对照，60FPS仍未达成。

### 用户报告的三个问题修好并真机验证，分辨率给出实测档位（2026-10-17 第二轮）

- 方向键（Xbox 十字键）：根因是原生输入（`frontend_activity.c` 的 `input_ready`）只读摇杆轴、**完全不读 HAT 轴**，而 Xbox 手柄在安卓上把十字键报成 `AXIS_HAT_X/Y`，于是被静默丢弃。现在按 ±0.5 阈值映射成 `DM_BTN_*` 写进**独立的 `hat_buttons` 位掩码**（`dm_input.h` 的 `dm_pad_hat_buttons`），免得 HAT 归零时把同时按住的 `DPAD_*` 键码一起清掉；源判断放宽到 `JOYSTICK|GAMEPAD`，纯 GAMEPAD 源的方向键不会再掉进触摸分支被当成手指坐标。宿主单测 `t_padmap` 新增 9 条 HAT 断言（共 29 项、0 失败）。
- 接入实体手柄时隐藏虚拟摇杆：用 `InputManager.InputDeviceListener`（设备级）加启动时扫一次，**不能用窗口事件分发** —— NativeActivity 把 `AInputQueue` 直接挂到主 Looper 消费手柄事件，窗口那层看不到。拔出走同一路径恢复显示。
- 切后台存档（原来的顺序是错的）：`onPause` 里挂起图形会把游戏线程挡在 NVN 入口外，紧接着请它存盘就永远等不到提交 —— 真机实录 3 秒内 0 次提交、只剩一条"未存盘退出"红字；而回到前台、图形放开之后，两次提交（角色与共享仓库各一次）立刻落盘。改成"**先请存盘 → 冻结客体时钟 → 挂起图形**（挂起随窗口销毁的 detach 走）"后，同一路径实测 **241–262 ms 内落盘 2 次**。这里同时补上 iOS 早就有、安卓缺的**客体时钟冻结**：不冻结时游戏线程挡在提交处而墙钟照走，回来时游戏内时间会跳过整段后台时长（真机曾见"整帧 83839.6 ms"）；冻结后实测后台 18 秒里客体时钟只走了 **7.0 秒**（正好等于那段时间真实出的 420 帧），没有跳。
- 切后台再打开会重启（用户报的第 3 条）：窗口销毁不再等于退出进程，真正退出走 `destroyed()`；表面/交换链/信号量/`screen_images` 抽出 `screen_create_swapchain` / `screen_release`，可按新的 `ANativeWindow` 重建。**过程中查出一个真竞争**：安卓给窗口的时机在 `onResume` **之后**（真机日志顺序 `图形恢复 → 窗口重建`），而重建时游戏线程已经在跑、可能正读着旧交换链；注释里写着"只能在挂起期间调用"却没做到。现在 `mtl_screen_attach` 自己先挂起（`gfx_enter`/`gfx_leave` 会等在途那次出图退出），重建完由前端按"当前是否在前台"放开（真机日志：`图形恢复 → 图形挂起 → 窗口重建 → 图形恢复`）。退出路径上重复的第二次请存盘也去掉了（那时图形已挂起、注定等不到，只会白等 3 秒再打一条假红字）。真机：PID 全程不变、回来后新出 23–24 条出图记录（≈59.9 FPS）、正常退出存盘 241 ms 且老进程 1.30 秒干净退出。
- 回前台另外照 iOS 补了 `dm_3d_scale_resumed()`：回来头几秒的慢帧是恢复（表面重建、缓存变冷），不告诉控制器就会被当成 GPU 吃紧，把 3D 内部比例降下去。
- 调试可观测性：调试线程每 10 秒多一行 `[android] 运行状态 frame=… 输入样本=… 3D=… 客体时钟=… s`。3D 比例决定"抬分辨率到底有没有让 3D 变清"，客体时钟决定"后台那段有没有被算进游戏时间"——原来那条 `game progress` 只在前 48 秒打（兼首帧看门狗），覆盖不到进局之后。
- 分辨率（颜色档位之外的第三行设置）：写 `DM_GAME_RES`，与 Mac/iOS 同一语义（主机档 = 设定值，掌机档按 0.8 同比例）。从界面到 native 的整链已真机验证：`分辨率：跟随档位 → 点一下 1920×1080 → 再点 2560×1440` → 配置写出 `resolution=2560x1440` → `环境核对 DM_GAME_RES=2560x1440` → `WindowInitialize 首张 2560x1440`。营地实测（16 个 60 帧窗口）：**1600×900 → 1920×1080 基本免费**（FPS 58.64 → 58.07、fence_wait 0.81 → 0.86 ms），**2560×1440 约 −2 FPS**（56.61）且 fence_wait 涨到 2.15 ms；三档都**没有**出现 `3D 比例控制` 降档记录（3D 内部比例保持满值）。注意 `vulkan-time` 的 `GPU=` 只统计游戏批次、不含最后的缩放合成，所以分辨率的代价要看帧率与 fence_wait，不能只看它。
- 本轮设备验证踩过的坑（下次直接用）：① `D2RActivity` 没有 `exported`，`adb shell am start` 拉不起它、而且**失败是静默的**（上一轮的"返回前台"测试就是这样假通过的）；正确做法是把整个任务移回前台 `am task focus <任务号>`，任务号要从 `dumpsys activity activities` 里按包名取 —— 从 `dumpsys activity recents` 取会取错，实测抓到过"设置"那个任务。② 设备测试脚本**必须先 `install -r` 当前构建**，否则是拿旧包在测（本轮因此白跑过一整轮）。③ `make -C dm/host/tests run` 在这台机器上跑不了（`t_thread_leak`、`t_i18n` 用了 macOS 专有头文件 `malloc/malloc.h`、`TargetConditionals.h`）；可用 `~/dm-work/tools/android/ndk/toolchains/llvm/prebuilt/linux-x86_64/bin` 里的 clang 单独编译。本轮实跑通过：`t_padmap` 29 项、`launch_settings_test` 普通 53 项 + 环境故障注入 59 项、`dm/i18n/scan_literals.py`、`dm/ios/tools/dm_i18n.py --check`（368 条）。
- 超分调研（用户问「Eden 似乎有 FSR 超分提升画质，调研下」）：Eden 用的是 **FSR 1.0**（EASU 上采样 + RCAS 锐化，纯空间），挂在**出屏前的最后一步当缩放滤镜**、倍率由内部分辨率隐式决定、只暴露一个锐度滑条；AMD 头是 MIT，但 Eden 自己写的胶水是 GPL-3.0 **不能并入本 MIT 项目**（它还给 Adreno 准备了 BSD-3 的高通 SGSR，官方说更快）。对照我们的现状：最后一步正是用 `VK_FILTER_LINEAR` 把 1600×900 以 **2.12 倍双线性**放大到 3392×2400 面板（上下留黑边，截图核对过），也就是 Eden 放 FSR 的那个位置 —— 所以「不抬渲染分辨率、只把这一步换成更好的空间超分」是一条独立于分辨率档位的画质杠杆。完整结论、许可边界、公开开销数据（手机 GPU 上原版两 pass ≈6.3 ms、优化单 pass ≈2 ms 净开销）与「先离线截图对照再上机」的步骤见 [FSR 超分调研](ANDROID_UPSCALING_RESEARCH.zh-CN.md)。**本轮只调研，没改渲染代码，也没在 Adreno 上测过任何 FSR 实现。**
- 可疑清单实测（发放种子清单前的风险核查）：先定位到预热路径上**唯一**一处"让驱动调用决定进程生死"的地方 —— `nvn_vulkan_pass.inc` 的 `graphics_pass_get` 里那句**无条件** `VK_CHECK(vkCreateRenderPass(...))`，而管道里传给它的附件格式是从清单直接读出来的（预热路径只校验了数量与 loadOp，没校验格式）。随后做了三组真机实验：① 内容恶意（71 行：全 0/全 f 的 pass、52 条随机翻位的合法长度记录、缺 tab、NUL、CRLF、1 MB 超长行）；② 文件 9.4 MB 超过 8 MiB 上限；③ 路径上放一个目录（非普通文件）。**三种都没有让游戏退出**：超限的被 `manifest_oversize` 拒绝、目录被 `S_ISREG` 静默跳过、畸形行在解析期被丢（56 条里只有 5 条被跳过）。再定向造了 5 条"结构完全合法、只有 `formats[0]` 非法"（0x99/0/0xffffffff/0x63/0x1234abcd）的记录，**这台驱动照样接受、全部 built、进程不退** —— 也就是说这条风险在这台设备上**没能复现**。仍然做了加固（把 `graphics_pass_get` 改成与 `graphics_pipeline_create` 同一约定：预热传 `fatal=0`，驱动拒绝就跳过该记录、运行期仍照旧大声退出），并做了修前/修后对照：两份构建的日志逐项一致（`records=144 built=144 skipped=0`、都在出图），即加固不改变正常行为，只在真正会拒绝的驱动上把"整进程退出"换成"跳过这条记录"。**结论：种子清单可以发布，但这条加固要如实标为"防御性"而非"修了一个已复现的崩溃"。**
- 手柄真机验收（用户当场接线，Xbox Wireless Controller）：先用 `dumpsys input` 与 `getevent -lp` 拿到该手柄的真实轴 —— 十字键走 `ABS_HAT0X/Y`，扳机走 `ABS_GAS`/`ABS_BRAKE`（安卓暴露为 `AXIS_RTRIGGER`/`AXIS_LTRIGGER`，flat=0.062），**不是** `DPAD_*`/`L2`/`R2` 键码；右摇杆在这台手柄上报在 `Z/RZ`（所以现有的 Z/RZ 优先、RX/RY 兜底正好对）。据此改了两处映射（HAT → 方向键、扳机轴 → ZL/ZR），并把三条输入路径都加上"变化时记一行"的日志。验收不是靠"看起来能用"：游戏的 `GetNpadState` 收样（`files/temp/input-receipts.jsonl`，167 条）里实际返回的 `buttons` 位值与按下的一一对应（左 4096/右 16384/上 8192/下 32768/ZL 256/ZR 512/LB 64/RB 128），阈值行为也符合设计（`LT=0.41/0.47` 不触发、`0.66/1.00` 触发）。同一轮截图确认"接入手柄后虚拟摇杆与屏幕按键自动隐藏"。**顺带查出一个配置缺陷**：启动界面重写 `launch-settings.conf` 时会把不归它管的键抹掉 —— 正是它让我第一次开不了 HID 收样（配置里写了 `input_receipts=1`，native 读到 `-1`）。改成保留外来键后，同一路径实测 `input_receipts=1` 被保留并被 native 读到。
- 「切后台再进入会终止这一局」的根因不在后台保留（那条实测是好的：进程保留、窗口重建、客体时钟不跳），而在**任务栈**：启动界面是本应用入口活动，从桌面点图标时安卓会在游戏**上面**新开一个启动界面实例，任务变成「启动界面(根)/游戏/启动界面(新)」；再点「启动游戏」开第三个活动并销毁中间的游戏活动，而销毁游戏活动等于结束整个进程（一个进程只跑一个游戏），新开的也一起死 → 安卓重起进程从头加载。真机对照：修前点一下 PID 26997 → 28545（换进程），修后 `monkey` 发真实图标意图回来 PID 30764 不变、窗口重建、新出 25 条出帧、**没有新的启动时间线**（＝没有重新加载）。代价已在文档里写明：游戏跑着时启动界面不可达，改设置要先从最近任务里划掉应用（那才是真正退出）。
- 游戏内菜单（用户要求照 Eden，返回键呼出）：原先返回键直接结束游戏页面，而本移植里结束游戏页面 = 结束整个进程；横屏手势导航下边缘内滑极易误触（实测一次返回 → 存盘 241 ms / 2 次提交 → 退出进程 → 回桌面）。现在返回键呼出右侧菜单，四项都逐条真机验过：**画质**四档真实改变 3D 内部分辨率（95%/85%/70% → 目标 1360×765 / 1120×630，回自动恢复 1600×900，输出始终不变），固定档位期间动态控制器不插手（`dm_3d_scale_set_preset`，-1 自动 / 0 高 / 1 平衡 / 2 性能）；**帧率显示**是运行时开关（native 改成读原子量，每拍按变化通知 Java 显隐）并写回配置；**虚拟摇杆**控制屏幕按键显隐（接实体手柄时的自动隐藏仍在）；**退出游戏**走 `finish()`，与按返回键退出同一条存盘路径。菜单打开时手柄与按键被挡在游戏外（菜单是独立窗口，不挡的话角色会在菜单后面继续动），返回键与触摸放行。**调试浮层同时改为全局默认关闭**（启动界面开关初值、菜单初值、native 启动读取三处统一）。顺带把启动界面的配置读写抽成共用的 `LaunchSettings`（读一个键 / 保留外来行 / 改一个键保留其余），两个活动用同一套语义。
- 存档导入 / 导出（用户要求，启动界面 + 游戏内菜单）：导出把 `files/save/` 整棵树打成 zip 并走系统「新建文档」选择器（本机是 OPPO 文件选择器：全部文件 → 文件名 → 保存），导入走「打开文档」并做「解到临时目录 → 校验 → 原子换入」。压缩/解压/替换是纯 Java 的 `SaveArchive`，宿主机 19 项测试覆盖往返、`..`/绝对路径/反斜杠越界、空包、替换失败回滚。**真机端到端**：启动界面导出 → `/sdcard/d2r-save-20261017-1001.zip`（17 个文件 + 说明文件，与 `files/save` 逐项一致，界面显示「已导出 17 个文件」）；往存档放一个不在包里的标记文件再**启动界面导入** → 标记消失、17 个文件原样恢复（证明整份替换）；**游戏内导出** → 新包 `d2r-save-20261017-1006.zip`、logcat `[save] exported files=17`、游戏进程不受影响（选择器暂停/恢复走的是已验证的那条路）；**游戏内导入** → `[save] imported files=17, exiting to reload`，退出路径打出 `跳过存盘：刚导入存档，本次退出不再写盘`（**关键：旧的内存状态没有覆盖刚导入的存档**），随后 `优雅退出`、进程结束，标记文件消失、存档 17 个文件 —— 重开即加载导入的存档。另做了一次更贴近玩家直觉的验证（用户要求）：把角色目录整份删掉（游戏随后回到「选择英雄职业」的新角色界面，截图存档）→ 启动界面导入 → `files/save` 里 `AndroidProbe` 与角色文件全部回来 → 再启动角色正常出现。

### 玩家路径端到端跑通：Release 出制品、设备清空、下载套件打包、导入进游戏（2026-10-17 第三轮）

- **Release 页为空不是构建失败**：工作流里创建 Release 那一步只在推 `v*` 标签时触发，而仓库此前一个标签都没有，制品只出现在 Actions 的 artifact（要登录、90 天过期）。前两次流水线失败（`dec63ba`/`4e663b0`）才是真失败，正是 `98202db` 修的"壳模式"那条；本轮 4 次全绿。推 `v0.2` 标签后 Release 挂上 `D2R-Android-port.apk`（15 MB）与 `d2r-package-kit-v0.2.zip`（10 MB）。
- 设备清空（用户要求，模拟新玩家）：卸载应用（连内部与外部私有目录，实测外部 `d2r-package/` 占 18 G）、删掉 `Download/` 里的游戏本体与更新包（`D2R-reference/`，手机副本，电脑上有同一份）、`/sdcard` 根下我们自己的文件、`/data/local/tmp` 的测试脚手架；设备上其它无关 APK 与用户资料一律未动。手机上那份存档先完整备份回电脑（`~/dm-work/backup/`：内部 `files/save` 的 tgz，17 个文件 + 两份导出 zip）。
- **打包流水线此前从未完整跑通过一次**，本轮跑出两个真 bug：① **目录形式的游戏文件不工作** —— `gather_game_files()` 对目录直接 `return`，`main()` 随后去扫刚创建的**空** `work/input`，于是报"没有找到本体"，而 CLI 帮助、模块 docstring 与 skill 三处都写着支持目录；② **兄弟脚本定位错** —— `TOOLS` 取自 `DM_ANDROID_TOOLS`，而该变量在 `bootstrap.py`/`build.py`/`ci_checks.py` 与 CI 里都指安卓 SDK 工具链目录（jdk/build-tools/ndk，里面**没有任何 Python 脚本**），默认值下第一步就死在 `can't open file '.../tools/android/extract_packages.py'`。修完从**下载来的套件**完整跑一遍：产出 `d2r-package.zip` 8.88 GiB，`inspect` 得 `format 1 / subgroup 64 / counts game 9, romfs 46, shaders 20795, derived 756`，三个模块字节数与旧包逐个相同。
- 这台 OPPO/ColorOS 平板上 **`adb install` 装不上**：`OplusPackageInstallInterceptManager: OPLUS_ADB_INSTALL_CANCEL`，会话 `failed: -99`，与 APK 本身无关；把 `adb_enabled`（USB 调试总开关，此前为 0 而 shell 走的是无线调试）置 1 也无效。**唯一走通的路**：把 APK 推到 `Download/`，由用户在「文件 → 下载」点它，走厂商自己的安装引导（继续安装 → 完成）。从浏览器/网盘的下载列表直接点安装会走安卓的**流式安装**（`com.android.packageinstaller/InstallStaging`），可能报「解析程序包时出现问题」。**这两条是机型行为，按用户明确要求没有写进 skill。**
- **导入验收加固**（六路独立审计 + 每条一个有"反驳"指令的核验者）：原先只检查"`game/`/`romfs/`/`shaders/` 各至少一个文件"，于是一个 13 KB 的最小包被判**导入成功**、界面显示"46 个资源、20795 个着色器"，而游戏启动即 `FATAL open .../game/sdk.fixups.tsv` —— 且**上一次能用的模块在打开新包之前就被 `deleteTree` 删掉了**。现在规则与验收移进不碰安卓接口的 `PackageArchive`（与 `SaveArchive` 同一理由：可在电脑上回归）：九个必需文件成套校验（三个模块各配 fixups/gates，模块不许 0 字节）、模块先解到暂存目录、**验收通过才换入**（旧的先改名留一手，失败回滚）、清单自报的 `format` 与 `counts` 必须与实际相符、条目名不许有空段/`.`/`..`、单条/总量/条目数/清单大小上限、完成标记原子落盘（tmp + rename）；失败带"撞了哪条规则"的键，由界面层翻成中英文字（双语表新增 6 条，`dm_i18n_tab.h` 重生成）。审计还查出一条我没看出来的：**`romfs/../manifest.json` 能通过前缀检查、规范化后又落回包目录之内**，因此它在完整性校验**之前**就把完成标记写了进去，绕过"最后才写 manifest"的顺序保证。`PackageArchiveTest` 39 项（事故包、缺布局、0 字节模块、截断包、格式不认识、数量不符、六种越界条目、无清单、取消、成功换入；每次失败都断言旧模块还在、暂存目录没留下），已接入 `ci_checks.py`（现 9 项全绿）。
- **端到端结果**：导入后界面显示「已导入：46 个资源、20795 个着色器」，内部 `files/d2r-package/game/` 九个模块字节数逐个正确；启动游戏后 frame 38880、**52.9–56 FPS**、红色告警 0 条、三模块 gate 数 34/110/0 与之前一致，画面为英雄职业选择。先在 `v0.2` Release 的 APK 上把整条路径验了一遍；随后把**含导入加固的最新构建**覆盖装了上去（同一签名密钥，**不卸载、已导入的 8.9 GB 数据包与存档全部保留**），新版本启动游戏同样正常（frame 480、59.76 FPS、红色告警 0）。
- **`versionName` 写死**：`build.py` 里一直是 `0.2-android-port`，与提交无关。后果是系统把两个不同构建都当作"同版本"，覆盖安装时提示的是"已安装相同版本 → 重新安装"，玩家也无法从系统信息里看出装的是哪一版。值得改成跟 tag/提交挂钩。
- 待办：**在真机上用加固后的导入再走一遍**（8.9 GB 约 2 分钟，这是加固唯一还没上真机的部分）；审计剩余中低项（重导入不清理旧 `romfs/`/`shaders/`/`derived.tsv`、`assemble` 只查目录非空、`to_nsp()` 可能取回第一次转换的那个文件、native 侧对包不做任何校验、`subgroup_size` 不在设备侧校验、导入失败时玩家看到的仍是原始异常串）。

### 版本号跟发布走、存档备份恢复角色、README 配图（2026-10-17 第四轮）

- **`versionName` 此前自第一个 APK 起就写死为 `0.2-android-port`，`versionCode` 恒为 1**，系统因此把两个不同构建当成同一个版本（覆盖安装弹"已安装相同版本 → 重新安装"），玩家也无从看出装的是哪一版。现在两者都由构建时给入的版本串决定：`build.py` 加 `--version`，`versionName` = 去掉前导 v 加移植后缀（`v0.3` → `0.3-android-port`），`versionCode` = 主\*10000+次\*100+修订（`v0.3` → 300）。**只有 `v<数字>` 形式才折**：分支与 dev 构建固定为 1 —— Android 拒绝版本号**倒退**的安装，而按提交折出来的数字可大可小，会把正常安装挡掉（版本号相同是允许覆盖的）。工作流算一次版本给 APK 与套件共用（发布用 tag 名，分支用 `main-<短提交>`），三处名字不会再各说各话 —— 此前套件用 `GITHUB_REF_NAME`、APK 用常量，正是它俩分家的原因。清单替换同时收紧成"必须恰好替换一次"，改清单会立刻报错，而不是把模板里的 `0.1-runtime-probe` 静默发出去。`dm/android/tests/test_version.py` 钉住折算（含"发布 tag 的版本号必须严格递增"），已进 `ci_checks.py`（现 10 项全绿）。**真机核对**：装 `v0.3` 时系统安装引导页显示"版本号 0.3-android-port"，且不再出现"已安装相同版本"那一步。
- **存档备份能完整恢复角色**：走启动界面「导入存档」选备份包（`d2r-save-restore.zip`，来自 `~/dm-work/backup/`），界面显示"已导入 17 个文件"；把恢复后的 `files/save` 与备份**逐条比 MD5，17/17 逐字节一致**（含 8 个名字带空格的 `Starter - X.fltr`）；进游戏后角色列表出现 `AndroidProbe / 1级野蛮人`，可直接进入游戏。结论：跨清空、重装、升级都成立。
- **README 配图**：两边 README 新增「界面 / Screens」一节，三张实机截图放 `docs/images/`（2400 宽 JPEG，共 1.3 MB）：启动界面（无帧率浮层，图注写明它是普通应用界面）、英雄选择（角色来自导入的存档）、**Act I 营地 60.0 FPS / 帧时 16.7 ms**（CPU 7.9 / GPU 10.9 ms）。游戏画面都带调试浮层（帧率/帧时/CPU/GPU/功耗/绘制）。**注意**：这三张图含游戏自身美术，是本仓库唯一越过"不含任何游戏内容"这条线的地方（作为移植文档截图，不是分发的游戏资源）。
- 本轮踩到的两个测量坑，记下来避免重犯：① `run-as ... md5sum $(find ...)` 会被**空格**拆成多个参数，`md5sum` 把 `-` 当标准输入，于是吐出 8 行"空文件"的哈希 —— 要用 `find -exec md5sum {} +`；② 别删一个**正在被游戏写着的** `game.log`（进程仍持有旧 inode，新文件一直为空，监控会永远看不到输出）。
