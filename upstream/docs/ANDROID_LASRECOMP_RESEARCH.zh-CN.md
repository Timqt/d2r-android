# LASRecomp 调研与 D2R Linux 实验

2026-10-16。用户已决定：**回到现有 Android 版提升帧率，LASRecomp只作渲染借鉴；停止PC静态重编译扩张。** 本文保留研究与实际失败证据；当前Android工作见[帧率优化记录](ANDROID_FPS_OPTIMIZATION_20261016.zh-CN.md)。尚未出现本机D2R游戏画面，约22FPS是旧Android基线。

## 下载与独立工作区

- 完整上游仓库：[LASRecomp](<LOCAL_WORK>/research/LASRecomp/README.md)，提交 `6244eb9d4d3497094e21cac7dabe998ded1f2800`，提交时间 `2026-10-14T23:51:42-05:00`；完整 clone，原源码保持不改。
- D2R 实验：`<LOCAL_WORK>/research/d2r-lasrecomp-linux`，复制上游源码后另行适配，不覆盖 Android 工作树。实验数据入口只引用既有 ExeFS/RomFS，存档另设实验目录。
- 本机真实硬件：Ryzen 9 7940HS / x86_64 / AVX2；初次探针使用 Radeon 780M。用户随后明确要求独显，已实际打通 RTX 4060 Laptop GPU、NVIDIA 615.71.09 / Vulkan 1.4.351，并以它运行正式启动尝试。
- 独立依赖目录：`<LOCAL_WORK>/research/d2r-lasrecomp-linux-deps/prefix`；头文件与系统运行库版本探针对照通过，不修改系统安装。

## 架构判断

LASRecomp 的 `gen.py → lifter.py → C → clang → lasgame.so → dispatch` 链路值得尝试：它把整数、访存和控制流翻译成操作 `Cpu` 状态的 C；ARM 主机上仅部分浮点/向量指令采用内联汇编，而且批次前后仍需装卸寄存器。因此“ARM 同构、零翻译损耗”不准确。D2R 当前 Android 方案已直接执行绝大多数原 ARM64 指令，换生成 C 不会自动消除现有 NVN 内存复制及系统调用成本。

源码证据：[生成器](https://github.com/peiikai/LASRecomp/blob/6244eb9d4d3497094e21cac7dabe998ded1f2800/tools/gen.py#L351)、[汇编批次](https://github.com/peiikai/LASRecomp/blob/6244eb9d4d3497094e21cac7dabe998ded1f2800/tools/lifter.py#L680)、[代码库 ABI](https://github.com/peiikai/LASRecomp/blob/6244eb9d4d3497094e21cac7dabe998ded1f2800/runtime/gamelib.h#L27)。

它的 Linux/aarch64 版在上游仍标明实验性、没有 ARM 真机游戏验证；当前没有 Android 目标。源码有完整桌面 Vulkan 后端，但也有未实现命令、特定游戏地址和数值近似，不能据目录名视为通用完整 NVN/Maxwell 实现。

## 用户提供文档的核对

已完整阅读：`$HOME/Downloads/LASRecomp_深度调研：Switch_静态重编译架构、Android_移植方案与跨游戏通用性评估.md`。保留原文，下面是独立核对意见。

| 文档判断 | 核对结果与影响 |
|---|---|
| 离线重编译 + HLE + Vulkan 的分层，可在 PC 预编译游戏库 | 与源码相符，是本次实际实验路线；Android 库仍须 NDK/Bionic 交叉编译、正确打包和加载 |
| Android 只改 200–400 行、3–5 人日；跨游戏 70%，指令 95–100% | 未给出实际构建或覆盖率证据，不能作为承诺。D2R 初次全量生成发现 1006 处成对独占访存未支持；补齐后又实跑发现函数拆片中的跳转入口缺失 |
| ARM64 汇编零损耗、手机能满帧 | 不能推出。生成 C 的寄存器状态、分发和 GPU 兼容仍有成本；本机与 Android 均须实跑测量 |
| 清空 patches、在 loader 增加 subsdk 就能先跑 | 还须隔离 randomizer 变体、扩展游戏库 ABI、函数分发表、主机构造顺序及图形中的专属读取；未实现接口不能返回成功 |
| Android NDK 没有 execinfo.h | 现有 NDK 确实有此头，但相关 backtrace API 从 API 33 提供；目标 API 29 需要独立回溯实现或正确条件编译 |
| 不支持 mirror-clamp 时直接改 clamp-to-edge | 两种采样语义不同，不能无条件替换后宣称渲染正确；应查询所需能力并验证实际采样结果 |
| 固定地址改成某个中低地址即可 | 指令常量、重定位、跳表和运行时须一致迁移，空闲映射必须真实探测，不能假设某地址永远安全 |

## 已完成的只读研究

- [Vulkan / NVN 报告](<LOCAL_WORK>/research/LASRecomp-vulkan-review.zh-CN.md)：页代数与增量更新、提交期临时缓冲、描述符驱动成本、纹理代数与 Maxwell 数值边界。
- [内存 / 线程报告](<LOCAL_WORK>/research/LASRecomp-runtime-review.zh-CN.md)：Linux 整窗 RW 与 D2R 真实权限不同；可借鉴同线程真实等待分账；不采用人为拉长计数器以切 30 FPS 的策略。
- [Android 可移植性报告](<LOCAL_WORK>/research/LASRecomp-android-review.zh-CN.md)：Bionic、生命周期、预编译库和文件路径、Vulkan 特性及驱动加载。

优先评估有真实代数与所有权支持的增量上传，保持 D2R 原有真实栅栏和顺序。当前已在开发的 heap 读取租约早于此次调研，不能把它包装成本次移植成果；其 Android 统一构建和真机测量仍待继续。

## Linux 实验当前证据

- 三模块真实解析，映射无重叠：`main@0x7000000000`、`sdk@0x7004000000`、`subsdk0@0x7008000000`。
- 初版全量生成 157164 个函数、9518160 个 text words、911 个 C 文件。生成器保留所有未支持处的真实 PC trap；不是指令语义全部正确的证明。
- 初次 1006 处成对独占访存已补真实 64/128 位原子实现，30 个有定义 Unicorn 对拍、4 个明确的拒绝策略用例及 200000 次并发读检查通过；已记录上游 CAS monitor 的 ABA 局限。三模块重新生成 Unsupported 为 0，原 BRK / 保留 trap 未删除，不代表全部控制流都正确。
- 全部 911 个 C 文件实际编译通过，统一宿主与 178 MiB 游戏库链接 exit 0；真实日志 `build/logs/20261015T152751.952894Z-all.log`。已恢复根据源码计算的自动游戏库 ID，保留 ABI / ID 校验。
- D2R 三模块 ABI / 分发表 / loader / 原 SDK 初始化顺序已接入；同一用户提供的 RomFS 生成一个实际镜像，46 个文件的路径与长度均保留，包括隐藏的 `.build.config`，没有游戏内容哈希校验。
- NVIDIA 实测：`probe-nvidia-final/` 的实际 recorder、sampler 第127槽、GPU timestamp、有限超时 fence 与未来 fence 超时测试，以及120次真实 present 全部 exit 0；PNG 是清屏诊断，不是游戏。
- 首轮真实 D2R：`build/runs/20261015T152953.414897Z/`。RTX 4060、ABI 2 的真实游戏库加载成功，完成 `_init_libc0 → nnosInitialize → _init_libc1 → nninitInitializeSdkModule → nninitStartup → _init_libc2`，设堆3726 MiB，进入 main 的852项构造前因 `SDK+0x568390` 不在函数表而 FATAL，实际程序 exit 1。
- 已定位该 PC 为 `__nnmusl_init_dso` 内部 switch 的合法 case。原生成器把同一原函数拆片后，只在片内解析跳表，漏掉跨片目标；正在修通用原函数上下文与目标发现，保留实际错误和原构造行为，不特判单地址或跳过构造。
- 第二轮 `build/runs/20261015T153924.319324Z/`：SDK跨片修复后48638函数，三模块161084函数、908对象复核及重链通过；真实RTX启动已越过旧入口，3.375秒后因main构造中的`main+0x1b6e17c`退出1。根因是另一个通用限制：跳表计算和间接跳转之间的初始化代码超过16条回看窗口。正在按原函数和基本块边界修复，并对三模块统一应用。没有运行到nnMain，也没有游戏FPS数据。
- 已补SelfController90/91真实返回合同：无挂起时累计时间0，返回实际未触发事件的独立copy句柄；Window10无载荷合法成功。真实服务加真实内核事件测试exit0，尚待下一次整机游戏启动验证。其他焦点/生命周期服务未假称已完整实现。

## 独显权限与重跑

系统 Cardwire Smart 用 eBPF 隐藏未授权进程的独显设备，因而普通 `nvidia-smi` / `/dev/nvidia0` 看似设备不存在；并非需要重装驱动。[Bazzite 官方说明](https://docs.bazzite.gg/Advanced/cardwire/)。用户已授权 GPU 访问，实际采用 `cardwire launch --gpu 1 <程序>` 给特定进程开放 RTX4060，并将 Vulkan ICD 限定为 NVIDIA。没有修改全局GPU模式或添加全设备权限。

运行入口：实验目录中 `python3 tools/run_d2r_linux.py --timeout 90`，默认独显、隔离存档、完整原始日志和真实退出码。每轮需要重新经 Cardwire 启动；进程结束后普通 shell 看不到独显，符合 Smart 模式。研究输入原文、上游 clone 和 Android 安装均保留。

停止点：第三轮统一构建`build/logs/20261015T155528.190141Z-all.log`exit0，三模块178714函数/897对象；736个变动C编译、main16case与SDK13case对拍、89147静态代码目标审查均通过。新RTX探针`probe-nvidia-observed`120present/真实NVN检查exit0。实际游戏`build/runs/20261015T155614.149730Z`1.521秒exit1，main构造阶段缺`SDK+0x53af38`，无截图/nnMain。已定位为memcpy以AND低两位界定的4项只读跳表，原生成器只认CMP界限；仅诊断、未实施第四轮修复。前两种入口问题已修但不能据此称跨游戏成熟。构建入口三模块/profile与自动源码ID校验已完整接入并检查通过。

用户指示后CPU/GPU/构建智能体已冻结全部PC源码与产物，无PC后台生成/编译/游戏进程。后续只取渲染实现中符合Android权限、代数、生命周期与真实测量前提的部分，不搬计时拉伸、静默未实现或采样式漏更新策略。
