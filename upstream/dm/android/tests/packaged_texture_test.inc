// Packaged CPU versions survive GPU writes; pixels prove dirty refresh and preservation of real GPU contents.
// 打包纹理的 CPU 版本在 GPU 写入后保留；真实像素验证脏数据刷新和已有 GPU 内容的保留。
#ifndef DM_ANDROID_PACKAGED_TEXTURE_TEST_INCLUDED
#define DM_ANDROID_PACKAGED_TEXTURE_TEST_INCLUDED
static void packaged_bc_fill(tex_t *texture,uint16_t color) {
    uint8_t blocks[64]={0};for(unsigned i=0;i<4;i++){blocks[i*16]=blocks[i*16+1]=255;memcpy(blocks+i*16+8,&color,2);memcpy(blocks+i*16+10,&color,2);}assert(!nvn_swizzle(&texture->d,0,0,blocks,32,texture->mem));
}
static void packaged_rgba_fill(tex_t *texture,const uint8_t color[4]) {
    uint8_t bytes[8*8*4];for(unsigned i=0;i<64;i++)memcpy(bytes+4*i,color,4);assert(!nvn_swizzle(&texture->d,0,0,bytes,32,texture->mem));
}
static void packaged_pixels(const tex_t *target,const uint8_t color[4],const char *label) {
    uint8_t pixels[16*16*4];NVNcopyRegion region={0,0,0,16,16,1};mtl_read_texels(target,0,0,&region,pixels,64,0);
    for(unsigned i=0;i<256;i++)if(memcmp(pixels+4*i,color,4)){fprintf(stderr,"Packaged %s pixel%u observed=%u,%u,%u,%u expected=%u,%u,%u,%u\n",label,i,pixels[i*4],pixels[i*4+1],pixels[i*4+2],pixels[i*4+3],color[0],color[1],color[2],color[3]);assert(0);}
}
static void packaged_two_colors(const tex_t *target,const uint8_t first[4],unsigned first_count,const uint8_t second[4],const char *label) {
    uint8_t pixels[16*16*4];NVNcopyRegion region={0,0,0,16,16,1};mtl_read_texels(target,0,0,&region,pixels,64,0);unsigned count=0;
    for(unsigned i=0;i<256;i++){if(!memcmp(pixels+4*i,first,4))count++;else if(memcmp(pixels+4*i,second,4)){fprintf(stderr,"Packaged %s unexpected pixel%u=%u,%u,%u,%u\n",label,i,pixels[i*4],pixels[i*4+1],pixels[i*4+2],pixels[i*4+3]);assert(0);}}if(count!=first_count){fprintf(stderr,"Packaged %s first_count=%u expected=%u\n",label,count,first_count);assert(0);}
}
static void packaged_draw(prog_t *program,tex_t *texture,tex_t *target) {
    mtl_reg_tex(110,texture,NULL);state.program=program;state.rt.n=1;state.rt.color[0]=target;state.rt.depth=NULL;state.textures[1][0]=110;state.scissor=(p_scissor){0,0,16,16};p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};mtl_sub_begin();draw_graphics(&draw,NULL);mtl_sub_commit();
}
static void packaged_unknown_source(tex_t *texture,const uint8_t *bytes) {
    post_op *staging=post_new(3);staging->source=buffer_create(texture->L.size);memcpy(staging->source.mapped,bytes,texture->L.size);
    post_op *producer=post_new(0);producer->destination=texture->mem;producer->size=texture->L.size;producer->output=buffer_create(texture->L.size);post_register_range(producer);VkBufferCopy copy={.size=texture->L.size};vkCmdCopyBuffer(command,staging->source.buffer,producer->output.buffer,1,&copy);forward_mark_gpu_recorded(producer,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,1);
}
static void packaged_texture_reference_test(void) {
    mtl_drain();uint64_t saved_generation=dirty_generation;const void *saved_tracking=tracked_vertices,*saved_protection=protected_vertices;
    uint8_t controls[3][0x880]={{1},{2},{17}};prog_t graphics={.nstage=2,.sd={{.control=controls[0]},{.control=controls[1]}}},compute={.nstage=1,.sd={{.control=controls[2]}}};
    tex_t texture={.serial=870201,.has_layout=1,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA_DXT5_SRGB,.width=8,.height=8,.depth=1,.levels=1,.pkg_block_h=1}},target={.serial=870202,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};
    nvn_tex_layout(&texture.d,&texture.L);nvn_tex_layout(&target.d,&target.L);texture.mem=calloc(1,texture.L.size);assert(texture.mem);mtl_pool_add(texture.mem,texture.L.size);mtl_set_tex_pool_n(256);
    const uint8_t red[4]={255,0,0,255},green[4]={0,255,0,255},blue[4]={0,0,255,255};memset(&state,0,sizeof state);state.mask.mask=15;state.depth_range=(p_depth_range){0,1};
    for(unsigned tracked=0;tracked<2;tracked++){
        texture.serial=870201+tracked*2;tracked_vertices=tracked?texture.mem:NULL;dirty_generation=0;packaged_bc_fill(&texture,0xf800);packaged_draw(&graphics,&texture,&target);packaged_pixels(&target,red,"initial BC source, including valid generation0");
        uint8_t blocks[64]={0};for(unsigned i=0;i<4;i++){blocks[i*16]=blocks[i*16+1]=255;uint16_t c=31;memcpy(blocks+i*16+8,&c,2);memcpy(blocks+i*16+10,&c,2);}p_copy_b2t copy={.src=(uintptr_t)blocks,.tex=&texture,.r={0,0,0,8,8,1}};
        mtl_sub_begin();copy_buffer_texture(&copy);mtl_sub_commit();uint64_t uploads=texture_uploads;packaged_draw(&graphics,&texture,&target);packaged_pixels(&target,blue,"GPU blue preserved while CPU source unchanged");assert(texture_uploads==uploads);
        dm_guest_write_prepare(texture.mem,texture.L.size);packaged_bc_fill(&texture,0x07e0);image_entry *image=graphics_probe_image(texture.serial);uint64_t old_stamp=image->source_stamp,old_hash=image->source_hash;
        mtl_sub_begin();image_gpu_written(image);image_gpu_written(image);mtl_sub_commit();assert(image->source_stamp==old_stamp&&image->source_hash==old_hash);packaged_draw(&graphics,&texture,&target);packaged_pixels(&target,green,"later CPU green survives repeated GPU marking");
        // Two descriptor serials sharing CPU storage refresh independently after the same real CPU change.
        // 共用 CPU 存储的两个描述符序号在同一次真实 CPU 改写后各自刷新。
        tex_t alias=texture;alias.serial+=100;packaged_draw(&graphics,&alias,&target);packaged_pixels(&target,green,"CPU storage alias initial");dm_guest_write_prepare(texture.mem,texture.L.size);packaged_bc_fill(&texture,0xf800);
        packaged_draw(&graphics,&texture,&target);packaged_pixels(&target,red,"CPU alias refresh original");packaged_draw(&graphics,&alias,&target);packaged_pixels(&target,red,"CPU alias refresh second serial");mtl_tex_release(&alias);
        if(tracked){
            uint8_t *new_storage=calloc(1,texture.L.size);tex_t new_texture=texture;new_texture.mem=new_storage;packaged_bc_fill(&new_texture,0x07e0);
            mtl_sub_begin();packaged_unknown_source(&texture,new_storage);uint64_t flushes=dependency_flushes;p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};draw_graphics(&draw,NULL);assert(dependency_flushes==flushes+1);mtl_sub_commit();packaged_pixels(&target,green,"unknown GPU source completed before CPU deswizzle");free(new_storage);
        }
        mtl_tex_release(&texture);
    }
    // A known pending guest copy is visible to upload before the guest destination is published.
    // 已知的待发布访客内存复制在目标内存发布前就应对纹理上传可见。
    texture.serial=870220;tracked_vertices=texture.mem;packaged_bc_fill(&texture,0xf800);packaged_draw(&graphics,&texture,&target);uint8_t *known_bytes=calloc(1,texture.L.size);tex_t known=texture;known.mem=known_bytes;packaged_bc_fill(&known,0x07e0);
    mtl_sub_begin();mtl_sub_copy((uintptr_t)known_bytes,(uintptr_t)texture.mem,texture.L.size);p_draw known_draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};draw_graphics(&known_draw,NULL);assert(memcmp(texture.mem,known_bytes,texture.L.size));mtl_sub_commit();packaged_pixels(&target,green,"known pending copy snapshot before guest publication");free(known_bytes);mtl_tex_release(&texture);
    // Partial GPU copy must keep the packaged destination's untouched blocks from its CPU source.
    // 部分 GPU 复制必须从 CPU 源保留打包目标中未覆盖的块。
    texture.serial=870221;packaged_bc_fill(&texture,0xf800);tex_t partial_source=texture;partial_source.serial=870222;partial_source.mem=calloc(1,texture.L.size);assert(partial_source.mem);packaged_bc_fill(&partial_source,31);
    p_copy_t2t partial={.src=&partial_source,.dst=&texture,.sr={0,0,0,4,4,1},.dr={0,0,0,4,4,1}};mtl_sub_begin();copy_texture_texture(&partial);mtl_sub_commit();packaged_draw(&graphics,&texture,&target);packaged_two_colors(&target,blue,64,red,"partial packaged GPU copy keeps remaining CPU red");mtl_tex_release(&texture);mtl_tex_release(&partial_source);free(partial_source.mem);
    // An untracked write just after snapshot read must be detected on the next actual bind.
    // 快照读取后发生的未跟踪写入必须在下一次真实绑定时被检测出来。
    tex_t racing={.serial=870223,.has_layout=1,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=8,.height=8,.depth=1,.levels=1,.pkg_block_h=1}};nvn_tex_layout(&racing.d,&racing.L);racing.mem=calloc(1,racing.L.size);assert(racing.mem);tracked_vertices=NULL;packaged_rgba_fill(&racing,red);write_after_read=(uintptr_t)racing.mem;write_after_read_value=0xff00ff00;
    packaged_draw(&graphics,&racing,&target);assert(!write_after_read);packaged_pixels(&target,red,"untracked snapshot retains bytes actually read");packaged_draw(&graphics,&racing,&target);packaged_two_colors(&target,green,4,red,"later untracked first-texel update is not swallowed");mtl_tex_release(&racing);free(racing.mem);
    // Plain GPU render targets retain their real clear value despite dirty old guest storage.
    // 普通 GPU 渲染目标应保留真实清除值，即使旧访客存储被标为脏数据。
    tex_t ordinary={.serial=870205,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=8,.height=8,.depth=1,.levels=1}};nvn_tex_layout(&ordinary.d,&ordinary.L);ordinary.mem=calloc(1,ordinary.L.size);assert(ordinary.mem);tracked_vertices=ordinary.mem;packaged_rgba_fill(&ordinary,red);
    state.rt.color[0]=&ordinary;state.scissor=(p_scissor){0,0,8,8};p_clear_color clear={.mask=15,.rgba={0,0,1,1}};mtl_sub_begin();clear_color(&clear);mtl_sub_commit();dm_guest_write_prepare(ordinary.mem,ordinary.L.size);packaged_rgba_fill(&ordinary,green);packaged_draw(&graphics,&ordinary,&target);packaged_pixels(&target,blue,"ordinary GPU target is not replaced with old CPU green");assert(!graphics_probe_image(ordinary.serial)->source_valid);mtl_tex_release(&ordinary);free(ordinary.mem);
    // Real imageLoad + imageStore must observe both the CPU initial color and subsequent CPU updates.
    // 真实 imageLoad 与 imageStore 必须读取 CPU 初始颜色和后续 CPU 更新。
    tex_t storage={.serial=870206,.has_layout=1,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=8,.height=8,.depth=1,.levels=1,.pkg_block_h=1}};nvn_tex_layout(&storage.d,&storage.L);storage.mem=calloc(1,storage.L.size);assert(storage.mem);tracked_vertices=storage.mem;mtl_pool_add(storage.mem,storage.L.size);packaged_rgba_fill(&storage,blue);mtl_reg_tex(111,&storage,NULL);state.program=&compute;state.images[5][0]=111;p_dispatch dispatch={1,1,1,0};
    mtl_sub_begin();dispatch_compute(&dispatch);mtl_sub_commit();packaged_draw(&graphics,&storage,&target);const uint8_t blue_rmw[4]={10,0,255,255};packaged_pixels(&target,blue_rmw,"packaged storage imageLoad initial blue");
    dm_guest_write_prepare(storage.mem,storage.L.size);packaged_rgba_fill(&storage,green);state.program=&compute;mtl_sub_begin();dispatch_compute(&dispatch);mtl_sub_commit();packaged_draw(&graphics,&storage,&target);const uint8_t green_rmw[4]={10,255,0,255};packaged_pixels(&target,green_rmw,"storage imageLoad observes later CPU green");
    uint8_t *storage_new=calloc(1,storage.L.size);tex_t storage_alias=storage;storage_alias.mem=storage_new;packaged_rgba_fill(&storage_alias,blue);state.program=&compute;mtl_sub_begin();packaged_unknown_source(&storage,storage_new);uint64_t flushes=dependency_flushes;dispatch_compute(&dispatch);assert(dependency_flushes==flushes+1);mtl_sub_commit();packaged_draw(&graphics,&storage,&target);packaged_pixels(&target,blue_rmw,"unknown GPU source before packaged storage RMW");free(storage_new);
    mtl_tex_release(&storage);mtl_pool_del(storage.mem);free(storage.mem);mtl_tex_release(&target);tracked_vertices=NULL;mtl_pool_del(texture.mem);free(texture.mem);tracked_vertices=saved_tracking;protected_vertices=saved_protection;dirty_generation=nvn_dirty_now()>saved_generation?nvn_dirty_now():saved_generation;memset(&state,0,sizeof state);state.depth_range=(p_depth_range){0,1};
    puts("Vulkan packaged textures: generation0, tracked/untracked BC refresh, preserved GPU copy, repeated marks, CPU aliases, pending/unknown source, partial copy, untracked race, ordinary GPU target and storage RMW pixels PASS");
}
#endif
