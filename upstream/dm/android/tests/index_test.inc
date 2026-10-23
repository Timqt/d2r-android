// 真正 indexed 像素与 CPU 读取量检查索引版本、最大值复用、u8 展开及 flush 后重新绑定。
// Real indexed pixels and CPU read counts verify index versions, maximum reuse, u8 expansion and rebinding after flushes.
static uint64_t index_test_serial=970000;
static void index_test_state(const prog_t *program,const tex_t *target,float *vertices,uint64_t bound_size,float *color) {
    memset(&state,0,sizeof state);state.program=program;state.rt.n=1;state.rt.color[0]=target;state.mask.mask=UINT32_MAX;state.polygon.mode=2;state.depth_range=(p_depth_range){0,1};state.scissor=(p_scissor){0,0,16,16};
    state.attributes[0]=(va_st){.fmt=NVN_FORMAT_RG32F};state.streams[0]=(vs_st){.stride=8};state.vertices[0]=(NVNbufferRange){(uintptr_t)vertices,bound_size};state.ubo[1][0]=(p_bind_buf){.addr=(uintptr_t)color,.size=16};
}
static void index_test_pixels(const tex_t *target,const uint8_t left[4],const uint8_t right[4]) {
    uint8_t pixels[16*16*4];NVNcopyRegion region={0,0,0,16,16,1};mtl_read_texels(target,0,0,&region,pixels,16*4,0);
    for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++)assert(!memcmp(pixels+(y*16+x)*4,x<8?left:right,4));
}
static void index_test_activation(void) {
    mtl_drain();mtl_sub_begin();uint16_t values[4]={0,257,2,0};vk_buffer first=buffer_create(sizeof values);memcpy(first.mapped,values,sizeof values);
    VkBuffer handle=first.buffer;uint64_t activation=first.owner->activation,hits=index_maximum_hits,misses=index_maximum_misses;
    assert(index_buffer_maximum(first,2)==257);assert(index_buffer_maximum(first,1)==2);vk_buffer prefix=first;prefix.size=2;assert(index_buffer_maximum(prefix,2)==0);
    vk_buffer borrowed=buffer_borrow(first);assert(borrowed.owner->activation==activation&&index_buffer_maximum(borrowed,2)==257);buffer_destroy(&borrowed);buffer_destroy(&first);
    // 同一 backing 被缓存复用后必须是新版本；仅缓存 VkBuffer handle/owner 地址会错误命中 257。
    // Reusing the same cached backing creates a new version; a VkBuffer-handle/owner-address key would incorrectly return 257.
    vk_buffer second=buffer_create(sizeof values);assert(second.buffer==handle&&second.owner->activation!=activation);memset(second.mapped,0,sizeof values);assert(index_buffer_maximum(second,2)==0);buffer_destroy(&second);
    assert(index_maximum_hits==hits+1&&index_maximum_misses==misses+4);uint64_t ticket=async_raw_commit();assert(mtl_wait(ticket,5000000000ull));
    puts("Vulkan index maxima: width/range keys, borrowed version identity and same-backing cache reuse activation PASS");
}
static void index_test_dirty_copy(void) {
    mtl_drain();uint16_t indices[3]={0,1,2},patch[2]={1,2};float vertices[8]={99,99,-1,-1,3,-1,-1,3},blue[4]={0,0,1,1};uint32_t unrelated=9;
    mtl_pool_add(indices,sizeof indices);mtl_pool_add(vertices,sizeof vertices);mtl_pool_add(blue,sizeof blue);mtl_pool_add(&unrelated,sizeof unrelated);tracked_vertices=indices;protected_vertices=NULL;dirty_generation++;
    uint8_t controls[3][0x880]={{5},{6},{4}};prog_t compute={.nstage=1,.serial=++index_test_serial,.sd={{.control=controls[0]}}},graphics={.nstage=2,.serial=++index_test_serial,.sd={{.control=controls[1]},{.control=controls[2]}}};
    tex_t target={.serial=++index_test_serial,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};nvn_tex_layout(&target.d,&target.L);
    memset(&state,0,sizeof state);state.program=&compute;state.ssbo[5][0]=(p_bind_buf){.addr=(uintptr_t)&unrelated,.size=sizeof unrelated};mtl_sub_begin();p_dispatch one={1,1,1,0};dispatch_compute(&one);
    index_test_state(&graphics,&target,vertices,1u<<20,blue);p_clear_color clear={.mask=15};clear_color(&clear);state.scissor.w=8;
    p_draw_idx triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.itype=NVN_INDEX_TYPE_UNSIGNED_SHORT,.count=3,.base_vertex=1,.instances=1,.ibuf=(uintptr_t)indices};
    uint64_t reads=snapshot_bytes,hits=index_maximum_hits,misses=index_maximum_misses,flushes=dependency_flushes;draw_graphics(NULL,&triangle);
    assert(snapshot_bytes==reads+sizeof indices+sizeof vertices+sizeof blue&&index_maximum_misses==misses+1&&index_maximum_hits==hits+1);
    reads=snapshot_bytes;hits=index_maximum_hits;draw_graphics(NULL,&triangle);assert(snapshot_bytes==reads+sizeof vertices+sizeof blue&&index_maximum_hits==hits+2&&index_maximum_misses==misses+1);
    // 真实 CPU 脏写改变索引最大值和可见像素；仍使用同一 guest 地址不能命中旧 snapshot。
    // A real dirty CPU write changes the maximum and pixels; keeping the guest address must not reuse an old snapshot.
    memset(indices,0,sizeof indices);dirty_generation++;state.scissor.x=8;reads=snapshot_bytes;misses=index_maximum_misses;draw_graphics(NULL,&triangle);
    assert(snapshot_bytes==reads+sizeof indices+16+sizeof blue&&index_maximum_misses==misses+1);
    uint64_t ticket=async_raw_commit();assert(mtl_wait(ticket,5000000000ull));const uint8_t blue_pixel[4]={0,0,255,255},black_pixel[4]={0,0,0,0};index_test_pixels(&target,blue_pixel,black_pixel);
    // 已知 partial copy 只覆盖后两个索引；必须 overlay 新值，不能依赖完成前的 guest 或旧最大值。
    // A known partial copy overwrites the last two indices; overlay those values rather than reading unpublished guest bytes or an old maximum.
    memset(&state,0,sizeof state);state.program=&compute;state.ssbo[5][0]=(p_bind_buf){.addr=(uintptr_t)&unrelated,.size=sizeof unrelated};mtl_sub_begin();dispatch_compute(&one);mtl_sub_copy((uintptr_t)patch,(uintptr_t)(indices+1),sizeof patch);
    index_test_state(&graphics,&target,vertices,1u<<20,blue);state.scissor=(p_scissor){8,0,8,16};reads=snapshot_bytes;misses=index_maximum_misses;draw_graphics(NULL,&triangle);
    assert(indices[0]==0&&indices[1]==0&&indices[2]==0);assert(snapshot_bytes==reads+sizeof indices*2+sizeof vertices+sizeof blue&&index_maximum_misses==misses+2&&dependency_flushes==flushes);
    ticket=async_raw_commit();assert(mtl_wait(ticket,5000000000ull));assert(indices[0]==0&&indices[1]==1&&indices[2]==2);index_test_pixels(&target,blue_pixel,blue_pixel);
    tracked_vertices=NULL;protected_vertices=NULL;mtl_pool_del(indices);mtl_pool_del(vertices);mtl_pool_del(blue);mtl_pool_del(&unrelated);mtl_tex_release(&target);mtl_prog_release(&compute);mtl_prog_release(&graphics);
    puts("Vulkan indexed draws: one tracked index read/maximum scan, dirty pixels, partial known-copy overlay and exact base-vertex prefix PASS");
}
static void index_test_width(unsigned width) {
    mtl_drain();uint8_t raw[12]={0};uint64_t vertex_bytes=width==1?256*8:32;float *vertices=malloc(vertex_bytes);assert(vertices);for(unsigned i=0;i<vertex_bytes/sizeof(float);i++)vertices[i]=99;
    if(width==1){raw[0]=0;raw[1]=128;raw[2]=255;vertices[0]=-1;vertices[1]=-1;vertices[128*2]=3;vertices[128*2+1]=-1;vertices[255*2]=-1;vertices[255*2+1]=3;}
    else{uint32_t values[3]={0,1,2};memcpy(raw,values,sizeof values);const float positions[8]={99,99,-1,-1,3,-1,-1,3};memcpy(vertices,positions,sizeof positions);}
    float blue[4]={0,0,1,1};uint32_t unrelated=9;mtl_pool_add(raw,sizeof raw);mtl_pool_add(vertices,vertex_bytes);mtl_pool_add(blue,sizeof blue);mtl_pool_add(&unrelated,sizeof unrelated);tracked_vertices=raw;protected_vertices=NULL;dirty_generation++;
    uint8_t controls[3][0x880]={{5},{6},{4}};prog_t compute={.nstage=1,.serial=++index_test_serial,.sd={{.control=controls[0]}}},graphics={.nstage=2,.serial=++index_test_serial,.sd={{.control=controls[1]},{.control=controls[2]}}};
    tex_t target={.serial=++index_test_serial,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};nvn_tex_layout(&target.d,&target.L);
    memset(&state,0,sizeof state);state.program=&compute;state.ssbo[5][0]=(p_bind_buf){.addr=(uintptr_t)&unrelated,.size=sizeof unrelated};mtl_sub_begin();p_dispatch one={1,1,1,0};dispatch_compute(&one);index_test_state(&graphics,&target,vertices,1u<<20,blue);
    p_draw_idx triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.itype=width==1?NVN_INDEX_TYPE_UNSIGNED_BYTE:NVN_INDEX_TYPE_UNSIGNED_INT,.count=3,.base_vertex=width==1?0:1,.instances=1,.ibuf=(uintptr_t)raw};
    uint64_t reads=snapshot_bytes,hits=index_maximum_hits,misses=index_maximum_misses;draw_graphics(NULL,&triangle);assert(snapshot_bytes==reads+3*width+vertex_bytes+sizeof blue);
    reads=snapshot_bytes;draw_graphics(NULL,&triangle);assert(snapshot_bytes==reads+vertex_bytes+sizeof blue&&index_maximum_hits==hits+3&&index_maximum_misses==misses+1);
    uint64_t ticket=async_raw_commit();assert(mtl_wait(ticket,5000000000ull));const uint8_t blue_pixel[4]={0,0,255,255};index_test_pixels(&target,blue_pixel,blue_pixel);
    tracked_vertices=NULL;protected_vertices=NULL;mtl_pool_del(raw);mtl_pool_del(vertices);mtl_pool_del(blue);mtl_pool_del(&unrelated);free(vertices);mtl_tex_release(&target);mtl_prog_release(&compute);mtl_prog_release(&graphics);
    printf("Vulkan indexed draws: u%u%s exact blue pixels and one index snapshot/maximum scan PASS\n",width*8,width==1?" including 128/255 expanded to u16":" with base vertex and bounded prefix");
}
static void index_test_flush_rebind(void) {
    mtl_drain();uint16_t indices[3]={0,2,1};float vertices[6]={-1,-1,3,-1,-1,3},blue[4]={0,0,1,1};mtl_pool_add(indices,sizeof indices);mtl_pool_add(vertices,sizeof vertices);mtl_pool_add(blue,sizeof blue);tracked_vertices=NULL;protected_vertices=NULL;
    uint8_t controls[2][0x880]={{6},{4}};prog_t graphics={.nstage=2,.serial=++index_test_serial,.sd={{.control=controls[0]},{.control=controls[1]}}};tex_t target={.serial=++index_test_serial,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};nvn_tex_layout(&target.d,&target.L);
    index_test_state(&graphics,&target,vertices,1u<<20,blue);mtl_sub_begin();p_clear_color clear={.mask=15};clear_color(&clear);graphics_pass_end();
    post_op *patch=post_new(0);patch->destination=vertices;patch->size=4;patch->output=buffer_create(4);post_register_range(patch);vkCmdFillBuffer(command,patch->output.buffer,0,4,0xbf800000);forward_mark_gpu_recorded(patch,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,1);
    VkMemoryBarrier visibility={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&visibility,0,NULL,0,NULL);
    // preflight 读取后修改两个索引，再由 partial vertex 依赖触发 flush。实际 Draw 须重新 snapshot，绘制退化三角形。
    // Change two indices after the preflight read, then flush for a partial vertex dependency. The actual draw must resnapshot and render a degenerate triangle.
    write_after_read=(uintptr_t)indices;write_after_read_value=0;p_draw_idx triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.itype=NVN_INDEX_TYPE_UNSIGNED_SHORT,.count=3,.instances=1,.ibuf=(uintptr_t)indices};
    uint64_t reads=snapshot_bytes,misses=index_maximum_misses,flushes=dependency_flushes;draw_graphics(NULL,&triangle);
    assert(!write_after_read&&indices[0]==0&&indices[1]==0&&indices[2]==1);assert(index_maximum_misses==misses+2&&dependency_flushes==flushes+1&&snapshot_bytes==reads+sizeof indices*2+16+sizeof blue);
    uint64_t ticket=async_raw_commit();assert(mtl_wait(ticket,5000000000ull));const uint8_t black_pixel[4]={0,0,0,0};index_test_pixels(&target,black_pixel,black_pixel);mtl_pool_del(indices);mtl_pool_del(vertices);mtl_pool_del(blue);mtl_tex_release(&target);mtl_prog_release(&graphics);
    puts("Vulkan indexed draw: preflight/vertex flush retires old snapshot; changed untracked indices are resnapshotted and exact pixels remain black PASS");
}
static void index_reference_test(void) {
    index_test_activation();index_test_dirty_copy();index_test_width(1);index_test_width(4);index_test_flush_rebind();
}
