// 预编译 SPIR-V、描述符绑定及计算提交。| Precompiled SPIR-V, descriptor binding and compute submission.
typedef struct { uint32_t kind,binding,slot,cb_slot,cb_offset,handle,type,count,flags,separate; } shader_binding;
typedef struct { VkDescriptorType type; uint32_t offset; int storage_slot,uniform_pointer,dynamic_index; } shader_binding_plan;
typedef struct shader_entry {
    char key[17]; uint32_t stage,set,count,c1_size,attributes[32];
    shader_binding *bindings; uint8_t *c1; VkShaderModule module; VkDescriptorSetLayout layout;
    shader_binding_plan *binding_plan; uint32_t buffer_count,image_count,texel_count,dynamic_count;
    VkPipelineLayout compute_layout; VkPipeline compute_pipeline;
    // 被折进 dispatch Z 的倍数（Mali：LocalSize Z 已折到 MALI_LOCAL_Z_TARGET），1 表示没折。
    // The factor folded into the dispatch Z (Mali: LocalSize Z folded to MALI_LOCAL_Z_TARGET); 1 means none.
    uint32_t local_z_fold;
    // 原始 SPIR-V 保留一份（仅诊断路径用来判断 LocalSize Z），着色器本来就只有几 KB。
    // Keep the original SPIR-V (only the diagnostic path reads LocalSize Z from it); shaders are a few KB.
    uint32_t *spirv_words; size_t spirv_word_count;
    vk_buffer constant_buffer;
    struct shader_entry *next;
} shader_entry;
static void graphics_probe_cs_inputs(const struct shader_entry *shader,const VkWriteDescriptorSet *writes,uint32_t count,const uint32_t *dynamic_offsets,unsigned dynamic_count);
static void graphics_probe_cs_outputs(const struct shader_entry *shader);
static shader_entry *shader_list;
// 提交线程与预热线程共享着色器/通道/布局表；这把锁串行化表的增查，预热对象因此直接进入运行期缓存。
// The submit and warm threads share the shader/pass/layout tables; this lock serializes table access so warmed objects land directly in the runtime caches.
static pthread_mutex_t graphics_object_lock=PTHREAD_MUTEX_INITIALIZER;
// 只计真实创建，缓存命中不读时钟；每类每60帧最多4条慢创建明细。
// Time actual creation only, never cache hits; cap slow details at four per category per 60 frames.
typedef struct resource_create_start {uint64_t wall,cpu;} resource_create_start;
typedef struct resource_create_stats {uint64_t count,wall,cpu,slow,max_wall,max_cpu,max_wall_frame,max_cpu_frame,window,logged;} resource_create_stats;
static resource_create_stats resource_creates[3];
// NVN 长帧账目用的累计值：真实创建条数与墙钟纳秒，永不按窗口清零（调用方取差）。
// Cumulative totals for the NVN long-frame accounting: genuine creation counts and wall nanoseconds, never reset per window (the caller diffs them).
static uint64_t resource_created_count[3],resource_created_ns[3];
// 预热线程不记账：后台创建的成本不属于任何一帧，预热自报 [vulkan-pso-warm] 明细。
// The warm thread does not account: background creation cost belongs to no frame; the warm reports its own [vulkan-pso-warm] details.
static _Thread_local int resource_create_quiet;
static const char *resource_create_names[]={"shader_load","graphics_pipeline","compute_pipeline"};
static resource_create_start resource_create_begin(void) {
    return (resource_create_start){dm_android_clock_ns(CLOCK_MONOTONIC),dm_android_clock_ns(CLOCK_THREAD_CPUTIME_ID)};
}
static void resource_create_end(unsigned kind,resource_create_start start,const char *shader,const char *fragment) {
    if(resource_create_quiet)return;
    uint64_t cpu=dm_android_clock_ns(CLOCK_THREAD_CPUTIME_ID)-start.cpu,wall=dm_android_clock_ns(CLOCK_MONOTONIC)-start.wall,frame=presents+1;
    resource_create_stats *s=&resource_creates[kind];s->count++;s->wall+=wall;s->cpu+=cpu;
    resource_created_count[kind]++;resource_created_ns[kind]+=wall;
    if(wall>s->max_wall){s->max_wall=wall;s->max_wall_frame=frame;}if(cpu>s->max_cpu){s->max_cpu=cpu;s->max_cpu_frame=frame;}
    if(wall<=2000000&&cpu<=2000000)return;s->slow++;
    uint64_t window=(frame-1)/60;if(s->window!=window){s->window=window;s->logged=0;}
    if(s->logged++<4)LOG("[vulkan-create-slow] frame=%llu kind=%s wall_ms=%.3f thread_cpu_ms=%.3f shader=%s fragment=%s scope=cache_miss_creation",(unsigned long long)frame,resource_create_names[kind],wall/1e6,cpu/1e6,shader,fragment?fragment:"none");
}
static void resource_create_report(void) {
    for(unsigned i=0;i<3;i++){resource_create_stats *s=&resource_creates[i];
        LOG("[vulkan-create-window] frame=%llu kind=%s count=%llu slow_over2ms=%llu wall_ms=%.3f thread_cpu_ms=%.3f max_wall_ms=%.3f max_wall_frame=%llu max_cpu_ms=%.3f max_cpu_frame=%llu",(unsigned long long)presents,resource_create_names[i],(unsigned long long)s->count,(unsigned long long)s->slow,s->wall/1e6,s->cpu/1e6,s->max_wall/1e6,(unsigned long long)s->max_wall_frame,s->max_cpu/1e6,(unsigned long long)s->max_cpu_frame);
        s->count=s->wall=s->cpu=s->slow=s->max_wall=s->max_cpu=s->max_wall_frame=s->max_cpu_frame=0;
    }
}
#include "nvn_vulkan_handles.inc"
// 离线翻译选项必须匹配实际 GPU 分组；独立 GLSL 测试和旧缓存的未知配置明确记录。
// Offline translation options must match actual GPU subgroups; explicitly record unknown profiles of independent GLSL tests and legacy caches.
static void shader_profile_check(void) {
    static int checked;if(checked)return;checked=1;
    const char *dir=getenv("DM_VK_SHADERS");if(!dir)die("Set DM_VK_SHADERS to Vulkan shader cache");
    char path[1024];snprintf(path,sizeof path,"%s/host-subgroup-size.txt",dir);FILE *file=fopen(path,"r");
    if(!file){if(errno!=ENOENT)die("Vulkan shader profile open");LOG("[vulkan] shader subgroup profile unknown; shader execution subgroup=%u",gpu_subgroup_size);return;}
    char data[32];size_t n=fread(data,1,sizeof data-1,file);int extra=fgetc(file),error=ferror(file);fclose(file);data[n]=0;
    errno=0;char *end;unsigned long size=strtoul(data,&end,10);while(*end==' '||*end=='\n'||*end=='\r'||*end=='\t')end++;
    if(error||extra!=EOF||errno||end==data||*end||!size||size>128||(size&(size-1)))die("Invalid Vulkan shader subgroup profile");
    if(size!=gpu_subgroup_size)die("Vulkan shader subgroup profile %lu differs from selected shader execution %u; rebuild for the actual policy",size,gpu_subgroup_size);
    LOG("[vulkan] shader subgroup profile=%lu matches selected shader execution",size);
}
static void *read_shader_file(const char *key,const char *suffix,size_t *size) {
    shader_profile_check();
    const char *dir=getenv("DM_VK_SHADERS");if(!dir)die("Set DM_VK_SHADERS to Vulkan shader cache");
    char path[1024];snprintf(path,sizeof path,"%s/%s.%s",dir,key,suffix);FILE *f=fopen(path,"rb");if(!f)die("Missing Vulkan shader %s",path);
    if(fseek(f,0,SEEK_END))die("Shader seek");long n=ftell(f);if(n<0||n>32*1024*1024)die("Shader size");rewind(f);void *data=malloc(n);if(!data||fread(data,1,n,f)!=(size_t)n)die("Shader read");fclose(f);*size=n;return data;
}
// 预热路径的非致命读取：清单可能引用已不在本机着色器缓存里的键，缺失按跳过处理。
// Non-fatal read for the warm path: manifests may reference keys no longer present in the local shader cache; missing files are skipped.
static void *read_shader_file_opt(const char *key,const char *suffix,size_t *size) {
    shader_profile_check();
    const char *dir=getenv("DM_VK_SHADERS");if(!dir)return NULL;
    char path[1024];snprintf(path,sizeof path,"%s/%s.%s",dir,key,suffix);FILE *f=fopen(path,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long n=ftell(f);if(n<0||n>32*1024*1024){fclose(f);return NULL;}rewind(f);void *data=malloc(n?n:1);if(!data||fread(data,1,n,f)!=(size_t)n){free(data);fclose(f);return NULL;}fclose(f);*size=n;return data;
}
// 只有内容变化就换 arena 槽位的客体 UBO 用动态偏移；支持数据、c1 与 c0/c2 驱动表槽位跨绘制稳定。
// Only guest UBOs that migrate arena slots as their contents change use dynamic offsets; the support
// data, c1 and the c0/c2 driver table slots stay put across draws.
// 客体 UBO 随内容变化，是换槽位最频繁的一档，动态偏移优先给它。
// Guest UBOs change with content and move slots most often, so they get dynamic offsets first.
static int descriptor_uniform_dynamic_guest(const shader_binding *b) {
    return b->kind==1&&b->slot>=3&&b->slot<19;
}
// c0/c2 驱动表也由 arena 槽位承载，位置跨绘制同样会变；c1 与支持数据位置固定，不用动态偏移。
// The c0/c2 driver tables also live in arena slots and move across draws; c1 and the support data keep fixed
// positions and need no dynamic offset.
static int descriptor_uniform_dynamic(const shader_binding *b) {
    // DM_VK_STATIC_UNIFORMS=1：全部退回静态 uniform，用来单变量验证"动态 uniform 是否是驱动崩溃的原因"。
    // DM_VK_STATIC_UNIFORMS=1 forces every uniform static, a single-variable test of whether dynamic uniforms
    // are what crashes the driver.
    { static int forced=-1; if(forced<0){const char *e=getenv("DM_VK_STATIC_UNIFORMS");
        forced=e&&*e?strcmp(e,"0")!=0:perf_config_int("static_uniforms",0);
        if(forced)LOG("[vulkan] 诊断：全部 uniform 退回静态");} if(forced)return 0; }
    return b->kind==1&&(b->slot==0||b->slot==2||(b->slot>=3&&b->slot<19));
}
// 静态与动态 uniform 共用一条读取与依赖路径，区别只在描述符里的位置由谁提供。
// Static and dynamic uniforms share one read and dependency path; only who supplies the descriptor position differs.
static int descriptor_type_uniform(VkDescriptorType type) {
    return type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER||type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
}
// Mali 驱动在 LocalSize 的 Z>1 的 compute 管线上创建时会在自己内部 SIGSEGV（实测 pc/lr 都落在
// libGLES_mali.so 的代码段、addr=0x18；把 Z 改成 1 后同一条管线即正常建出并执行）。
// 这里做等价改写：把 SPIR-V 的 LocalSize Z 置 1，并把调用方的 dispatch Z 乘上原来的 LocalZ，
// 线程总数与 (x,y,z) 展开次序完全不变，只是换成驱动能接受的调度形状。
// Mali's driver SIGSEGVs inside itself while creating a compute pipeline whose LocalSize has Z>1
// (measured: pc and lr both inside libGLES_mali.so, addr=0x18; setting Z to 1 makes the same pipeline
// build and run). Rewrite equivalently: set the SPIR-V LocalSize Z to 1 and multiply the caller's
// dispatch Z by the original LocalZ, leaving thread counts and (x,y,z) expansion order unchanged.
enum { DM_OP_EXECUTION_MODE = 16, DM_EXECUTION_MODE_LOCAL_SIZE = 17 };
// Mali 的这一限制按驱动家族判断：设备名里带 Mali。
// 注意：着色器创建会在提交线程与 pso-warm 预热线程上并发发生，所以这里的惰性初始化必须线程安全，
// 否则两条线程可能同时看到 value<0 并各自执行一次 vkGetPhysicalDeviceProperties。
// Judge by driver family: a device name that says Mali.
// Shader creation happens concurrently on the submit and the pso-warm thread, so this lazy init must be
// thread safe; otherwise both threads can observe value<0 and each run vkGetPhysicalDeviceProperties.
static int gpu_has_mali_local_z_limit(void) {
    static _Atomic int cached = -1;
    int value = atomic_load_explicit(&cached, memory_order_acquire);
    if(value < 0){
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physical,&properties);
        value = strstr(properties.deviceName,"Mali") ? 1 : 0;
        int expected = -1;
        if(atomic_compare_exchange_strong(&cached,&expected,value)){
            if(value)LOG("[vulkan] Mali 检测到：compute 的 LocalSize Z 会折进 dispatch（驱动在 Z>1 时崩溃）");
        }else value = atomic_load_explicit(&cached, memory_order_acquire);
    }
    return value;
}
static uint32_t shader_local_size_z(const uint32_t *words,size_t count) {
    size_t at=5;
    while(at<count){
        uint32_t opcode=words[at]&0xFFFFu,length=words[at]>>16;
        if(!length||at+length>count)break;
        // OpExecutionMode 的布局是 [opcode|len, entryPoint, mode, x, y, z]，总共 6 个 word；
        // length 含 opcode，所以是 length==6、Z 在 at+5，mode 在 at+2。
        // OpExecutionMode lays out as [opcode|len, entryPoint, mode, x, y, z] -- 6 words total; the length
        // includes the opcode, so length==6, mode at +2 and Z at +5.
        if(opcode==DM_OP_EXECUTION_MODE&&length>=6&&words[at+2]==DM_EXECUTION_MODE_LOCAL_SIZE)return words[at+5];
        at+=length;
    }
    return 0;
}
// Mali 上把 compute 的 LocalSize Z 折小（MALI_LOCAL_Z_TARGET），并把调用方 dispatch 的 Z 乘上
// 原来的 LocalZ/目标 倍。为什么是"折小到 4"而不是"折到 1"：
//   GlobalInvocationId.z = WorkgroupId.z * LocalSize.z + LocalInvocationId.z，
// 只要 dispatch.z 与 LocalSize.z 的乘积不变，gz 覆盖的区间就完全不变，Z 维语义天然等价；
// 而实测 Z=8 必崩、Z=4 稳定、Z=1 会走进另一条不稳定路径。
// 带 Workgroup（shared memory）的着色器不能折：barrier 的跨 Z 语义会被改变，这类保持原样。
// Fold the compute LocalSize Z down on Mali (MALI_LOCAL_Z_TARGET) and multiply the caller's dispatch Z by
// originalLocalZ/target. Why fold to 4 rather than 1: GlobalInvocationId.z = WorkgroupId.z * LocalSize.z +
// LocalInvocationId.z, so as long as dispatch.z times LocalSize.z is unchanged the gz range is identical and
// the Z semantics are exactly preserved. Measured: Z=8 always crashes, Z=4 is stable, Z=1 hits another
// unstable path. Shaders that declare Workgroup (shared) memory are left alone: folding would change the
// cross-Z barrier semantics.
#define MALI_LOCAL_Z_TARGET 4u
static int shader_declares_workgroup_memory(const uint32_t *words,size_t count) {
    size_t at=5;
    while(at<count){
        uint32_t opcode=words[at]&0xFFFFu,length=words[at]>>16;
        if(!length||at+length>count)break;
        // OpVariable=59：[opcode|len, resultType, resultId, storageClass]，Workgroup=4。
        // OpVariable is 59: [opcode|len, resultType, resultId, storageClass]; Workgroup is 4.
        if(opcode==59&&length>=4&&words[at+3]==4)return 1;
        at+=length;
    }
    return 0;
}
// Mali LocalSize Z 折叠的配套改写（SPIR-V 层面）。
// The companion rewrite of the hand-computed Z index that the Mali LocalSize Z fold invalidates.
#include "nvn_vulkan_zfold.inc"
// 返回改写后的副本（调用方负责 free）；不需要改写时返回空。*folded 是被折进 dispatch 的倍数。
// Returns a rewritten copy (caller frees); null when no rewrite is needed. *folded is the factor folded
// into the dispatch Z.
static uint32_t *shader_fold_local_z(const uint32_t *words,size_t count,uint32_t *folded) {
    // 开关 local_z_fold 0（perf.conf）关掉折叠，用于单变量对照"折叠本身是不是新的崩溃来源"。
    // local_z_fold 0 in perf.conf disables the fold, isolating whether the fold itself is a new crash source.
    { static int enabled=-1; if(enabled<0){const char *e=getenv("DM_VK_LOCAL_Z_FOLD");
        enabled=e&&*e?strcmp(e,"0")!=0:perf_config_int("local_z_fold",1);
        if(!enabled)LOG("[vulkan] LocalSize 折叠已关闭（local_z_fold 0）");} if(!enabled)return NULL; }
    uint32_t z=shader_local_size_z(words,count);
    if(z<=MALI_LOCAL_Z_TARGET||!gpu_has_mali_local_z_limit())return NULL;
    if(shader_declares_workgroup_memory(words,count))return NULL;
    uint32_t *copy=malloc(count*sizeof *copy);
    if(!copy)die("Vulkan shader LocalSize fold allocation");
    memcpy(copy,words,count*sizeof *copy);
    size_t total=count;
    // 折叠会让着色器手算的 z 索引失配（推导见 nvn_vulkan_zfold.inc）：先改索引，再改 LocalSize.z。
    // 顺序不能反 —— 缺常量时要往模块里插一条 OpConstant，那会挪动后面的指令。
    // The fold invalidates the shader's hand-computed Z index; fix it before touching LocalSize.z.
    int fixed=dm_spirv_fix_manual_z_index(&copy,&total,z);
    if(fixed<0){free(copy);die("Vulkan shader LocalSize fold: hand-computed Z index cannot be fixed");}
    if(fixed>0)LOG("[vulkan] LocalSize fold: hand-computed Z index shift <<%u changed to <<%u (matches the driver global z)",
                   (unsigned)dm_spirv_log2(z),(unsigned)dm_spirv_log2(MALI_LOCAL_Z_TARGET));
    count=total;
    size_t at=5;
    while(at<count){
        uint32_t opcode=copy[at]&0xFFFFu,length=copy[at]>>16;
        if(!length||at+length>count){free(copy);die("Vulkan shader LocalSize fold structure");}
        if(opcode==DM_OP_EXECUTION_MODE&&length>=6&&copy[at+2]==DM_EXECUTION_MODE_LOCAL_SIZE){
            copy[at+5]=MALI_LOCAL_Z_TARGET;*folded=z/MALI_LOCAL_Z_TARGET;return copy;}
        at+=length;
    }
    free(copy);return NULL;
}
// Mali 的这一限制按驱动家族判断：设备名里带 Mali（定义在上方）。
// The Mali restriction is judged by driver family: a device name that says Mali (defined above).
static VkDescriptorType descriptor_type(const shader_binding *b) {
    if(b->kind<=1)return descriptor_uniform_dynamic(b)?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    if(b->kind==2)return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    if(b->kind==4)return (b->type&255)==2?VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    if((b->type&255)==2)return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    if(b->separate)return (b->type&255)==0?VK_DESCRIPTOR_TYPE_SAMPLER:VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
}
static shader_entry *shader_load_by_key(const char *key,int stats,int fatal) {
    pthread_mutex_lock(&graphics_object_lock);
    for(shader_entry *s=shader_list;s;s=s->next)if(!strcmp(s->key,key)){pthread_mutex_unlock(&graphics_object_lock);return s;}
    resource_create_start creation=resource_create_begin();
    size_t bytes;uint32_t *meta=fatal?read_shader_file(key,"meta",&bytes):read_shader_file_opt(key,"meta",&bytes);
    if(!meta){pthread_mutex_unlock(&graphics_object_lock);return NULL;}
    if(bytes<28||memcmp(meta,"DMVK",4)||meta[1]!=1||meta[4]>256||meta[5]>65536||meta[6]!=32||bytes!=28+meta[4]*40+meta[5]+128){
        if(fatal)die("Invalid Vulkan reflection %s",key);
        free(meta);pthread_mutex_unlock(&graphics_object_lock);return NULL;
    }
    void *code=fatal?read_shader_file(key,"spv",&bytes):read_shader_file_opt(key,"spv",&bytes);
    if(!code){free(meta);pthread_mutex_unlock(&graphics_object_lock);return NULL;}
    shader_entry *s=calloc(1,sizeof *s);if(!s)die("Vulkan shader allocation");strcpy(s->key,key);s->local_z_fold=1;s->stage=meta[2];s->set=meta[3];s->count=meta[4];s->c1_size=meta[5];s->bindings=(void*)(meta+7);s->c1=(uint8_t*)s->bindings+s->count*40;memcpy(s->attributes,s->c1+s->c1_size,128);
    VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=bytes,.pCode=code};
    // 留一份原始 SPIR-V：诊断路径要从中读 LocalSize Z（折叠后已改，读不到原值）。
    // Keep a copy of the original SPIR-V: the diagnostic path reads LocalSize Z from it (the folded copy no
    // longer carries the original value).
    { size_t words=bytes/sizeof(uint32_t);uint32_t *keep=malloc(words*sizeof *keep);
      if(keep){memcpy(keep,code,words*sizeof *keep);s->spirv_words=keep;s->spirv_word_count=words;} }
    // Mali 下把 compute 的 LocalSize Z 折成 1（见 shader_fold_local_z 的说明），倍数留给 dispatch 用。
    // On Mali fold the compute LocalSize Z down to 1 (see shader_fold_local_z); the multiplier is kept for dispatch.
    { uint32_t folded=0;uint32_t *rewritten=shader_fold_local_z((const uint32_t*)code,bytes/sizeof(uint32_t),&folded);
      if(rewritten){s->local_z_fold=folded;mi.pCode=rewritten;}
      VK_CHECK(vkCreateShaderModule(device,&mi,NULL,&s->module));
      if(rewritten)free(rewritten); }
    free(code);
    // 反射与 shader 同寿命；只预计算布局和索引，不缓存动态资源地址或工作区指针。
    // Reflection lives with the shader; precompute only layout and indices, never dynamic resource addresses or scratch pointers.
    if(s->count){s->binding_plan=calloc(s->count,sizeof *s->binding_plan);if(!s->binding_plan)die("Vulkan shader binding plan allocation");}
    VkDescriptorSetLayoutBinding bindings[256];for(unsigned i=0;i<s->count;i++){
        const shader_binding *b=&s->bindings[i];if(!b->count||b->count>128)die("Vulkan descriptor array capacity");
        shader_binding_plan *p=&s->binding_plan[i];p->type=descriptor_type(b);p->dynamic_index=-1;
        if(descriptor_type_uniform(p->type)||p->type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER){p->offset=s->buffer_count;s->buffer_count+=b->count;}
        else if(p->type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER||p->type==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER){p->offset=s->texel_count;s->texel_count+=b->count;}
        else{p->offset=s->image_count;s->image_count+=b->count;}
        if(p->type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER){
            unsigned base=s->stage==0?0x110:s->stage==5?0x310:0x510;int off=b->cb_offset*4;
            p->uniform_pointer=s->stage==5&&off>=0x210&&off<0x310;p->storage_slot=(off-(p->uniform_pointer?0x210:(int)base))/16;
            if(b->cb_slot!=0||p->storage_slot<0||p->storage_slot>=16)die("Vulkan storage buffer mapping pending: shader=%s cb=%u offset=%#x",s->key,b->cb_slot,off);
        }
        bindings[i]=(VkDescriptorSetLayoutBinding){.binding=b->binding,.descriptorType=p->type,.descriptorCount=b->count,.stageFlags=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT};
    }
    // 动态 uniform 的条数必须落在设备上限内；超限就整批退回静态，不能带病创建布局。
    // The dynamic uniform count must fit the device limit; if it does not, fall back to static for all of them
    // instead of creating a layout the device would reject.
    unsigned dynamic_limit=(unsigned)gpu_limits.maxDescriptorSetUniformBuffersDynamic,guest_candidates=0,driver_candidates=0;
    for(unsigned i=0;i<s->count;i++)if(s->binding_plan[i].type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC){
        if(descriptor_uniform_dynamic_guest(&s->bindings[i]))guest_candidates+=s->bindings[i].count;else driver_candidates+=s->bindings[i].count;}
    // 两级回退：先退掉位置相对稳定的驱动表槽位，仍超限才整批退回静态。
    // Two-tier fallback: drop the steadier driver-table slots first, and fall back to all-static only if still over.
    if(guest_candidates+driver_candidates>dynamic_limit){
        LOG("[vulkan] dynamic uniform fallback: shader=%s guest=%u driver=%u limit=%u",s->key,guest_candidates,driver_candidates,dynamic_limit);
        for(unsigned i=0;i<s->count;i++)if(s->binding_plan[i].type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC&&!descriptor_uniform_dynamic_guest(&s->bindings[i])){s->binding_plan[i].type=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;}
        if(guest_candidates>dynamic_limit)for(unsigned i=0;i<s->count;i++)if(s->binding_plan[i].type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC){s->binding_plan[i].type=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;}
    }
    // 动态偏移按 binding 号升序排位：Vulkan 要求这套数组的顺序与绑定的 binding 号一致，反射顺序不保证。
    // Dynamic offsets are ranked by ascending binding number: Vulkan requires that array order to follow binding
    // numbers, which reflection order does not guarantee.
    for(unsigned i=0;i<s->count;i++){
        unsigned best=s->count;
        for(unsigned j=0;j<s->count;j++)if(s->binding_plan[j].dynamic_index<0&&s->binding_plan[j].type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
            if(best==s->count||s->bindings[j].binding<s->bindings[best].binding)best=j;
        if(best==s->count)break;
        s->binding_plan[best].dynamic_index=(int)s->dynamic_count;s->dynamic_count+=s->bindings[best].count;
    }
    {unsigned dynamic_sum=0;
     for(unsigned i=0;i<s->count;i++)if(s->binding_plan[i].type==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)dynamic_sum+=s->bindings[i].count;
     if(s->dynamic_count!=dynamic_sum)die("Vulkan dynamic uniform placement: shader=%s placed=%u expected=%u",s->key,s->dynamic_count,dynamic_sum);}
    VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=s->count,.pBindings=bindings};VK_CHECK(vkCreateDescriptorSetLayout(device,&li,NULL,&s->layout));s->next=shader_list;shader_list=s;
    pthread_mutex_unlock(&graphics_object_lock);
    if(stats)resource_create_end(0,creation,s->key,NULL);LOG("[vulkan] shader %s stage=%u bindings=%u dynamic_uniforms=%u",s->key,s->stage,s->count,s->dynamic_count);return s;
}
// 运行期入口：控制块哈希出键，命中共享表或走致命加载；预热入口按清单键非致命加载。
// Runtime entry: hash the control block to a key, then hit the shared table or load fatally; the warm entry loads non-fatally by manifest key.
static shader_entry *shader_get(const void *control) {
    uint8_t hash[32];dm_android_sha256_context ctx;dm_android_sha256_init(&ctx);dm_android_sha256_update(&ctx,control,0x880);dm_android_sha256_final(hash,&ctx);char key[17];for(int i=0;i<8;i++)snprintf(key+i*2,3,"%02x",hash[i]);
    return shader_load_by_key(key,1,1);
}
// ProgramSetShaders 每次生成新序号；按程序地址及序号复用解析，防地址重用返回旧着色器。
// ProgramSetShaders generates a new serial every time; cache resolution by program address/serial so reused addresses cannot return old shaders.
typedef struct resolved_program { const prog_t *program; uint64_t serial; shader_entry *shaders[6]; struct resolved_program *next; } resolved_program;
static resolved_program *program_index[1024];
static shader_entry *program_shader(const prog_t *program,unsigned index) {
    if(index>=program->nstage||index>=6)die("Vulkan program stage bounds");
    if(!program->serial)return shader_get(program->sd[index].control);
    unsigned bucket=((uintptr_t)program>>4)&1023;resolved_program *entry=program_index[bucket];while(entry&&entry->program!=program)entry=entry->next;
    if(!entry){entry=calloc(1,sizeof *entry);if(!entry)die("Vulkan program cache allocation");entry->program=program;entry->next=program_index[bucket];program_index[bucket]=entry;}
    if(entry->serial!=program->serial){entry->serial=program->serial;memset(entry->shaders,0,sizeof entry->shaders);}
    if(!entry->shaders[index])entry->shaders[index]=shader_get(program->sd[index].control);return entry->shaders[index];
}
static void program_remove(const prog_t *program) {
    unsigned bucket=((uintptr_t)program>>4)&1023;resolved_program **link=&program_index[bucket];while(*link&&(*link)->program!=program)link=&(*link)->next;
    if(*link){resolved_program *entry=*link;*link=entry->next;free(entry);}
}
static VkSamplerAddressMode wrap_mode(unsigned wrap) {switch(wrap){case 1:return VK_SAMPLER_ADDRESS_MODE_REPEAT;case 6:return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;case 4:case 5:return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;default:return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;}}
#include "nvn_vulkan_sampler.inc"

static texture_descriptor descriptor_texture(int id) {
    texture_descriptor d={0};pthread_mutex_lock(&descriptor_lock);if(id>=0&&id<texture_capacity)d=texture_descriptors[id];pthread_mutex_unlock(&descriptor_lock);return d;
}
static image_entry *descriptor_image(const texture_descriptor *d,uint32_t type,int storage,VkPipelineStageFlags stage,VkAccessFlags access) {
    if(d->valid){image_entry *e=image_get(&d->texture);if(!storage||(e->texture.has_layout&&e->texture.mem))image_upload(e);if(storage)image_gpu_written(e);image_access(e,stage,access);return e;}
    // 未绑定采样资源保留零贴图；可写空槽用独立图像，防止计算输出污染后续采样。
    // Keep unbound sampled resources zero; separate writable images prevent compute output from polluting later samples.
    static tex_t dummy[2][32];unsigned index=(type&255)+(!!(type&256))*6+(!!(type&1024))*12;if(index>=32)die("Dummy texture type %u",type);
    tex_t *t=&dummy[!!storage][index];
    if(!t->serial){t->serial=UINT64_MAX-index-(!!storage)*32;t->d=(nvn_texdesc){.target=(type&255)==4?NVN_TEXTURE_TARGET_3D:(type&255)==5?NVN_TEXTURE_TARGET_CUBEMAP:(type&256)?NVN_TEXTURE_TARGET_2D_ARRAY:NVN_TEXTURE_TARGET_2D,.width=1,.height=1,.depth=(type&255)==5?6:1,.levels=1,.format=(type&1024)?NVN_FORMAT_DEPTH32F:NVN_FORMAT_RGBA8};nvn_tex_layout(&t->d,&t->L);}
    image_entry *e=image_get(t);if(!e->populated){image_access(e,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);VkClearColorValue zero={0};VkImageSubresourceRange r={e->aspect,0,1,0,e->texture.L.layers};if(type&1024){VkClearDepthStencilValue depth={1,0};vkCmdClearDepthStencilImage(command,e->image,VK_IMAGE_LAYOUT_GENERAL,&depth,1,&r);}else vkCmdClearColorImage(command,e->image,VK_IMAGE_LAYOUT_GENERAL,&zero,1,&r);e->populated=1;}image_access(e,stage,access);return e;
}
static VkImageView descriptor_view(const texture_descriptor *descriptor,image_entry *e,uint32_t type,int storage) {
    view_t v=descriptor->valid?descriptor->view:(view_t){0};
    unsigned mip=v.set_levels?v.base_level:0,layers=v.set_layers?v.min_layer:0;
    if(mip>=e->texture.d.levels||layers>=e->texture.L.layers)die("Vulkan descriptor view bounds");
    VkImageViewType vt;switch(type&255){case 1:vt=(type&256)?VK_IMAGE_VIEW_TYPE_1D_ARRAY:VK_IMAGE_VIEW_TYPE_1D;break;case 4:vt=VK_IMAGE_VIEW_TYPE_3D;break;case 5:vt=(type&256)?VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:VK_IMAGE_VIEW_TYPE_CUBE;break;default:vt=(type&256)?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D;}
    uint32_t levels=storage?1:v.set_levels&&v.nlevels>0?v.nlevels:e->texture.d.levels-mip,count=v.set_layers&&v.nlayers>0?v.nlayers:e->texture.L.layers-layers;
    if(vt==VK_IMAGE_VIEW_TYPE_1D||vt==VK_IMAGE_VIEW_TYPE_2D||vt==VK_IMAGE_VIEW_TYPE_3D)count=1;
    VkImageAspectFlags aspect=e->aspect & VK_IMAGE_ASPECT_DEPTH_BIT?VK_IMAGE_ASPECT_DEPTH_BIT:e->aspect;
    return image_view_get(e,vt,(VkImageSubresourceRange){aspect,mip,levels,layers,count});
}
#include "nvn_vulkan_uniform.inc"
#include "nvn_vulkan_binding_cache.inc"
// arena 槽位承载的 uniform 把位置交给动态偏移；描述符只留块基址与范围，同一块内换槽位就不再改变描述符。
// A uniform carried by an arena slot hands its position to the dynamic offset; the descriptor keeps only the block
// base and the range, so moving slots inside one block no longer changes the descriptor.
static VkDescriptorBufferInfo uniform_dynamic_split(const shader_binding *b,VkDescriptorBufferInfo info,uint32_t *dynamic_offset) {
    if(!descriptor_uniform_dynamic(b))return info;
    if(info.offset>(uint64_t)UINT32_MAX)die("Vulkan dynamic uniform offset range");
    *dynamic_offset=(uint32_t)info.offset;info.offset=0;return info;
}
static VkDescriptorBufferInfo uniform_buffer(shader_entry *s,const shader_binding *b,uint32_t *dynamic_offset) {
    *dynamic_offset=0;
    // 支持缓冲与每个着色器的 c1 始终不变，跨绘制共享，避免反复清零 64 KiB。
    // Support data and each shader's c1 are immutable; share across draws instead of repeatedly clearing 64 KiB.
    static vk_buffer support,zero;
    if(b->kind==0){if(!support.buffer){support=buffer_create(65536);memset(support.mapped,0,65536);for(int i=0;i<73;i++)*(float*)((uint8_t*)support.mapped+192+16*i)=1;immutable_buffer_bytes+=support.capacity;}return (VkDescriptorBufferInfo){support.buffer,0,65536};}
    if(b->slot==1){if(!s->constant_buffer.buffer){s->constant_buffer=buffer_create(65536);memset(s->constant_buffer.mapped,0,65536);memcpy(s->constant_buffer.mapped,s->c1,s->c1_size);immutable_buffer_bytes+=s->constant_buffer.capacity;}return (VkDescriptorBufferInfo){s->constant_buffer.buffer,0,65536};}
    if(b->slot==0){VkDescriptorBufferInfo current;if(uniform_driver_current(s->stage,0,&current))return uniform_dynamic_split(b,current,dynamic_offset);
        uint8_t data[0x610]={0};unsigned base=s->stage==0?0x110:s->stage==5?0x310:0x510;
        for(unsigned i=0;i<16;i++){memcpy(data+base+16*i,&state.ssbo[s->stage][i].addr,8);uint32_t size=state.ssbo[s->stage][i].size;memcpy(data+base+16*i+8,&size,4);}
        // 计算 c0 同时保存 0x210 的 UBO 地址表和 0x310 的 SSBO 地址表。
        // Compute c0 contains both the UBO address table at 0x210 and the SSBO table at 0x310.
        if(s->stage==5)for(unsigned i=0;i<16;i++){memcpy(data+0x210+16*i,&state.ubo[5][i].addr,8);uint32_t size=state.ubo[5][i].size;memcpy(data+0x210+16*i+8,&size,4);}
        return uniform_dynamic_split(b,uniform_driver(s->stage,0,data,base+256),dynamic_offset);}
    if(b->slot==2){VkDescriptorBufferInfo current;if(uniform_driver_current(s->stage,1,&current))return uniform_dynamic_split(b,current,dynamic_offset);
        uint8_t data[0x968]={0};for(unsigned i=0;i<128;i++){uint32_t tex=state.textures[s->stage][i],samp=state.samplers[s->stage][i];memcpy(data+0x168+8*i,&tex,4);memcpy(data+0x568+8*i,&samp,4);}for(unsigned i=0;i<8;i++){uint32_t image=state.images[s->stage][i];memcpy(data+0x120+8*i,&image,4);}
        return uniform_dynamic_split(b,uniform_driver(s->stage,1,data,sizeof data),dynamic_offset);}
    if(b->slot>=3&&b->slot<19){const p_bind_buf *u=&state.ubo[s->stage][b->slot-3];uint32_t n=u->addr&&u->size?(u->size<65536?u->size:65536):0;
        // 未绑定 uniform 的完整范围同为零，可共享一个不可变页，不占每 draw 的 arena 空间。
        // Unbound uniforms have the same all-zero range, so share an immutable page without per-draw arena space.
        if(!n){if(!zero.buffer){zero=buffer_create(65536);memset(zero.mapped,0,65536);immutable_buffer_bytes+=zero.capacity;}return (VkDescriptorBufferInfo){zero.buffer,0,65536};}
        return uniform_dynamic_split(b,uniform_read_cached(u->addr,n),dynamic_offset);}
    die("Unsupported Vulkan constant slot %u",b->slot);
}
typedef struct descriptor_chunk { VkDescriptorPool pool; struct descriptor_chunk *next; } descriptor_chunk;
static descriptor_chunk *descriptor_chunks,*descriptor_current;
static void descriptor_reset(void) {
    // 前一批 GPU 已完成，可整体重用描述符；容量不足时保留额外块供后续批次使用。
    // The previous batch has completed, so descriptors can be reset together; retain extra chunks for later batches.
    for(descriptor_chunk *c=descriptor_chunks;c;c=c->next)VK_CHECK(vkResetDescriptorPool(device,c->pool,0));descriptor_current=descriptor_chunks;uniform_reset();binding_cache_reset();
}
static descriptor_chunk *descriptor_chunk_new(void) {
    descriptor_chunk *c=calloc(1,sizeof *c);if(!c)die("Vulkan descriptor chunk allocation");
    // 池必须覆盖用到的每个描述符类型；动态 uniform 是类型 8，只列 0-7 会让分配失败。
    // The pool must cover every descriptor type in use; dynamic uniform is type 8, so listing only 0-7 makes allocation fail.
    VkDescriptorPoolSize sizes[10];for(unsigned i=0;i<10;i++)sizes[i]=(VkDescriptorPoolSize){i,4096};
    VkDescriptorPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=256,.poolSizeCount=10,.pPoolSizes=sizes};VK_CHECK(vkCreateDescriptorPool(device,&pi,NULL,&c->pool));return c;
}
static VkDescriptorSet descriptor_allocate(VkDescriptorSetLayout layout) {
    if(!descriptor_current){descriptor_chunks=descriptor_chunk_new();descriptor_current=descriptor_chunks;}
    VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorSetCount=1,.pSetLayouts=&layout};VkDescriptorSet set;
    for(;;){ai.descriptorPool=descriptor_current->pool;VkResult result=vkAllocateDescriptorSets(device,&ai,&set);if(result==VK_SUCCESS)return set;
        if(result!=VK_ERROR_OUT_OF_POOL_MEMORY&&result!=VK_ERROR_FRAGMENTED_POOL)die("Vulkan descriptor allocation: %d",result);
        if(!descriptor_current->next){descriptor_current->next=descriptor_chunk_new();ai.descriptorPool=descriptor_current->next->pool;VK_CHECK(vkAllocateDescriptorSets(device,&ai,&set));descriptor_current=descriptor_current->next;return set;}
        descriptor_current=descriptor_current->next;
    }
}
typedef struct descriptor_scratch {
    VkDescriptorBufferInfo *buffers;
    VkDescriptorImageInfo *images;
    VkBufferView *texels;
    size_t buffer_capacity,image_capacity,texel_capacity;
} descriptor_scratch;
static descriptor_scratch descriptor_workspace;
static void descriptor_reserve(shader_entry *s) {
    size_t buffers=s->buffer_count,images=s->image_count,texels=s->texel_count;
    // 队列锁串行调用 bind_shader；保留 CPU 工作区，所有 binding 一次更新，避免逐项 Vulkan 调用。
    // The queue lock serializes bind_shader; retain CPU scratch and update all bindings with one Vulkan call.
    if(buffers>descriptor_workspace.buffer_capacity){descriptor_workspace.buffers=realloc(descriptor_workspace.buffers,buffers*sizeof *descriptor_workspace.buffers);if(!descriptor_workspace.buffers)die("Vulkan descriptor buffers allocation");descriptor_workspace.buffer_capacity=buffers;}
    if(images>descriptor_workspace.image_capacity){descriptor_workspace.images=realloc(descriptor_workspace.images,images*sizeof *descriptor_workspace.images);if(!descriptor_workspace.images)die("Vulkan descriptor images allocation");descriptor_workspace.image_capacity=images;}
    if(texels>descriptor_workspace.texel_capacity){descriptor_workspace.texels=realloc(descriptor_workspace.texels,texels*sizeof *descriptor_workspace.texels);if(!descriptor_workspace.texels)die("Vulkan descriptor texels allocation");descriptor_workspace.texel_capacity=texels;}
}
static VkPipelineStageFlags shader_pipeline_stage(const shader_entry *s) {
    switch(s->stage){case 0:return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;case 1:return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;case 5:return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;default:die("Unsupported Vulkan shader stage %u",s->stage);}
}
#include "nvn_vulkan_forward_bind.inc"
#include "nvn_vulkan_texel.inc"
static VkDescriptorSet bind_shader(shader_entry *s,post_op *lifetime,uint32_t *dynamic_offsets,unsigned *dynamic_count) {
    (void)lifetime;descriptor_reserve(s);
    unsigned dynamic_base=*dynamic_count;
    // 两个集合的动态偏移必须连续排在一套数组里：先第一个集合的全部偏移，再第二个集合的全部偏移。
    // Both sets' dynamic offsets must be contiguous in one array: every offset of the first set, then the second's.
    if(dynamic_base+s->dynamic_count>256)die("Vulkan dynamic uniform offset capacity: shader=%s base=%u count=%u",s->key,dynamic_base,s->dynamic_count);
    *dynamic_count=dynamic_base+s->dynamic_count;
    VkWriteDescriptorSet writes[256];
    uint64_t res_phase=dm_cpu_phase_begin();
    int trace_bind=getenv("DM_VK_TRACE_DISPATCH")!=NULL||perf_config_int("trace_dispatch",1);
    for(unsigned i=0;i<s->count;i++){
        const shader_binding *b=&s->bindings[i];const shader_binding_plan *plan=&s->binding_plan[i];VkDescriptorType type=plan->type;VkDescriptorBufferInfo *buffers=NULL;VkDescriptorImageInfo *images=NULL;VkBufferView *texel_views=NULL;
        if(trace_bind&&!strcmp(s->key,"60f3906f2280721c"))LOG("[vulkan-bind] %s i=%u/%u binding=%u type=%d count=%u kind=%u flags=%u",
            s->key,i,s->count,b->binding,(int)type,b->count,b->kind,b->flags);
        if(descriptor_type_uniform(type)||type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)buffers=descriptor_workspace.buffers+plan->offset;
        else if(type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER||type==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)texel_views=descriptor_workspace.texels+plan->offset;
        else images=descriptor_workspace.images+plan->offset;
        VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstBinding=b->binding,.descriptorCount=b->count,.descriptorType=type,.pBufferInfo=buffers,.pImageInfo=images,.pTexelBufferView=texel_views};
        VkDescriptorBufferInfo uniform={0};uint32_t uniform_offset=0;
        if(descriptor_type_uniform(type)){uniform=uniform_buffer(s,b,&uniform_offset);if(plan->dynamic_index>=0)for(unsigned j=0;j<b->count;j++)dynamic_offsets[dynamic_base+plan->dynamic_index+j]=uniform_offset;}
        for(unsigned j=0;j<b->count;j++){
            if(descriptor_type_uniform(type))buffers[j]=uniform;
            else if(type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER){
                int uniform_ptr=plan->uniform_pointer,slot=plan->storage_slot;
                const p_bind_buf *u=uniform_ptr?&state.ubo[s->stage][slot]:&state.ssbo[s->stage][slot];uint64_t bytes=u->size?u->size:65536;vk_buffer buffer;
                // BufferUsageFlags.Write 是位 0；图像 flags 的位号不可用于 SSBO。
                // BufferUsageFlags.Write is bit 0; image flag bits do not apply to SSBOs.
                if(u->addr&&!(b->flags&1))buffers[j]=forward_readonly_shader_buffer(s,b,j,slot,UINT32_MAX,uniform_ptr?2:1,u->size,u->addr,bytes,gpu_limits.minStorageBufferOffsetAlignment,shader_pipeline_stage(s),VK_ACCESS_SHADER_READ_BIT);
                else{post_op *p=post_new(u->addr?0:3);p->destination=(void*)(uintptr_t)u->addr;p->size=bytes;p->output=buffer_create(bytes);if(u->addr){post_register_range(p);forward_seed(&p->output,u->addr,bytes,shader_pipeline_stage(s));}else memset(p->output.mapped,0,bytes);buffer=p->output;buffers[j]=(VkDescriptorBufferInfo){buffer.buffer,0,bytes};}w.pBufferInfo=buffers;
            }
            else if(type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER||type==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER){
                int writable=type==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;unsigned id=shader_handle(b,s->stage,j)&0xfffff;
                texture_descriptor d=descriptor_texture(id);
                uint64_t bytes=d.valid?d.texture.L.size:65536,read_before=snapshot_bytes,forward_before=forward_read_bytes;VkDescriptorBufferInfo buffer;
                // 只读类型化缓冲也由 GPU 直接消费已编码的完整版本；可写初值保留原 CPU 等待路径。
                // Read-only typed buffers consume complete recorded versions on the GPU; writable initial contents retain the existing CPU-wait path.
                if(d.valid&&!writable)buffer=forward_readonly_shader_buffer(s,b,j,UINT32_MAX,id,3,d.texture.L.size,(uintptr_t)d.texture.mem,bytes,texel_buffer_alignment(&d),shader_pipeline_stage(s),VK_ACCESS_SHADER_READ_BIT);
                else{post_op *p=post_new(d.valid&&writable?0:3);p->size=bytes;p->output=buffer_create(bytes);memset(p->output.mapped,0,bytes);if(d.valid&&snapshot_read((uintptr_t)d.texture.mem,p->output.mapped,bytes))die("Vulkan texel-buffer read");if(d.valid&&writable){p->destination=d.texture.mem;post_register_range(p);}buffer=(VkDescriptorBufferInfo){p->output.buffer,0,bytes};}
                texel_cpu_read_bytes+=snapshot_bytes-read_before;texel_forward_bytes+=forward_read_bytes-forward_before;
                texel_views[j]=texel_view_get(buffer,descriptor_view_format(d.valid?&d:NULL,VK_FORMAT_R32G32B32A32_SFLOAT));w.pTexelBufferView=texel_views;
            }
            else {
                uint32_t handle=shader_handle(b,s->stage,j);
                images[j]=(VkDescriptorImageInfo){.sampler=sampler_get_for_type((handle>>20)&0xfff,type==VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER?b->type:0),.imageLayout=VK_IMAGE_LAYOUT_GENERAL};
                if(type!=VK_DESCRIPTOR_TYPE_SAMPLER){texture_descriptor descriptor=descriptor_texture(handle&0xfffff);
                    // 只读计数：游戏绑了句柄、端口却查不到登记，就会退回 1x1 替身贴图，整个四边形采到同一个颜色。
                    // Read-only counter: the game bound a handle the port cannot resolve, which falls back to a 1x1
                    // stand-in texture; the whole quad then samples one colour.
                    if(handle&&!descriptor.valid){gpu_unregistered_textures++;if(gpu_unregistered_textures<=10||!(gpu_unregistered_textures%256))LOG("[vulkan-unregistered-tex] 绑定了未登记的贴图 id=%u handle=%#x shader=%s slot=%d（退回 1x1 替身）",handle&0xfffff,handle,s->key,b->binding);}
                    VkAccessFlags access=VK_ACCESS_SHADER_READ_BIT;
                    // ImageStore 是位 2；保守保留读取权限，反射未单独标注 imageLoad。
                    // ImageStore is bit 2; conservatively retain reads because reflection does not separately flag imageLoad.
                    if(b->kind==4&&(b->flags&4))access|=VK_ACCESS_SHADER_WRITE_BIT;
                    image_entry *e=descriptor_image(&descriptor,b->type,b->kind==4,shader_pipeline_stage(s),access);images[j].imageView=descriptor_view(&descriptor,e,b->type,b->kind==4);images[j].imageLayout=e->layout;}
                w.pImageInfo=images;
            }
        }
        writes[i]=w;
        if(trace_bind&&!strcmp(s->key,"60f3906f2280721c"))LOG("[vulkan-bind] %s i=%u done buffers=%p images=%p",s->key,i,(void*)buffers,(void*)images);
    }
    dm_cpu_phase_end(DM_CPU_PHASE_RESOLVE,res_phase);
    // 仍逐次执行上传/访问风险处理，再比较真实 payload；命中不能跳过 GPU 同步。
    // Perform uploads/access hazard handling on every call before comparing actual payloads; hits never skip GPU synchronization.
    graphics_probe_cs_inputs(s,writes,s->count,dynamic_offsets+dynamic_base,s->dynamic_count);
    uint64_t descset_phase=dm_cpu_phase_begin();
    VkDescriptorSet set=descriptor_cache_find(s->layout,writes,s->count);
    if(set){dm_cpu_phase_end(DM_CPU_PHASE_DESCSET,descset_phase);return set;}
    set=descriptor_allocate(s->layout);for(unsigned i=0;i<s->count;i++)writes[i].dstSet=set;
    if(s->count)vkUpdateDescriptorSets(device,s->count,writes,0,NULL);descriptor_cache_store(s->layout,set);
    dm_cpu_phase_end(DM_CPU_PHASE_DESCSET,descset_phase);
    return set;
}
#include "nvn_vulkan_dependencies.inc"
// 这个 compute 是否绑定 3D storage image（kind==4 且 type 维数==4，即 NVN_TEXTURE_TARGET_3D）。
// 历史上这里曾判断"3D image 让 Mali 建管线时 SIGSEGV"，但 2026-10-23 用独立最小程序
// （pack-workspace/probe/portrepro2.c）证伪：同一份 SPIR-V 在 3D/2D、任意 LocalSize Z、
// 带不带 requiredSubgroupSize 下都能建出。当年的崩溃真因是诊断代码在持 queue_lock 时重复加锁。
// 现在这个判据只服务于两个**诊断**开关，缺省都不生效；保留它是为了驱动真的退回时还能应急定位。
// Whether this compute binds a 3D storage image (kind==4 and dimension type==4, i.e. NVN_TEXTURE_TARGET_3D).
// It was once believed that a 3D image made Mali SIGSEGV while creating the pipeline; a standalone minimal
// program (pack-workspace/probe/portrepro2.c) disproved that on 2026-10-23: the same SPIR-V builds for 3D and
// 2D, at any LocalSize Z, with or without requiredSubgroupSize. The crash back then came from a diagnostic
// that re-locked queue_lock. This predicate now only serves two *diagnostic* switches, both off by default;
// it stays so a real driver regression can still be triaged.
static int shader_binds_3d_storage_image(const shader_entry *s) {
    // 只在 Mali 上启用：这两个跳过开关会改变渲染结果，绝不能在 Adreno 手机上生效。
    // Mali only: both skip switches change rendering results and must never apply on an Adreno phone.
    if(!gpu_has_mali_local_z_limit())return 0;
    for(unsigned i=0;i<s->count;i++)if(s->bindings[i].kind==4&&(s->bindings[i].type&255)==4)return 1;
    return 0;
}
static int shader_binds_storage_image(const shader_entry *s) {
    for(unsigned i=0;i<s->count;i++)if(s->bindings[i].kind==4)return 1;
    return 0;
}
static int shader_host_writes(const shader_entry *s) {
    for(unsigned i=0;i<s->count;i++){const shader_binding *b=&s->bindings[i];if((b->kind==2&&(b->flags&1))||(b->kind==4&&(b->type&255)==2&&(b->flags&4)))return 1;}return 0;
}
static void dispatch_compute(const p_dispatch *d) {
    snapshot_prepare();graphics_pass_end();
    if(!state.program||!state.program->nstage)die("Vulkan dispatch without program");shader_entry *s=program_shader(state.program,0);if(s->stage!=5)die("Vulkan dispatch with graphics shader");
    { int trace=getenv("DM_VK_TRACE_DISPATCH")!=NULL||perf_config_int("trace_dispatch",1);
      if(trace)LOG("[vulkan-dispatch] %s enter dependencies check_layout=%d module=%p",s->key,s->layout!=VK_NULL_HANDLE,(void*)s->module); }
    dependencies_prepare_shader(s);
    { int trace=getenv("DM_VK_TRACE_DISPATCH")!=NULL||perf_config_int("trace_dispatch",1);
      if(trace)LOG("[vulkan-dispatch] %s dependencies done pipeline=%p",s->key,(void*)s->compute_pipeline); }
    // 诊断开关 skip_folded_compute：把"需要折叠 LocalSize Z"的 compute 直接跳过（不建管线、不派发）。
    // Mali 在这些着色器上会让驱动内部 SIGSEGV，跳过可以把它们从因果链里摘掉，确认其余部分能否跑通。
    // 这会产生不正确的结果（这些 compute 是写 3D 贴图的），只用于定位，不是产品方案。
    // Diagnostic switch skip_folded_compute: compute shaders that need the LocalSize Z fold are skipped
    // entirely (no pipeline, no dispatch). Mali SIGSEGVs inside the driver on them, so skipping removes them
    // from the causal chain to see whether everything else works. Results are wrong (those computes fill 3D
    // textures); this is for diagnosis only, not a product path.
    // Mali 上的 3D storage image compute 曾被认为必崩；2026-10-23 的实验证明那是
    // 诊断代码自锁造成的假象，管线本身完全能建出（见 docs/ANDROID_MALI_LIMITS.zh-CN.md）。
    // 因此缺省改为 0（照常建管线并派发）；保留开关只为将来驱动真的退回时能应急定位。
    // A 3D-storage-image compute on Mali was believed to always crash; the 2026-10-23 experiments showed
    // that was an artifact of a self-deadlocking diagnostic, and the pipeline builds fine (see
    // docs/ANDROID_MALI_LIMITS.zh-CN.md). So the default is now 0 (create and dispatch as usual); the switch
    // stays only for emergency triage if a driver regression ever brings the crash back.
    { static int skip3d=-1;
      if(skip3d<0){const char *e=getenv("DM_VK_MALI_SKIP_3D_COMPUTE");
        skip3d=e&&*e?strcmp(e,"0")!=0:perf_config_int("mali_skip_3d_compute",0);}
      if(skip3d&&shader_binds_3d_storage_image(s)){
        static unsigned skipped;
        if(skipped<16){skipped++;LOG("[vulkan-dispatch] %s 不派发（3D storage image 在 Mali 上必崩，目标保留清空内容）",s->key);}
        return;
      } }
    { static int skip=-1;
      if(skip<0){const char *e=getenv("DM_VK_SKIP_FOLDED_COMPUTE");
        skip=e&&*e?strcmp(e,"0")!=0:perf_config_int("skip_folded_compute",0);}
      if(skip&&shader_binds_3d_storage_image(s)){
        static unsigned reported;
        if(reported<16){reported++;LOG("[vulkan-dispatch] %s 跳过（诊断 skip_folded_compute；3D storage image，Mali 会崩）",s->key);}
        return;
      } }
    // 与固定着色器同寿命复用计算管线；批次资源只包含本次绑定，不销毁共享管线。
    // Keep compute pipelines for the shader lifetime; batch resources hold bindings, not shared pipeline ownership.
    if(!s->compute_pipeline){resource_create_start creation=resource_create_begin();VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&s->layout};VK_CHECK(vkCreatePipelineLayout(device,&li,NULL,&s->compute_layout));
        // 诊断开关 quiesce_before_pipeline：建 compute 管线前先让 GPU 空一次。
        // 独立最小程序用同一份 SPIR-V 能建出管线，所以端口里的崩溃很可能与"创建时还有在途工作"有关；
        // 先空一次能把这条变量单独摘出来。
        // Diagnostic quiesce_before_pipeline: drain the GPU before creating a compute pipeline. A standalone
        // minimal program builds a pipeline from the same SPIR-V, so the port's crash may depend on in-flight
        // work at creation time; draining isolates that variable.
        if(perf_config_int("quiesce_before_pipeline",0)){
            graphics_pass_end();
            // 这里由录制线程调用，它已经持 queue_lock（见 nvn_vulkan_guest_publish.inc 的说明），
            // 再对同一把非递归锁加锁会自锁：整进程所有线程停在 futex 上，日志断在 dependencies done 之后。
            // This runs on the recording thread, which already holds queue_lock (see nvn_vulkan_guest_publish.inc);
            // locking that non-recursive mutex again self-deadlocks the whole process on a futex, and the log
            // stops right after "dependencies done".
            VkResult q=vkQueueWaitIdle(queue);
            LOG("[vulkan-pipeline] %s 建管线前已等待 GPU 空闲 result=%d",s->key,q);
        }
        // 有 storage image 的 compute 是 Mali 的已知崩溃点：先记下来，这样即便驱动随后崩掉，
        // 日志里也能看出是哪一个、以及它绑了几个 image。
        // A compute with a storage image is the known Mali crash point: record it first so that even if the
        // driver dies right after, the log still shows which shader and how many image bindings it had.
        if(shader_binds_storage_image(s))
            LOG("[vulkan-pipeline] %s 带 storage image 的 compute（Mali 已知崩溃点；bindings=%u）",s->key,s->count);
        VkComputePipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=s->module,.pName="main"},.layout=s->compute_layout};
        // 把交给驱动的每一项都打出来：独立最小程序用同一份 SPIR-V 能建出管线，所以差异必然在参数或
        // 布局里。没有这份记录就只能继续猜。
        // Log every field handed to the driver: a standalone minimal program builds a pipeline from the same
        // SPIR-V, so the difference must be in the parameters or the layout. Without this record it is guesswork.
        if(perf_config_int("trace_dispatch",1)){
            LOG("[vulkan-pipeline-dump] %s module=%p pName=%s layout=%p cache=%p stage_flags=%#x pNext=%p",
                s->key,(void*)s->module,ci.stage.pName,(void*)s->compute_layout,(void*)pipeline_cache,
                (unsigned)ci.stage.flags,(void*)ci.stage.pNext);
            for(unsigned i=0;i<s->count;i++){
                const shader_binding *b=&s->bindings[i];const shader_binding_plan *plan=&s->binding_plan[i];
                LOG("[vulkan-pipeline-dump]     binding=%u type=%d descCount=%u stageFlags=none slot=%u kind=%u flags=%#x",
                    b->binding,(int)plan->type,b->count,plan->storage_slot,b->kind,b->flags);
            }
        }
#ifdef __ANDROID__
        VkPipelineShaderStageRequiredSubgroupSizeCreateInfo required;
        if(!dm_vk_driver_prepare_shader_stage(&vk_api,&ci.stage,&required))die("Vulkan compute subgroup policy: %s",dm_vk_driver_error(&vk_api));
#endif
        VK_CHECK(pipeline_create_compute(&ci,&s->compute_pipeline));resource_create_end(2,creation,s->key,NULL);
        // Mali 驱动在 LocalSize 的 Z>1 的 compute 管线创建时会在自己内部 SIGSEGV（实测 pc/lr 都落在
        // libGLES_mali.so 的代码段、addr=0x18）。换一种等价调度：把 Z 折进 dispatch 的 Z 维，
        // LocalSize 保持 Z=1，线程总数与访存映射完全不变（视作 (x,y,z*LocalZ+z') 展开）。
        // Mali's driver SIGSEGVs inside itself while creating a compute pipeline whose LocalSize has Z>1
        // (measured: pc and lr both inside libGLES_mali.so, addr=0x18). Use an equivalent schedule instead:
        // fold Z into the dispatch Z dimension and keep LocalSize Z=1, so thread counts and memory mapping
        // are unchanged (the same launch expressed as (x, y, z*LocalZ+z')).
        }
    post_op *const *output_scope=post_copy_tail;forward_scope_marker=output_scope;
    uint32_t dynamic_offsets[256];unsigned dynamic_count=0;
    // 分段落点：崩溃在驱动内时，只有"走到哪一步"能说明是哪次调用炸的（Mali 会死在驱动自己的代码里）。
    // Step markers: when the crash is inside the driver, only "how far we got" says which call blew up
    // (Mali dies inside its own code).
    int trace_dispatch=getenv("DM_VK_TRACE_DISPATCH")!=NULL||perf_config_int("trace_dispatch",1);
    if(trace_dispatch)LOG("[vulkan-dispatch] %s begin bind_shader bindings=%u",s->key,s->count);
    VkDescriptorSet set=bind_shader(s,NULL,dynamic_offsets,&dynamic_count);
    // LocalSize Z 被折成 1 时，dispatch 的 Z 要按同样倍数放大，线程总数才不变。
    // When LocalSize Z was folded to 1, the dispatch Z must grow by the same factor to keep the thread count.
    uint32_t groups_z=d->z;
    if(s->local_z_fold>1){groups_z=d->z*s->local_z_fold;
        { int trace=getenv("DM_VK_TRACE_DISPATCH")!=NULL||perf_config_int("trace_dispatch",1);
          if(trace)LOG("[vulkan-dispatch] %s LocalSize Z folded %ux: dispatch z %u -> %u",s->key,s->local_z_fold,d->z,groups_z); } }
    if(trace_dispatch)LOG("[vulkan-dispatch] %s bound set=%p dynamic=%u pipeline=%p layout=%p dispatch=%u,%u,%u",
        s->key,(void*)set,dynamic_count,(void*)s->compute_pipeline,(void*)s->compute_layout,d->x,d->y,d->z);
    vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,s->compute_pipeline);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,s->compute_layout,0,1,&set,dynamic_count,dynamic_offsets);graphics_probe_dispatch(d);vkCmdDispatch(command,d->x,d->y,groups_z);gpu_snapshot_consumer_encoded();forward_record_outputs(output_scope,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    if(trace_dispatch)LOG("[vulkan-dispatch] %s recorded",s->key);
    graphics_probe_cs_outputs(s);
    // 图像消费者由 image_access 精确同步；仅真实缓冲写回需要主机可见性屏障。
    // image_access synchronizes image consumers precisely; only actual buffer writebacks need host visibility.
    if(shader_host_writes(s)){VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);}
}

#include "nvn_vulkan_draw.inc"
