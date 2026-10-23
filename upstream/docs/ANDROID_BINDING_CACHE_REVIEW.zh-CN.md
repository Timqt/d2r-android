# Android uniform 与 descriptor 缓存记录

日期：2026-10-14。此记录补充长期移植进度，不包含游戏内容、密钥或生成的 shader。

## 已有证据

- 第一轮 uniform arena 与 render-pass 复用通过 Adreno 真实像素回归后，同一角色选择场景由约 2.4 FPS 提升至约 7.1 FPS；NVN 提交 CPU 约 83 ms、GPU 约 32 ms，约 12 次提交、51 个 pass。仍未达到 60 FPS。
- 角色选择日志实际加载 77 个 shader：46 个 FS、20 个 VS、11 个 CS；营地日志加载 114 个：69 个 FS、32 个 VS、13 个 CS。大型片元 shader 最多有 35 个 descriptor binding。
- 动态 UBO 反射主要涉及 FS 的 slot 3/4/5/6/7/9/13、VS 的 slot 3/6/7/13。这个统计是已加载 shader 的反射数量，**不是每帧绘制频率或内容不变率**。
- 最新 CPU 用户态采样中 `process_vm_readv` 约 3.23%、`bind_shader` 约 1.83%；内核执行时间没有包含在用户态 cycles 中，不能仅据此认定 UBO 读取无关紧要。

## 参考 Eden 的实际做法

`src/video_core/buffer_cache/buffer_cache.h` 的 `BindHostGraphicsUniformBuffer()` 区分小 uniform 的映射流式上传和大缓冲的同步缓存路径；判断还涉及 GPU 修改与 offset 对齐。

`src/video_core/renderer_vulkan/vk_graphics_pipeline.cpp` 的 `ConfigureDraw()` 保存 `last_descriptor_payload`，比较实际 descriptor 内容，并在不同 descriptor 路径下避免重复分配或更新。它仍然执行纹理反馈、缓冲同步和 pass 调度；descriptor 相同不表示内容没有变化。

当前 Android 移植采用已有 Vulkan 1.1 descriptor-set 路径，不假定其它 descriptor 扩展可用。

## 本轮实现

`dm/android/native/nvn_vulkan_binding_cache.inc` 提供批内缓存，统一在 `descriptor_reset()` 调用点清理索引；调用时上一批 GPU 已真正完成。

1. UBO 查找键为客体地址与真实绑定大小。命中还需范围仍受跟踪、页代数未改变、最新重叠的已知 pending-copy 节点未改变。首次读取先保护页，再取代数，再读取。
2. 已知 pending-copy 的源快照在本批中保持不变。新重叠复制形成新版本，部分复制仍按原命令顺序覆盖；客体内存继续在完成后发布。`source == NULL` 的当前 GPU 输出不会被当成已完成复制。
3. 未跟踪内存每次重新读取；稳定的地址本身不能证明模型矩阵未更新。64 KiB descriptor range 的零 padding 与 arena 数据版本继续保留。
4. 先完成每次 image upload/access 风险处理，再根据实际 `VkBuffer`/offset/range、`VkImageView`/sampler/layout、`VkBufferView` 和 set-layout 比较 descriptor payload。相同 payload 可以复用 set，不能跳过图像同步。
5. payload 逐字段编码，避免把结构体 padding 当内容；hash 再混合高位，防止 64 KiB 对齐的 arena offset 全部撞入同一个桶。碰撞后仍比较全部字段。
6. CPU 缓存节点按 64 项分块保留，完成后重用，避免把每次 GPU 缓冲分配替换成新的每 draw CPU 分配。

累计统计变量为 `uniform_version_hits/misses/untracked/read_bytes` 和 `descriptor_version_hits/misses`。需结合实际 FPS 与 NVN CPU 时间评价收益，而不能把单独命中次数当性能结论。

## 回归覆盖与当前验证状态

独立 `dm/android/tests/binding_cache_test.inc` 使用现有无游戏内容的 shader fixture，覆盖真实像素：稳定 uniform 命中、CPU 脏写、待发布完整与部分复制、真实大小缩小后零 padding、未跟踪 CPU 更新、读取刚结束后的写入竞争、同逻辑 ID 重新登记到不同图像、set 命中但图像内容变更后仍正确同步。

当前完整测试翻译单元已以 Android ARM64 `-O3 -fno-strict-aliasing -fno-omit-frame-pointer` 编译和链接通过。此轮缓存的 Adreno 运行结果与性能数据等待主代理验证；不得据离线编译宣称实机通过或帧率提升。

真机补充：完整Adreno像素/dirty/已知复制/descriptor风险回归已通过。实际游戏每帧UBO hits/miss=4061/1199、descriptor=847/1203、UBO读取1.27MiB；目前三批真实异步后的同场景稳态约11–12FPS，未达60FPS。
