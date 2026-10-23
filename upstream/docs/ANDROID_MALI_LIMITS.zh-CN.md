# Mali GPU（含 Immortalis）上的 Android 端口：实测限制与当前进展

本页只写**在真机上实测到**的事实和端口里对应的处理，不含推测。目标设备：
小米 25079RPDCC 平板，**Mali-G925-Immortalis MC12**，Android 16（SDK 36），Vulkan 1.3.278，无 root。

## 硬件限制（探针实测，不是查资料）

用自建的 Vulkan 探针（`pack-workspace/probe/vkprops.c`、`vklimits.c`、`vkfmt.c`）直接问驱动：

| 项 | 实测值 | 后果 |
|---|---|---|
| `subgroupSize` | **16**，`EXT_subgroup_size_control` 的 `min=max=16` | 数据包的 64 档 `host-subgroup-size.txt` 必须改成 16，否则 `shader_profile_check()` 直接 die |
| BC 压缩纹理 | **所有 BC 格式 `optimalTilingFeatures = 0`** | 游戏贴图几乎全是 BC，驱动收到后在自己代码里 SIGSEGV |
| GPU timestamp | `supported=0` | 计数器回退 CPU 时钟 |
| `maxComputeWorkGroupInvocations` | 1024（8×8×8=512 在限制内） | 所以 compute 崩溃不是超限 |
| ASTC / ETC2 | 支持（`optimalTilingFeatures = 0x1d401`） | 理论替代路径 |

**`subgroup` 档位重译是无效功**：同一着色器在 64 档与 128 档译出的 SPIR-V **逐字节相同**
（Ryujinx 生成的是运行时自适应代码，`gl_SubgroupSize` 由驱动提供）。所以换 GPU 只要改那个
`host-subgroup-size.txt` 文件，**不需要重跑着色器翻译**。

## 崩溃一：BC 贴图 → 已在端口里绕过

现象：`pc`/`lr` 都落在 `/vendor/lib64/egl/mt6991/libGLES_mali.so` 的代码段，`addr` 未映射。

处理：复用仓库里现成的 `dm/gfx/tex/nvn_bcdec.c`（BC1–BC7 全覆盖，Metal 后端老机型路径一直在用），
在 Vulkan 后端加**无 BC 通路**：

- `image_get()`：检测到设备不支持 BC 时，按解压后的格式创建 image（`bc_unpacked()`）
- `image_upload()`：反排布后逐层 `nvn_bc_decode()` 解成非压缩像素再上传
- `descriptor_view_format()`：descriptor 视图必须用同一个解压后格式，否则描述符与 image 不一致
- `host_link.py` / `build_gpu_fixtures.py` 的链接列表补上 `nvn_bcdec.c`

效果：原来的“第一张 BC 贴图就崩”变成可以跑完全部纹理登记。

> **⚠️ 本节的“3D storage image 必崩”结论已被 2026-10-23 的实验推翻，见下面「崩溃二的更正」。**
> 真正当时代码里的致病因素是**自加的诊断代码在持 `queue_lock` 时再 lock**（自锁），
> 与 3D/2D、LocalSize、subgroup 全都无关。

## 崩溃二：3D storage image 的 compute → 判据已定位

现象：`vkCreateComputePipelines()` 内部 SIGSEGV，`pc`/`lr` 都在 `libGLES_mali.so`，
`addr=0x18`（偶见 `0x8`/`0x10`）。驱动**不返回错误**，进程直接死。

**判据是 `IMAGE` 绑定的维数，不是 LocalSize**（一度误判过，已用单变量排除）：

| shader | image 绑定 | LocalSize | 结果 |
|---|---|---|---|
| `a1217a07d94c1184` | IMAGE `type=3`（2D） | `8 8 1` | **建出** |
| `da5e3bfe00b88521` | 无 image（只有 SSBO） | `64 1 1` | 建出（后续另有问题） |
| `890de4b2c99eb9ba` | IMAGE `type=4`（**3D**） | `8 8 8` | 崩 |
| `552f45c348a61b86` | IMAGE `type=4`（**3D**） | `8 8 8` | 崩 |

`kind==4 && (type&255)==4` 即「3D storage image」，见 `shader_binds_3d_storage_image()`。

已逐个排除的假因（都是单变量实验，不是推测）：

- **不是 LocalSize**：`8 8 8`→`8 8 4`→`8 8 2`→`8 8 1`→`1 1 1` 全都崩；`64 1 1` 反而能建。
- **不是 Z 坐标语义**：按 `GlobalInvocationId.z = WorkgroupId.z*LocalSize.z + LocalInvocationId.z` 做了正确折叠
  （新增独立常量把 `Shl` 的位移量改成 0，`spirv-val` exit=0），仍崩。
- **不是 dynamic uniform**：`static_uniforms 1` 强制全静态仍崩。
- **不是 subgroup**：能建出的 `a1217a07d` 与崩掉的 `890de4b2` capability 完全相同
  （`GroupNonUniformBallot/Shuffle/Vote`），`OpGroupNonUniform*` 数量都是 0。
- **不是 workgroup 超限**：设备 `maxComputeWorkGroupInvocations=1024`，`8×8×8=512` 在限内。
- **不是 descriptor 数量**：都只有 5 个绑定。
- **不是 2D/3D 声明本身**：只把 `Dim` 从 3D 改成 2D、或把 `sampled=2` 改成 1，都仍崩——
  因为端口侧 `binding.type` 还是 4，两边不一致；判据必须用绑定的 `type`。

当前处理（`shader_binds_3d_storage_image()` 命中就**不建管线、不派发**）：
进程**零崩溃**并继续推进到 1792×1792 纹理 clear 与后续 compute。
代价是这些 compute 的目标 3D 贴图保留清空内容——**画面会不正确，但不崩**。

## 崩溃三：跳过之后的下一个卡点

跳过上面两个之后，`da5e3bfe00b88521`（`LocalSize 64 1 1`，只有 SSBO，无 image）会停在建管线，
进程随后静默退出（无 CRASH 记录）。它单独跑是能建出的，所以怀疑是：

- 它读取被跳过的 compute 产出的 3D 贴图，形成等待；或
- Mali 在被跳过的状态之后行为异常。

把它的 `LocalSize` 从 `64 1 1` 改成 `8 1 1` 仍然卡住，所以不是 LocalSize X 超过 subgroup(16) 的问题。

## 已排除的整体方向

- **重译着色器到 16 档是无效功**：同一 shader 在 64 档与 128 档译出的 SPIR-V **逐字节相同**
  （Ryujinx 生成运行时自适应代码）。换 GPU 只要改数据包里的 `host-subgroup-size.txt`。
- **改 LocalSize 不能绕过**：见上表。
- **把 3D 改成 2D 不能绕过**：判据在端口的 `binding.type`，改 shader 单边无效。


## 崩溃三：`Surface::setFrameRate` 的 binder 路径

```
Surface::hook_perform → Surface::perform → Surface::setFrameRate
  → BufferQueueProducer::setFrameRate → BLASTBufferQueue::setFrameRate
    → SurfaceComposerClient::Transaction::apply → BpBinder::transact   SIGSEGV (addr=0x10)
```

端口主动请求系统帧率时，向 SurfaceFlinger 提交事务崩。用 `$DM_TEMP/no_frame_rate` 标记可关掉。

## 诊断开关一览

`$DM_TEMP/perf.conf`（`键 值` 每行一条）：

| 键 | 缺省 | 作用 |
|---|---|---|
| `trace_dispatch` | 1 | compute 派发的分段落点（定位“卡在哪一步”） |
| `write_trace` | 1 | 宿主写回客体的轨迹，崩溃时打印 |
| `local_z_fold` | 1 | 是否把 `LocalSize Z>4` 折到 4。**Mali 上必须开**（关掉必现 `addr=0x18` 崩溃） |
| `skip_folded_compute` | 0 | 跳过 3D storage image 的 compute（**诊断用，结果不正确**；已收在 Mali 判断下） |
| `mali_skip_3d_compute` | 1 | 同上，另一条策略开关；**已收在 Mali 判断下**，Adreno 不触发 |
| `quiesce_before_pipeline` | 0 | 建 compute 管线前 `vkQueueWaitIdle`。**注意**：曾在持 `queue_lock` 时重复加锁导致全进程自锁，已修 |
| `pso_warm` | 1 | 后台 PSO 预热线程；关掉可排除并发建管线 |
| `static_uniforms` | 0 | 强制所有 uniform 静态，验证 dynamic uniform 是否是崩溃源 |
| `read_only_layout` / `flush_draws` | — | 性能相关（见 ANDROID_FPS 文档） |

**排查"卡死 vs 崩溃"的关键判据**：进程还活着但日志不动、且
`/proc/<pid>/task/*/wchan` 全为 0、状态全 `S` → **futex 等锁（自锁）**，不是驱动崩溃。
这时候先找刚加进去的"持锁调用"，别去怀疑驱动。


`$DM_TEMP` 下的标记文件（存在即生效）：

- `audio_off`：音频走静音接收端（仍按实时消耗缓冲，只去掉声卡通路）
- `no_frame_rate`：不向系统请求帧率

## 调试手法（这次真正有用的）

1. **崩溃处理器读 `/proc/self/maps`**：把 `pc`/`lr`/`sp`/`addr` 各自落到哪个映射打出来。
   一条 `pc=… ∈ …libGLES_mali.so` 就把“guest 跳飞”和“驱动内部崩溃”彻底分开——这是本轮定位的转折点。
2. **寄存器里的 ASCII**：`x5=0x7367616c662e656c` 按小端读是 `le.flags`，说明驱动在解析元数据。
3. **分段落点**：驱动崩溃时没有栈，只有“走到哪一步”能说明是哪次调用炸的。
4. **崩溃前必须清 `files/pipeline-cache/`**：残留状态会让 Mali 行为在“崩”和“挂死”之间随机切换，
   同一个包一次成功一次失败。做 A/B 前先清。

## 崩溃二的更正（2026-10-23 实测，推翻上面三条结论）

上面的「3D storage image 必崩」是**错的**，错在把「端口上下文里的行为」当成了「着色器特征」。

**实验**：写独立最小程序 `pack-workspace/probe/portrepro2.c`，用真实 `890de4b2` 的 SPIR-V，
把端口"额外做的事"逐个加进来，九组配置**全部 `RETURNED 0 *** CREATED ***`**：

| 变量 | 取值 |
|---|---|
| SPIR-V | 原始 Z=8 / 折叠 Z=4 / 折叠 Z=1（三种都试） |
| `EXT_subgroup_size_control` | 设备开 / 不开 |
| `requiredSubgroupSize` | 16 / 不设 |
| binding `stageFlags` | 仅 COMPUTE / VERTEX\|FRAGMENT\|COMPUTE |
| pipeline cache | 有 / 无 |

**所以这些全都不是原因**：3D image 维数、LocalSize Z、required subgroup size、stageFlags、cache。

**真正的致病因素**：当时 `perf.conf` 里有我自己为定位加的
`quiesce_before_pipeline 1`，它在 `dispatch_compute()` 里做：

```c
graphics_pass_end();
pthread_mutex_lock(&queue_lock);            // ← 录制线程已经持有这把非递归锁
VkResult q = vkQueueWaitIdle(queue);
pthread_mutex_unlock(&queue_lock);
```

录制线程本来就已经持 `queue_lock`（见 `nvn_vulkan_guest_publish.inc` 的注释），
对同一把**非递归**互斥锁再加锁 → **全进程自锁**。表现是：

- 所有线程都是 `S` 状态、`wchan=0`（典型 futex 等待），进程不退出；
- 日志断在 `<shader> dependencies done pipeline=0x0` 之后，
  **连 `[vulkan-pipeline-dump]` 都没打印**——因为它就在下一行；
- 同一份配置有时像"崩"、有时像"挂死"，因为管线缓存残留会让它落在不同阶段。

修法：去掉那次重复加锁（录制线程已持锁，直接 `vkQueueWaitIdle` 即可），
并把 `quiesce_before_pipeline` 缺省置 0。

### 顺带修正：`local_z_fold` **确实是必需的**

关掉折叠（`local_z_fold 0`）仍然复现标志性崩溃
（`addr=0x18`、`x5=0x7367616c662e656c` = ASCII `le.flags`）；
开启折叠则 `NO_CRASH` 并推进到 `frame>1380`。
所以折叠保留，且同样收在 `gpu_has_mali_local_z_limit()` 设备判断下。

### 顺带修正：`shader_binds_3d_storage_image()` 原本会误伤手机

它当时**没有设备判断**，`mali_skip_3d_compute` 缺省又是 1，
于是 Adreno 上也会命中跳过分支——**画质直接不对**。已改为先判 `gpu_has_mali_local_z_limit()`。

## 当前状态（2026-10-23）

装 **v0.6.57**，Mali-G925 上：**出画面、无崩溃**，并且**真实营地场景已跑通**。

| 指标 | 实测 |
|---|---|
| 首帧 | `frame` 推进到 **1380+**（此前一直卡在 `frame=0`） |
| 崩溃 | `NO_CRASH`（`files/crash.txt` 不存在） |
| 帧率 | **22.7–24.6 fps**（角色选择界面） |
| GPU 时间 | 24–25 ms/帧 |
| fence_wait | 11.5–13.4 ms/帧 |
| CPU | `draw` 16.6 ms / 1360 calls，`pipe` 命中缓存后 0.15 ms |
| **营地场景（1600×900）** | **画面正常**（此前 3D 世界近乎全黑，已修，见下节） |

**曾被认为必崩的 compute 全部建出并派发**：`890de4b2`（Z 折叠 2×，dispatch 4,4,8）、
`552f45c3`、`da5e3bfe`、`4639dd6d`、`098a126f`、`6a7a308d`、`b54d192f`。

画面已验证：启动器、英雄职业选择、游戏模式选择、角色命名四个界面渲染正常，
3008×1880 输出、中文字形正确、色彩正常（启动器金翼/蓝天完全正常，说明输出色彩无偏移）。

> **⚠️ 上面那句"游戏内暗调是场景本身氛围光"已被 2026-10-23 推翻**：那是只看到角色选择/命名
> 界面时的误判。真正跑进**营地**之后，3D 世界近乎全黑（场景区平均 sRGB 5.2、68% 像素纯黑），
> 而界面/血球/物品栏正常 —— 根因见下面「3D 场景全黑：LocalSize Z 折叠让手算 z 索引失配」。

### 仍未做完

1. **角色创建输入**：游戏要按 A 才唤起软键盘，`adb input text` 直接打不进去。
   属于交互细节，不影响渲染验证。
2. `libhwui.so` 的 UI 线程曾出现一次 `pc=0` 空指针（`android::Thread::_threadLoop`），
   属 Android 系统 UI 层，与本端口 Vulkan 路径无关，未复现。

---

## 3D 场景全黑：LocalSize Z 折叠让手算 z 索引失配（2026-10-23 已修）

### 现象

营地/战斗场景里 **3D 世界近乎全黑，界面、血球、物品栏完全正常**。量化（场景区 30%~70% × 16%~62%，
线性亮度）：

| | 平均 sRGB | 线性均值 | p50 | 纯黑像素 |
| --- | --- | --- | --- | --- |
| 修复前 | 5.2 | 0.0037 | 0.0 | **68.1%** |
| 修复后 | **45.4** | 0.0322 | 0.0222 | **0.0%** |
| Adreno 手机同场景参考 | 43.4 | 0.0302 | 0.0203 | 0.3% |

**不是分辨率问题、不是游戏内时间、也不是"被跳过的 3D storage image compute"**：
`552f45c348a61b86`（32³ 调色表）实测**已建出并派发**（dispatch=4,4,4），
Adreno 上同一场景同代码正常。

### 根因

为绕开 Mali 建 compute 管线时的崩溃，端口把 compute 的 `OpExecutionMode LocalSize.z` 从 8 折到 4、
dispatch.z 同乘 2。原来的论证是：

> `GlobalInvocationId.z = WorkgroupId.z * LocalSize.z + LocalInvocationId.z`，
> 只要 dispatch.z 与 LocalSize.z 的乘积不变，gz 覆盖的区间就完全不变，Z 维语义天然等价。

**这条论证只对用 `gl_GlobalInvocationID` 的着色器成立。** 实际着色器是**手算**索引：

```glsl
%60 = OpShiftLeftLogical %int %58 %int_3     // gz << 3   ← 移位量 3 硬编码
%82 = OpIAdd %int %78 %81                    // + lz
%87 = OpUGreaterThanEqual %bool %85 %uint_32 // z >= 32 → return
```

折叠后 `lz∈[0,4)`、`gz∈[0,8)` ⇒ 手算 z = gz*8+lz 只落在 **{0-3, 8-11, 16-19, 24-27}**：
一半 z 切片**永远不会被写**，其余越界被着色器自己的边界检查丢掉。于是写 3D 贴图的 compute 只填一半 ——
**32×8×32 光照网格**（`1188eaed`，dispatch 4,8,4；片元每帧采样约 1882 次）与 **32³ 调色表**
（`552f45c3`）都半空 → 3D 世界几乎没有环境光。界面在色调映射之后才合成，所以**完全不受影响**。

### 修法

折叠时把 z 那条移位从 `<<log2(Z)` 改成 `<<log2(MALI_LOCAL_Z_TARGET)`，手算 z 即等于驱动的
global z = gz*4+lz ∈ [0,32)，每个切片恰好写一次，**等价于没有折叠**。x/y 的 LocalSize 没变，移位不动。

新增 `dm/android/native/nvn_vulkan_zfold.inc`，在 `shader_fold_local_z()` 里调用；
独立测试 `dm/android/tests/zfold_test.c`（宿主可跑，3/3 PASS）。设备日志会打：

```
[vulkan] LocalSize fold: hand-computed Z index shift <<3 changed to <<2 (matches the driver global z)
```

**两个坑**：

- `gl_WorkGroupId` 必须按 **BuiltIn 装饰**（`OpDecorate %var BuiltIn 26`）认，
  **不能靠 OpEntryPoint 接口列表的位置** —— 不同着色器里 WorkgroupId/LocalInvocationId 的先后不一致
  （`2988a1dc` 的接口列表是反的，靠位置会认错变量）。
- 会被折叠的 compute 共 6 个（用数据包 `*.res.json` 的 `local` 字段筛 `z>4`）：
  `1188eaed`(8,1,8)、`2988a1dc`、`552f45c3`、`73a87ffb`、`890de4b2`、`de7267a5`，
  全是 (8,8,8)/(8,1,8)。**只修到 4 个会漏**：那两个恰好在营地场景没派发，别的地方就会用到。

### 怎么定位的（可复用）

1. **先找同项目其它设备的同场景历史截图当基线**（工作区 `pack-workspace/perf-20261022/` 就有），
   一比就分出"设备差异"和"时间回归" —— 这一步直接推翻了"用户改分辨率导致"的猜测。
2. **反汇编真实的 SPIR-V**（`spirv-dis`，NDK 自带），别信代码注释里的等价性论证。
3. **零构建 A/B**：着色器是运行时从数据包读的，包不做逐文件摘要校验、字节数不变就不会被察觉，
   直接替换设备上的 `.spv` 就能验证假设（正式修法仍要落到源码）。
4. 设备日志 + `dumpsys` + `screencap` 三个加起来才是完整证据：日志能证明 compute 被派发、
   `WindowInitialize` 能证明交换链尺寸、截图能证明画面。


