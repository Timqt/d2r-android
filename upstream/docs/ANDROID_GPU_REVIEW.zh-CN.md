# Android GPU 实现对照与落地记录

研究日期：2026-10-14。三个智能体并行检查原项目图形后端、Android 上下文门、Eden Android/Vulkan；主智能体整合并执行真机检查。

## 源码版本

- 当前移植工作区基于 d2r-mobile `2eb5758771f110cd53082b999d88d242766610da`。已 `git fetch origin`，最新原项目引用 `origin/main` 为 `685d3ddef8bf75768fd4c51228566f2e8d2a5182`；只读取比较，没有将更新合并覆盖现有未提交修改。
- 按用户指定地址 clone Eden 最新 master 到 `<LOCAL_WORK>/research/eden`。检出 `a594c8d60ef79280adc046760561912207e16e17`，核对 `git ls-remote origin refs/heads/master` 一致。Eden 工作树未修改。
- 详细研究笔记在仓库外：`~/dm-work/research/EDEN_ANDROID_GPU_RESEARCH.zh-CN.md`、`D2R_UPSTREAM_GRAPHICS_REVIEW.md`、`ANDROID_CONTEXT_GATE_REVIEW.md`。

## 当前已经使用 GPU

当前后端在 Adreno 830 创建 Vulkan graphics + compute queue，实际执行 draw/indexed draw、compute dispatch、buffer/image copies，再经 Android surface/swapchain 显示。贴图为 device-local、optimal tiling 的真实 VkImage；已有 shader、graphics pipeline、sampler、image view、descriptor pool 及部分只读 buffer 缓存。

CPU 仍需回放 NVN 命令、解析绑定和同步客体内存。纹理解排布目前在 CPU 上做，再上传 GPU；普通 buffer copy 和可写 SSBO 的结果仍经 GPU 完成后回传客体内存。因此“GPU 渲染已启用”和“全部数据处理都已高效使用 GPU”是两个不同进度。

Eden 是完整 Switch 模拟器，处理 Tegra GPU 寄存器和内存；d2r-mobile 执行改写后的 ARM64 游戏代码并截获 NVN。可以借鉴资源生命周期和调度结构，不能将整个 Eden 后端直接插入当前 C 回放器。

## 对照得到的工程方向

| 方向 | 原项目 / Eden 的依据 | 当前 Android 差距 | 后续顺序 |
| --- | --- | --- | --- |
| 异步提交 | 原 Metal 完成回调；Eden `Flush` 交 worker，按 timeline tick 或 fence 跟踪完成 | 每批 `vkQueueSubmit` 后立刻等待；单 command/descriptor/query 上下文立即复用 | 先测等待，再建立有限在途批次及真实完成后回收；不能只删等待 |
| 通道与对象复用 | 原 Metal 延续兼容 encoder；Eden 相同 framebuffer/renderpass/extent 继续 pass，并缓存对象 | graphics pipeline 已缓存，renderpass/framebuffer/layout 仍逐 draw 创建；compute 先前逐 dispatch 创建 | compute 缓存已落地；图形对象缓存与通道合并继续做 |
| 参数/上传缓冲 | 原 Metal 4 MiB、256 B 对齐 arena；Eden stream/staging 按完成 tick 复用 | 动态 UBO 仍各占 64 KiB 并清零，快照/写回仍有 CPU 开销 | 先批内 arena，再 GPU/CPU dirty ownership；保留每 draw 的参数版本 |
| 减少重复状态处理 | 最新原项目旧 Metal 分支按 pass/resource/usage 去重；Eden 有状态追踪 | 相同资源的 descriptor/barrier 仍重复处理 | 按真实读写风险复用状态，不能直接取消 barrier |
| GPU 纹理解排布 | 原项目有 compute 解排布；Eden 有受格式/布局限制的 GPU decode/unswizzle | 当前 CPU deswizzle；尚未证明其占据稳态帧时间大头 | 先测实际上传/解排布耗时，再实现需要的格式；不能用 ASTC 开关预告 FPS |
| Adreno 驱动 | Eden 可经统一 Vulkan library handle 装载 AdrenoTools 驱动并系统回退；有 Qualcomm workaround | 当前使用系统 Vulkan，尚无统一自定义 driver dispatch | 后续受控 A/B；不以换驱动代替 NVN 语义修复 |

Eden 固定版本入口：[异步 Scheduler](https://git.eden-emu.dev/eden-emu/eden/src/commit/a594c8d60ef79280adc046760561912207e16e17/src/video_core/renderer_vulkan/vk_scheduler.cpp#L57)、[完成序号](https://git.eden-emu.dev/eden-emu/eden/src/commit/a594c8d60ef79280adc046760561912207e16e17/src/video_core/renderer_vulkan/vk_master_semaphore.cpp#L79)、[上传缓冲池](https://git.eden-emu.dev/eden-emu/eden/src/commit/a594c8d60ef79280adc046760561912207e16e17/src/video_core/renderer_vulkan/vk_staging_buffer_pool.cpp#L130)、[Android 驱动装载](https://git.eden-emu.dev/eden-emu/eden/src/commit/a594c8d60ef79280adc046760561912207e16e17/src/android/app/src/main/jni/native.cpp#L162)。

原项目当前最新变化主要是 `DM_NVN_M3` 对较老 Metal API 的适配，仍共享同一套 NVN 回放，不是新的 Android 实现。旧 Metal 分支的资源声明去重有测量依据，但其收益比例不能直接套用 Android。

## 本轮已落实的修改

1. **专用上下文入口**：原 Android 每次查询上下文借用 800 字节完整寄存器门并调用 C getter。改为直接访问 hidden ELF TLSDESC 的入口，64 字节栈；保持除输出 x16 外的全部 GPR/SIMD、NZCV、FPSR/FPCR、Android x18 与线程指针。首版自身 25 条 ARM64 指令，后续真实 PC 采样发现多数成本仍在 FPCR/FPSR 写回；最终版 31 条静态指令，通常执行 29 条，仅状态真正改变时写回 FPCR/FPSR，NZCV 总恢复。系统调用/观测 `dm_gate` 仍保留完整现场。依据 [Bionic ARM64 TLSDESC resolver](https://android.googlesource.com/platform/bionic/+/refs/heads/main/linker/arch/arm64/tlsdesc_resolver.S)，没有引入 Bionic 私有 TLS 槽偏移。
2. **compute 管线缓存**：VkComputePipeline / VkPipelineLayout 与固定 shader 同寿命复用，批次销毁不再销毁共享管线。
3. **修复 SSBO 写标志**：Ryujinx `BufferUsageFlags.Write = 1 << 0`，之前误用位 1，导致真实 `flags=1` 的可写 SSBO 被当成只读快照。改成 `&1`，fixture 同步用真实枚举值，验证连续 compute 写回再读取。
4. **隔离空资源**：未绑定采样贴图与未绑定可写图像使用两组 dummy 和不同 serial。新增实际 GPU 像素检查：compute 写空图像后，采样空贴图必须仍为零。修复前主机 Vulkan 重现失败，修复后主机与 Adreno 均通过。整数 dummy 类型兼容尚未扩展。
5. **调整脏页捕获顺序**：只读缓存/贴图上传与原 Metal 一样先保护，再取 generation，再读取，避免读后保护前的客体写入漏记。新增确定性写入窗口测试：第一次读取可观察旧值，但刚读完发生的客体写入必须使下一快照失效。临时旧顺序后端实际重现断言失败，新顺序在主机和 Adreno 通过；真实并发压力测试仍可补充。
6. **真实 GPU / 等待指标**：检查 queue timestampValidBits 和 timestampPeriod，每批 top/bottom timestamp 在 fence 真正完成后读取，按有效位处理回绕；浮层加 GPU 批次累计时间和 fence 等待。该 Adreno 支持 48 位、52.083332 ns/tick。GPU 批次区间不包括显示/vsync，也不代表硬件利用率；CPU/GPU/等待不能直接相加。

## 验证与限制

- `~/dm-work/tools/android-tests-context-conditional.log` 最终全套通过：Unicorn 真实入口机器码（20 场景、按 FPCR/FPSR 是否改变验证写入轨迹）、原指令等价、真机 dlopen 动态 TLS 冷/热路径 4 线程 × 1001 次全寄存器/线程隔离、存档、音频、信号/退出，以及真实 Adreno GPU 像素/复制/计数/缓存/可写 SSBO/写入窗口/dummy 隔离/timestamp。最终安装日志 `~/dm-work/tools/android-install-context-conditional.log`；`git diff --check` 通过。
- 真实角色选择测量：首版约2.38–2.44 FPS，GPU约121 ms/帧、fence等待约150 ms/帧；条件恢复版60帧均值2.43 FPS，GPU122.84 ms、等待151.70 ms。截图 `dm/android/build/device-debug-context-conditional.png` 显示2.51 FPS、CPU238.3ms、GPU122.8ms、等待151.1ms。整体帧率尚无明确提升，仍需通道/对象复用及安全异步；小尺寸GPU fixture不代表游戏FPS。
- 最终条件恢复版 CPU 采样11.971秒/8915样本/无丢失，报告 `~/dm-work/tools/android-cpu-profile-conditional.txt`：上下文入口用户态周期占比从首版30.93%降到11.79%；CPU查询成本确实降低，但不能直接推算墙钟帧率。首次专用入口的实际PC报告 `~/dm-work/tools/android-cpu-profile-context-pcs.txt` 排除了符号范围误归因，热点确在FPCR/FPSR恢复序列。下一步仍需减少重复TLS查询和图形提交关键路径开销。
- **3D 光照偏暗已定因并修好（2026-10-23，Mali）**：不是"可写缓冲错误"那层，也不是 HDR/LUT 链路上哪一环算错，
  而是 **LocalSize Z 折叠让着色器手算的 z 索引失配** —— 折叠只改了 `OpExecutionMode LocalSize.z`（8→4）、
  dispatch.z 同步 ×2，驱动算出的 global z 区间不变，但这些 compute **不用 `gl_GlobalInvocationID`**，
  而是自己手算 `(WorkgroupID[2] << log2(LocalSize.z)) + LocalInvocationID[2]`（移位量 3 硬编码）。
  折叠后手算 z 只落在 {0-3, 8-11, 16-19, 24-27}，**一半 z 切片永远不写**（其余越界被着色器自己的边界检查丢掉），
  写 3D 贴图的 compute（32×8×32 光照网格、32³ 调色表）因此只填一半 → 3D 世界几乎没有环境光，
  而界面在色调映射之后合成所以完全正常。
  修法：折叠时把 z 那条移位改成 `<<log2(MALI_LOCAL_Z_TARGET)`，手算 z 即等于驱动 global z（`dm/android/native/nvn_vulkan_zfold.inc`）。
  量化：营地场景区平均 sRGB **5.2 → 45.4**、纯黑像素 **68% → 0%**（Adreno 同场景 43.4）。
  结论仍是原来那句：**不能用强制提亮掩盖问题** —— 这次是找到"黑从哪一阶段产生"之后才动的 SPIR-V 索引，不是加亮度。
  详见 [ANDROID_MALI_LIMITS](ANDROID_MALI_LIMITS.zh-CN.md) 的「3D 场景全黑」一节。
- 当前完整虚拟触控、实际存档重载后移动、窗口重建和长期游玩仍待完成。本轮未运行 Eden APK、未测 Eden 下的 D2R、未安装 Turnip。
