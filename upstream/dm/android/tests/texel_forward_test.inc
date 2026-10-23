// 真正 GPU 类型化写→读取/视图复用；用不同 CPU 初值证明没有读取尚未发布的旧数据。
// Real GPU typed-buffer writes/reads and view reuse; different CPU initial values rule out reading unpublished stale data.
static void texel_forward_case(int crossing) {
#ifdef DM_VK_REFERENCE_GATES
    if(crossing&&!reference_gates_supported()){puts("Vulkan texel forwarding: timeline case unavailable");return;}forward_binding_gates gates;
#else
    if(crossing)return;
#endif
    mtl_drain();tracked_vertices=NULL;protected_vertices=NULL;float color[4]={1,0,0,1};mtl_pool_add(color,sizeof color);
    uint8_t controls[3][0x880]={{9},{1},{8}};prog_t compute={.nstage=1,.serial=++forward_binding_serial,.sd={{.control=controls[0]}}},graphics={.nstage=2,.serial=++forward_binding_serial,.sd={{.control=controls[1]},{.control=controls[2]}}};
    tex_t input={.serial=++forward_binding_serial,.mem=(uint8_t*)color,.d={.target=NVN_TEXTURE_TARGET_BUFFER,.format=NVN_FORMAT_RGBA32F,.width=1,.height=1,.depth=1,.levels=1}},target={.serial=++forward_binding_serial,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};
    nvn_tex_layout(&input.d,&input.L);nvn_tex_layout(&target.d,&target.L);mtl_set_tex_pool_n(256);mtl_reg_tex(111,&input,NULL);
    memset(&state,0,sizeof state);state.program=&compute;state.images[5][0]=111;uint64_t flushes=dependency_flushes,produced=0;int fired=0;
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
    uint64_t hits=texel_view_hits;forward_binding_graphics_state(&graphics,&target);state.textures[1][0]=111;p_draw triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};draw_graphics(&triangle,NULL);draw_graphics(&triangle,NULL);
    assert(snapshot_bytes==reads&&dependency_flushes==flushes&&texel_view_hits>hits&&color[0]==1&&color[1]==0);
    uint64_t consumed=async_raw_commit();
#ifdef DM_VK_REFERENCE_GATES
    if(crossing){fired|=forward_binding_gates_producer(&gates,produced);assert(color[0]==10.f/255.f&&color[1]==80.f/255.f);fired|=forward_binding_gates_consumer(&gates,consumed);}
    else
#endif
    assert(mtl_wait(consumed,5000000000ull));assert(!fired);const uint8_t expected[4]={10,80,220,255};forward_binding_pixels(&target,expected);
    mtl_pool_del(color);mtl_tex_release(&target);mtl_prog_release(&compute);mtl_prog_release(&graphics);
    printf("Vulkan texel forwarding: %s actual GPU rgba writes, exact 10/80/220 pixels, no CPU reread/flush and reused view PASS\n",crossing?"cross-batch":"same-batch");
}
static void texel_forward_partial_case(void) {
    mtl_drain();tracked_vertices=NULL;protected_vertices=NULL;float color[4]={0,1,0,1},red=1;mtl_pool_add(color,sizeof color);
    uint8_t controls[3][0x880]={{9},{1},{8}};prog_t compute={.nstage=1,.serial=++forward_binding_serial,.sd={{.control=controls[0]}}},graphics={.nstage=2,.serial=++forward_binding_serial,.sd={{.control=controls[1]},{.control=controls[2]}}};
    tex_t input={.serial=++forward_binding_serial,.mem=(uint8_t*)color,.d={.target=NVN_TEXTURE_TARGET_BUFFER,.format=NVN_FORMAT_RGBA32F,.width=1,.height=1,.depth=1,.levels=1}},target={.serial=++forward_binding_serial,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};
    nvn_tex_layout(&input.d,&input.L);nvn_tex_layout(&target.d,&target.L);mtl_set_tex_pool_n(256);mtl_reg_tex(112,&input,NULL);
    memset(&state,0,sizeof state);state.program=&compute;state.images[5][0]=112;mtl_sub_begin();p_dispatch one={1,1,1,0};dispatch_compute(&one);mtl_sub_copy((uintptr_t)&red,(uintptr_t)color,sizeof red);uint64_t flushes=dependency_flushes;
    forward_binding_graphics_state(&graphics,&target);state.textures[1][0]=112;p_draw triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};draw_graphics(&triangle,NULL);assert(dependency_flushes==flushes+1);
    uint64_t ticket=async_raw_commit();assert(mtl_wait(ticket,5000000000ull));const uint8_t expected[4]={255,80,220,255};forward_binding_pixels(&target,expected);
    mtl_pool_del(color);mtl_tex_release(&target);mtl_prog_release(&compute);mtl_prog_release(&graphics);
    puts("Vulkan texel forwarding: later partial write forces real wait and preserves merged exact pixels PASS");
}
static void texel_forward_known_offset_case(void) {
    mtl_drain();tracked_vertices=NULL;protected_vertices=NULL;mtl_init();uint64_t alignment=gpu_limits.minTexelBufferOffsetAlignment;if(alignment<16)alignment=16;uint64_t bytes=alignment+16;uint8_t *destination=calloc(1,bytes),*source=calloc(1,bytes);assert(destination&&source);
    float rgba[4]={0,0,1,1};memcpy(source+alignment,rgba,sizeof rgba);mtl_pool_add(destination,bytes);
    uint8_t controls[2][0x880]={{1},{8}};prog_t graphics={.nstage=2,.serial=++forward_binding_serial,.sd={{.control=controls[0]},{.control=controls[1]}}};
    tex_t input={.serial=++forward_binding_serial,.mem=destination+alignment,.d={.target=NVN_TEXTURE_TARGET_BUFFER,.format=NVN_FORMAT_RGBA32F,.width=1,.height=1,.depth=1,.levels=1}},target={.serial=++forward_binding_serial,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};
    nvn_tex_layout(&input.d,&input.L);nvn_tex_layout(&target.d,&target.L);mtl_set_tex_pool_n(256);mtl_reg_tex(113,&input,NULL);mtl_sub_begin();mtl_sub_copy((uintptr_t)source,(uintptr_t)destination,bytes);uint64_t reads=snapshot_bytes,flushes=dependency_flushes,hits=texel_view_hits;
    forward_binding_graphics_state(&graphics,&target);state.textures[1][0]=113;p_draw triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};draw_graphics(&triangle,NULL);draw_graphics(&triangle,NULL);
    assert(snapshot_bytes==reads&&dependency_flushes==flushes&&texel_view_hits==hits+1&&!memcmp(destination+alignment,(float[4]){0},16));
    uint64_t ticket=async_raw_commit();assert(mtl_wait(ticket,5000000000ull));const uint8_t expected[4]={0,0,255,255};forward_binding_pixels(&target,expected);
    mtl_pool_del(destination);mtl_tex_release(&target);mtl_prog_release(&graphics);free(destination);free(source);
    puts("Vulkan texel forwarding: immutable known-copy version, exact nonzero aligned offset, deferred guest publication and blue pixels PASS");
}
static void texel_forward_reference_test(void) {texel_forward_case(0);texel_forward_case(1);texel_forward_partial_case();texel_forward_known_offset_case();}
