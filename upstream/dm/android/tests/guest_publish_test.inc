// 固定两个真实 timeline 门，证明 guest 发布完成不要求等待后面的纯 GPU batch。
// Two real timeline gates prove guest publication completion does not require waiting for a later pure GPU batch.
#ifdef DM_VK_REFERENCE_GATES
typedef struct guest_publish_control {
    pthread_mutex_t lock;
    pthread_cond_t ready;
    VkSemaphore gate;
    reference_gate_watchdog *watchdog;
    guest_publish_result observed;
    int entered,fired;
} guest_publish_control;
static guest_publish_control *guest_publish_active_control;
static uint64_t guest_publish_test_serial=980000;
static void guest_publish_test_observe(const guest_publish_result *result) {
    guest_publish_control *control=guest_publish_active_control;assert(control);pthread_mutex_lock(&control->lock);control->observed=*result;control->entered=1;pthread_cond_signal(&control->ready);pthread_mutex_unlock(&control->lock);
}
static void *guest_publish_test_signal(void *argument) {
    guest_publish_control *control=argument;pthread_mutex_lock(&control->lock);while(!control->entered)pthread_cond_wait(&control->ready,&control->lock);pthread_mutex_unlock(&control->lock);
    control->fired=reference_gate_watchdog_stop(control->watchdog);reference_gate_signal(control->gate,1);return NULL;
}
static void guest_publish_test_control_start(guest_publish_control *control,VkSemaphore gate,reference_gate_watchdog *watchdog,pthread_t *thread) {
    memset(control,0,sizeof *control);assert(!pthread_mutex_init(&control->lock,NULL));assert(!pthread_cond_init(&control->ready,NULL));control->gate=gate;control->watchdog=watchdog;guest_publish_active_control=control;guest_publish_reference_before_wait=guest_publish_test_observe;assert(!pthread_create(thread,NULL,guest_publish_test_signal,control));
}
static void guest_publish_test_control_stop(guest_publish_control *control,pthread_t thread) {
    assert(!pthread_join(thread,NULL));guest_publish_reference_before_wait=NULL;guest_publish_active_control=NULL;pthread_cond_destroy(&control->ready);pthread_mutex_destroy(&control->lock);
}
static void guest_publish_test_state(const prog_t *program,const tex_t *target) {
    memset(&state,0,sizeof state);state.program=program;state.rt.n=1;state.rt.color[0]=target;state.mask.mask=UINT32_MAX;state.polygon.mode=2;state.depth_range=(p_depth_range){0,1};state.scissor=(p_scissor){0,0,16,16};
}
static void guest_publish_test_pixels(const tex_t *target,const uint8_t rgba[4]) {
    uint8_t pixels[16*16*4];NVNcopyRegion region={0,0,0,16,16,1};mtl_read_texels(target,0,0,&region,pixels,16*4,0);for(unsigned i=0;i<256;i++)assert(!memcmp(pixels+i*4,rgba,4));
}
static void guest_publish_partial_reference_test(void) {
    mtl_drain();tracked_vertices=NULL;protected_vertices=NULL;float guest[4]={1,0,0,1},green[4]={0,1,0,1};volatile uint32_t event=0;mtl_pool_add(guest,sizeof guest);
    uint8_t controls[2][0x880]={{1},{7}};prog_t graphics={.nstage=2,.serial=++guest_publish_test_serial,.sd={{.control=controls[0]},{.control=controls[1]}}};tex_t target={.serial=++guest_publish_test_serial,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};nvn_tex_layout(&target.d,&target.L);
    VkSemaphore gates[2]={reference_gate_create(),reference_gate_create()};reference_gate_watchdog watchdog[2];mtl_sub_begin();reference_gate_arm(gates[0],1);mtl_sub_copy((uintptr_t)green,(uintptr_t)guest,sizeof guest);post_op *producer=post_copy_first;assert(producer&&producer->source.buffer);vk_buffer retained=producer->source;mtl_sub_event(&event,0,73);uint64_t produced=async_raw_commit();
    for(unsigned i=0;i<2;i++)reference_gate_watchdog_start(&watchdog[i],gates[i],1,5);
    mtl_sub_begin();reference_gate_arm(gates[1],1);guest_publish_test_state(&graphics,&target);state.ssbo[1][0]=(p_bind_buf){.addr=(uintptr_t)guest,.size=sizeof guest};p_draw triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};draw_graphics(&triangle,NULL);
    assert(guest[0]==1&&guest[1]==0&&event==0);assert(!post_copy_first&&!pending_host_sync&&atomic_load(&retained.owner->references)==2);
    // 保持 batch2 仍在录制，让 production helper 自己真正 commit；不经测试的同步 commit wrapper。
    // Leave batch2 recording so the production helper commits it itself, without the test's synchronous commit wrapper.
    guest_publish_control control;pthread_t thread;guest_publish_test_control_start(&control,gates[0],&watchdog[0],&thread);guest_publish_result result=guest_publish_wait(0);guest_publish_test_control_stop(&control,thread);
    int tail_fired=reference_gate_watchdog_stop(&watchdog[1]);assert(!control.fired&&!tail_fired);
    assert(result.queued_ticket==produced+1&&result.required_ticket==produced&&result.gap==1);assert(control.observed.required_ticket==produced&&!recording);
    assert(mtl_wait(produced,0)&&!mtl_wait(result.queued_ticket,0));assert(!memcmp(guest,green,sizeof guest)&&event==73);assert(atomic_load(&retained.owner->references)==1);
    pthread_mutex_lock(&buffer_cache_lock);for(cached_buffer *entry=buffer_cache;entry;entry=entry->next)assert(entry->buffer.buffer!=retained.buffer);pthread_mutex_unlock(&buffer_cache_lock);
    // 已完成 guest producer 后，pure GPU consumer 的引用仍保留 backing，直到它自己的真实 fence。
    // The pure GPU consumer retains the backing after guest publication until its own real fence completes.
    guest_publish_result again=guest_publish_wait(produced);assert(again.required_ticket==produced&&again.gap==1&&!mtl_wait(again.queued_ticket,0));reference_gate_signal(gates[1],1);assert(mtl_wait(result.queued_ticket,5000000000ull));
    for(unsigned i=0;i<2;i++)reference_gate_close(gates[i],1);const uint8_t green_pixel[4]={0,255,0,255};guest_publish_test_pixels(&target,green_pixel);mtl_pool_del(guest);mtl_tex_release(&target);mtl_prog_release(&graphics);
    puts("Vulkan guest publication: recording committed; copy/event truly published while later gated pure GPU draw remains in flight with retained backing; exact green pixels PASS");
}
static void guest_publish_acquire_reference_test(void) {
    mtl_drain();tracked_vertices=NULL;protected_vertices=NULL;float blue[4]={0,0,1,1};uint8_t controls[2][0x880]={{1},{4}};prog_t graphics={.nstage=2,.serial=++guest_publish_test_serial,.sd={{.control=controls[0]},{.control=controls[1]}}};tex_t target={.serial=++guest_publish_test_serial,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};nvn_tex_layout(&target.d,&target.L);
    VkSemaphore gate=reference_gate_create();reference_gate_watchdog watchdog;mtl_sub_begin();reference_gate_arm(gate,1);guest_publish_test_state(&graphics,&target);state.ubo[1][0]=(p_bind_buf){.addr=(uintptr_t)blue,.size=sizeof blue};p_draw triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};draw_graphics(&triangle,NULL);assert(!post_copy_first&&!pending_host_sync);uint64_t consumed=async_raw_commit();reference_gate_watchdog_start(&watchdog,gate,1,5);
    // 没有 guest 写入时仍必须等待 acquire token 的消费票号，不能仅凭“pure GPU”提前复用。
    // Even without guest writes the acquire-token consumer ticket must complete before reuse; being pure GPU is not enough.
    guest_publish_control control;pthread_t thread;guest_publish_test_control_start(&control,gate,&watchdog,&thread);guest_publish_result result=guest_publish_wait(consumed);guest_publish_test_control_stop(&control,thread);assert(!control.fired&&control.observed.required_ticket==consumed&&result.required_ticket==consumed&&result.queued_ticket==consumed&&result.gap==0&&mtl_wait(consumed,0));reference_gate_close(gate,1);
    const uint8_t blue_pixel[4]={0,0,255,255};guest_publish_test_pixels(&target,blue_pixel);mtl_tex_release(&target);mtl_prog_release(&graphics);puts("Vulkan guest publication: pure GPU acquire-token consumer still waits for real completion; exact blue pixels PASS");
}
#endif
static void guest_publish_reference_test(void) {
#ifdef DM_VK_REFERENCE_GATES
    if(!reference_gates_supported()){puts("Vulkan guest publication: timeline cases unavailable");return;}guest_publish_partial_reference_test();guest_publish_acquire_reference_test();
#endif
}
