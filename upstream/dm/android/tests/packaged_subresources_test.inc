// 打包数组纹理的真实像素验证：部分 GPU 复制保留其他 mip/层，后来 CPU 源版本按原 Metal 整体刷新。
// Real packaged-array pixels: partial GPU copies preserve other mips/layers; a later CPU source version refreshes the whole image like Metal.
#ifndef DM_ANDROID_PACKAGED_SUBRESOURCES_TEST_INCLUDED
#define DM_ANDROID_PACKAGED_SUBRESOURCES_TEST_INCLUDED
static void pkg_sub_fill(tex_t *texture,unsigned mip,unsigned layer,uint16_t color) {
    unsigned width=dimension(texture->d.width,mip),height=dimension(texture->d.height,mip),columns=(width+3)/4,rows=(height+3)/4;
    uint8_t blocks[64]={0};assert(columns*rows*16<=sizeof blocks);
    for(unsigned i=0;i<columns*rows;i++){blocks[i*16]=blocks[i*16+1]=255;memcpy(blocks+i*16+8,&color,2);memcpy(blocks+i*16+10,&color,2);}
    assert(!nvn_swizzle(&texture->d,mip,layer,blocks,columns*16,texture->mem));
}
static void pkg_sub_draw(prog_t *program,tex_t *texture,tex_t *target,unsigned mip,unsigned layer) {
    view_t view={.set_levels=1,.base_level=mip,.nlevels=1,.set_layers=1,.min_layer=layer,.nlayers=1};mtl_reg_tex(112,texture,&view);
    state.program=program;state.rt.n=1;state.rt.color[0]=target;state.rt.depth=NULL;state.textures[1][0]=((uint64_t)112<<20)|112;
    state.scissor=(p_scissor){0,0,16,16};p_draw draw={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};mtl_sub_begin();draw_graphics(&draw,NULL);mtl_sub_commit();
}
static void pkg_sub_pixels(tex_t *target,const uint8_t first[4],unsigned count,const uint8_t second[4],const char *label) {
    uint8_t pixels[16*16*4];NVNcopyRegion region={0,0,0,16,16,1};mtl_read_texels(target,0,0,&region,pixels,64,0);unsigned observed=0;
    for(unsigned i=0;i<256;i++){if(!memcmp(pixels+i*4,first,4))observed++;else if(!second||memcmp(pixels+i*4,second,4)){fprintf(stderr,"Packaged subresources %s pixel%u=%u,%u,%u,%u\n",label,i,pixels[i*4],pixels[i*4+1],pixels[i*4+2],pixels[i*4+3]);assert(0);}}
    if(observed!=count){fprintf(stderr,"Packaged subresources %s observed=%u expected=%u\n",label,observed,count);assert(0);}
}
static void packaged_subresources_reference_test(void) {
    mtl_drain();uint64_t saved_generation=dirty_generation;const void *saved_tracking=tracked_vertices,*saved_protection=protected_vertices;
    tex_t texture={.serial=870301,.has_layout=1,.d={.target=NVN_TEXTURE_TARGET_2D_ARRAY,.format=NVN_FORMAT_RGBA_DXT5_SRGB,.width=8,.height=8,.depth=2,.levels=2,.pkg_block_h=1}};
    tex_t target={.serial=870302,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};
    nvn_tex_layout(&texture.d,&texture.L);nvn_tex_layout(&target.d,&target.L);texture.mem=calloc(1,texture.L.size);assert(texture.mem&&texture.L.layers==2);mtl_pool_add(texture.mem,texture.L.size);mtl_set_tex_pool_n(256);
    const uint8_t red[4]={255,0,0,255},green[4]={0,255,0,255},yellow[4]={255,255,0,255},cyan[4]={0,255,255,255},blue[4]={0,0,255,255},magenta[4]={255,0,255,255};
    uint8_t controls[2][0x880]={{1},{2}};prog_t graphics={.nstage=2,.sd={{.control=controls[0]},{.control=controls[1]}}};
    samp_par sampler={.minf=NVN_MIN_FILTER_NEAREST,.magf=NVN_MAG_FILTER_NEAREST,.ws=NVN_WRAP_MODE_CLAMP_TO_EDGE,.wt=NVN_WRAP_MODE_CLAMP_TO_EDGE,.wr=NVN_WRAP_MODE_CLAMP_TO_EDGE,.lod_max=0};mtl_reg_samp(112,&sampler);
    memset(&state,0,sizeof state);state.mask.mask=15;state.depth_range=(p_depth_range){0,1};
    // 跟踪替身只模拟已知源范围和写代数；每个预期颜色都通过 GPU 采样及读回核对，不镜像上传实现。
    // The tracker stub models a known source range and write generations; every expected color is verified by GPU sampling/readback, without mirroring uploads.
    tracked_vertices=texture.mem;dirty_generation=0;pkg_sub_fill(&texture,0,0,0xf800);pkg_sub_fill(&texture,1,0,0x07e0);pkg_sub_fill(&texture,0,1,0xffe0);pkg_sub_fill(&texture,1,1,0x07ff);
    const uint8_t *initial[2][2]={{red,green},{yellow,cyan}};
    for(unsigned layer=0;layer<2;layer++)for(unsigned mip=0;mip<2;mip++){pkg_sub_draw(&graphics,&texture,&target,mip,layer);pkg_sub_pixels(&target,initial[layer][mip],256,NULL,"initial layer/mip view");}
    // 只复制 layer0/mip0 的一个 BC 块，必须保留同级其余块、另一 mip 和另一层的真值。
    // Copy just one BC block of layer0/mip0; retain the other blocks, the other mip and the other layer's actual values.
    uint8_t block[16]={255,255};uint16_t blue565=31;memcpy(block+8,&blue565,2);memcpy(block+10,&blue565,2);
    p_copy_b2t copy={.src=(uintptr_t)block,.tex=&texture,.has_view=1,.view={.set_levels=1,.base_level=0,.nlevels=1,.set_layers=1,.min_layer=0,.nlayers=1},.r={0,0,0,4,4,1}};
    mtl_sub_begin();copy_buffer_texture(&copy);mtl_sub_commit();uint64_t uploads=texture_uploads;
    pkg_sub_draw(&graphics,&texture,&target,0,0);pkg_sub_pixels(&target,blue,64,red,"quarter copy preserves remaining mip0 red");
    for(unsigned layer=0;layer<2;layer++)for(unsigned mip=0;mip<2;mip++)if(layer||mip){pkg_sub_draw(&graphics,&texture,&target,mip,layer);pkg_sub_pixels(&target,initial[layer][mip],256,NULL,"GPU copy preserves other layer/mip");}
    assert(texture_uploads==uploads);
    // 后来 CPU 只改 layer1/mip1，原 Metal 契约仍上传整份打包源；旧 GPU 蓝块应恢复 CPU 红。
    // A later CPU write changes only layer1/mip1; Metal's whole-packaged-source upload also restores the old GPU blue block to CPU red.
    dm_guest_write_prepare(texture.mem,texture.L.size);pkg_sub_fill(&texture,1,1,0xf81f);
    const uint8_t *updated[2][2]={{red,green},{yellow,magenta}};
    for(unsigned layer=0;layer<2;layer++)for(unsigned mip=0;mip<2;mip++){pkg_sub_draw(&graphics,&texture,&target,mip,layer);pkg_sub_pixels(&target,updated[layer][mip],256,NULL,"new CPU source version refreshes all layer/mip views");}
    assert(texture_uploads==uploads+1);
    mtl_tex_release(&texture);mtl_tex_release(&target);tracked_vertices=NULL;mtl_pool_del(texture.mem);free(texture.mem);tracked_vertices=saved_tracking;protected_vertices=saved_protection;dirty_generation=nvn_dirty_now()>saved_generation?nvn_dirty_now():saved_generation;memset(&state,0,sizeof state);state.depth_range=(p_depth_range){0,1};
    puts("Vulkan packaged subresources: two layers/two mips, true view colors, one-block GPU copy with untouched red/green/yellow/cyan preservation, later CPU mip change and whole-source refresh PASS");
}
#endif
