// 独立测试：Mali LocalSize Z 折叠时改写着色器手算的 z 索引。
// 折叠把 LocalSize.z 从 8 折到 4、dispatch.z 乘 2 之后，驱动算出的 global z 仍然覆盖原区间，
// 但着色器自己手算的 (WorkgroupID[2] << 3) + LocalInvocationID[2] 只落在
// {0..3, 8..11, 16..19, 24..27}，一半 z 切片永远不写 —— 写 3D 贴图的 compute 因此只填了一半，
// 3D 世界几乎没有环境光（界面正常、世界近乎全黑）。
// 这个测试证明：折叠时把 z 那条移位从 <<3 改成 <<2 之后，手算索引与驱动 global z 一致，
// 而 x/y 的移位不受影响。
//
// Standalone test: the Mali LocalSize Z fold must rewrite the shader's hand-computed Z index.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../native/nvn_vulkan_zfold.inc"

typedef struct { uint32_t *w; size_t n,cap; } mod_t;

static void mod_put(mod_t *m,uint32_t word) {
    if(m->n==m->cap){m->cap=m->cap?m->cap*2:64;m->w=realloc(m->w,m->cap*sizeof *m->w);assert(m->w);}
    m->w[m->n++]=word;
}
static void mod_instr(mod_t *m,uint32_t opcode,uint32_t len,const uint32_t *ops) {
    mod_put(m,(len<<16)|opcode);
    for(uint32_t i=0;i+1<len;i++)mod_put(m,ops[i]);
}

// 造一个最小 compute 模块：三条手算全局索引（x/y/z 各一条），移位量都是 <<3。
// Builds a minimal compute module with three hand-computed global indices, all shifted by <<3.
static void build_module(mod_t *m,int with_z,int with_shift_amount_two) {
    memset(m,0,sizeof *m);
    uint32_t next=1;
    uint32_t id_void=next++,id_int=next++,id_uint=next++,id_vec=next++;
    uint32_t id_ptr_vec=next++,id_ptr_uint=next++,id_fntype=next++;
    uint32_t id_wg=next++,id_li=next++;
    uint32_t id_c0=next++,id_c1=next++,id_c3=next++;
    uint32_t id_c2=with_shift_amount_two?next++:0;
    uint32_t id_fn=next++,id_label=next++;
    uint32_t id_ac[3]={0,0,0},id_load[3]={0,0,0},id_bc[3]={0,0,0},id_sh[3]={0,0,0};
    for(int c=0;c<3;c++){id_ac[c]=next++;id_load[c]=next++;id_bc[c]=next++;id_sh[c]=next++;}
    mod_put(m,0x07230203);mod_put(m,0x00010000);mod_put(m,0);mod_put(m,next);mod_put(m,0);
    uint32_t ops[8];
    ops[0]=1;mod_instr(m,17,2,ops);                                   // OpCapability Shader
    ops[0]=0;ops[1]=1;mod_instr(m,14,3,ops);                           // OpMemoryModel Logical GLSL450
    ops[0]=5;ops[1]=id_fn;ops[2]=0x006E6961;ops[3]=id_wg;ops[4]=id_li; // OpEntryPoint GLCompute %fn "main" %wg %li
    mod_instr(m,15,6,ops);
    ops[0]=id_wg;ops[1]=11;ops[2]=26;mod_instr(m,71,4,ops);            // OpDecorate %wg BuiltIn WorkgroupId
    ops[0]=id_li;ops[1]=11;ops[2]=27;mod_instr(m,71,4,ops);            // OpDecorate %li BuiltIn LocalInvocationId
    ops[0]=id_void;mod_instr(m,19,2,ops);                              // OpTypeVoid
    ops[0]=id_int;ops[1]=32;ops[2]=1;mod_instr(m,21,4,ops);            // OpTypeInt %int 32 1
    ops[0]=id_uint;ops[1]=32;ops[2]=0;mod_instr(m,21,4,ops);           // OpTypeInt %uint 32 0
    ops[0]=id_vec;ops[1]=id_uint;ops[2]=3;mod_instr(m,23,4,ops);        // OpTypeVector %v3uint %uint 3
    ops[0]=id_ptr_vec;ops[1]=1;mod_instr(m,32,4,ops);                  // OpTypePointer Input %v3uint
    ops[0]=id_ptr_uint;ops[1]=1;mod_instr(m,32,4,ops);                 // OpTypePointer Input %uint
    ops[0]=id_fntype;ops[1]=id_void;mod_instr(m,33,3,ops);             // OpTypeFunction %fntype %void
    ops[0]=id_ptr_vec;ops[1]=id_wg;ops[2]=1;mod_instr(m,59,4,ops);     // OpVariable %wg
    ops[0]=id_ptr_vec;ops[1]=id_li;ops[2]=1;mod_instr(m,59,4,ops);     // OpVariable %li
    ops[0]=id_int;ops[1]=id_c0;ops[2]=0;mod_instr(m,43,4,ops);
    ops[0]=id_int;ops[1]=id_c1;ops[2]=1;mod_instr(m,43,4,ops);
    if(id_c2){ops[0]=id_int;ops[1]=id_c2;ops[2]=2;mod_instr(m,43,4,ops);}
    ops[0]=id_int;ops[1]=id_c3;ops[2]=3;mod_instr(m,43,4,ops);
    ops[0]=id_void;ops[1]=id_fn;ops[2]=0;ops[3]=id_fntype;mod_instr(m,54,5,ops); // OpFunction
    ops[0]=id_label;mod_instr(m,248,2,ops);                           // OpLabel
    for(int c=0;c<3;c++){
        if(c==2&&!with_z)continue;
        if(c==2&&!id_c2){ /* 没有值为 2 的常量时用 c1 顶替：这条 AccessChain 不再是分量 2 */ }
        uint32_t index=c==2&&!id_c2?id_c1:(c==2?id_c2:id_c0);
        ops[0]=id_ptr_uint;ops[1]=id_ac[c];ops[2]=id_wg;ops[3]=index;mod_instr(m,65,5,ops);
        ops[0]=id_uint;ops[1]=id_load[c];ops[2]=id_ac[c];mod_instr(m,61,4,ops);
        ops[0]=id_int;ops[1]=id_bc[c];ops[2]=id_load[c];mod_instr(m,124,4,ops);
        if(with_shift_amount_two){
            ops[0]=id_int;ops[1]=id_sh[c];ops[2]=id_bc[c];ops[3]=id_c3;mod_instr(m,196,5,ops);
        }
    }
    mod_instr(m,253,1,ops);                                           // OpReturn
    mod_instr(m,56,1,ops);                                            // OpFunctionEnd
    // 记录关键 id 供断言用
    m->w[m->n]=0;
}

// 取所有 OpShiftLeftLogical 的移位量 id（按出现顺序）。
// Collect the shift amount ids of every OpShiftLeftLogical, in order.
static size_t shift_amounts(const uint32_t *w,size_t n,uint32_t *out,size_t cap) {
    size_t k=0,at=DM_SPIRV_HEADER_WORDS;
    while(at<n){uint32_t op=w[at]&0xFFFFu,len=w[at]>>16;if(!len||at+len>n)break;
        if(op==DM_SPIRV_OP_SHIFT_LEFT_LOGICAL&&len>=5&&k<cap)out[k++]=w[at+4];
        at+=len;}
    return k;
}

int main(void) {
    // ① 正常形态：z 那条移位从 <<3 改成 <<2，x/y 不动，模块词数不变。
    {
        mod_t m;build_module(&m,1,1);
        uint32_t before[8],after[8];
        size_t n0=shift_amounts(m.w,m.n,before,8);
        assert(n0==3);
        uint32_t *w=m.w;size_t n=m.n;
        int r=dm_spirv_fix_manual_z_index(&w,&n,8);
        assert(r==1);
        size_t n1=shift_amounts(w,n,after,8);
        assert(n1==3);
        assert(n==m.n);                                   // 词数没变：值 2 的常量本来就在
        assert(after[0]==before[0]&&after[1]==before[1]); // x/y 移位没动
        assert(after[2]!=before[2]);                      // z 移位改了
        // 改完之后手算 z = gz*4 + lz，与驱动的 global z 一致。
        // 移位量的常量值必须变成 2。
        uint32_t type=0,value=0;
        dm_spirv_const_of(w,n,after[2],&type,&value);
        assert(value==2);
        // WorkgroupId 仍能按 BuiltIn 装饰认出（接口列表顺序无关）。
        assert(dm_spirv_workgroup_id(w,n)!=0);
        free(w);
        printf("z-fold rewrite: Z shift <<3 -> <<2, x/y untouched, module size unchanged PASS\n");
    }
    // ② 没有 z 分量（只有 x/y）：没有 z 索引要改，模块原样返回。
    {
        mod_t m;build_module(&m,0,1);
        uint32_t *w=m.w;size_t n=m.n;
        int r=dm_spirv_fix_manual_z_index(&w,&n,8);
        assert(r==0);
        assert(n==m.n);
        free(w);
        printf("z-fold rewrite: no Z component, module unchanged PASS\n");
    }
    // ③ 完全没有手算索引（没有移位）：不需要改写。
    {
        mod_t m;build_module(&m,1,0);
        uint32_t *w=m.w;size_t n=m.n;
        int r=dm_spirv_fix_manual_z_index(&w,&n,8);
        assert(r==0);
        assert(n==m.n);
        free(w);
        printf("z-fold rewrite: no hand-computed index, module unchanged PASS\n");
    }
    printf("SPIR-V LocalSize Z fold index rewrite: 3/3 PASS\n");
    return 0;
}
