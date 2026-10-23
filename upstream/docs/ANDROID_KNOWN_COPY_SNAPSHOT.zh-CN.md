# 完整已知复制的快照读取优化

2026-10-15。目标是减少真实CPU重复复制，不改变队列完成、GPU输出和客体内存公布时序。完整Host、新Adreno系统分发及同App UID的system/T30完整GPU回归均已通过。真实系统营地的此快路径命中为0，没有本场景性能收益。

## 行为与适用范围

旧 `snapshot_read` 先读取整段客体内存，再按队列顺序覆盖尚未公布的已知CPU复制。有多次完整覆盖时，会读入一份马上被全部覆盖的数据，并重复拷贝几个已经过时的版本。

现在只在能够证明请求最终完全来自最后一份不可变CPU复制时，直接将该份数据拷到消费者自己的快照。保留queue_lock对源记录和backing的寿命保护，仍等待生产者的真实GPU fence才向客体公布结果；不会提前改客体内存，也不会将未完成GPU输出当CPU输入。

完整覆盖快路径必须同时满足：

- 所有当前/待完成批次中，与请求重叠的复制都是已知CPU源，没有任何重叠GPU未知写、事件或计数器。
- 最新重叠复制完整包含请求；若最新只是部分覆盖，不能退回更旧的完整复制。
- 源有真实buffer、owner和mapped地址，非零偏移在post范围与实际源大小内；地址和累计字节没有溢出。

不满足时沿用原客体读取和顺序覆盖。依赖预检、GPU真等待、只读缓存失效、dirty代数和64KiB常量缓冲零padding没有改变。没有引入区间索引、部分分段合并、固定游戏资源ID或永不失效的缓存。

代码入口：

- [nvn_vulkan_async.inc](../dm/android/native/nvn_vulkan_async.inc)：完整已知复制读取helper及计数。
- [nvn_vulkan.c](../dm/android/native/nvn_vulkan.c)：仅 `snapshot_read` 接入与每60帧统计。
- [known_copy_snapshot_test.inc](../dm/android/tests/known_copy_snapshot_test.inc)：实际着色器、像素、队列门和内存公布回归。

## 验证证据

完整O3 Host Vulkan回归实际exit0，驱动为lavapipe软件设备，着色器/查询/真实队列等待均执行；这不是Adreno硬件验证。日志 `~/dm-work/tools/android-tests-known-copy-host.log`。全部已有异步门、查询读回、材质、跨mip/layer和转发回归保留，并新增：

1. 非零源切片、录制后改变原源/客体、最新完整版本：实际左红右绿像素，客体提交前保持旧值，读取/覆盖节省计数精确。
2. 后续四字节部分覆盖：拒绝旧完整版本，原顺序合并实际得到黄色。
3. 缩短实际源范围、空目标和地址溢出：拒绝读取且目标保持原字节；此项为明确的负例边界检查。
4. 真实timeline门阻塞生产者：源在fence前未释放或入缓存，跨批消费者读到不可变绿值，客体尚未公布，最终GPU绿像素。
5. 重叠事件/计数器：拒绝完整CPU快路径，真正提交和等待门后才读取已公布的事件黑像素/真实计数器字节。
6. CPU已知seed后真正GPU计算+7：拒绝旧CPU源，真等后读回red255，不能误取旧red248。

复现编译/运行（项目根目录、Linux Vulkan开发库及已有GLSL fixture缓存）：

```bash
cc -O3 -fno-strict-aliasing -fno-omit-frame-pointer -g -pthread \
  -I <LOCAL_WORK>/tools/vulkan-headers \
  -I dm/host -I dm/android/native -I dm/android/native/compat -I dm/gfx/nvn/include \
  dm/android/tests/vulkan_test.c dm/android/native/legacy_digest.c \
  dm/gfx/nvn/nvn_tex.c dm/gfx/tex/nvn_deswizzle.c \
  -l:libvulkan.so.1 -lm -o dm/android/build/vulkan-test-known-copy-host
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json \
  DM_VK_SHADERS="$PWD/dm/android/build/gpu-test-shaders" \
  dm/android/build/vulkan-test-known-copy-host
```

Android完整fixture同样包含这些检查，由现有 `tools/test.py` 或新APK的诊断组件从当前源码构建。真机新系统动态分发完整回归 `~/dm-work/tools/android-tests-unified-dispatch-system.log` 实际exit0。同App UID的system与strict T30完整suite也实际exit0，六项新增检查及原查询/材质/等待检查全部执行：`dm/android/build/gpu-fixtures/{system,turnip}-libdmgputestprobe.so.device.log`。T30身份为driver18/PurpleVK26.3.0、subgroup128；此独立GLSL测试并不验证游戏Maxwell缓存。

## 性能计量

`[vulkan-known-copy]` 分开记录命中数、避免的客体读取字节、避免的重复覆盖写入字节及仍执行的最终复制字节。原逻辑 `snapshot_bytes` 不减报，因为最终数据仍需进入消费者快照/GPU。

旧CPU剖析只用于选择研究方向。新的系统驱动、full64、1600×900营地采集在 `~/dm-work/research/android-camp-unified-system-900p`：真实坐标4009,4929，截图20.5FPS/48.8ms，CPU24.4ms/GPU23.1ms/等待12.5ms；稳态仍约19–21FPS。`vulkan-known-copy` 正常营地命中约0，诊断帧仅约0.02MiB省读，不能给本优化归功。当前60FPS目标仍未达成。
