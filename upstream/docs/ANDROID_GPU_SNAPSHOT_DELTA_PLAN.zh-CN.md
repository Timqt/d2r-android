# Android GPU只读快照增量实现计划

**当前安装为 `adpf-cycle-20261016.apk`：** 分项帧耗时与ADPF周期计量修复已编译、安装、采集。营地均35.82387FPS，1800帧>33.333ms207次（本轮基线295次），最慢50.634ms；一次非温控对照不能保证稳定收益，60FPS未达到。入口 `~/dm-work/research/android-adpf-cycle-20261016/handoff.zh-CN.md`。未写或运行测试，以下为历史。

**最新为第七版：** `binding-plan-20261016.apk` 正常构建和安装成功，真机营地4200–6000帧31窗均36.54258FPS（33.49–40.63），同4009,4929画面正常，60FPS未达到。新改动是不可变着色器绑定规划和上下文入口命中路径免NZCV保存恢复，动态读/上传/同步及线程缓存检查未削减。frame6000 CPU提交10.946ms、GPU clone0、UBO fallback/errors0。未写或运行测试。证据与后续入口 `~/dm-work/research/android-binding-plan-20261016/handoff.zh-CN.md`；以下第六版数据和旧计划按历史保留。

2026-10-16。状态：**第六版已实际安装并进入同4009,4929营地，2400–4200帧均35.09129FPS（32.37–41.88），画面正常。** 大GPU clone为0、UBO heap快读全命中，99.98%必败复制搜索已跳过；当前仍未达到60FPS。没有温度匹配A/B，不能将版本间差额全部归为代码收益。

**用户最新要求：不要再写测试，专注实际移植优化。** 此后不新增/运行测试或fixture，直接修改生产实现、独立编译、正常APK构建及真机画面/计数/FPS验证；下方测试记录是该指令之前已完成的历史，不作为继续扩测试的计划。

## 本轮当前交接入口

- 当前安装第六版`~/dm-work/build/android/checkpoints/word-hash-20261016.apk`，97,886,317字节；正常build exit0/16.846秒，冻结源构建前后一致，install-r Success/2.822秒，没有运行测试套件。仅改`pass.inc::graphics_hash`，8B/完整尾字节用memcpy安全载入、长度入种子、最终混合；四调用者完整key比较不变，不修改key/padding/持久化/driver cache。证据`~/dm-work/research/android-word-hash-20261016/`。
- 第六版实际menu207/Play360、4200帧采集exit0/120.475秒；同4009,4929、摇杆居中、材质正常。2400–4200三十一窗均35.09129FPS（32.37–41.88），GPU均10.970ms；不能用峰值41.88代替稳定帧率。截图`camp/device.png`，完整窗口`metrics-summary.json`。
- 第六版frame4200 submit wall19.397/CPU11.590/kernel1.853ms，dirty43.0calls/0.460ms、fault23.6/prepared258页；UBO heap4319次/4.587MiB、fallback/errors0，known-filter4413.67次中4412.67次负筛选（99.98%），snapshot GPU_clone0/inplace1、raw及所有拒绝0。每帧GPU约11ms、CPU提交仍约12ms且存在真实完成等待，60FPS尚未达成；下一轮需依据本版profile定位剩余主线程工作与依赖，而非再靠省大块复制。
- 第六版继续4800–9480共79窗均35.04101FPS（30.75–47.09），GPU clone继续0且fallback0；峰值47.09不代表稳定帧率。当前生产源逐项hash仍与已安装APK构建freeze一致，`final-source-state.json`实际true，没有遗留未部署的生产改动。
- 第六版新的12秒真实profile完成，12110 samples/lost0；user-cycle self：draw2.70%、bind5.26%、context入口7.71%、memcpy6.44%、pending-tree2.05%、snapshot_buffer2.24%，known-full原扫描已不在主要热点。最热线程14.34秒窗口user8.06/kernel1.31秒。原始APK对应symfs、record、报告与前后画面/输入保留于当前证据`cpu-profile/`；各比例不是wall ms，也不能把比例变动直接当耗时差。下一步重点依据该版定位bind_shader与入口工作，结合真实依赖等待判断可减少的CPU工作；ADPF会话只做一次性实际取证后再定分账修复。
- 第五版正常build exit0/16.844秒、install-r Success/3.290秒，97,886,317字节；fresh菜单187/Play360，营地4200帧采集exit0/123.972秒，同4009,4929、摇杆居中、画面正常。2400–4200三十一窗均34.29968FPS（31.49–37.87），GPU均11.13323ms；没有温度匹配A/B，不报告稳定提速百分比。
- 第五版真实known-filter每帧约4413.67次查询中4412.67次负筛选、仅1次原扫描（99.98%）；UBO heap约4319次/4.587MiB、fallback/errors0；snapshot GPU_clone0/inplace1/fallback0。frame4200 submit wall21.177/CPU12.892/kernel1.559ms，dirty43.8calls/0.486ms、fault24.3/prepared258页。最大的CPU整块读取、GPU整块clone与页面保护放大已消除，仍有原生guest逻辑和绘制CPU/真实GPU等待耗时，不能称60FPS完成。
- ADPF仅只读核查，没有改调度：现session源码只绑定首次TID，统计却混入GPU done worker工作，且不含NVN batch外完整guest主帧；显式fence/pacer/acquire等待未直接计入。实际ownUID日志无ADPF、连无条件frontend INFO也缺，初始env/四log属性为空不能证明会话状态。下一次需通过已有game.log一次性确认真实会话/绑定TID/首报告，再考虑修分账；不能凭一次核心频率读数断言调度错误。详细交接`~/dm-work/research/adpf-readonly-handoff-20261016.zh-CN.md`。当前没有改系统时钟、锁频、绑核或恢复PC路线。

## 第三、四版已完成记录

- 第三版`ubo-content-20261016.apk`实际正常build exit0/17.079秒、install-r Success，97,869,933字节。逐次完整安全读取+FIFO覆盖后比较同批不可变arena字节，去除UBO自身dirty/stamp/pool/copy查询；内容变化分配新slot，不覆盖旧消费者。已知CPU复制为空的批次同时免去必败的known-full链扫描。
- 实际1800帧采集exit0/37.630秒，menu ready676、Play740，**960–1800含加载不能作营地均值**。实际1440–1800七窗均29.44429FPS；2400–4200三十一窗均29.08097FPS（27.28–30.18）。营地截图正常、4009,4929、摇杆居中。没有温度匹配A/B，不报告可重复收益比例。
- 第三版frame1500实际mprotect44.1/frame、0.436ms、fault24.6；frame7800进一步31.8/frame、0.399ms、fault14.6。UBO每帧3116 byte hits/1203 misses，实际读4.59MiB、compare1.95MiB、arena copy2.64MiB，仍保留原完整边界与零填充。submit CPU仍约17.6ms/kernel6.6ms，说明只减少fault并不等于60FPS。
- 第三版12秒真实user-cycle profile：12952 samples/lost0，memcpy4.38%、known-full4.41%、draw5.01%、bind4.71%、context入口10.20%、process_vm_readv1.80%。它们是用户周期比例，不是wall分账；另有真实proc线程窗口14.49秒，最热线程user7.28/kernel3.03秒。目录`~/dm-work/research/android-ubo-content-20261016/`。
- 第四版仅扩大到**固定queue-owned uniform_read_scratch[65536]**的完整独占目标：known-full优先，冻结HLE的heap权限租约成功才直拷，拒绝则完整原safe reader；FIFO overlay与逻辑计数各一次，不更改通用reader/heap权限实现、不对借用arena做原位写。没有每调用clock，实际heap/fallback/known计数由`[vulkan-ubo-read]`报告。
- 第四版另将只供pipeline创建的派生状态移入cache miss，动态viewport/scissor/blend/depthbias/stencil照旧逐draw提交；key/hash、required64、依赖/附件/索引/顶点顺序不变。独立只读复核无阻挡，两平台对象编译通过；正常APK build exit0/17.152秒、install-r Success/2.822秒，97,869,933字节。检查点`ubo-heap-draw-20261016.apk`，证据`~/dm-work/research/android-ubo-heap-draw-20261016/`。
- 第四版实际menu1391/Play1465，首张1800帧截图是加载页，**不得把该页48.7FPS当营地成绩或当黑屏回归**。继续4200帧已完成，`camp/continued-device.png`实际同4009,4929营地、摇杆居中、材质正常；2400–4200三十一窗均33.51065FPS（31.27–36.27），GPU均11.28903ms。没有温度匹配A/B，不作稳定百分比收益结论。
- frame4200实际submit wall21.442/CPU13.285/kernel1.533ms，dirty40.5calls/0.463ms、fault21.6/prepared258页；frame4320实际UBO heap_api1、heap_fast4319次/4.587MiB、known0/safe_fallback0/errors0；snapshot inplace1、GPU_clone0、fallback0。实际系统调用时间已下降，但原生游戏逻辑与绘制CPU仍需继续优化。
- 第五版范围负筛选只改async.inc：perbatch仅登记已知CPU源的完整可见目的min/max；不重叠才提前0，可能重叠/范围溢出仍完整原扫描；reset只在重新录制。`[vulkan-known-copy-filter]`分账真实negative/original_scan，源码SHA748ca834d5e34b6fbee770cc6cfe762bd6b0facb1a7bbc1f3082785648d22be2。实际构建、安装和营地结果见上方当前入口，证据`~/dm-work/research/android-known-range-20261016/`。
- root独占ADB，serial仍`<设备序列号>`，严格T30/64/720p/probe0与原存档保持。下一候选是known-copy无重叠区间的保守负筛选；只能提前拒绝不可能命中请求，有可能重叠仍走完整原扫描，不能跳unknown/partial/FIFO规则。

## 第二版已完成记录

- 正常APK构建exit0/16.979秒，97,869,933字节，全部冻结生产文件构建前后hash一致；没有执行测试套件。检查点`~/dm-work/build/android/checkpoints/gpu-snapshot-inplace-post0-20261016.apk`已install-r Success/2.494秒。
- 新APK保持严格T30/PurpleVK、shader subgroup64、掌机720p、probe0；原存档不替换。日志先force-stop并移到唯一备份后启动，1800帧采集exit0/52.276秒。截图正常，坐标4012,4948且摇杆按下，与旧4009,4929不一致；可能存在用户实时操作，不能宣称严格静止A/B。
- 生产合包包括GPU原位稀疏更新（有尚未编码的旧消费者时保留COW）、真实完成后post0连续页解保护与实际复制合批，以及draw_count超过12后不再读取无用的DM_VK_TRACE环境变量。post0统计是原mprotect计数子集，不能重复相加。
- 30FPS限制只读核查：游戏请求interval2，但当前60Hz display模式在`hle_nvn.c`将实际interval取1，即16.667ms；eco_fps=0。查无本port固定30FPS上限，也未修改游戏时钟。现有display模式本身覆盖原interval语义，不能据此保证游戏内所有状态都支持60，也未完整反编译NSO排除内部状态上限。
- 真实960–1800十五窗均29.02FPS（27.23–30.67），GPU10.68067ms；2400–4200三十一窗均28.53516FPS（25.96–34.50），GPU11.02581ms。统计细表及场景边界写在证据根目录`metrics-summary.json`。
- 真实GPU snapshot每帧inplace1、GPU_clone0、COW0，patch_guest约0.0218MiB，所有fallback0；post0每帧11次/2.3385MiB，helper全部接管，258页只作2次RW，0.05–0.066ms，fallback0。整块GPU clone和post0逐页RW均已实际消除，但frame1800总mprotect仍760calls/7.421ms、fault384.3；不能把省calls直接当全部耗时消失。
- 新12秒实际user-cycle profile完成：11625samples/lost0，memcpy self3.19%、context入口8.40%、draw5.45%、bind3.82%、known-copy函数2.82%。最热线程14.65秒窗口user6.29/kernel4.03秒；不是wall阶段比例。下一步小UBO总4296次/frame中3156hit、1140miss、2.83MB读取，尝试每次安全读取后比较同批不可变内容，替换UBO自身protect/stamp复用；其他cache保护不主动解除，不跳过原安全读取/overlay。
- 此节为当前状态；下方首版与早期开发状态按历史保留，不能将其尚未构建/待测试措辞当作下一步计划。

## 已有证据

实际同营地1800帧来源诊断已证明：vertex只读SSBO的合法绑定为19,573,408字节，900–1800采样仅6–8页被write-marked，却整块读取约5.2–8.8ms。另一1MiB texel全页标记且只耗约0.06ms。详见[当天记录](ANDROID_FPS_OPTIMIZATION_20261016.zh-CN.md)。Eden持久buffer按dirty runs上传可借鉴；LAS标准匿名heap导入在系统和T30实际均缺`VK_EXT_external_memory_host`，不继续直接移植。

## 实现边界

只用于大于64KiB的只读SSBO/compute UBO pointer的GPU消费回退；完整已有GPU forward不变。不得硬编码游戏地址、shader key、固定页数或任意截小NVN bound。保留完整绑定和旧版本不可变性。

GPU增量条目必须与原CPU-ready snapshot分区，防止索引扫描立即读尚未完成的GPU clone。拟给read_snapshot增加GPU-only域，共享现有256MiB LRU和pool remove/fence生命周期，不新开另一份同等预算。

新增普通线程宿主写同步边界：FS/OE/纹理CPU readback的真正写入持shared锁；snapshot只try-exclusive，忙则立即raw安全全量读取、无protect、无缓存，不得进入原会protect/cache的snapshot_buffer。post0/event/counter原queue串行边界保留。signal fault不接新锁。宿主shared不带入queue锁、GPU等待或guest回调。

snapshot顺序：queue锁 → try宿主exclusive → dirty元数据锁；首次完整基线也必须在此协议内形成，才能纳入可信GPU谱系。CPU dirty事务先取stamp，再收集`gen>oldstamp OR state!=PROT`候选页与合并run；BUSY、元数据/epoch变化、保护后非PROT、表容量不足或真实mprotect失败不能认证增量。必须有pool incarnation防地址/槽复用。读取候选后再重验，失败走安全完整基线并拒缓存；不能拿诊断写标记数量代替实际计划。

仅在重验成功后编码“GPU clone旧完整buffer → 新staging的候选run GPU copies → consumer barrier”。新旧buffer、staging都持有到真实consumer fence，临时退役用post3，不能伪造kind0 guest写回。首版有pending/unknown来源即拒增量，保持真实FIFO/wait/overlay；不能先CPU overlay再被GPU clone盖掉。

直接guest写仍遵守合法NVN资源所有权/同步语义；跨线程任意非法数据竞争不承诺原子快照。宿主内核写必须完整落在新begin/end范围，不能仅依赖fault早于store的gen。现原CPU cache/纹理epoch快捷还存在独立漏洞，本轮不以改新GPU-only谱系为由默称全部修复。

## 并行分工与验证

- CPU agent `/root/cpu_binding_plan_review`：独占`dm/gfx/nvn/nvn_dirty.c/h`，事务、pool incarnation、checked保护、重验与真实暂停点夹具；先向GPU owner同步固定API。
- 宿主 agent `/root/dirty_range_batch_review`：独占`dm/host/crash.c`、`dm.h`、`hle_fs.c`、`hle_oe.c`，写begin/end及snapshot try-lock、全部失败出口/errno/锁序；独立Host/ARM验证。
- GPU agent `/root/cpu_binding_plan_review/safe_reader_review`：独占Android后端C/shader/forward、新delta include及`nvn_vulkan_tex.inc`的CPU写guard，真实Vulkan字节/异步旧版本寿命/CPU索引分区验证。
- root独占ADB、统一构建与标准回归（build.py/test.py不得并发），实际AppUID新夹具、严格T30完整GPU回归、同营地1800及继续帧数、截图与复制字节/submit CPU/GPU/FPS数据，实际收益再报告。

必要负对照包括NONE旧代数P1→P2、BUSY、标记在读取中改变、pool销毁/复用、第二段mprotect失败、宿主writer持锁try失败、真实pread部分/错误出口、GPU clone在途旧consumer、CPU index别名及pending overlay拒绝。任何测试用替身必须标注，不能冒充生产或真机通过。

当前设备serial仍`<设备序列号>`，最后实际mDNS`<设备地址>`/transport12。当前已安装**首版增量APK**`gpu-snapshot-delta-first-20261016.apk`（97853549字节）；原T30严格64/掌机720p/probe0保持。上一来源诊断APK作为回退检查点保留，未覆盖用户原存档。

## 已完成验证与当前采样

- 三方source已freeze并做独立只读快审；`android-gpu-snapshot-delta-20261016/source-sha256.json`固定13个实际生产源。封闭heap helper保持旧freeze。主库实际nm验证host guard四接口、dirty事务三接口及heap helper共八函数均强定义T。
- 正常`build.py --game-run`实际exit0/18.775秒，APK97853549字节；顺序标准`test.py`实际exit0/25.086秒，install-r Success，更新真实nativeLibraryDir后继续验证。证据均在`~/dm-work/research/android-gpu-snapshot-delta-20261016/`。
- 新四PIE实际AppUID uid10046执行全exit0：hostguard0.299秒；FS0.039秒；dirty事务0.090秒/11CASE（含真实kernel VMA hole失败及回滚）；完整真实dirty+guard+readv+GPU组合0.164秒/5CASE，actual_dirty/guard/reader均1且实际T30身份核对通过。预期abort/failure用例由父进程验证，日志保留，不是游戏崩溃。
- GPU组合Host先在llvmpipe真实Vulkan通过，再在上述AppUID T30通过：旧gatedconsumer/两run patch/同址CPUready别名/共享LRU/hostbusy/pending overlay/dense full/incarnation/revalidate/unaligned/protect failure全有完整GPU字节读回。marker的patch_guest12288含8192成功patch及4096失败尝试，绝不将失败读取藏掉。
- 同一新APK严格T30 required64完整原GPU套件和原实际游戏profile检查exit0/1.195秒。profile数字、shader缓存、精度、设备时钟未改变。
- 已先force-stop并保留旧game.log到唯一名字，再fresh启动避免旧日志误采。1800帧采集正在后台，目录`android-gpu-snapshot-delta-20261016/camp/`；须看画面、真实api命中、trusted full/raw失败、patch guest与GPU clone分账，再报告FPS。以下开发历史保留。

## 首版实际游戏数据与第二版生产工作

- 实际采集1800帧exit0/59.666秒，同4009,4929营地截图正常，960–1800共15窗均27.52533FPS（26.47–28.64），GPU均11.67333ms。该段有改善，但live2400–2760均22.1329FPS（19.43–24.66）、GPU13.13ms，因此仍不称稳定提速。统计命中始终4calls/3cleanhits/1delta每帧，raw与所有拒绝均0，波动不是增量失效。
- CPU source真读取：原18.667MiB整块变成平均0.0229467MiB/frame候选读取；1800帧exact2run/23200B。GPU仍每帧clone18.667MiB，必须与CPU省读分开。全logical snapshot由约24.7降为6.0MiB/frame，heap仅1MiB texel/~0.074ms。frame1800 submit CPU16.207ms、kernel6.755ms；页面保护仍1004.8calls/1273pages/6.690ms。
- 新真实12秒profile实际11778样本：用户周期memcpy由旧21.13%降为3.09%，context_entry9.29%、draw5.86%、bind_shader3.86%；这是用户周期比例，不是wall ms。原始profile/report在`android-gpu-snapshot-delta-20261016/cpu-profile/`。观察到待继续优化的页面权限操作和整块GPU clone，不把省字节直接当60FPS。
- 第二版原位GPU patch已生产实现并独立Host/ARM对象编译：entry last_acquired与只在外层真实Draw/Dispatch后推进的encoded serial，保证已有consumer确实编码才允许同queue WAR/WAW屏障后原位patch；仍有待编码旧consumer时保留COW。认证成功前不借用旧target，失败raw只写fresh独占目的。统计分inplace/COW与actual GPU clone。尚未构建/安装第二版。
- 第二版post0封闭合批API也已生产实现并独立ARM-Werror对象编译：metadata pin覆盖批量checked RW、原prepare hooks恰一次、真实memcpy及全覆盖页完成代数（含原NONE）；仅用于queue内真实fence后的owned CPUready输出。g_slot生命周期使原先未证明的queue-only方案边界闭合。GPU owner正在接async kind0/低频分账，不能称已节省259calls，须实际getter看命中/保护页/rw calls/wall与fallback。

## 开发验证历史（先前状态，已由上方结果覆盖）

- 宿主4文件及原测试3处链接适配已freeze，真实crash.c并发/6共享writer/嵌套/errno/10预期abort child及真实ReadFile+pread正常/EOF/短读/EINTR/EIO出口检查通过。API29的两个PIE已编译并ELF检查：`~/dm-work/research/host-write-exclusion/libdmhostguardprobe.so`（56400B）、`libdmhostfsprobe.so`（68792B）。README明确Host信号类型适配、错误注入与原macOS/APFS全套未运行边界；真机尚未跑。
- CPU事务接口已在header固定：`nvn_dirty_snapshot_begin/end/abort`，32个有界run，元数据pin、pool incarnation与真实checked mprotect；新GPU谱系不改原CPU cache算法。实际生产C+真实crash.c在Host O0/O3首轮10条CASE通过，含P1→P2负对照、NONE union、BUSY、后写重验、pool reuse、32run溢出、RO失败真RW回滚/rollback失败预期abort。root审查补获一条真实风险：rollback把原PROT页改NONE时必须发布fresh gen，防原CPU cache误clean，现已修并增加负对照。目录`~/dm-work/research/dirty-snapshot-transaction/`，ARM PIE已出现，最终freeze/hash/真机结果待owner收口。
- GPU增量后端及新`nvn_vulkan_delta.inc`已写，独立Host/ARM对象编译通过，尚未freeze。原CPU cache显式排除gpu_only，hash域分离但共享LRU预算；新raw回退不调用原protect/cache。真实dirty+crash+host_posix readv+Vulkan组合夹具正在编译运行，包含旧gated consumer、同地址CPU索引别名、pending/hostbusy等，**尚未通过最终组合验证，不以对象编译代替成功**。
- root下一步：等三方源freeze与组合实际测试，通过后stage三个CPU/host PIE和GPU新PIE；统一build→standard test→install→更新nativeLibraryDir→实际AppUID探针→strict T30完整GPU→fresh同营地1800及继续采样。root早前新heap helper生产路径保持原freeze，不扩大任意源/目的memcpy。
