// 独立 D16 数组/立方体阴影像素真值；覆盖层、LOD、gather 与比较方向，不修改正常渲染。
// Independent D16 array/cube shadow pixel truth for layers, LOD, gather and comparison direction; no normal-rendering changes.
#ifndef DM_ANDROID_SHADOW_ARRAY_TEST_INCLUDED
#define DM_ANDROID_SHADOW_ARRAY_TEST_INCLUDED
static void shadow_assert_pixels(const tex_t *target,const uint8_t expected[4],const char *label) {
    uint8_t pixels[16*16*4];NVNcopyRegion region={0,0,0,16,16,1};mtl_read_texels(target,0,0,&region,pixels,16*4,0);
    for(unsigned i=0;i<256;i++)if(memcmp(pixels+4*i,expected,4)){fprintf(stderr,"Shadow %s pixel%u observed=%u,%u,%u,%u expected=%u,%u,%u,%u\n",label,i,pixels[4*i],pixels[4*i+1],pixels[4*i+2],pixels[4*i+3],expected[0],expected[1],expected[2],expected[3]);assert(0);}
}
static void shadow_fill_level(tex_t *texture,unsigned level,unsigned layer,uint16_t depth,int pattern) {
    unsigned width=texture->d.width>>level,height=texture->d.height>>level;uint16_t linear[8*8];assert(width&&height&&width*height<=64);
    for(unsigned i=0;i<width*height;i++)linear[i]=depth;if(pattern)linear[3*width+3]=49151;
    assert(!nvn_swizzle(&texture->d,level,layer,linear,width*2,texture->mem));
}
static void shadow_array_reference_test(void) {
    mtl_drain();tex_t array={.serial=850011,.d={.target=NVN_TEXTURE_TARGET_2D_ARRAY,.format=NVN_FORMAT_DEPTH16,.width=8,.height=8,.depth=2,.levels=2}};
    tex_t cube={.serial=850012,.d={.target=NVN_TEXTURE_TARGET_CUBEMAP,.format=NVN_FORMAT_DEPTH16,.width=8,.height=8,.depth=6,.levels=2}};
    tex_t target={.serial=850013,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};
    tex_t patterned={.serial=850014,.d={.target=NVN_TEXTURE_TARGET_2D_ARRAY,.format=NVN_FORMAT_DEPTH16,.width=8,.height=8,.depth=2,.levels=1}};
    nvn_tex_layout(&array.d,&array.L);nvn_tex_layout(&cube.d,&cube.L);nvn_tex_layout(&target.d,&target.L);nvn_tex_layout(&patterned.d,&patterned.L);
    array.mem=calloc(1,array.L.size);cube.mem=calloc(1,cube.L.size);patterned.mem=calloc(1,patterned.L.size);assert(array.mem&&cube.mem&&patterned.mem);
    shadow_fill_level(&array,0,0,16384,0);shadow_fill_level(&array,0,1,49151,0);shadow_fill_level(&array,1,0,58982,0);shadow_fill_level(&array,1,1,6554,0);
    for(unsigned face=0;face<6;face++){shadow_fill_level(&cube,0,face,(uint16_t)((face<3?face+1:face+2)*8192-1),0);shadow_fill_level(&cube,1,face,face?6554:58982,0);}
    shadow_fill_level(&patterned,0,0,16384,1);shadow_fill_level(&patterned,0,1,49151,0);
    mtl_set_tex_pool_n(256);mtl_reg_tex(90,&array,NULL);mtl_reg_tex(91,&cube,NULL);mtl_reg_tex(92,&patterned,NULL);
    uint8_t controls[3][0x880]={{1},{15},{16}};prog_t array_program={.nstage=2,.sd={{.control=controls[0]},{.control=controls[1]}}},cube_program={.nstage=2,.sd={{.control=controls[0]},{.control=controls[2]}}};
    float params[8]={.5f,.5f,0,.5f,0,0,0,0};p_draw triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};p_clear_color clear={.mask=15};
    samp_par sampler={.minf=NVN_MIN_FILTER_NEAREST_MIPMAP_NEAREST,.magf=NVN_MAG_FILTER_NEAREST,.ws=NVN_WRAP_MODE_CLAMP_TO_EDGE,.wt=NVN_WRAP_MODE_CLAMP_TO_EDGE,.wr=NVN_WRAP_MODE_CLAMP_TO_EDGE,.cmp_mode=NVN_COMPARE_MODE_COMPARE_R_TO_TEXTURE,.cmp_func=NVN_COMPARE_FUNC_LEQUAL,.lod_max=1,.border={1,1,1,1}};
    memset(&state,0,sizeof state);state.program=&array_program;state.rt.n=1;state.rt.color[0]=&target;state.mask.mask=15;state.depth_range=(p_depth_range){0,1};state.scissor=(p_scissor){0,0,16,16};state.ubo[1][0]=(p_bind_buf){.addr=(uintptr_t)params,.size=sizeof params};state.textures[1][0]=((uint64_t)90<<20)|90;
    const uint8_t cases[8][4]={{0,0,0,255},{255,255,255,255},{255,0,0,255},{0,255,255,255},{255,255,255,255},{0,0,0,255},{0,0,0,255},{255,255,255,255}};
    for(unsigned i=0;i<8;i++){params[2]=i&1;params[4]=i==2||i==3?1:0;sampler.cmp_func=i==4||i==5?NVN_COMPARE_FUNC_GREATER:NVN_COMPARE_FUNC_LEQUAL;sampler.minf=i>=6?NVN_MIN_FILTER_LINEAR_MIPMAP_NEAREST:NVN_MIN_FILTER_NEAREST_MIPMAP_NEAREST;sampler.magf=i>=6?NVN_MAG_FILTER_LINEAR:NVN_MAG_FILTER_NEAREST;
        mtl_reg_samp(90,&sampler);mtl_sub_begin();clear_color(&clear);draw_graphics(&triangle,NULL);mtl_sub_commit();shadow_assert_pixels(&target,cases[i],"array layer/lod/filter/compare");}
    // 视图起层必须改变数组 layer0 的真实来源；gather 的四个比较值不能被复制成一个。
    // The view base layer changes array layer0's actual source; gather must preserve four distinct comparisons.
    view_t view={.set_layers=1,.min_layer=1,.nlayers=1};mtl_reg_tex(93,&array,&view);state.textures[1][0]=((uint64_t)90<<20)|93;params[2]=0;params[4]=0;mtl_sub_begin();draw_graphics(&triangle,NULL);mtl_sub_commit();shadow_assert_pixels(&target,cases[1],"array base-layer view");
    state.textures[1][0]=((uint64_t)90<<20)|92;sampler.minf=NVN_MIN_FILTER_NEAREST_MIPMAP_NEAREST;sampler.magf=NVN_MAG_FILTER_NEAREST;mtl_reg_samp(90,&sampler);mtl_sub_begin();draw_graphics(&triangle,NULL);mtl_sub_commit();const uint8_t mixed[4]={0,0,128,255};shadow_assert_pixels(&target,mixed,"array mixed gather");
    const float directions[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};state.program=&cube_program;state.textures[1][0]=((uint64_t)90<<20)|91;
    for(unsigned face=0;face<6;face++){memcpy(params,directions[face],12);params[3]=.5f;params[4]=0;mtl_sub_begin();draw_graphics(&triangle,NULL);mtl_sub_commit();shadow_assert_pixels(&target,face<3?cases[0]:cases[1],"cube face implicit/LOD/gather");}
    memcpy(params,directions[0],12);params[4]=1;mtl_sub_begin();draw_graphics(&triangle,NULL);mtl_sub_commit();const uint8_t cube_mip[4]={0,255,0,255};shadow_assert_pixels(&target,cube_mip,"cube explicit mip1");
    view=(view_t){.set_levels=1,.base_level=1,.nlevels=1};mtl_reg_tex(94,&cube,&view);state.textures[1][0]=((uint64_t)90<<20)|94;params[4]=0;mtl_sub_begin();draw_graphics(&triangle,NULL);mtl_sub_commit();shadow_assert_pixels(&target,cases[1],"cube base-mip view");
    mtl_tex_release(&array);mtl_tex_release(&cube);mtl_tex_release(&target);mtl_tex_release(&patterned);free(array.mem);free(cube.mem);free(patterned.mem);memset(&state,0,sizeof state);state.depth_range=(p_depth_range){0,1};
    puts("Vulkan D16 shadows: array layers and views, explicit mip1, nearest/linear compare, mixed four-sample gather, six cube faces and cube mip views PASS");
}
#endif
