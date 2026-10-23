// Mali LocalSize Z 折叠的配套改写：折叠 compute 的 LocalSize.z 时，
// 同步改写着色器**自己手算**的 Z 索引移位。
//
// 为什么必须一起改：Ryujinx 生成的 compute 不用 gl_GlobalInvocationID，而是手算
//     idx = (WorkgroupID[axis] << log2(LocalSize[axis])) + LocalInvocationID[axis]
// 折叠把 LocalSize.z 从 Z 折到 MALI_LOCAL_Z_TARGET（4）、dispatch.z 乘 Z/4 之后，
// 驱动算出的 global z = WorkgroupId.z*4 + LocalInvocationId.z 仍覆盖原来的 z 区间，
// 但着色器手算的 <<log2(Z) 不再匹配：gz∈[0, Z/4)、lz∈[0,4) 时
//     手算 z = gz*Z + lz
// 只落在 {0..3, Z..Z+3, 2Z..2Z+3, ...} —— 一半 z 切片永远不会被写（保持清空的全零），
// 另一半越界被着色器自己的边界检查丢掉。
// 后果：写 3D 贴图的 compute（32×8×32 光照网格、32³ 调色表等）只填了一半，
// 3D 场景几乎没有环境光 —— 界面正常、世界近乎全黑（实测 2026-10-23，Mali-G925）。
//
// 改法：把 z 那一条移位从 <<log2(Z) 改成 <<log2(MALI_LOCAL_Z_TARGET)，
// 手算 z 就与驱动的 global z 完全一致，每个切片恰好写一次，等价于没有折叠。
// x/y 的 LocalSize 没变，移位保持原样。
//
// The Z fold must also rewrite the shader's own hand-computed Z index shift.
// The Ryujinx-generated computes do not use gl_GlobalInvocationID: they compute
//     idx = (WorkgroupID[axis] << log2(LocalSize[axis])) + LocalInvocationID[axis]
// Folding LocalSize.z from Z to 4 while multiplying dispatch.z by Z/4 keeps the driver's
// global z range intact, but the hand-computed <<log2(Z) no longer matches: gz in [0, Z/4)
// and lz in [0,4) yield z = gz*Z + lz, which only hits {0..3, Z..Z+3, ...}. Half the z
// slices are never written and the rest is dropped by the shader's own bounds check, so
// the 3D textures end up half empty and the 3D world loses its ambient light.
// Rewriting the z shift to <<log2(MALI_LOCAL_Z_TARGET) makes the hand-computed z equal
// the driver's global z again.
#ifndef MALI_LOCAL_Z_TARGET
#define MALI_LOCAL_Z_TARGET 4u
#endif
#define DM_SPIRV_HEADER_WORDS 5
#define DM_SPIRV_OP_DECORATE 71
#define DM_SPIRV_OP_CONSTANT 43
#define DM_SPIRV_OP_FUNCTION 54
#define DM_SPIRV_OP_LOAD 61
#define DM_SPIRV_OP_ACCESS_CHAIN 65
#define DM_SPIRV_OP_BITCAST 124
#define DM_SPIRV_OP_SHIFT_LEFT_LOGICAL 196
#define DM_SPIRV_DECORATION_BUILTIN 11
#define DM_SPIRV_BUILTIN_WORKGROUP_ID 26

// 2 的整数次幂的 log2；不是 2 的幂（或为 0）返回 0。
// log2 of a power of two; 0 when the value is not a power of two.
static uint32_t dm_spirv_log2(uint32_t value) {
    uint32_t r=0;if(!value)return 0;
    while((value&1u)==0){value>>=1;r++;}
    return value==1?r:0;
}

// 按 BuiltIn 装饰找 gl_WorkGroupId（OpDecorate %var BuiltIn 26）。
// 不能靠 OpEntryPoint 接口列表的位置：不同着色器里 WorkgroupId / LocalInvocationId 的先后不一致
// （实测 2988a1dc 的接口列表是反的），按位置认会认错变量。
// Find gl_WorkGroupId by its BuiltIn decoration; the interface list order differs between shaders.
static uint32_t dm_spirv_workgroup_id(const uint32_t *w,size_t count) {
    size_t at=DM_SPIRV_HEADER_WORDS;
    while(at<count){uint32_t op=w[at]&0xFFFFu,len=w[at]>>16;if(!len||at+len>count)break;
        if(op==DM_SPIRV_OP_DECORATE&&len>=4&&w[at+2]==DM_SPIRV_DECORATION_BUILTIN&&w[at+3]==DM_SPIRV_BUILTIN_WORKGROUP_ID)return w[at+1];
        at+=len;}
    return 0;
}

// 按结果 id 找定义它的指令（只看值产生指令：AccessChain / Load / Bitcast / ShiftLeftLogical）。
// Find the instruction defining a result id (value producers only).
static size_t dm_spirv_def(const uint32_t *w,size_t count,uint32_t id,uint32_t *op_out) {
    size_t at=DM_SPIRV_HEADER_WORDS;
    while(at<count){uint32_t op=w[at]&0xFFFFu,len=w[at]>>16;if(!len||at+len>count)break;
        if(len>=3&&w[at+2]==id&&(op==DM_SPIRV_OP_ACCESS_CHAIN||op==DM_SPIRV_OP_LOAD||op==DM_SPIRV_OP_BITCAST||op==DM_SPIRV_OP_SHIFT_LEFT_LOGICAL)){
            if(op_out)*op_out=op;return at;}
        at+=len;}
    return (size_t)-1;
}

// 取某个常量的（类型 id, 值）。
// Get a constant's (type id, value).
static void dm_spirv_const_of(const uint32_t *w,size_t count,uint32_t id,uint32_t *type,uint32_t *value) {
    size_t at=DM_SPIRV_HEADER_WORDS;
    while(at<count){uint32_t op=w[at]&0xFFFFu,len=w[at]>>16;if(!len||at+len>count)break;
        if(op==DM_SPIRV_OP_CONSTANT&&len>=4&&w[at+2]==id){*type=w[at+1];*value=w[at+3];return;}
        at+=len;}
    *type=0;*value=0;
}

// 找同类型、同值的常量 id；没有返回 0。
// Find a constant with the same type and value; 0 when absent.
static uint32_t dm_spirv_find_const(const uint32_t *w,size_t count,uint32_t type,uint32_t value) {
    size_t at=DM_SPIRV_HEADER_WORDS;
    while(at<count){uint32_t op=w[at]&0xFFFFu,len=w[at]>>16;if(!len||at+len>count)break;
        if(op==DM_SPIRV_OP_CONSTANT&&len>=4&&w[at+1]==type&&w[at+3]==value)return w[at+2];
        at+=len;}
    return 0;
}

// 插入一条 OpConstant（类型 type、值 value），返回新常量的 id；模块的 id 上界 +1，词数 +4。
// 插在常量段末尾（函数段之前），SPIR-V 的逻辑布局仍然成立。内存不够时返回 0，调用方负责报错。
// Insert one OpConstant; the id bound grows by one and the word count by four.
static uint32_t dm_spirv_insert_const(uint32_t **wp,size_t *countp,uint32_t type,uint32_t value) {
    uint32_t *w=*wp;size_t count=*countp;
    size_t at=DM_SPIRV_HEADER_WORDS,insert_at=0;
    while(at<count){uint32_t op=w[at]&0xFFFFu,len=w[at]>>16;if(!len||at+len>count)break;
        if(op==DM_SPIRV_OP_FUNCTION)break;
        if(op==DM_SPIRV_OP_CONSTANT)insert_at=at+len;
        at+=len;}
    uint32_t id=w[3];                                   // id 上界 | the id bound
    uint32_t *grown=malloc((count+4)*sizeof *grown);
    if(!grown)return 0;
    memcpy(grown,w,insert_at*sizeof *grown);
    grown[insert_at]=(4u<<16)|DM_SPIRV_OP_CONSTANT;
    grown[insert_at+1]=type;grown[insert_at+2]=id;grown[insert_at+3]=value;
    memcpy(grown+insert_at+4,w+insert_at,(count-insert_at)*sizeof *grown);
    grown[3]=id+1;
    free(w);*wp=grown;*countp=count+4;
    return id;
}

// 这条移位是不是手算的全局索引（WorkgroupID 某个分量 << 移位量）？返回分量 0/1/2，不是则返回 -1。
// Which component of the hand-computed global index does this shift belong to?
static int dm_spirv_manual_axis(const uint32_t *w,size_t count,uint32_t workgroup_id,size_t shift_at) {
    uint32_t bop=0,lop=0,cop=0;
    size_t b=dm_spirv_def(w,count,w[shift_at+3],&bop);
    if(b==(size_t)-1||bop!=DM_SPIRV_OP_BITCAST)return -1;
    size_t l=dm_spirv_def(w,count,w[b+3],&lop);
    if(l==(size_t)-1||lop!=DM_SPIRV_OP_LOAD)return -1;
    size_t c=dm_spirv_def(w,count,w[l+3],&cop);
    if(c==(size_t)-1||cop!=DM_SPIRV_OP_ACCESS_CHAIN)return -1;
    if(w[c+3]!=workgroup_id)return -1;
    uint32_t itype=0,ivalue=0;
    dm_spirv_const_of(w,count,w[c+4],&itype,&ivalue);
    return ivalue<=2?(int)ivalue:-1;
}

// 折叠后同步改写手算的 z 索引移位。
// 返回 0：着色器没有手算的 z 索引（用的是 gl_GlobalInvocationID，或只算 x/y），无需改写。
// 返回 1：已改写。
// 返回 -1：手算了 z 索引但改不动 —— 调用方必须报错，不能带着错的索引上机。
// Rewrite the hand-computed Z index shift after the fold.
// 0: nothing to rewrite; 1: rewritten; -1: it uses one but cannot be fixed.
static int dm_spirv_fix_manual_z_index(uint32_t **wp,size_t *countp,uint32_t original_z) {
    uint32_t *w=*wp;size_t count=*countp;
    uint32_t workgroup=dm_spirv_workgroup_id(w,count);
    if(!workgroup)return 0;
    uint32_t old_shift=dm_spirv_log2(original_z);
    uint32_t new_shift=dm_spirv_log2(MALI_LOCAL_Z_TARGET);
    if(!old_shift||!new_shift)return -1;
    // 找 z 轴（分量 2）那条移位，并取它现在的移位量（类型 + 值）。
    size_t shift_at=(size_t)-1;uint32_t amount_type=0,amount_value=0;
    size_t at=DM_SPIRV_HEADER_WORDS;
    while(at<count){uint32_t op=w[at]&0xFFFFu,len=w[at]>>16;if(!len||at+len>count)break;
        if(op==DM_SPIRV_OP_SHIFT_LEFT_LOGICAL&&len>=5&&dm_spirv_manual_axis(w,count,workgroup,at)==2){shift_at=at;break;}
        at+=len;}
    if(shift_at==(size_t)-1)return 0;
    dm_spirv_const_of(w,count,w[shift_at+4],&amount_type,&amount_value);
    if(!amount_type||amount_value!=old_shift)return -1;   // 移位量不是 <<log2(Z)：推不出该改成什么
    // 新的移位量常量：同类型同值优先复用，没有就插入一条。
    uint32_t new_id=dm_spirv_find_const(w,count,amount_type,new_shift);
    if(!new_id){
        new_id=dm_spirv_insert_const(wp,countp,amount_type,new_shift);
        w=*wp;count=*countp;
    }
    // 插入常量会挪动后续指令，重新定位后把所有 z 轴那条都改掉。
    unsigned patched=0;
    at=DM_SPIRV_HEADER_WORDS;
    while(at<count){uint32_t op=w[at]&0xFFFFu,len=w[at]>>16;if(!len||at+len>count)break;
        if(op==DM_SPIRV_OP_SHIFT_LEFT_LOGICAL&&len>=5&&dm_spirv_manual_axis(w,count,workgroup,at)==2){w[at+4]=new_id;patched++;}
        at+=len;}
    return patched?1:0;
}
