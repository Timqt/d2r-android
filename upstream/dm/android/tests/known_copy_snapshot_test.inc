// 已知 CPU 完整复制的快照真值；真实 GPU 像素/门验证版本、部分覆盖与发布寿命。
// Known full CPU-copy snapshot references; real GPU pixels/gates verify versions, partial overlays and publication lifetime.
static uint64_t known_copy_serial=1200000;
static void known_copy_graphics_state(const prog_t *program,const tex_t *target) {
    memset(&state,0,sizeof state);state.program=program;state.rt.n=1;state.rt.color[0]=target;state.mask.mask=UINT32_MAX;state.polygon.mode=2;
    state.depth_range=(p_depth_range){0,1};state.scissor=(p_scissor){0,0,16,16};
}
static void known_copy_pixels(const tex_t *target,const uint8_t left[4],const uint8_t right[4]) {
    uint8_t pixels[16*16*4];NVNcopyRegion region={0,0,0,16,16,1};mtl_read_texels(target,0,0,&region,pixels,16*4,0);
    for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++)assert(!memcmp(pixels+(y*16+x)*4,x<8?left:right,4));
}
static tex_t known_copy_target(void) {
    tex_t target={.serial=++known_copy_serial,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};
    nvn_tex_layout(&target.d,&target.L);return target;
}
static void known_copy_full_and_partial_test(void) {
    mtl_drain();tracked_vertices=NULL;protected_vertices=NULL;
    float guest[12]={0},source[12]={0};const float red[4]={1,0,0,1},green[4]={0,1,0,1};
    memcpy(source+4,red,sizeof red);mtl_pool_add(guest,sizeof guest);
    uint8_t controls[2][0x880]={{1},{4}};prog_t graphics={.nstage=2,.serial=++known_copy_serial,.sd={{.control=controls[0]},{.control=controls[1]}}};tex_t target=known_copy_target();
    known_copy_graphics_state(&graphics,&target);state.ubo[1][0]=(p_bind_buf){.addr=(uintptr_t)(guest+4),.size=sizeof red};
    memcpy(guest+4,green,sizeof green);mtl_sub_begin();mtl_sub_copy((uintptr_t)source,(uintptr_t)guest,sizeof guest);
    // 原源数组和客体均在录制后改变；不可变 post 快照仍是最后排队的复制版本。
    // Change both original source and guest after recording; the immutable post snapshot remains the queued copy version.
    memcpy(source+4,green,sizeof green);memset(guest+4,0,sizeof red);
    uint64_t hits=snapshot_known_copy_hits,reads=snapshot_known_copy_read_saved,overlays=snapshot_known_copy_overlay_saved,final=snapshot_known_copy_final_bytes;
    p_draw triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};state.scissor.w=8;draw_graphics(&triangle,NULL);
    assert(snapshot_known_copy_hits==hits+1&&snapshot_known_copy_read_saved==reads+sizeof red&&snapshot_known_copy_overlay_saved==overlays);
    assert(!memcmp(guest+4,(float[4]){0},sizeof red));
    mtl_sub_copy((uintptr_t)green,(uintptr_t)(guest+4),sizeof green);state.scissor.x=8;draw_graphics(&triangle,NULL);
    assert(snapshot_known_copy_hits==hits+2&&snapshot_known_copy_read_saved==reads+2*sizeof red&&snapshot_known_copy_overlay_saved==overlays+sizeof red&&snapshot_known_copy_final_bytes==final+2*sizeof red);
    const post_op *latest=async_copy_version((uintptr_t)(guest+4),sizeof red);assert(latest);
    // 缩短实际 source 的合法范围时 helper 必须拒绝，且不改目标；恢复后仍正常发布。
    // Reject a shortened actual source range without touching the destination; restore metadata before real publication.
    uint64_t size=((post_op*)latest)->source.size;((post_op*)latest)->source.size=8;uint8_t untouched[16];memset(untouched,0xa5,sizeof untouched);
    assert(!async_read_known_full_copy((uintptr_t)(guest+4),untouched,sizeof untouched));for(unsigned i=0;i<sizeof untouched;i++)assert(untouched[i]==0xa5);((post_op*)latest)->source.size=size;
    assert(!async_read_known_full_copy(UINTPTR_MAX-3,untouched,8)&&!async_read_known_full_copy((uintptr_t)guest,NULL,4));
    uint64_t ticket=async_raw_commit();assert(mtl_wait(ticket,5000000000ull));assert(!memcmp(guest+4,green,sizeof green));
    const uint8_t red_pixel[4]={255,0,0,255},green_pixel[4]={0,255,0,255};known_copy_pixels(&target,red_pixel,green_pixel);
    puts("Vulkan known-copy snapshots: nonzero source slice, immutable source/CPU-write order, latest full version, exact left red/right green, precise avoided bytes and bounds rejection PASS");

    memcpy(guest+4,(float[4]){0,0,1,1},sizeof red);known_copy_graphics_state(&graphics,&target);state.ubo[1][0]=(p_bind_buf){.addr=(uintptr_t)(guest+4),.size=sizeof red};
    mtl_sub_begin();mtl_sub_copy((uintptr_t)red,(uintptr_t)(guest+4),sizeof red);float one=1;mtl_sub_copy((uintptr_t)&one,(uintptr_t)(guest+5),sizeof one);
    hits=snapshot_known_copy_hits;reads=snapshot_known_copy_read_saved;draw_graphics(&triangle,NULL);
    assert(snapshot_known_copy_hits==hits&&snapshot_known_copy_read_saved==reads&&guest[6]==1);
    ticket=async_raw_commit();assert(mtl_wait(ticket,5000000000ull));const uint8_t yellow[4]={255,255,0,255};known_copy_pixels(&target,yellow,yellow);
    puts("Vulkan known-copy snapshots: latest partial overwrite rejects old-full shortcut, original ordered merge gives exact yellow rather than stale red PASS");
    mtl_pool_del(guest);mtl_tex_release(&target);mtl_prog_release(&graphics);
}

#ifdef DM_VK_REFERENCE_GATES
typedef struct known_copy_gate_control {
    VkSemaphore gate;uint64_t submitted_ticket;
    const void *guest,*initial;size_t size;
    pthread_t thread;reference_gate_watchdog watchdog;
} known_copy_gate_control;
static void *known_copy_signal_after_submit(void *argument) {
    known_copy_gate_control *control=argument;uint64_t until=dm_android_clock_ns(CLOCK_MONOTONIC)+5000000000ull;
    while(mtl_last_fence()<control->submitted_ticket&&dm_android_clock_ns(CLOCK_MONOTONIC)<until)sched_yield();
    assert(mtl_last_fence()>=control->submitted_ticket&&!mtl_wait(control->submitted_ticket,0));
    assert(!memcmp(control->guest,control->initial,control->size));reference_gate_signal(control->gate,1);return NULL;
}
static void known_copy_gate_start(known_copy_gate_control *control,VkSemaphore gate,uint64_t ticket,const void *guest,const void *initial,size_t size) {
    *control=(known_copy_gate_control){.gate=gate,.submitted_ticket=ticket,.guest=guest,.initial=initial,.size=size};
    reference_gate_watchdog_start(&control->watchdog,gate,1,5);assert(!pthread_create(&control->thread,NULL,known_copy_signal_after_submit,control));
}
static void known_copy_gate_stop(known_copy_gate_control *control) {
    assert(!pthread_join(control->thread,NULL));assert(!reference_gate_watchdog_stop(&control->watchdog));reference_gate_close(control->gate,1);
}
static void known_copy_cross_batch_test(void) {
    mtl_drain();tracked_vertices=NULL;protected_vertices=NULL;float guest[8]={0},source[8]={0};memcpy(source+4,(float[4]){0,1,0,1},16);mtl_pool_add(guest,sizeof guest);
    uint8_t controls[2][0x880]={{1},{4}};prog_t graphics={.nstage=2,.serial=++known_copy_serial,.sd={{.control=controls[0]},{.control=controls[1]}}};tex_t target=known_copy_target();
    VkSemaphore gate=reference_gate_create();mtl_sub_begin();reference_gate_arm(gate,1);mtl_sub_copy((uintptr_t)source,(uintptr_t)guest,sizeof guest);
    vk_buffer backing=post_copy_first->source;uint64_t produced=async_raw_commit();mtl_sub_begin();
    assert(!mtl_wait(produced,0)&&atomic_load(&backing.owner->references)==1);
    pthread_mutex_lock(&buffer_cache_lock);for(cached_buffer *entry=buffer_cache;entry;entry=entry->next)assert(entry->buffer.buffer!=backing.buffer);pthread_mutex_unlock(&buffer_cache_lock);
    memcpy(source+4,(float[4]){1,0,0,1},16);known_copy_graphics_state(&graphics,&target);state.ubo[1][0]=(p_bind_buf){.addr=(uintptr_t)(guest+4),.size=16};
    uint64_t hits=snapshot_known_copy_hits;p_draw triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};draw_graphics(&triangle,NULL);
    assert(snapshot_known_copy_hits==hits+1&&!memcmp(guest,(float[8]){0},sizeof guest)&&atomic_load(&backing.owner->references)==1);
    known_copy_gate_control control;float initial[8]={0};known_copy_gate_start(&control,gate,produced+1,guest,initial,sizeof guest);
    uint64_t consumed=async_raw_commit();assert(mtl_wait(consumed,5000000000ull));known_copy_gate_stop(&control);
    assert(!memcmp(guest+4,(float[4]){0,1,0,1},16));const uint8_t green[4]={0,255,0,255};known_copy_pixels(&target,green,green);
    mtl_pool_del(guest);mtl_tex_release(&target);mtl_prog_release(&graphics);
    puts("Vulkan known-copy snapshots: gated producer source stays alive/uncached until fence, cross-batch offset reads immutable green before publication, exact GPU green PASS");
}
static void known_copy_host_effect_test(int counter) {
    mtl_drain();tracked_vertices=NULL;protected_vertices=NULL;uint64_t guest[2]={0x1111111111111111ull,0x2222222222222222ull},initial[2];memcpy(initial,guest,sizeof guest);mtl_pool_add(guest,sizeof guest);
    uint8_t controls[2][0x880]={{1},{4}};prog_t graphics={.nstage=2,.serial=++known_copy_serial,.sd={{.control=controls[0]},{.control=controls[1]}}};tex_t target=known_copy_target();
    VkSemaphore gate=reference_gate_create();mtl_sub_begin();reference_gate_arm(gate,1);const float red[4]={1,0,0,1};mtl_sub_copy((uintptr_t)red,(uintptr_t)guest,sizeof red);
    if(counter){post_new(4);mtl_sub_counter((uintptr_t)guest,1);}else mtl_sub_event((volatile uint32_t*)guest,0,0);
    uint8_t ignored[16];assert(!async_read_known_full_copy((uintptr_t)guest,ignored,sizeof ignored));
    uint64_t hits=snapshot_known_copy_hits,flushes=dependency_flushes;known_copy_gate_control control;known_copy_gate_start(&control,gate,mtl_last_fence()+1,guest,initial,sizeof guest);
    if(counter){dependencies_prepare_range((uintptr_t)guest,sizeof guest);uint64_t observed[2]={0};assert(!snapshot_read((uintptr_t)guest,observed,sizeof observed));assert(observed[0]==0&&observed[1]>0&&!memcmp(observed,guest,sizeof guest));}
    else{known_copy_graphics_state(&graphics,&target);state.ubo[1][0]=(p_bind_buf){.addr=(uintptr_t)guest,.size=16};p_draw triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};draw_graphics(&triangle,NULL);}
    assert(dependency_flushes==flushes+1&&snapshot_known_copy_hits==hits);uint64_t finished=async_raw_commit();assert(mtl_wait(finished,5000000000ull));known_copy_gate_stop(&control);
    if(!counter){const uint8_t black[4]={0,0,0,255};known_copy_pixels(&target,black,black);}
    mtl_pool_del(guest);mtl_tex_release(&target);mtl_prog_release(&graphics);
    printf("Vulkan known-copy snapshots: overlapping %s rejects CPU full-copy shortcut, real gated wait/publication and %s PASS\n",counter?"counter":"event",counter?"actual counter bytes":"exact black pixels");
}
static void known_copy_unknown_gpu_test(void) {
    mtl_drain();tracked_vertices=NULL;protected_vertices=NULL;
    tex_t texture={.serial=++known_copy_serial,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=1,.height=1,.depth=1,.levels=1,.flags=NVN_TEXTURE_FLAGS_LINEAR}};nvn_tex_layout(&texture.d,&texture.L);texture.mem=calloc(1,texture.L.size);assert(texture.mem);
    *(uint32_t*)texture.mem=0xff00ff00;uint8_t *initial=malloc(texture.L.size);assert(initial);memcpy(initial,texture.mem,texture.L.size);mtl_pool_add(texture.mem,texture.L.size);
    uint8_t control[0x880]={5};prog_t compute={.nstage=1,.serial=++known_copy_serial,.sd={{.control=control}}};memset(&state,0,sizeof state);state.program=&compute;state.ssbo[5][0]=(p_bind_buf){.addr=(uintptr_t)texture.mem,.size=4};
    VkSemaphore gate=reference_gate_create();mtl_sub_begin();reference_gate_arm(gate,1);uint32_t seed=0xff0000f8;mtl_sub_copy((uintptr_t)&seed,(uintptr_t)texture.mem,sizeof seed);p_dispatch one={1,1,1,0};dispatch_compute(&one);
    uint32_t ignored=0;assert(!async_read_known_full_copy((uintptr_t)texture.mem,&ignored,4));uint64_t hits=snapshot_known_copy_hits,flushes=dependency_flushes;
    known_copy_gate_control condition;known_copy_gate_start(&condition,gate,mtl_last_fence()+1,texture.mem,initial,texture.L.size);
    dependencies_prepare_range((uintptr_t)texture.mem,4);uint32_t observed=0;assert(!snapshot_read((uintptr_t)texture.mem,&observed,4));assert(observed==0xff0000ff&&dependency_flushes==flushes+1&&snapshot_known_copy_hits==hits);
    uint64_t finished=async_raw_commit();assert(mtl_wait(finished,5000000000ull));known_copy_gate_stop(&condition);
    NVNcopyRegion region={0,0,0,1,1,1};uint8_t pixel[4];mtl_read_texels(&texture,0,0,&region,pixel,4,0);assert(!memcmp(pixel,(uint8_t[4]){255,0,0,255},4));
    mtl_pool_del(texture.mem);mtl_tex_release(&texture);mtl_prog_release(&compute);free(initial);free(texture.mem);
    puts("Vulkan known-copy snapshots: later real GPU +7 write rejects CPU shortcut and truly waits; exact red 255 instead of stale 248 PASS");
}
#endif
static void known_copy_snapshot_reference_test(void) {
    known_copy_full_and_partial_test();
#ifdef DM_VK_REFERENCE_GATES
    assert(reference_gates_supported());known_copy_cross_batch_test();known_copy_host_effect_test(0);known_copy_host_effect_test(1);known_copy_unknown_gpu_test();
#endif
}
