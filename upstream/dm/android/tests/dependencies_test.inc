// 实际 GPU 写回与事件按真实 CPU 读取范围等待；行间 padding 不应触发等待。
// Wait for real CPU read ranges of GPU writes/events; row padding must not force completion.
static void dependencies_reference_test(void) {
    uint32_t source[4]={9,9,0xff0000ff,0};float red[4]={1,0,0,1};volatile uint32_t event=0;uint64_t report[2]={0};
    tex_t texture={.serial=930001,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=1,.height=2,.depth=1,.levels=1}};
    tex_t target={.serial=930002,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};
    nvn_tex_layout(&texture.d,&texture.L);nvn_tex_layout(&target.d,&target.L);texture.mem=calloc(1,texture.L.size);assert(texture.mem);
    uint8_t controls[3][0x880]={{5},{1},{4}};prog_t compute={.nstage=1,.sd={{.control=controls[0]}}},solid={.nstage=2,.sd={{.control=controls[1]},{.control=controls[2]}}};
    p_dispatch one={1,1,1,0};p_draw triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};p_clear_color clear={.mask=15};
    mtl_pool_add(source,sizeof source);memset(&state,0,sizeof state);state.program=&compute;state.ssbo[5][0]=(p_bind_buf){.addr=(uintptr_t)(source+1),.size=4};
    uint64_t before=mtl_last_fence();mtl_sub_begin();post_new(4);dispatch_compute(&one);mtl_sub_event(&event,0,123);mtl_sub_counter((uintptr_t)report,1);
    state.program=&solid;state.ubo[1][0]=(p_bind_buf){.addr=(uintptr_t)red,.size=sizeof red};state.rt.n=1;state.rt.color[0]=&target;state.mask.mask=15;state.scissor=(p_scissor){0,0,16,16};
    clear_color(&clear);draw_graphics(&triangle,NULL);assert(mtl_last_fence()==before&&source[1]==9&&event==0);mtl_sub_commit();assert(source[1]==16&&event==123&&report[0]==0&&samples_passed==256);
    uint8_t pixels[16*16*4];NVNcopyRegion full={0,0,0,16,16,1};mtl_read_texels(&target,0,0,&full,pixels,16*4,0);const uint8_t expected_red[4]={255,0,0,255};for(unsigned i=0;i<256;i++)assert(!memcmp(pixels+i*4,expected_red,4));
    // GPU 改行间 padding；仅复制两个真实像素，仍可留在同一批。
    // A GPU write touches row padding; copying only the two actual pixels can stay in the same batch.
    source[1]=9;state.program=&compute;before=mtl_last_fence();mtl_sub_begin();dispatch_compute(&one);state.copy_stride=8;
    p_copy_b2t copy={.src=(uintptr_t)source,.tex=&texture,.r={0,0,0,1,2,1}};copy_buffer_texture(&copy);assert(mtl_last_fence()==before&&source[1]==9);mtl_sub_commit();assert(source[1]==16);
    uint8_t texels[8];NVNcopyRegion two={0,0,0,1,2,1};mtl_read_texels(&texture,0,0,&two,texels,4,0);assert(!memcmp(texels,source,4)&&!memcmp(texels+4,source+2,4));
    // 改为写真实首像素：必须先完成并公布，再建立上传快照。
    // Writing the actual first pixel requires completion/publication before creating its upload snapshot.
    source[0]=9;state.ssbo[5][0].addr=(uintptr_t)source;before=mtl_last_fence();mtl_sub_begin();dispatch_compute(&one);copy_buffer_texture(&copy);assert(mtl_last_fence()==before+1&&source[0]==16);mtl_sub_commit();
    mtl_read_texels(&texture,0,0,&two,texels,4,0);assert(!memcmp(texels,source,4)&&!memcmp(texels+4,source+2,4));
    mtl_pool_del(source);mtl_tex_release(&texture);mtl_tex_release(&target);free(texture.mem);
    puts("Vulkan dependencies: unrelated GPU writes/events/counters remain deferred; strided padding avoids waits; actual source overlap waits before upload PASS");
}
