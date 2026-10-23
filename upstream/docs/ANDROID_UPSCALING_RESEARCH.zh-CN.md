# FSR 超分调研（Eden 的做法与移动端可用实现）

2026-10-17。用户提出「Eden 似乎有 FSR 超分提升画质的能力，调研下」。本文给出：Eden 到底用的是什么、接在管线哪里、许可能不能用来改我们的代码、公开的实测开销、以及对本移植项目（Android + Vulkan + Adreno 830）可以怎么用。**本轮只做调研：没有改任何渲染代码，也没有在真机上测过任何 FSR 实现。** 下面凡是没有一手证据的地方都明确标注为「未验证」或「推测」。

## 一句话结论

Eden 用的是 **AMD FSR 1.0**（EASU 上采样 + RCAS 锐化，纯空间、无时域信息），挂在**出屏前的最后一步当「缩放滤镜」**，倍率由「内部渲染分辨率设置」隐式决定，只暴露一个锐度滑条。对我们最有价值的不是它的代码（Eden 的胶水是 GPL-3.0，不能并入本 MIT 项目），而是这个**位置**：它正好就是我们现在做最后那次缩放的地方 —— 我们用 `VK_FILTER_LINEAR` 把 1600×900 的游戏画面**双线性拉大到 3392×2400 的面板**（见下第 5 节，有真机日志）。也就是说：不提高渲染分辨率、只把这一步的「双线性」换成「边缘自适应上采样 + 轻锐化」，就有可能在**同样的渲染代价**下让画面明显更清楚。代价估计 ~1–2 ms/帧（别人的手机 GPU 实测，**我们没有在 Adreno 上测过**）。

## 1. Eden 里到底是什么（本地源码核对）

本地检出：`~/dm-work/research/eden`（`master`，HEAD `a594c8d6`，与 `docs/ANDROID_GPU_REVIEW.zh-CN.md` 记录的固定版本一致）。

- **版本**：`externals/FidelityFX-FSR/ffx-fsr/ffx_fsr1.h` 头部写着 `AMD FidelityFX SUPER RESOLUTION [FSR 1] ::: SPATIAL SCALING & EXTRAS - v1.20210629`，同一文件注明两段式：`[EASU] Edge Adaptive Spatial Upsampling`（1x–4x 面积范围）与 `[RCAS] Robust Contrast Adaptive Sharpening`（不改变尺寸，必须作为独立 pass 在 EASU 之后）。
- **接入位置**：出屏（present）阶段的「缩放滤镜」，是 `ScalingFilter` 枚举的一个取值（默认 `NearestNeighbor`，即**默认关闭**）。它在 FXAA/SMAA 与可选 ReShade 之后、窗口适配之前执行：`present/layer.cpp` 里 `if (ScalingFilter == Fsr) fsr->Draw(...)`；`present/fsr.cpp` 里是两个独立的 fragment render pass（全屏三角形），中间图是 `VK_FORMAT_R16G16B16A16_SFLOAT`，设备支持 FP16 时走 fp16 shader（Adreno 支持，所以手机上走 fp16 路径）。**不是 compute，不用 subgroup。**
- **参数只有一个**：`fsr_sharpening_slider`（0–200，Android 默认 **0**、桌面默认 25），直接喂给 `FsrRcasCon`。**没有** Ultra Quality / Quality / Balanced / Performance 这种档位下拉；倍率 = 输出尺寸 ÷ 输入尺寸，而输入 = 游戏 framebuffer × `resolution_setup` 倍率。所以 Eden 的「画质档位」实际是「分辨率设置 + 锐度」两个旋钮。
- **许可证分层（决定我们能拿什么）**：AMD 头文件是 **MIT**（`externals/FidelityFX-FSR/license.txt`，`.reuse/dep5` 也标 MIT）；Eden 自己写的胶水 —— 常量生成函数的重写（`src/video_core/fsr.cpp`）、滤镜实现（`src/video_core/renderer_vulkan/present/fsr.cpp`）与那批 `*_frag` / 预编译 `*_spv.h` —— 是 **GPL-3.0-or-later**（叠加 yuzu 的 GPL-2.0）。
- **顺带澄清两点**：Eden 里**没有 FSR 2/3 超分**（FSR 2 是时域超分，需要运动矢量/深度/抖动，Eden 没有）；它的帧生成是 **LSFG（Lossless Scaling）**，与 FSR 3 帧生成无关。Eden 官方还提供了 **SGSR / SGSR Edge**（高通骁龙超分），文档里写明「为 Adreno 设备优化」，而 FSR 的缺点被写成 "somewhat slow, and may be offputtingly sharp"（有点慢、可能锐得过头）。

## 2. 许可：哪些能用、哪些不能用

- **不能用**：直接拷 Eden 的 `fsr.cpp` / `present/fsr.cpp` / 预编译 shader → 引入 GPL-3.0，与我们的 MIT 项目不兼容（这与 `docs/ANDROID_PORT_PROGRESS.zh-CN.md` 里「Eden 的着色器翻译是 GPL-3.0，不能并入本 MIT 项目」的判断一致）。
- **能用**（三类都是可再分发的宽松许可）：
  1. AMD 官方 FSR 1.0 实现（**MIT**，GPUOpen；头文件 + 常量函数 + 参考 shader）；
  2. 手机优化版（**MIT**，atyuwen 的 gist 与说明文：单 pass 合并 EASU/RCAS、fp16、early-quit）；
  3. 高通 SGSR 着色器（**BSD-3-Clause**，Qualcomm；Adreno 上官方称比其它移动超分方案快约 2 倍）。

## 3. 开销（公开实测，注意不是在 Adreno 上）

- atyuwen 的手机实测（iPhone 12，A14 GPU；1772×819 → 2532×1170）：原版 FSR 1.0 像素着色器实现 **EASU 5.4 ms + RCAS 0.9 ms ≈ 6.3 ms**，作者原话是「absolutely unacceptable」；优化后（fp16 + 去掉 deringing + 简化分析 + 双线性采样 early-quit + EASU/RCAS 合成单 pass）**EASU 1.8 + RCAS 0.9 = 2.7 ms**，扣掉本来也要做的「离屏图 → backbuffer」拷贝约 0.7 ms，**净开销 ≈ 2.0 ms**。
- 高通的官方口径：SGSR 是**单 pass**、只用 15 条纹理指令，比「其它移动超分方案」快约 2 倍，并点名「其它方案至少要对输入图跑两遍」（正是 FSR 的 EASU+RCAS 两 pass）。
- **未验证**：没有任何一手数据是「FSR 1.0 在 Adreno 上的毫秒数」；我们自己在 Adreno 830 上也**没有测过**。上面两条只能作为量级参考。

## 4. 画质：文字/UI 是这类方案的弱项

- 高通 SGSR 官方 README 明确建议：**2D UI 应尽量按设备原生分辨率渲染再叠加**，因为上采样伪影在 2D UI 上更明显，而且在 UI 上做超分省不了多少性能。
- Eden 官方把 FSR 的缺点写成 "may be offputtingly sharp"（可能锐得过头）。
- 机制上（原理，非实测）：FSR 1.0 不区分 3D 与 UI，而文字是高频小尺寸特征，拉伸核容易把它糊掉、RCAS 再锐化容易在文字边缘产生过冲。

## 5. 对我们项目的意义（现状对照，含真机证据）

我们这条链路上**已经有**「降内部比例 + 放大回输出」的全部零件，只是最后一跳用的是最朴素的双线性：

1. 游戏把 3D 目标按 `DM_3D_SCALE_DYN` 的比例渲染（缺省 1.0；掉帧时控制器降到 0.72/0.70），再用游戏自带的 `ps_upsample_p` 放大到「出图分辨率」；
2. UI 层本来就按「出图分辨率」渲染（所以 UI 与 3D 的内部比例无关）；
3. **最后**，我们自己的 `mtl_present` 用 `vkCmdBlitImage(..., VK_FILTER_LINEAR)` 把这张「出图分辨率」的图**双线性拉伸到交换链尺寸**。

真机日志证据：游戏出图 `WindowInitialize … 首张 1600x900`，而我们创建的表面是 `Android game surface 3392x2400`，面板 `wm size` = 2400×3392。blit 的缩放比例是 `min(输出宽/输入宽, 输出高/输入高)`：`min(3392/1600, 2400/900) = 2.12`，所以画面**宽度撑满、上下各留约 246 像素黑边**（游戏画面实际占 3392×1908），截图已核对。也就是说：**用户现在看到的画面，是把 1600×900 以 2.12 倍双线性放大的结果** —— 这也是为什么文字看起来偏软。

由此得到两条互相独立的画质杠杆：

- **杠杆一（已上线）**：把 `DM_GAME_RES` 提高，让整张图（含 UI）更接近面板原生分辨率。本轮营地实测：1600×900 → 1920×1080 基本免费，2560×1440 约 −2 FPS 且 fence_wait 从 0.81 涨到 2.15 ms；再往上就会越过 16.7 ms 预算而触发 3D 内部比例降档。
- **杠杆二（本文建议、尚未实施）**：保持 `DM_GAME_RES` 不变，把第 3 步的 **`VK_FILTER_LINEAR` 换成 EASU+RCAS 这一类的空间超分**。它正好落在 Eden 放 FSR 的位置（输入=游戏最终帧、输出=显示区域），**完全不用碰游戏的 shader / render graph**，做成我们 present 路径里的一个可开关滤镜即可。按第 3 节的量级估计净开销 ~1–2 ms/帧，而我们的输入更小（1280×720 或 1600×900 → 3392×2400，比 atyuwen 测的 1772×819 → 2532×1170 略大）。

**实施前必须先算清/验清的（未验证项，不要当成已成立）**：

- Adreno 830 上的真实开销：我们在 60 FPS 预算下只剩几毫秒余量（营地已经 56–58 FPS），2 ms 不是小数目。
- 我们的 blit 目前是「一次 blit 直接写交换链图像」；EASU 需要能采样输入纹理、RCAS 需要一张中间 target，所以要先渲染到我们自己的中间图（`R16G16B16A16_SFLOAT`）再发到交换链 —— 这会多一次离屏往返，链路要重新走通（`crop`、`screen_extent`、坐标/翻转都要重新对）。
- 与 `DM_3D_SCALE_DYN` 的交互：它降的是游戏内部 3D 目标的比例、且在游戏内部就被放大回去了，所以超分作用在**最终合成帧**上，与它并不冲突，但也**不能**替代它（3D 被降档时，超分只能把那张已经糊了的图锐化一点）。
- 文字/UI 的实际观感：SGSR 的建议是「UI 走原生分辨率」，而我们的 UI 与 3D 在最后一步已经合成在一起了，无法只对 3D 做超分。因此**必须用真实截图对照**（同一张营地截图，1600×900 双线性 vs EASU+RCAS，看文字/远处物体），不能只凭「更锐=更好」推断。

## 6. 建议的下一步（如果要试）

1. **先离线**：用 MIT 的手机优化版实现（单 pass、fp16），拿同一张 1600×900 的营地截图离线放大到 3392×2400，与现用的双线性并排比对文字、UI 边框、远处植被。离线不用上真机，几分钟就能看出值不值得。
2. **再上机 A/B**：接成 present 路径上的开关，同一位置同一走位，对比 FPS / `vulkan-time GPU` / fence_wait 与截图；把超分开销明确记进帧预算，不与其他诊断混算。
3. **最后才谈产品化**：只有当实测净开销在 1.5–2 ms 以内、且画面确有改善时，才考虑做成启动界面上的一个开关（沿用现在「只有一个锐度滑条」的克制设计，档位交给已有的分辨率档）。
4. 若离线对照发现「锐化后文字更差」，那么结论就是**走杠杆一**（老老实实提高出图分辨率），并把这条调研结论写在进度文档里，避免下一个人重复试。

## 参考

- 本机 Eden 检出：`~/dm-work/research/eden`（`master`，`a594c8d6`）；相关文件：`externals/FidelityFX-FSR/{ffx-fsr/ffx_fsr1.h,license.txt}`、`src/video_core/fsr.cpp`、`src/video_core/renderer_vulkan/present/{fsr.cpp,layer.cpp}`、`src/video_core/host_shaders/{fidelityfx_fsr.frag,vulkan_fidelityfx_fsr_*}`、`docs/user/Graphics.md`、`src/common/settings.h`、`src/common/settings_enums.h`。
- 另一个检出 `~/dm-work/research/eden-android-reference` 不是源码（只有 APK、截图、logcat 与运行配置），其中记录的 Eden Android 版本 `a594c8d60e` 与上面的 HEAD 一致。
- AMD FSR 1.0（MIT）：<https://gpuopen.com/fidelityfx-superresolution/>、<https://github.com/GPUOpen-Effects/FidelityFX-FSR>
- 手机优化版（MIT）：<https://gist.github.com/atyuwen/78d6e810e6d0f7fd4aa6207d416f2eeb>、<https://atyuwen.github.io/posts/optimizing-fsr/>
- 高通 SGSR（BSD-3-Clause）：<https://github.com/SnapdragonGameStudios/snapdragon-gsr>、<https://www.qualcomm.com/news/onq/2023/04/introducing-snapdragon-game-super-resolution>
- 社区反馈只取到了标题与搜索摘要（Reddit 正文被反爬拦截，未作为结论依据）。
