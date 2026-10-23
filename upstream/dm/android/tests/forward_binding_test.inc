// 走真实 dispatch/draw 绑定路径验证 GPU→SSBO、RW 初值与 vertex slice，不能读旧 CPU 值来“通过”。
// Exercise production dispatch/draw binding for GPU-to-SSBO, RW seeding and vertex slices, rather than passing with stale CPU values.
static uint64_t forward_binding_serial=940000;
static void forward_binding_graphics_state(const prog_t *program,const tex_t *target) {
    memset(&state,0,sizeof state);state.program=program;state.rt.n=1;state.rt.color[0]=target;state.mask.mask=UINT32_MAX;state.polygon.mode=2;
    state.scissor=(p_scissor){0,0,16,16};state.depth_range=(p_depth_range){0,1};
}
static void forward_binding_pixels(const tex_t *target,const uint8_t rgba[4]) {
    uint8_t pixels[16*16*4];NVNcopyRegion region={0,0,0,16,16,1};mtl_read_texels(target,0,0,&region,pixels,16*4,0);
    for(unsigned i=0;i<256;i++)assert(!memcmp(pixels+i*4,rgba,4));
}
#ifdef DM_VK_REFERENCE_GATES
typedef struct forward_binding_gates { VkSemaphore gates[2];reference_gate_watchdog watchdog[2]; } forward_binding_gates;
static void forward_binding_gates_create(forward_binding_gates *gates) {
    gates->gates[0]=reference_gate_create();gates->gates[1]=reference_gate_create();
}
static void forward_binding_gates_watch(forward_binding_gates *gates) {
    // 错误 preflight 可能提交 gated 空批并同步等待；两个 gate 都设超时释放，以报断言而非永久死锁。
    // An erroneous preflight may submit a gated empty batch and wait; watchdogs release both gates to produce an assertion rather than a permanent deadlock.
    for(unsigned i=0;i<2;i++)reference_gate_watchdog_start(&gates->watchdog[i],gates->gates[i],1,5);
}
static int forward_binding_gates_producer(forward_binding_gates *gates,uint64_t ticket) {
    int fired=reference_gate_watchdog_stop(&gates->watchdog[0]);reference_gate_signal(gates->gates[0],1);assert(mtl_wait(ticket,5000000000ull));return fired;
}
static int forward_binding_gates_consumer(forward_binding_gates *gates,uint64_t ticket) {
    int fired=reference_gate_watchdog_stop(&gates->watchdog[1]);reference_gate_signal(gates->gates[1],1);assert(mtl_wait(ticket,5000000000ull));
    for(unsigned i=0;i<2;i++)reference_gate_close(gates->gates[i],1);return fired;
}
#endif
static void forward_binding_storage_case(int crossing) {
#ifdef DM_VK_REFERENCE_GATES
    if(crossing&&!reference_gates_supported()){puts("Vulkan production storage forwarding: timeline case unavailable");return;}forward_binding_gates gates;
#else
    if(crossing)return;
#endif
    mtl_drain();tracked_vertices=NULL;protected_vertices=NULL;uint32_t color[4]={0x3efffffb,0,0,0x3f800000};mtl_pool_add(color,sizeof color);
    uint8_t controls[3][0x880]={{5},{1},{7}};prog_t add={.nstage=1,.serial=++forward_binding_serial,.sd={{.control=controls[0]}}},graphics={.nstage=2,.serial=++forward_binding_serial,.sd={{.control=controls[1]},{.control=controls[2]}}};
    tex_t target={.serial=++forward_binding_serial,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};nvn_tex_layout(&target.d,&target.L);
    uint64_t flushes=dependency_flushes,produced=0;int fired=0,producer_visible=1;memset(&state,0,sizeof state);state.program=&add;state.ssbo[5][0]=(p_bind_buf){.addr=(uintptr_t)color,.size=sizeof color};
#ifdef DM_VK_REFERENCE_GATES
    if(crossing)forward_binding_gates_create(&gates);
#endif
    mtl_sub_begin();
#ifdef DM_VK_REFERENCE_GATES
    if(crossing)reference_gate_arm(gates.gates[0],1);
#endif
    p_dispatch one={1,1,1,0};dispatch_compute(&one);uint64_t reads=snapshot_bytes;
    if(crossing){produced=async_raw_commit();
#ifdef DM_VK_REFERENCE_GATES
        forward_binding_gates_watch(&gates);
#endif
        mtl_sub_begin();
#ifdef DM_VK_REFERENCE_GATES
        reference_gate_arm(gates.gates[1],1);
#endif
    }
    forward_binding_graphics_state(&graphics,&target);state.ssbo[1][0]=(p_bind_buf){.addr=(uintptr_t)color,.size=sizeof color};state.scissor.w=8;p_draw triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};draw_graphics(&triangle,NULL);
    // 第二次 RW 必须复制上一份 GPU 内容再 +7，不能再次从仍为旧值的 guest 初始化。
    // The second RW dispatch must seed the previous GPU contents before adding seven, rather than reseeding stale guest bytes.
    state.program=&add;state.ssbo[5][0]=(p_bind_buf){.addr=(uintptr_t)color,.size=sizeof color};dispatch_compute(&one);
    state.program=&graphics;state.ssbo[1][0]=(p_bind_buf){.addr=(uintptr_t)color,.size=sizeof color};state.scissor.x=8;draw_graphics(&triangle,NULL);
    uint64_t reads_after=snapshot_bytes,flushes_after=dependency_flushes;uint64_t consumed=async_raw_commit();
#ifdef DM_VK_REFERENCE_GATES
    if(crossing){fired|=forward_binding_gates_producer(&gates,produced);producer_visible=color[0]==0x3f000002;fired|=forward_binding_gates_consumer(&gates,consumed);}
    else
#endif
    assert(mtl_wait(consumed,5000000000ull));
    assert(!fired&&producer_visible);assert(flushes_after==flushes&&reads_after==reads);assert(color[0]==0x3f000009&&color[1]==0&&color[2]==0&&color[3]==0x3f800000);
    const uint8_t expected[4]={128,0,0,255};forward_binding_pixels(&target,expected);mtl_pool_del(color);mtl_tex_release(&target);mtl_prog_release(&add);mtl_prog_release(&graphics);
    printf("Vulkan production storage forwarding: %s GPU +7/+14, exact red 128, no CPU reread/dependency flush PASS\n",crossing?"cross-batch":"same-batch");
}
static void forward_binding_vertex_case(int crossing) {
#ifdef DM_VK_REFERENCE_GATES
    if(crossing&&!reference_gates_supported()){puts("Vulkan production vertex forwarding: timeline case unavailable");return;}forward_binding_gates gates;
#else
    if(crossing)return;
#endif
    mtl_drain();tracked_vertices=NULL;protected_vertices=NULL;float vertices[10];for(unsigned i=0;i<10;i++)vertices[i]=9;float blue[4]={0,0,1,1};mtl_pool_add(vertices,sizeof vertices);mtl_pool_add(blue,sizeof blue);
    uint8_t controls[2][0x880]={{6},{4}};prog_t graphics={.nstage=2,.serial=++forward_binding_serial,.sd={{.control=controls[0]},{.control=controls[1]}}};
    tex_t target={.serial=++forward_binding_serial,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};nvn_tex_layout(&target.d,&target.L);
    uint64_t flushes=dependency_flushes,produced=0;int fired=0,producer_visible=1;
#ifdef DM_VK_REFERENCE_GATES
    if(crossing)forward_binding_gates_create(&gates);
#endif
    mtl_sub_begin();
#ifdef DM_VK_REFERENCE_GATES
    if(crossing)reference_gate_arm(gates.gates[0],1);
#endif
    post_op *output=post_new(0);output->destination=vertices;output->size=sizeof vertices;output->output=buffer_create(sizeof vertices);post_register_range(output);memset(output->output.mapped,0,sizeof vertices);
    vkCmdFillBuffer(command,output->output.buffer,0,sizeof vertices,0xbf800000);vkCmdFillBuffer(command,output->output.buffer,24,4,0x40400000);vkCmdFillBuffer(command,output->output.buffer,36,4,0x40400000);
    forward_mark_gpu_recorded(output,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,1);forward_test_host_visibility();uint64_t reads=snapshot_bytes;
    if(crossing){produced=async_raw_commit();
#ifdef DM_VK_REFERENCE_GATES
        forward_binding_gates_watch(&gates);
#endif
        mtl_sub_begin();
#ifdef DM_VK_REFERENCE_GATES
        reference_gate_arm(gates.gates[1],1);
#endif
    }
    forward_binding_graphics_state(&graphics,&target);state.ubo[1][0]=(p_bind_buf){.addr=(uintptr_t)blue,.size=sizeof blue};state.attributes[0]=(va_st){.fmt=NVN_FORMAT_RG32F};state.streams[0]=(vs_st){.stride=8};state.vertices[0]=(NVNbufferRange){(uintptr_t)(vertices+4),24};
    p_draw triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};draw_graphics(&triangle,NULL);uint64_t reads_after=snapshot_bytes,flushes_after=dependency_flushes;uint64_t consumed=async_raw_commit();
#ifdef DM_VK_REFERENCE_GATES
    if(crossing){fired|=forward_binding_gates_producer(&gates,produced);producer_visible=vertices[4]==-1&&vertices[6]==3&&vertices[9]==3;fired|=forward_binding_gates_consumer(&gates,consumed);}
    else
#endif
    assert(mtl_wait(consumed,5000000000ull));
    assert(!fired&&producer_visible);assert(flushes_after==flushes&&reads_after-reads==sizeof blue);assert(vertices[4]==-1&&vertices[5]==-1&&vertices[6]==3&&vertices[7]==-1&&vertices[8]==-1&&vertices[9]==3);
    const uint8_t expected[4]={0,0,255,255};forward_binding_pixels(&target,expected);mtl_pool_del(vertices);mtl_pool_del(blue);mtl_tex_release(&target);mtl_prog_release(&graphics);
    printf("Vulkan production vertex forwarding: %s nonzero-offset GPU vertices, exact blue pixels, no vertex CPU reread/dependency flush PASS\n",crossing?"cross-batch":"same-batch");
}
static void forward_binding_copy_storage_case(int crossing) {
#ifdef DM_VK_REFERENCE_GATES
    if(crossing&&!reference_gates_supported()){puts("Vulkan dispatch/copy/storage forwarding: timeline case unavailable");return;}forward_binding_gates gates;
#else
    if(crossing)return;
#endif
    mtl_drain();tracked_vertices=NULL;protected_vertices=NULL;uint32_t color[4]={0x3efffffb,0,0,0x3f800000},copied[4]={0,0,0x3f800000,0x3f800000};mtl_pool_add(color,sizeof color);mtl_pool_add(copied,sizeof copied);
    uint8_t controls[3][0x880]={{5},{1},{7}};prog_t add={.nstage=1,.serial=++forward_binding_serial,.sd={{.control=controls[0]}}},graphics={.nstage=2,.serial=++forward_binding_serial,.sd={{.control=controls[1]},{.control=controls[2]}}};
    tex_t target={.serial=++forward_binding_serial,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};nvn_tex_layout(&target.d,&target.L);
    uint64_t flushes=dependency_flushes,produced=0;int fired=0,producer_visible=1;memset(&state,0,sizeof state);state.program=&add;state.ssbo[5][0]=(p_bind_buf){.addr=(uintptr_t)color,.size=sizeof color};
#ifdef DM_VK_REFERENCE_GATES
    if(crossing)forward_binding_gates_create(&gates);
#endif
    mtl_sub_begin();
#ifdef DM_VK_REFERENCE_GATES
    if(crossing)reference_gate_arm(gates.gates[0],1);
#endif
    p_dispatch one={1,1,1,0};dispatch_compute(&one);uint64_t reads=snapshot_bytes,copies=forward_copy_bytes;
    if(crossing){produced=async_raw_commit();
#ifdef DM_VK_REFERENCE_GATES
        forward_binding_gates_watch(&gates);
#endif
        mtl_sub_begin();
#ifdef DM_VK_REFERENCE_GATES
        reference_gate_arm(gates.gates[1],1);
#endif
    }
    mtl_sub_copy((uintptr_t)color,(uintptr_t)copied,sizeof color);forward_binding_graphics_state(&graphics,&target);state.ssbo[1][0]=(p_bind_buf){.addr=(uintptr_t)copied,.size=sizeof copied};p_draw triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};draw_graphics(&triangle,NULL);
    uint64_t reads_after=snapshot_bytes,flushes_after=dependency_flushes,copies_after=forward_copy_bytes;uint64_t consumed=async_raw_commit();
#ifdef DM_VK_REFERENCE_GATES
    if(crossing){fired|=forward_binding_gates_producer(&gates,produced);producer_visible=color[0]==0x3f000002&&copied[0]==0&&copied[2]==0x3f800000;fired|=forward_binding_gates_consumer(&gates,consumed);}
    else
#endif
    assert(mtl_wait(consumed,5000000000ull));
    assert(!fired&&producer_visible);assert(reads_after==reads&&flushes_after==flushes&&copies_after-copies==sizeof color);assert(!memcmp(color,copied,sizeof color)&&copied[0]==0x3f000002);
    const uint8_t expected[4]={128,0,0,255};forward_binding_pixels(&target,expected);mtl_pool_del(color);mtl_pool_del(copied);mtl_tex_release(&target);mtl_prog_release(&add);mtl_prog_release(&graphics);
    printf("Vulkan dispatch/copy/storage forwarding: %s GPU NVN copy, exact red 128, no CPU reread/dependency flush PASS\n",crossing?"cross-batch":"same-batch");
}
static void forward_binding_copy_vertex_case(int crossing) {
#ifdef DM_VK_REFERENCE_GATES
    if(crossing&&!reference_gates_supported()){puts("Vulkan dispatch/copy/vertex forwarding: timeline case unavailable");return;}forward_binding_gates gates;
#else
    if(crossing)return;
#endif
    mtl_drain();tracked_vertices=NULL;protected_vertices=NULL;uint32_t vertex_bits[6]={0xbf7ffff9,0xbf800000,0x40400000,0xbf800000,0xbf800000,0x40400000};float copied[10];for(unsigned i=0;i<10;i++)copied[i]=9;float blue[4]={0,0,1,1};mtl_pool_add(vertex_bits,sizeof vertex_bits);mtl_pool_add(copied,sizeof copied);mtl_pool_add(blue,sizeof blue);
    uint8_t controls[3][0x880]={{5},{6},{4}};prog_t add={.nstage=1,.serial=++forward_binding_serial,.sd={{.control=controls[0]}}},graphics={.nstage=2,.serial=++forward_binding_serial,.sd={{.control=controls[1]},{.control=controls[2]}}};
    tex_t target={.serial=++forward_binding_serial,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};nvn_tex_layout(&target.d,&target.L);
    uint64_t flushes=dependency_flushes,produced=0;int fired=0,producer_visible=1;memset(&state,0,sizeof state);state.program=&add;state.ssbo[5][0]=(p_bind_buf){.addr=(uintptr_t)vertex_bits,.size=sizeof vertex_bits};
#ifdef DM_VK_REFERENCE_GATES
    if(crossing)forward_binding_gates_create(&gates);
#endif
    mtl_sub_begin();
#ifdef DM_VK_REFERENCE_GATES
    if(crossing)reference_gate_arm(gates.gates[0],1);
#endif
    p_dispatch one={1,1,1,0};dispatch_compute(&one);uint64_t reads=snapshot_bytes,copies=forward_copy_bytes;
    if(crossing){produced=async_raw_commit();
#ifdef DM_VK_REFERENCE_GATES
        forward_binding_gates_watch(&gates);
#endif
        mtl_sub_begin();
#ifdef DM_VK_REFERENCE_GATES
        reference_gate_arm(gates.gates[1],1);
#endif
    }
    mtl_sub_copy((uintptr_t)vertex_bits,(uintptr_t)(copied+4),sizeof vertex_bits);forward_binding_graphics_state(&graphics,&target);state.ubo[1][0]=(p_bind_buf){.addr=(uintptr_t)blue,.size=sizeof blue};state.attributes[0]=(va_st){.fmt=NVN_FORMAT_RG32F};state.streams[0]=(vs_st){.stride=8};state.vertices[0]=(NVNbufferRange){(uintptr_t)(copied+4),sizeof vertex_bits};
    p_draw triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};draw_graphics(&triangle,NULL);uint64_t reads_after=snapshot_bytes,flushes_after=dependency_flushes,copies_after=forward_copy_bytes;uint64_t consumed=async_raw_commit();
#ifdef DM_VK_REFERENCE_GATES
    if(crossing){fired|=forward_binding_gates_producer(&gates,produced);producer_visible=vertex_bits[0]==0xbf800000&&copied[4]==9;fired|=forward_binding_gates_consumer(&gates,consumed);}
    else
#endif
    assert(mtl_wait(consumed,5000000000ull));
    assert(!fired&&producer_visible);assert(reads_after-reads==sizeof blue&&flushes_after==flushes&&copies_after-copies==sizeof vertex_bits);assert(!memcmp(copied+4,vertex_bits,sizeof vertex_bits));for(unsigned i=0;i<4;i++)assert(copied[i]==9);assert(copied[4]==-1&&copied[5]==-1&&copied[6]==3&&copied[7]==-1&&copied[8]==-1&&copied[9]==3);
    const uint8_t expected[4]={0,0,255,255};forward_binding_pixels(&target,expected);mtl_pool_del(vertex_bits);mtl_pool_del(copied);mtl_pool_del(blue);mtl_tex_release(&target);mtl_prog_release(&add);mtl_prog_release(&graphics);
    printf("Vulkan dispatch/copy/vertex forwarding: %s GPU NVN copy into offset guest range, exact blue pixels, no vertex CPU reread/dependency flush PASS\n",crossing?"cross-batch":"same-batch");
}
static void forward_binding_copy_partial_test(void) {
    mtl_drain();tracked_vertices=NULL;protected_vertices=NULL;uint32_t color[4]={0x3efffffb,0,0,0x3f800000},copied[4]={0,0,0x3f800000,0x3f800000};mtl_pool_add(color,sizeof color);mtl_pool_add(copied,sizeof copied);
    uint8_t controls[3][0x880]={{5},{1},{7}};prog_t add={.nstage=1,.serial=++forward_binding_serial,.sd={{.control=controls[0]}}},graphics={.nstage=2,.serial=++forward_binding_serial,.sd={{.control=controls[1]},{.control=controls[2]}}};
    tex_t target={.serial=++forward_binding_serial,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};nvn_tex_layout(&target.d,&target.L);
    memset(&state,0,sizeof state);state.program=&add;state.ssbo[5][0]=(p_bind_buf){.addr=(uintptr_t)color,.size=sizeof color};mtl_sub_begin();p_dispatch one={1,1,1,0};dispatch_compute(&one);
    // 后一次仅写 green；完整复制不得偷偷借旧的 red-only 全缓冲，必须公布两次写再取合并值。
    // A later green-only output blocks borrowing the older full red buffer; publish both writes before taking the merged fallback snapshot.
    post_op *patch=post_new(0);patch->destination=color+1;patch->size=4;patch->output=buffer_create(4);post_register_range(patch);vkCmdFillBuffer(command,patch->output.buffer,0,4,0x3f800000);forward_mark_gpu_recorded(patch,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,1);forward_test_host_visibility();
    forward_slice slice;assert(forward_find((uintptr_t)color,sizeof color,4,NULL,&slice)==FORWARD_PARTIAL);uint64_t reads=snapshot_bytes,flushes=dependency_flushes,copies=forward_copy_bytes;
    mtl_sub_copy((uintptr_t)color,(uintptr_t)copied,sizeof color);assert(dependency_flushes==flushes+1&&snapshot_bytes==reads+sizeof color&&forward_copy_bytes==copies);
    forward_binding_graphics_state(&graphics,&target);state.ssbo[1][0]=(p_bind_buf){.addr=(uintptr_t)copied,.size=sizeof copied};p_draw triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};draw_graphics(&triangle,NULL);assert(snapshot_bytes==reads+sizeof color&&dependency_flushes==flushes+1);uint64_t consumed=async_raw_commit();assert(mtl_wait(consumed,5000000000ull));
    assert(!memcmp(color,copied,sizeof color)&&color[0]==0x3f000002&&color[1]==0x3f800000&&color[2]==0&&color[3]==0x3f800000);const uint8_t expected[4]={128,255,0,255};forward_binding_pixels(&target,expected);mtl_pool_del(color);mtl_pool_del(copied);mtl_tex_release(&target);mtl_prog_release(&add);mtl_prog_release(&graphics);
    puts("Vulkan GPU NVN copy: later partial GPU write forces one real flush, preserves merged bytes and exact red/green pixels PASS");
}
static void forward_binding_reference_test(void) {
    forward_binding_storage_case(0);forward_binding_storage_case(1);forward_binding_vertex_case(0);forward_binding_vertex_case(1);
    forward_binding_copy_storage_case(0);forward_binding_copy_storage_case(1);forward_binding_copy_vertex_case(0);forward_binding_copy_vertex_case(1);forward_binding_copy_partial_test();
}
