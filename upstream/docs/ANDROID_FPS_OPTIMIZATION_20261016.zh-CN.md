# Android 帧率优化：2026-10-16

**新USB手机横屏适配已装机：** V2505A/Adreno840首次资源部署完成；Activity原已横屏，修复Vulkan错误预旋转声明，实机current0x2→selectedIDENTITY0x1，3168x1440底图与HUD方向一致。控件按可用高度缩放并靠安全屏幕边缘。build exit0/11.388秒，已install-r且root看过截图，用户自行建角进入区域2野外，截图59.0FPS、120帧短窗0>33ms，横屏与触控正确；尚非长期全场景60，勿混同平板采样。证据 `<LOCAL_WORK>/research/android-phone-landscape-20261016/handoff.zh-CN.md`。以下为历史。

**最新创建诊断已实机定位移动尖峰：** build11.512秒/exit0。静止营地59.99839FPS，1800帧0次>33ms、最长28.372ms。真实摇杆移动frame7270的129.182ms慢帧中，graphics_pipeline创建占109.447ms；frame6996的88.856ms中创建70.538ms。移动窗口均59.45FPS但仍有卡顿，不能称全程稳定60。已开始设计/实施持久管线缓存，尚未构建，不声称能避免首次新状态编译。证据 `<LOCAL_WORK>/research/android-create-timing-20261016/handoff.zh-CN.md`。用户要求的新USB设备安装由new_device_install并行处理。以下为历史。

**最新实机约60FPS：** game_run补游戏类别后，OPPO从mIsGame=false变true，触摸空闲降帧暂停，物理屏幕60Hz；独立短窗实际呈现59.99810FPS。借鉴dodola增加完整key最近管线命中缓存。正常构建exit0/11.306秒并安装，4200采样截图正常。31窗均59.73516FPS（54.55–60.37）；实际1800帧5次>33.333ms、2次>50ms，最长132.286ms，主要CPU。此次用户移动，不能称同位置静止对照或全程稳定60。仍须处理移动尖峰，目标保持active。未写/运行测试。完整证据 `<LOCAL_WORK>/research/android-pipeline-game-category-20261016/handoff.zh-CN.md`。以下为历史。

**最新窗口模式版实机：** build exit0/5.471秒，安装与营地4200帧采集成功。31窗均50.07613FPS（49.86–50.30）；实际2282–4081共1800帧，2次>33.333ms，0次>50ms，最大35.984ms。截图正常，独立系统呈现短窗50.07080FPS。公开窗口模式请求及系统应用模式投票都为60，物理屏幕仍50Hz，60目标未完成；未改全局设置、未新增/执行测试。营地静止结论不代表战斗/移动全程。证据 `<LOCAL_WORK>/research/android-window-mode-20261016/handoff.zh-CN.md`。下一步排查实际显示模式选择并压CPU尖峰。以下为历史。

**当前实机异步Present：** 按原Metal入队契约恢复CPU/GPU重叠，真实纹理复用与SyncWait/Finish仍等FIFO发布完成。正常build11.217秒、安装与营地采样成功。31报告窗均50.07129FPS（49.84–50.31），1800帧0次>33.333ms，最大31.43ms；SurfaceFlinger独立短窗口126个实际呈现时间戳为50.07080FPS，画面正常。此前driver参数表版仅38.718FPS，未将其包装成提帧。仍未60；现检查Android目标刷新率声明。无新增/执行测试。证据`<LOCAL_WORK>/research/android-async-present-20261016/handoff.zh-CN.md`。以下为历史。

**当前实机版本 context-owner：** 优化线程上下文入口常用路径（实际APK机器码27→18条，保留所有权/析构/TLS fallback），同时按实际使用项重置GPU时间戳查询池。build exit0/5.451秒、安装成功、营地截图正常。FPS报告2400–4200共31窗均42.17581FPS（36.36–47.90）；稳定性实际2282–4081共1800帧，>33.333ms35次、>50ms0、最长44.283ms，按帧间隔加权42.06181FPS。起始counter-domain同聚合窗71次，经过counter-reset为43次；没有严格温度匹配，不能保证收益比例。仍有慢帧，不是稳定60。未新增/运行测试。最新CPU实采16333/lost0，context_entry7.36%（前9.41%）、resetQueryPool0.64%（前1.05%），仍需继续压CPU尖峰。代码/真实画面/指标在`<LOCAL_WORK>/research/android-context-owner-20261016/`。以下均为历史。

**当前实机版本 GPU counter domain：** build exit0/11.467秒、安装成功、营地画面正常。真实GPU时间戳+按写入顺序组合缓冲区已消除每帧中途依赖等待，4报告/帧、CPU fallback0。FPS报告2400–4200的31窗均40.16774（36.55–48.78）。稳定性聚合窗实际覆盖2282–4081共1800帧，>33.333ms 71次、>50ms 0次、最长44.822ms；它与FPS窗口不完全重合。仍非稳定60FPS。最慢帧CPU36.482ms、发布等待5.080ms，继续重点减少CPU尖峰；帧尾等待不能直接删。真实CPU采样16586/lost0，context_entry9.41%、memcpy6.56%、bind_shader4.42%，无新增或运行测试。当前证据：`<LOCAL_WORK>/research/android-gpu-counter-domain-20261016/`。以下是历史记录。

**最新根因已实机确认，GPU修复正在实现：** 每帧约6.93481ms等待来自compute读取64B时间戳报告，而非索引或固定睡眠。明细明确post kind2/type0、forward=host_effect；生产shader实际将16B报告后8B时间戳压紧复制。当前装机仍是`counter-source-20261016.apk`诊断包（build exit0、10.989秒，同营地截图正常），不宣称提帧。正在实现真实GPU时间戳生成及FIFO范围组合；精确32位整数转换shader已生产编译，但整个GPU路径尚未构建装机。无测试新增/执行。当前交接 `~/dm-work/research/android-counter-source-20261016/handoff.zh-CN.md`。以下为历史。

**最新提前提交版已装机：** 正常构建11.058秒、安装成功、同4009,4929营地截图正常。2400–4200共31窗口均36.73419FPS；1800帧中>33.333ms为153次（前版207），>50ms仍1次、最慢50.888ms。实际提前提交1.17871次/帧、开销0.05184ms/帧。未匹配温度或重复A/B，不能宣称稳定提升，60FPS尚未达到。无测试新增/执行。证据 `~/dm-work/research/android-chunks-20261016/handoff.zh-CN.md`。后续定位真实GPU依赖等待及CPU入口开销，不删除必要同步。以下为历史。

**最新ADPF周期修复已装机：** 已确认部分44ms慢帧中CPU占32ms，显示等待与worker均低于1ms；修复单线程性能提示混加worker、漏guest工作的计量。营地均35.82387FPS（本轮基线34.68774）；1800帧>33.333ms由295到207，但>50ms均1次、最慢仍50.634ms，不能宣称稳定或60FPS。实测4320周期上报全成功。无测试新增/执行。证据 `~/dm-work/research/android-adpf-cycle-20261016/handoff.zh-CN.md`。以下为历史。

**最新第七版已装机：** 不可变绑定规划与上下文入口条件标志优化，正常build成功；营地4200–6000帧31窗均36.54258FPS（33.49–40.63），截图正常。没有严格温度匹配A/B，不能确认稳定收益比例，60FPS仍未达到。未写或运行测试。CPU采样12900样本/lost0，context入口仍self10.09%、绑定4.82%、memcpy6.41%；后续应定位入口实际命中/慢路和真实等待，不能凭百分比下结论。证据 `~/dm-work/research/android-binding-plan-20261016/`。以下为历史记录。

用户最新决定：继续现有 Android 移植、提升真实FPS；LASRecomp只作为渲染实现的借鉴。停止PC重编译路线继续扩张，保留独立源码、编译产物和失败记录；没有PC游戏画面，也没有该路线的性能成绩。

**最新用户约束：不要再写测试，专注实际工作。** 之后仅生产实现、编译及真机画面/耗时验证，不新增或运行测试/fixture。当前第六版`word-hash-20261016.apk`已正常构建/安装，实际同4009,4929营地2400–4200帧均35.09129FPS（32.37–41.88），画面正常，GPU均10.970ms；没有温度匹配A/B，不作稳定收益百分比。GPU整块clone为0，小UBO内容复用且4319次heap权限租约快读全命中，页面权限43次/0.46ms、fault约24次；99.98%不可能命中的复制搜索已跳过。提交CPU仍约11.6ms并有真实完成等待，60FPS仍未达到。详见[当前进展](ANDROID_GPU_SNAPSHOT_DELTA_PLAN.zh-CN.md)。以下早期计划/测试为历史，以最新记录为准。

## 当前实际状态

- 最新长段：第六版4800–9480帧79窗均35.04101FPS（30.75–47.09），继续clone0/fallback0；生产源仍与已安装APK freeze一致。最新真实profile12110样本/lost0，bind_shader self5.26%、context入口7.71%、draw2.70%；后续应定位这些CPU工作和真实依赖等待，不再将大块复制当作未解决主因。详细数字与API边界以[当前交接](ANDROID_GPU_SNAPSHOT_DELTA_PLAN.zh-CN.md)为准。

- 设备：OPD2409/Adreno830，ADB序列号`<设备序列号>`；原34171端口断开后用户恢复无线调试，现已实际在线于`<设备地址>`、transport12。root独占ADB；子智能体不操作设备。
- 保持用户T30/PurpleVK驱动、strict custom、明确shader subgroup64、掌机720p、probe0关闭；未降低精度、修改时钟或跳过真实完成等待。
- 旧冻结参考：`android-camp-reader-buckets-turnip-720p/metrics-summary.json`中960–1800共15窗，均22.074FPS/GPU16.172ms。4次大于64KiB实际安全读取合计21.917MiB/12.510ms每帧，caller尚未由桶统计证明。
- 已排除普通scratch中转：真实AppUID Vulkan基准中它比直接readv慢，不接生产。
- 本轮新APK正常build.py实际构建exit0，13.491秒；完整Android标准回归exit0，15.412秒；APK96394870字节已实际install-r成功，检查点`owned-heap-first-20261016.apk`。日志与结果在`~/dm-work/research/android-owned-heap-20261016/`。新营地1800帧和继续4200帧均已采集；初段改善没有按原幅度保持，不能报告稳定20%加速，详见下方。

## 已有源码与验证边界

生产`hle_svc.c`/`hle.h`的封闭heap读取租约保持冻结；与SetHeapSize共享读写锁，完整源权限检查和复制在同一租约内，busy/范围不符返回未写入并fallback。真实权限失败返回非零结果并永久关闭快路。`diag.c`的Android旧布尔检查后memcpy也已迁移。

首版`nvn_vulkan.c::snapshot_read_owned`只接新建、完整、独占的snapshot_buffer映射目标，大于64KiB才尝试真实heap API；保留known-copy优先、原安全reader、顺序overlay、dirty保护与真实fence。普通stack/shadow/forward_seed/mtl_copy尚未扩展，不能假设覆盖全部4次大读取。

- 真实Host权限/并发九例及丢锁负对照已通过；CPUagent本轮只读确认源码、ARM PIE及构建provenance仍匹配。
- 原完整Host Vulkan和新fallback/owned合同检查已通过。
- ARM PIE `libdmheapleaseprobe.so`184232字节、`libdmownedsnapshotprobe.so`845032字节已在新APK nativeLibraryDir实际AppUID执行，均exit0。前者九例/显式负对照/Android uid10046最终RESULT全部出现。后者带`--expect-real-heap`，真实T30 identity、heap_api1、真实HLE+Vulkan拷贝、overlay/partial/errno/缩堆拒绝全部PASS，实际heap_fast_calls4/262160bytes；不是弱API fallback。
- strict T30 required64属性与完整GPU套件均exit0。可选游戏profile检查初次直接由run-as打开外部存储被Permission denied拒绝（保留失败），随后用shell读取实际外部cache的`host-subgroup-size.txt`原始内容（值为64）、逐字节原样复制到AppUID内部诊断目录，仅重跑profile属性检查exit0；没有改游戏cache/profile数字或绕校验。真实游戏初始化已再次通过实际cache检查。全部错误与复验日志保留。
- 生产主库已实际nm证明`dm_android_guest_heap_copy_owned`为强定义T符号，非未解析weak。

## 本轮执行顺序

1. 旧APK冷启动采营地1800帧，截图、日志与坐标验证后才作为fresh基线。
2. 顺序执行标准Android回归；安装新APK，实际AppUID运行heap PIE、owned PIE和strict T30完整GPU/游戏cache profile。
3. 同配置新APK冷启动采同一营地1800帧；确认heap_api=1、真实heap_fast/heap_bytes覆盖及safe读桶变化，再比较FPS、submit CPU/kernel、read wall与画面。
4. 若覆盖不足，先增加最小caller证据；不靠猜测扩大任意memcpy。LAS的增量页更新只在真实写代数与所有权成立时借鉴，已有arena/descriptor cache不重复包装为新优化。

首次基线采集碰到启动文件替换前的旧日志，已主动终止并写`INVALID_BASELINE.txt`，不得作对照。fresh重试先force-stop保留旧日志再启动，实际menu ready208帧、按A363帧、采集1800帧exit0。`~/dm-work/research/android-camp-baseline-20261016-before-owned-heap-fresh`实际同4009,4929营地/720p截图已看，960–1800共15窗均21.88267FPS、范围21.08–22.73、GPU均16.046ms；frame1800大读仍4次/21.917MiB/12.626ms。

新版本`android-camp-owned-heap-20261016-first`实际menu ready307、按A394、1800帧exit0，同位置营地截图正常。960–1800的15窗均26.39467FPS（22.27–29.22），GPU均11.702ms；但1500帧后已回落，继续2400–4200共31窗只有22.90742FPS（21.76–24.00），GPU均12.64710ms。后续截图约20.6FPS、原日志窗口约21–22；没有温度/DVFS匹配的旧新版热态A–B–A，因此不能称稳定20%收益或确认降频。设备退出前frame30840为22.49FPS，随后日志明确window destroyed/优雅退出，没有游戏崩溃证据。

frame1800实际heap_api1、两次heap读取19.667MiB/8.227ms，无错误或拒绝；大safe readv已由4次21.917MiB降为2次2.250MiB/0.800ms。租约快路减少了系统调用，却没有减少那19.667MiB的快照字节。真实simpleperf采样11.9591秒、11740样本/lost0：用户周期21.13%在memcpy，其中主要可解析链为`heap_copy_owned → snapshot_buffer → bind_shader`。这只定位绑定路径，**不能据此把两个实例都称SSBO**，只读texel同样可走此路径；周期百分比也不是wall耗时。

## 用户追问大量复制：Eden / LAS 对照与下一步

- Eden固定本地提交`a594c8d60ef79280adc046760561912207e16e17`：持久GPU buffer覆盖复用，按4KiB CPU脏页合并上传，排除GPU最新范围，使用有GPU tick生命周期的staging。它仍按完整合法descriptor尺寸绑定，并非随意截小shader范围。报告：[Eden复制对照](<LOCAL_WORK>/research/eden-buffer-copy-review-20261016.zh-CN.md)。
- LAS固定`6244eb9d4d3497094e21cac7dabe998ded1f2800`：storage主要走整堆导入与全局地址shader ABI；GFast可能仍作完整bound范围的GPU copy；UBO cbuf_size缩读和Windows vertex/index dirty缓存不能直接套给当前未知资源。报告：[LAS绑定范围](<LOCAL_WORK>/research/LASRecomp-storage-range-review.zh-CN.md)。
- 来源诊断现已实际统一构建exit0/16.868秒、标准安卓回归exit0/26.962秒、严格T30实际AppUID完整GPU及同一游戏profile回归exit0/0.912秒；96542501字节检查点`owned-heap-snapshot-source-20261016.apk`已install-r。生产主库两个诊断/heap API均nm强定义T，真实游戏dirty_api=1。营地采集1800帧exit0/69.82秒，menu187/A360，同4009,4929画面已看正常；960–1800十五窗均23.28667FPS（22.45–26.47），GPU均12.572ms。这是诊断APK，不宣称新优化收益。证据目录`android-snapshot-source-20261016/camp/`。
- **两次大复制已逐项定位：19,573,408字节（18.667MiB）的vertex只读SSBO，slot4/binding1；另1MiB为fragment只读texel/descriptor701。** 前者900/1200/1500/1800四样本都是旧entry因dirty失效，4779页仅8/6/6/7页被标记，范围内标记字节31392/23200/23200/27296、均2runs；却分别整块读取5.1665/8.2261/7.8199/8.7739ms。后者257/257页全标记，复制仅0.057–0.072ms。NVN录制→state→bind实际传递原size，未扩大为32MiB allocation capacity；没有尺寸解析错误证据。两块实际字节之和恰与19.667MiB聚合对应。报告：[1800帧实际结论](<LOCAL_WORK>/research/snapshot-source-diagnostics/final-findings.zh-CN.md)。
- write-marked仍非内容diff；干净页不能仅据gen判定。后续候选必须包含`gen>old_stamp OR state!=PROT`，须修取戳/重保护窗口、宿主写完成和pool incarnation；不可直接拿上述6–8页当合法patch清单。root已授权三路开始实现最窄GPU-only不可变增量路线，方案与分工见[增量实现计划](ANDROID_GPU_SNAPSHOT_DELTA_PLAN.zh-CN.md)，**尚未完成或接入真机**。
- root趁设备在线，先从冻结`owned-heap-first-20261016.apk`只追加两个独立诊断PIE，经zipalign/apksigner正常签名、install-r成功；没有在backend修改途中提前编译生产库。`owned-heap-query-only.apk`96509733字节，生产game代码仍是原冻结版。第一次签名因shell PATH无java失败，保留`query-only-package-no-java.log`，改用正常build.py同一JDK环境后sign/verify均exit0，未跳验证。
- **真实AppUID已查询：系统driver8/API1.3.284与strict T30 driver18/git62ac221a33/API1.4.359，均`VK_EXT_external_memory_host advertised=0`、`QUERY_HOST_IMPORT supported=0`。** 两次查询exit0且明确VkDevice/alloc/import/submit均0；因此LAS对当前匿名heap做标准host-pointer import的必要扩展不满足，停止这条直接移植路线。T30的dma-buf/AHardwareBuffer/map-memory-placed支持不等于现匿名heap可导入。原日志、真实选择身份、argv在`~/dm-work/research/android-snapshot-source-20261016/driver-query/`。
- 新write-marked诊断API只固定dirty槽/数组元数据，原子只读gen/state，不改变原dirty算法或protect调用。在Host O0/O3七组后，独立ARM PIE已真实AppUID执行七组全PASS，exit0/0.378秒，uid10046、页4096、20000次query/9959次并发销池复用；日志`android-snapshot-source-20261016/dirty-write-marked.appuid.log`。来源/reason诊断已安装并完成上一条真实1800帧采样。
- post0通用写前合批没有接生产：queue_lock不固定dirty槽生命周期，已有旧代数并发反例；当前mprotect总数也未归因post0，不能先宣称收益。

源码无commit/reset/checkout，用户游戏文件不做完整性/版本校验，原始存档不替换。最终成绩须写实际结果，60FPS仍未达到。
