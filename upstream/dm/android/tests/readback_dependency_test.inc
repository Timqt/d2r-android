// CPU ReadTexels 上传前真实等待池中 GPU 写回；仅图像复制排在后面不能修正过早的 CPU 快照。
// CPU ReadTexels truly waits for GPU pool writeback before upload; ordering the image copy later cannot fix an early CPU snapshot.
typedef struct readback_dependency_call {const tex_t *texture;uint8_t pixel[4];} readback_dependency_call;
static void *readback_dependency_worker(void *argument) {
    readback_dependency_call *call=argument;NVNcopyRegion region={0,0,0,1,1,1};mtl_read_texels(call->texture,0,0,&region,call->pixel,4,0);return NULL;
}
static void readback_dependency_reference_test(void) {
#ifdef DM_VK_REFERENCE_GATES
    if(!reference_gates_supported()){puts("Vulkan CPU texture readback: timeline test unavailable");return;}
    mtl_drain();tracked_vertices=NULL;protected_vertices=NULL;tex_t texture={.serial=++forward_binding_serial,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=1,.height=1,.depth=1,.levels=1,.flags=NVN_TEXTURE_FLAGS_LINEAR}};
    nvn_tex_layout(&texture.d,&texture.L);texture.mem=calloc(1,texture.L.size);assert(texture.mem);*(uint32_t*)texture.mem=0xff0000f8;mtl_pool_add(texture.mem,texture.L.size);
    uint8_t control[0x880]={5};prog_t compute={.nstage=1,.serial=++forward_binding_serial,.sd={{.control=control}}};memset(&state,0,sizeof state);state.program=&compute;state.ssbo[5][0]=(p_bind_buf){.addr=(uintptr_t)texture.mem,.size=4};
    VkSemaphore gate=reference_gate_create();mtl_sub_begin();reference_gate_arm(gate,1);p_dispatch one={1,1,1,0};dispatch_compute(&one);uint64_t produced=async_raw_commit(),flushes=dependency_flushes;
    readback_dependency_call call={.texture=&texture};pthread_t thread;assert(!pthread_create(&thread,NULL,readback_dependency_worker,&call));
    // 新代码先提交空的依赖批；旧代码先读旧 CPU 像素再提交读回批。两者均在此提交点之后才解门。
    // The fix submits a dependency batch first; the old path snapshots stale pixels before submitting readback. Release only after either submit point.
    uint64_t until=dm_android_clock_ns(CLOCK_MONOTONIC)+5000000000ull;while(mtl_last_fence()<produced+1&&dm_android_clock_ns(CLOCK_MONOTONIC)<until)sched_yield();
    assert(mtl_last_fence()>=produced+1&&*(uint32_t*)texture.mem==0xff0000f8);reference_gate_signal(gate,1);assert(!pthread_join(thread,NULL));
    assert(dependency_flushes==flushes+1&&*(uint32_t*)texture.mem==0xff0000ff);const uint8_t expected[4]={255,0,0,255};assert(!memcmp(call.pixel,expected,4));
    reference_gate_close(gate,1);mtl_pool_del(texture.mem);mtl_tex_release(&texture);mtl_prog_release(&compute);free(texture.mem);
    puts("Vulkan CPU texture readback: gated GPU pool write completes before upload, exact red 255 rather than stale 248 PASS");
#endif
}
