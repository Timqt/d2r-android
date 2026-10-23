// 用真实像素检查 uniform 版本命中、脏写、待发布复制、描述符变化及命中后的图像风险。
// Real pixels check uniform-version hits, dirty writes, deferred copies, changed descriptors and image hazards after hits.
static void binding_cache_pixels(const tex_t *target,uint8_t *pixels,const uint8_t *left,const uint8_t *right) {
    NVNcopyRegion region={0,0,0,16,16,1};mtl_read_texels(target,0,0,&region,pixels,16*4,0);
    for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++)assert(!memcmp(pixels+(y*16+x)*4,x<8?left:right,4));
}
static void binding_cache_reference_test(void) {
    const uint8_t red_pixel[4]={255,0,0,255},green_pixel[4]={0,255,0,255},blue_pixel[4]={0,0,255,255},black_pixel[4]={0,0,0,255};
    float uniform[4]={1,0,0,1},red[4]={1,0,0,1},green[4]={0,1,0,1},blue[4]={0,0,1,1};uint8_t pixels[16*16*4];
    tex_t target={.serial=90001,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};nvn_tex_layout(&target.d,&target.L);
    tex_t texture=target,other=target;texture.serial=90002;other.serial=90003;
    uint8_t controls[3][0x880]={{1},{4},{2}};prog_t solid={.nstage=2,.serial=90001,.sd={{.control=controls[0]},{.control=controls[1]}}},sampled={.nstage=2,.serial=90002,.sd={{.control=controls[0]},{.control=controls[2]}}};
    p_draw triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};p_clear_color red_clear={.mask=15,.rgba={1,0,0,1}},green_clear={.mask=15,.rgba={0,1,0,1}};
    mtl_pool_add(uniform,sizeof uniform);tracked_vertices=uniform;protected_vertices=NULL;dirty_generation++;
    memset(&state,0,sizeof state);state.program=&solid;state.rt.n=1;state.rt.color[0]=&target;state.mask.mask=UINT32_MAX;state.polygon.mode=2;
    state.ubo[1][0]=(p_bind_buf){.addr=(uintptr_t)uniform,.size=sizeof uniform};state.scissor=(p_scissor){0,0,8,16};
    uint64_t hits=uniform_version_hits,descriptor_hits=descriptor_version_hits,reads=uniform_version_read_bytes;
    mtl_sub_begin();draw_graphics(&triangle,NULL);draw_graphics(&triangle,NULL);assert(uniform_version_hits==hits+1&&uniform_version_read_bytes==reads+sizeof uniform);assert(descriptor_version_hits>=descriptor_hits+2);
    memcpy(uniform,green,sizeof uniform);dirty_generation++;state.scissor.x=8;draw_graphics(&triangle,NULL);mtl_sub_commit();binding_cache_pixels(&target,pixels,red_pixel,green_pixel);
    // 内容相同的 CPU 地址也会发生新模型写；同页代数改变后不能命中旧的矩阵快照。
    // A stable CPU address can receive a new model value; a new page generation must not hit an old snapshot.
    memcpy(uniform,red,sizeof uniform);dirty_generation++;state.scissor=(p_scissor){0,0,4,16};mtl_sub_begin();draw_graphics(&triangle,NULL);
    mtl_sub_copy((uintptr_t)green,(uintptr_t)uniform,sizeof uniform);state.scissor.x=4;draw_graphics(&triangle,NULL);hits=uniform_version_hits;draw_graphics(&triangle,NULL);assert(uniform_version_hits==hits+1);
    // 部分覆盖必须保留前一个待发布复制的其它分量；客体内容到 fence 后才改变。
    // Partial overlap retains other components from the preceding deferred copy; guest bytes change only after the fence.
    mtl_sub_copy((uintptr_t)red,(uintptr_t)uniform,4);state.scissor.x=8;draw_graphics(&triangle,NULL);
    mtl_sub_copy((uintptr_t)green,(uintptr_t)uniform,sizeof uniform);state.scissor.x=12;draw_graphics(&triangle,NULL);assert(!memcmp(uniform,red,sizeof uniform));mtl_sub_commit();assert(!memcmp(uniform,green,sizeof uniform));
    NVNcopyRegion region={0,0,0,16,16,1};mtl_read_texels(&target,0,0,&region,pixels,16*4,0);const uint8_t yellow[4]={255,255,0,255};
    for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++)assert(!memcmp(pixels+(y*16+x)*4,x<4?red_pixel:x<8?green_pixel:x<12?yellow:green_pixel,4));
    // 缩小真实绑定大小后第四分量应读取零 padding，不能按地址命中上一份 16 字节版本。
    // Shrinking the bound size makes the fourth component read zero padding; an address-only hit would reuse the old 16-byte version.
    state.scissor=(p_scissor){0,0,8,16};mtl_sub_begin();draw_graphics(&triangle,NULL);state.ubo[1][0].size=12;state.scissor.x=8;draw_graphics(&triangle,NULL);mtl_sub_commit();
    const uint8_t transparent_green[4]={0,255,0,0};binding_cache_pixels(&target,pixels,green_pixel,transparent_green);state.ubo[1][0].size=sizeof uniform;
    // 未跟踪内存始终重新读取，即使脏页代数没有增加，CPU 修改也不能被忽略。
    // Always reread untracked memory; CPU changes remain visible even without a dirty-page generation increment.
    tracked_vertices=NULL;protected_vertices=NULL;memcpy(uniform,red,sizeof uniform);state.scissor=(p_scissor){0,0,8,16};mtl_sub_begin();draw_graphics(&triangle,NULL);
    hits=uniform_version_hits;memcpy(uniform,blue,sizeof uniform);state.scissor.x=8;draw_graphics(&triangle,NULL);assert(uniform_version_hits==hits);mtl_sub_commit();binding_cache_pixels(&target,pixels,red_pixel,blue_pixel);
    // 读取刚结束的真实脏写必须使下一次查找失效，同时保留本次绘制取得的旧值。
    // A dirty write immediately after reading invalidates the next lookup while retaining this draw's captured old value.
    tracked_vertices=uniform;protected_vertices=NULL;memcpy(uniform,red,sizeof uniform);dirty_generation++;write_after_read=(uintptr_t)uniform;write_after_read_value=0;
    state.scissor=(p_scissor){0,0,8,16};mtl_sub_begin();draw_graphics(&triangle,NULL);assert(!write_after_read&&uniform[0]==0);state.scissor.x=8;draw_graphics(&triangle,NULL);mtl_sub_commit();binding_cache_pixels(&target,pixels,red_pixel,black_pixel);
    tracked_vertices=NULL;protected_vertices=NULL;mtl_pool_del(uniform);
    // 重新登记同一 texture descriptor 到另一图像，payload 必须改变；相同逻辑 ID 不能直接复用旧 set。
    // Re-registering the same texture descriptor to another image changes its payload; a stable logical ID cannot reuse the old set.
    mtl_set_tex_pool_n(64);memset(&state,0,sizeof state);state.program=&sampled;state.rt.n=1;state.mask.mask=UINT32_MAX;state.polygon.mode=2;state.textures[1][0]=50;
    mtl_sub_begin();state.rt.color[0]=&texture;state.scissor=(p_scissor){0,0,16,16};clear_color(&red_clear);state.rt.color[0]=&other;clear_color(&green_clear);
    mtl_reg_tex(50,&texture,NULL);state.rt.color[0]=&target;state.scissor=(p_scissor){0,0,8,16};draw_graphics(&triangle,NULL);
    mtl_reg_tex(50,&other,NULL);state.scissor.x=8;draw_graphics(&triangle,NULL);mtl_sub_commit();binding_cache_pixels(&target,pixels,red_pixel,green_pixel);
    // 同一 VkImage/View 的内容在两 draw 之间改变：set 可以命中，但 transfer→shader 风险仍须同步。
    // One VkImage/View changes contents between draws: the set may hit, but transfer-to-shader hazards still need synchronization.
    mtl_reg_tex(50,&texture,NULL);mtl_sub_begin();state.rt.color[0]=&texture;state.scissor=(p_scissor){0,0,16,16};clear_color(&red_clear);
    state.rt.color[0]=&target;state.scissor=(p_scissor){0,0,8,16};draw_graphics(&triangle,NULL);descriptor_hits=descriptor_version_hits;
    state.rt.color[0]=&texture;state.scissor=(p_scissor){0,0,16,16};clear_color(&green_clear);state.rt.color[0]=&target;state.scissor=(p_scissor){8,0,8,16};draw_graphics(&triangle,NULL);assert(descriptor_version_hits>=descriptor_hits+2);
    mtl_sub_commit();binding_cache_pixels(&target,pixels,red_pixel,green_pixel);
    mtl_tex_release(&target);mtl_tex_release(&texture);mtl_tex_release(&other);mtl_prog_release(&solid);mtl_prog_release(&sampled);
    printf("Vulkan binding cache: stable uniforms, dirty writes, deferred partial copies, size/zero padding, untracked reads, changed descriptors and cache-hit hazards PASS; UBO hits=%llu misses=%llu descriptors hits=%llu misses=%llu\n",(unsigned long long)uniform_version_hits,(unsigned long long)uniform_version_misses,(unsigned long long)descriptor_version_hits,(unsigned long long)descriptor_version_misses);
}
