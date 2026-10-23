// 用真正 Vulkan 队列验证复制、命令调用、事件发布和栅栏顺序。
// Verify copies, command calls, event publication and fence ordering on a real Vulkan queue.
#define _GNU_SOURCE
#define DM_VK_REFERENCE_GATES 1
#include "../native/nvn_vulkan.c"
#include <assert.h>
#include <unistd.h>
#include <sched.h>
static const void *tracked_vertices;
static const void *protected_vertices;
static uintptr_t write_after_read;
static uint32_t write_after_read_value;
static uint64_t dirty_generation=1;
void dm_guest_write_prepare(void *p, size_t n) { (void)n; if(p==tracked_vertices)dirty_generation++; }
int dm_fps_setting(void) { return 60; }
void dm_shot_written(const char *p, int ok) { (void)p; (void)ok; }
uint64_t dm_android_clock_ns(clockid_t clock) { struct timespec t; assert(!clock_gettime(clock, &t)); return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec; }
int dm_android_read_memory(uintptr_t address, void *destination, size_t size) {
    memcpy(destination,(void*)address,size);
    // 固定重现“读取刚结束时发生客体写入”；仅已保护的页产生缺页代数。
    // Deterministically inject a guest write just after a read; only protected pages produce a fault generation.
    if(address==write_after_read&&size>=sizeof(uint32_t)){*(uint32_t*)address=write_after_read_value;if(protected_vertices==(void*)address)dirty_generation++;write_after_read=0;}
    return 0;
}
void nvn_dirty_protect(const void *addr, size_t len) { (void)len;if(addr==tracked_vertices)protected_vertices=addr; }
int nvn_dirty_tracked(const void *addr, size_t len) { (void)len; return addr==tracked_vertices; }
int nvn_dirty_since(const void *addr, size_t len, uint64_t stamp) { (void)len; return addr==tracked_vertices && dirty_generation>stamp; }
uint64_t nvn_dirty_now(void) { return dirty_generation; }
void *front_metal_layer(void) { return NULL; }
static unsigned char packets[512];
static size_t used;
static void packet(unsigned op, const void *payload, size_t n) {
    op_hdr h = {op, (sizeof h + n + 7) & ~(size_t)7}; assert(used + h.size <= sizeof packets);
    memcpy(packets + used, &h, sizeof h); if (n) memcpy(packets + used + 8, payload, n); used += h.size;
}
static void *waiter(void *p) { assert(mtl_wait(*(uint64_t *)p, 1000000000)); return NULL; }
// 原有像素断言读取提交结果，显式等待；异步回归直接使用真实 commit。
// Existing pixel assertions consume submission results and explicitly wait; async regressions use the real commit.
static uint64_t async_raw_commit(void){return mtl_sub_commit();}
static uint64_t test_sync_commit(void){uint64_t ticket=async_raw_commit();assert(mtl_wait(ticket,UINT64_MAX));return ticket;}
#define mtl_sub_commit test_sync_commit
#include "sampler_test.inc"
#include "shadow_array_test.inc"
#include "handle_decode_test.inc"
#include "batch_resources_test.inc"
#include "uniform_arena_test.inc"
#include "binding_cache_test.inc"
#include "depth_range_test.inc"
#include "dependencies_test.inc"
#include "async_test.inc"
#include "forward_test.inc"
#include "pending_index_test.inc"
#include "forward_binding_test.inc"
#include "texel_forward_test.inc"
#include "readback_dependency_test.inc"
#include "guest_publish_test.inc"
#include "index_test.inc"
#include "packaged_texture_test.inc"
#include "packaged_subresources_test.inc"
#include "query_readback_test.inc"
#include "known_copy_snapshot_test.inc"
#include "typed_clear_test.inc"
int main(void) {
    assert(timestamp_delta(250,5,8)==11);
    assert(timestamp_delta(UINT64_MAX-2,3,64)==6);
    assert(timestamp_delta(12,15,0)==0);
    uint8_t src[4096], dst[8192]; volatile uint32_t event = 11;
    for (unsigned i = 0; i < sizeof src; ++i) src[i] = i * 37 + 13;
    memset(dst, 0xcd, sizeof dst); mtl_pool_add(dst, sizeof dst);
    assert(mtl_addr((uintptr_t)dst + 12, sizeof dst - 12, NULL, NULL));
    assert(!mtl_addr((uintptr_t)dst + 12, sizeof dst - 11, NULL, NULL));
    assert(!mtl_wait(mtl_last_fence() + 1, 1000000));
    uint64_t copy[3] = {(uintptr_t)src, (uintptr_t)dst, sizeof src}; packet(OP_COPY_B2B, copy, sizeof copy);
    uint64_t signal[2] = {(uintptr_t)&event, (uint64_t)1 << 32}; packet(OP_SIGNAL_EVENT, signal, sizeof signal); packet(OP_END, NULL, 0);
    uint64_t expected = mtl_last_fence() + 1; pthread_t thread; assert(!pthread_create(&thread, NULL, waiter, &expected));
    mtl_sub_begin(); mtl_exec(packets); assert(event == 11 && dst[0] == 0xcd); uint64_t fence = mtl_sub_commit();
    assert(mtl_wait(fence, 0)); pthread_join(thread, NULL); assert(event == 1); assert(!memcmp(src, dst, sizeof src));
    // 同提交先写再读同一区域：后一次必须读 GPU 已完成的前一次结果。
    // A write followed by a read of the same range in one submit must consume the completed GPU result.
    memset(dst,0xcd,sizeof dst);mtl_sub_begin(); mtl_sub_copy((uintptr_t)src, (uintptr_t)dst, sizeof src); mtl_sub_copy((uintptr_t)dst, (uintptr_t)(dst + 4096), sizeof src);
    assert(dst[0]==0xcd && dst[4096]==0xcd);mtl_sub_event(&event, 1, 3); mtl_sub_commit();
    assert(event == 4 && !memcmp(src, dst + 4096, sizeof src));
    unsigned char child[32]; op_hdr end = {OP_END, 8}, begin = {OP_BEGIN, 8}; memcpy(child, &begin, 8); memcpy(child + 8, &end, 8);
    used = 0; uint64_t call = (uintptr_t)child; packet(OP_CALL, &call, 8); packet(OP_END, NULL, 0);
    mtl_sub_begin(); mtl_exec(packets); mtl_sub_commit(); mtl_drain(); mtl_pool_del(dst);
    mtl_res_count counts; mtl_res_snapshot(&counts); assert(counts.pools == 0 && counts.pool_adds == 1);
    if (getenv("DM_VK_SHADERS")) {
        // 固定像素真值：NVN 解排布 + 采样绘制，再由计算核覆盖输出。
        // Fixed pixel references: NVN deswizzling plus sampled drawing, followed by compute overwriting the output.
        tex_t texture={.serial=100,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=8,.height=8,.depth=1,.levels=1}};
        nvn_tex_layout(&texture.d,&texture.L);texture.mem=calloc(1,texture.L.size);assert(texture.mem);
        uint8_t linear[8*8*4];for(unsigned i=0;i<64;i++){linear[i*4]=200;linear[i*4+1]=40;linear[i*4+2]=80;linear[i*4+3]=255;}assert(!nvn_swizzle(&texture.d,0,0,linear,8*4,texture.mem));
        tex_t target={.serial=101,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};nvn_tex_layout(&target.d,&target.L);
        uint8_t controls[3][0x880]={{1},{2},{3}};prog_t graphics={.nstage=2,.sd={{.control=controls[0]},{.control=controls[1]}}},compute={.nstage=1,.sd={{.control=controls[2]}}};
        mtl_set_tex_pool_n(4);mtl_reg_tex(1,&texture,NULL);mtl_reg_tex(2,&target,NULL);
        memset(&state,0,sizeof state);state.program=&graphics;state.rt.n=1;state.rt.color[0]=&target;state.mask.mask=UINT32_MAX;state.polygon.mode=2;state.textures[1][0]=1;
        p_draw triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};mtl_sub_begin();p_clear_color clear={.mask=15};clear_color(&clear);draw_graphics(&triangle,NULL);mtl_sub_commit();
        assert(samples_passed==256);
        uint8_t pixels[16*16*4];NVNcopyRegion region={0,0,0,16,16,1};mtl_read_texels(&target,0,0,&region,pixels,16*4,0);
        for(unsigned i=0;i<256;i++)assert(!memcmp(pixels+i*4,linear,4));
        state.program=&compute;state.images[5][0]=2;mtl_sub_begin();p_dispatch dispatch={2,2,1,0};dispatch_compute(&dispatch);mtl_sub_commit();mtl_read_texels(&target,0,0,&region,pixels,16*4,0);
        const uint8_t blue[4]={10,80,220,255};for(unsigned i=0;i<256;i++)assert(!memcmp(pixels+i*4,blue,4));
        // 同批两次 uniform 更新各自保留快照；真实像素验证左红右绿，提交前客体缓冲不变。
        // Two uniform updates in one batch retain separate snapshots; verify red/green pixels and no early guest writes.
        uint8_t extra_controls[2][0x880]={{4},{5}};prog_t solid={.nstage=2,.sd={{.control=controls[0]},{.control=extra_controls[0]}}};
        float uniform[4]={0},red[4]={1,0,0,1},green[4]={0,1,0,1};mtl_pool_add(uniform,sizeof uniform);
        state.program=&solid;state.ubo[1][0]=(p_bind_buf){.addr=(uintptr_t)uniform,.size=sizeof uniform};
        mtl_sub_begin();post_new(4);state.scissor=(p_scissor){0,0,8,16};uint64_t before=mtl_last_fence();
        mtl_sub_copy((uintptr_t)red,(uintptr_t)uniform,sizeof red);draw_graphics(&triangle,NULL);
        state.scissor.x=8;mtl_sub_copy((uintptr_t)green,(uintptr_t)uniform,sizeof green);draw_graphics(&triangle,NULL);
        assert(mtl_last_fence()==before && uniform[0]==0 && uniform[1]==0);mtl_sub_commit();assert(samples_passed==256 && uniform[1]==1);
        mtl_read_texels(&target,0,0,&region,pixels,16*4,0);const uint8_t red_pixel[4]={255,0,0,255},green_pixel[4]={0,255,0,255};
        for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++)assert(!memcmp(pixels+(y*16+x)*4,x<8?red_pixel:green_pixel,4));
        // 查询重置/报告跟随命令顺序；两个半屏绘制分别报告 128，不是累计 256。
        // Query resets/reports follow command order; each half-screen draw reports 128, rather than cumulative 256.
        uint64_t reports[2][2]={{0}};mtl_sub_begin();post_new(4);draw_graphics(&triangle,NULL);mtl_sub_counter((uintptr_t)reports[0],1);
        post_new(4);draw_graphics(&triangle,NULL);mtl_sub_counter((uintptr_t)reports[1],1);mtl_sub_commit();assert(reports[0][0]==128 && reports[1][0]==128);
        // 计算 SSBO 写回必须在后续复制读取前完成；再次计算须读到第一次的值。
        // Compute SSBO writes must complete before subsequent copies; a second dispatch must read the first result.
        uint32_t number=9,result=0;mtl_pool_add(&number,sizeof number);mtl_pool_add(&result,sizeof result);
        prog_t add={.nstage=1,.sd={{.control=extra_controls[1]}}};state.program=&add;state.ssbo[5][0]=(p_bind_buf){.addr=(uintptr_t)&number,.size=4};
        mtl_sub_begin();p_dispatch one={1,1,1,0};dispatch_compute(&one);dispatch_compute(&one);mtl_sub_copy((uintptr_t)&number,(uintptr_t)&result,4);mtl_sub_commit();assert(number==23 && result==23);
        // 待发布的 Tegra 排布复制也必须被下一次贴图采样看到。
        // A pending Tegra-layout copy must also be visible to the following texture sample.
        uint8_t *swizzled=calloc(1,texture.L.size);assert(swizzled);for(unsigned i=0;i<64;i++)memcpy(linear+i*4,blue,4);
        assert(!nvn_swizzle(&texture.d,0,0,linear,8*4,swizzled));mtl_pool_add(texture.mem,texture.L.size);
        state.program=&graphics;state.scissor=(p_scissor){0,0,16,16};mtl_sub_begin();mtl_sub_copy((uintptr_t)swizzled,(uintptr_t)texture.mem,texture.L.size);draw_graphics(&triangle,NULL);mtl_sub_commit();
        mtl_read_texels(&target,0,0,&region,pixels,16*4,0);for(unsigned i=0;i<256;i++)assert(!memcmp(pixels+i*4,blue,4));free(swizzled);mtl_pool_del(texture.mem);
        // 连续绘制共用一个通道/查询，仍保留每个真实像素与精确累计样本。
        // Consecutive draws share one pass/query while retaining actual pixels and exact sample totals.
        uint64_t start=dm_android_clock_ns(CLOCK_MONOTONIC),submits_before=mtl_last_fence(),passes_before=render_passes;mtl_sub_begin();post_new(4);
        p_draw_idx empty={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=0,.instances=1};draw_graphics(NULL,&empty);
        for(unsigned i=0;i<96;i++)draw_graphics(&triangle,NULL);assert(used_queries==1);mtl_sub_commit();assert(samples_passed==96*256 && buffer_reuses>0);
        assert(mtl_last_fence()-submits_before==1&&render_passes-passes_before==1);mtl_read_texels(&target,0,0,&region,pixels,16*4,0);for(unsigned i=0;i<256;i++)assert(!memcmp(pixels+i*4,blue,4));
        printf("Vulkan batching: 96 draws, 1 submit/pass/query, %.2f ms, allocations=%llu reuse=%llu\n",(dm_android_clock_ns(CLOCK_MONOTONIC)-start)/1e6,(unsigned long long)buffer_allocations,(unsigned long long)buffer_reuses);
        uint64_t layout_before=graphics_layout_creations,pass_before=graphics_pass_creations,frame_before=graphics_frame_creations;submits_before=mtl_last_fence();passes_before=render_passes;
        mtl_sub_begin();post_new(4);for(unsigned i=0;i<257;i++)draw_graphics(&triangle,NULL);mtl_sub_commit();assert(samples_passed==257*256);
        assert(mtl_last_fence()-submits_before==2&&render_passes-passes_before==2);
        assert(graphics_layout_creations==layout_before&&graphics_pass_creations==pass_before&&graphics_frame_creations==frame_before);
        // 真实顶点像素验证缓存复用与脏写失效；销池不能留下可复用的旧地址。
        // Real vertex pixels verify cache reuse and dirty invalidation; pool destruction must not retain reusable old addresses.
        float vertices[6]={-1,-1,3,-1,-1,3};mtl_pool_add(vertices,sizeof vertices);tracked_vertices=vertices;
        uint8_t vertex_control[0x880]={6};prog_t vertex_solid={.nstage=2,.sd={{.control=vertex_control},{.control=extra_controls[0]}}};
        state.program=&vertex_solid;state.attributes[0]=(va_st){.fmt=NVN_FORMAT_RG32F};state.streams[0]=(vs_st){.stride=8};state.vertices[0]=(NVNbufferRange){(uintptr_t)vertices,sizeof vertices};
        uint64_t hits=read_cache_hits;mtl_sub_begin();draw_graphics(&triangle,NULL);draw_graphics(&triangle,NULL);assert(read_cache_hits==hits+1);mtl_sub_commit();
        mtl_read_texels(&target,0,0,&region,pixels,16*4,0);for(unsigned i=0;i<256;i++)assert(!memcmp(pixels+i*4,green_pixel,4));
        for(unsigned i=0;i<6;i++)vertices[i]=9;dirty_generation++;mtl_sub_begin();clear_color(&clear);draw_graphics(&triangle,NULL);mtl_sub_commit();
        mtl_read_texels(&target,0,0,&region,pixels,16*4,0);for(unsigned i=0;i<sizeof pixels;i++)assert(pixels[i]==0);
        // 带基顶点的索引绘制只需读取实际前缀，不能读绑定范围后面的无效地址。
        // Indexed draws with a base vertex read only the actual prefix, never invalid addresses after the bound data.
        float shifted[8]={99,99,-1,-1,3,-1,-1,3};uint16_t indices[3]={0,1,2};
        state.vertices[0]=(NVNbufferRange){(uintptr_t)shifted,1u<<20};p_draw_idx indexed_triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.itype=NVN_INDEX_TYPE_UNSIGNED_SHORT,.count=3,.instances=1,.base_vertex=1,.ibuf=(uintptr_t)indices};
        mtl_sub_begin();draw_graphics(NULL,&indexed_triangle);mtl_sub_commit();mtl_read_texels(&target,0,0,&region,pixels,16*4,0);for(unsigned i=0;i<256;i++)assert(!memcmp(pixels+i*4,green_pixel,4));
        mtl_pool_del(vertices);assert(read_cache_bytes==0);tracked_vertices=NULL;
        // 只读 SSBO/类型化缓冲使用同一脏页缓存；计算类型化写回也能成为后续 uniform 输入。
        // Read-only SSBO/texel buffers share dirty tracking; compute texel writes can become later uniform inputs.
        uint8_t resource_controls[3][0x880]={{7},{8},{9}};prog_t storage_graphics={.nstage=2,.sd={{.control=controls[0]},{.control=resource_controls[0]}}};
        state.program=&storage_graphics;state.ssbo[1][0]=(p_bind_buf){.addr=(uintptr_t)uniform,.size=sizeof uniform};tracked_vertices=uniform;dirty_generation++;
        mtl_sub_begin();draw_graphics(&triangle,NULL);draw_graphics(&triangle,NULL);mtl_sub_commit();mtl_read_texels(&target,0,0,&region,pixels,16*4,0);for(unsigned i=0;i<256;i++)assert(!memcmp(pixels+i*4,green_pixel,4));
        mtl_sub_begin();mtl_sub_copy((uintptr_t)red,(uintptr_t)uniform,sizeof red);draw_graphics(&triangle,NULL);mtl_sub_commit();mtl_read_texels(&target,0,0,&region,pixels,16*4,0);for(unsigned i=0;i<256;i++)assert(!memcmp(pixels+i*4,red_pixel,4));
        tex_t texel={.serial=102,.mem=(uint8_t*)uniform,.d={.target=NVN_TEXTURE_TARGET_BUFFER,.format=NVN_FORMAT_RGBA32F,.width=1,.height=1,.depth=1,.levels=1}};nvn_tex_layout(&texel.d,&texel.L);mtl_reg_tex(3,&texel,NULL);
        prog_t texel_graphics={.nstage=2,.sd={{.control=controls[0]},{.control=resource_controls[1]}}};state.program=&texel_graphics;state.textures[1][0]=3;
        mtl_sub_begin();draw_graphics(&triangle,NULL);mtl_sub_commit();mtl_read_texels(&target,0,0,&region,pixels,16*4,0);for(unsigned i=0;i<256;i++)assert(!memcmp(pixels+i*4,red_pixel,4));
        prog_t texel_compute={.nstage=1,.sd={{.control=resource_controls[2]}}};state.program=&texel_compute;state.images[5][0]=3;
        mtl_sub_begin();dispatch_compute(&one);assert(uniform[0]==1);state.program=&solid;draw_graphics(&triangle,NULL);mtl_sub_commit();mtl_read_texels(&target,0,0,&region,pixels,16*4,0);for(unsigned i=0;i<256;i++)assert(!memcmp(pixels+i*4,blue,4));tracked_vertices=NULL;
        // 贴图哈希碰撞不混淆实际图像，删除碰撞项也不损坏同桶的存活贴图。
        // Texture hash collisions keep real images separate, and removing one leaves the other usable.
        tex_t colliding=target;colliding.serial+=4096;state.rt.color[0]=&colliding;mtl_sub_begin();p_clear_color red_clear={.mask=15,.rgba={1,0,0,1}};clear_color(&red_clear);mtl_sub_commit();
        mtl_read_texels(&colliding,0,0,&region,pixels,16*4,0);for(unsigned i=0;i<256;i++)assert(!memcmp(pixels+i*4,red_pixel,4));mtl_tex_release(&colliding);state.rt.color[0]=&target;
        mtl_read_texels(&target,0,0,&region,pixels,16*4,0);for(unsigned i=0;i<256;i++)assert(!memcmp(pixels+i*4,blue,4));
        // 同一 control 地址复用为新程序时依序号重解析，不能继续使用旧片元着色器。
        // Reusing one control address for a new program resolves the new serial, rather than retaining the old fragment shader.
        uint8_t reused_control[0x880]={4};solid.serial=40001;solid.sd[1].control=reused_control;state.program=&solid;memcpy(uniform,red,sizeof red);
        mtl_sub_begin();draw_graphics(&triangle,NULL);mtl_sub_commit();mtl_read_texels(&target,0,0,&region,pixels,16*4,0);for(unsigned i=0;i<256;i++)assert(!memcmp(pixels+i*4,red_pixel,4));
        reused_control[0]=2;solid.serial++;state.textures[1][0]=1;mtl_sub_begin();draw_graphics(&triangle,NULL);mtl_sub_commit();mtl_read_texels(&target,0,0,&region,pixels,16*4,0);for(unsigned i=0;i<256;i++)assert(!memcmp(pixels+i*4,blue,4));mtl_prog_release(&solid);
        // 选择两个确实同桶的地址，验证快照命中、脏写和销池仍各自正确。
        // Select genuinely colliding addresses and verify independent snapshot hits, dirty writes and pool removal.
        uint32_t *values=calloc(8193,16);int buckets[8192];for(unsigned i=0;i<8192;i++)buckets[i]=-1;assert(values);int first=-1,second=-1;
        for(unsigned i=0;i<8193;i++){unsigned bucket=read_bucket((uintptr_t)(values+i*4),16);if(buckets[bucket]>=0){first=buckets[bucket];second=i;break;}buckets[bucket]=i;}assert(first>=0&&second>=0);
        values[first*4]=17;values[second*4]=23;mtl_pool_add(values,8193*16);mtl_sub_begin();tracked_vertices=values+first*4;assert(*(uint32_t*)snapshot_buffer((uintptr_t)tracked_vertices,16).mapped==17);
        tracked_vertices=values+second*4;assert(*(uint32_t*)snapshot_buffer((uintptr_t)tracked_vertices,16).mapped==23);tracked_vertices=values+first*4;values[first*4]=31;dirty_generation++;
        assert(*(uint32_t*)snapshot_buffer((uintptr_t)tracked_vertices,16).mapped==31);tracked_vertices=values+second*4;assert(*(uint32_t*)snapshot_buffer((uintptr_t)tracked_vertices,16).mapped==23);mtl_sub_commit();tracked_vertices=NULL;mtl_pool_del(values);free(values);
        // 首次读取可观察旧值；读取后发生的写必须使下一次快照失效，不能永久缓存旧值。
        // The first read may see the old value; a subsequent guest write must invalidate the next snapshot.
        uint32_t raced[4]={17};mtl_pool_add(raced,sizeof raced);tracked_vertices=raced;protected_vertices=NULL;write_after_read=(uintptr_t)raced;write_after_read_value=31;
        mtl_sub_begin();assert(*(uint32_t*)snapshot_buffer((uintptr_t)raced,sizeof raced).mapped==17);assert(!write_after_read&&raced[0]==31);
        assert(*(uint32_t*)snapshot_buffer((uintptr_t)raced,sizeof raced).mapped==31);mtl_sub_commit();tracked_vertices=NULL;protected_vertices=NULL;mtl_pool_del(raced);
        // 未绑定可写图像的输出不能污染未绑定采样图像；先确认计算确实写入，再检查整屏零 RGBA。
        // Unbound writable images must not pollute unbound sampled images; verify the compute write, then a full zero-RGBA screen.
        memset(&state,0,sizeof state);state.program=&compute;mtl_sub_begin();dispatch_compute(&one);
        texture_descriptor absent={0};image_entry *unbound_output=descriptor_image(&absent,3,1,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);mtl_sub_commit();
        uint8_t unbound_pixel[4];NVNcopyRegion single_pixel={0,0,0,1,1,1};mtl_read_texels(&unbound_output->texture,0,0,&single_pixel,unbound_pixel,4,0);assert(!memcmp(unbound_pixel,blue,4));
        state.program=&graphics;state.rt.n=1;state.rt.color[0]=&target;state.mask.mask=UINT32_MAX;state.polygon.mode=2;state.scissor=(p_scissor){0,0,16,16};
        mtl_sub_begin();post_new(4);clear_color(&red_clear);draw_graphics(&triangle,NULL);mtl_sub_commit();assert(samples_passed==256);
        mtl_read_texels(&target,0,0,&region,pixels,16*4,0);for(unsigned i=0;i<sizeof pixels;i++)assert(pixels[i]==0);
        mtl_pool_del(uniform);mtl_pool_del(&number);mtl_pool_del(&result);
        mtl_tex_release(&texture);mtl_tex_release(&target);free(texture.mem);puts("Vulkan NVN: swizzled texture sampling, triangle pixels, compute image writes and readback PASS");
        puts("Vulkan NVN: batched uniform snapshots, deferred copy forwarding, ordered occlusion resets and SSBO visibility PASS");
        puts("Vulkan NVN: vertex cache pixels, dirty invalidation and pool destruction PASS");
        puts("Vulkan NVN: indexed vertex prefix bounds and base vertex pixels PASS");
        puts("Vulkan NVN: read-only SSBO/texel cache and compute texel-to-uniform visibility PASS");
        puts("Vulkan NVN: image/buffer hash collisions, dirty writes and program serial reuse PASS");
        puts("Vulkan NVN: guest write immediately after snapshot read invalidates the next cache hit PASS");
        puts("Vulkan NVN: unbound writable and sampled images remain isolated with zero RGBA pixels PASS");
    }
    if(getenv("DM_VK_SHADERS")){uniform_arena_reference_test();binding_cache_reference_test();depth_range_reference_test();dependencies_reference_test();sampler_reference_test();shadow_array_reference_test();handle_decode_reference_test();batch_resources_reference_test();query_readback_reference_test();}
    async_reference_test();forward_reference_test();pending_index_reference_test();if(getenv("DM_VK_SHADERS")){forward_binding_reference_test();texel_forward_reference_test();readback_dependency_reference_test();guest_publish_reference_test();index_reference_test();packaged_texture_reference_test();packaged_subresources_reference_test();known_copy_snapshot_reference_test();}
    typed_clear_reference_test();
    assert(pending_index_oracle_queries&&pending_index_oracle_nodes);
    printf("Vulkan pending index: %llu real query/old-chain equivalence checks, old chain nodes=%llu index tree nodes=%llu, all commit registration audits PASS (diagnostic suite, not game FPS)\n",(unsigned long long)pending_index_oracle_queries,(unsigned long long)pending_index_oracle_nodes,(unsigned long long)(pending_index_nodes[0]+pending_index_nodes[1]));
    assert(fence_wait_total_ns>0);
    if(batch_times){assert(gpu_total_ns>0);printf("Vulkan timestamps: %.3f ms GPU batches, %.3f ms fence waits PASS\n",gpu_total_ns/1e6,fence_wait_total_ns/1e6);}
    puts("Vulkan NVN: GPU copy, deferred events, overlap, CALL, fences and pool bounds PASS"); return 0;
}
