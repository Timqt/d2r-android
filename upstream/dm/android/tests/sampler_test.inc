// 标准边界色、槽号重登记及 shadow compare 均以 GPU 像素验证，旧绑定必须继续有效。
// Verify standard borders, slot re-registration and shadow comparison with GPU pixels, preserving earlier recorded bindings.
#ifndef DM_ANDROID_SAMPLER_TEST_INCLUDED
#define DM_ANDROID_SAMPLER_TEST_INCLUDED
static void sampler_assert_target(const tex_t *target,const uint8_t left[4],const uint8_t right[4]) {
    uint8_t pixels[16*16*4];NVNcopyRegion region={0,0,0,16,16,1};mtl_read_texels(target,0,0,&region,pixels,16*4,0);
    for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++){
        const uint8_t *expected=x<8?left:right,*actual=pixels+(y*16+x)*4;
        if(memcmp(actual,expected,4)){fprintf(stderr,"Sampler pixel %u,%u: observed %u,%u,%u,%u expected %u,%u,%u,%u\n",x,y,actual[0],actual[1],actual[2],actual[3],expected[0],expected[1],expected[2],expected[3]);assert(0);}
    }
}
static void sampler_reference_test(void) {
    tex_t source={.serial=830011,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=8,.height=8,.depth=1,.levels=1}};
    tex_t shadow={.serial=830012,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_DEPTH32F,.width=8,.height=8,.depth=1,.levels=1}};
    tex_t target={.serial=830013,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};
    nvn_tex_layout(&source.d,&source.L);nvn_tex_layout(&shadow.d,&shadow.L);nvn_tex_layout(&target.d,&target.L);
    source.mem=calloc(1,source.L.size);assert(source.mem);const uint8_t blue[4]={10,80,220,255},white[4]={255,255,255,255},black[4]={0,0,0,255},transparent[4]={0,0,0,0};
    uint8_t linear[8*8*4];for(unsigned i=0;i<64;i++)memcpy(linear+i*4,blue,4);assert(!nvn_swizzle(&source.d,0,0,linear,8*4,source.mem));
    uint8_t controls[3][0x880]={{1},{12},{13}};prog_t regular={.nstage=2,.sd={{.control=controls[0]},{.control=controls[1]}}},comparison={.nstage=2,.sd={{.control=controls[0]},{.control=controls[2]}}};
    mtl_set_tex_pool_n(128);mtl_reg_tex(64,&source,NULL);mtl_reg_tex(65,&shadow,NULL);
    memset(&state,0,sizeof state);state.program=&regular;state.rt.n=1;state.rt.color[0]=&target;state.mask.mask=15;state.polygon.mode=NVN_POLYGON_MODE_FILL;
    state.depth_range=(p_depth_range){0,1};state.scissor=(p_scissor){0,0,16,16};state.textures[1][0]=((uint64_t)64<<20)|64;
    samp_par parameters={.ws=NVN_WRAP_MODE_CLAMP_TO_BORDER,.wt=NVN_WRAP_MODE_CLAMP_TO_EDGE,.wr=NVN_WRAP_MODE_CLAMP_TO_EDGE,.lod_max=1000,.border={1,1,1,1}};
    p_draw triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};p_clear_color clear={.mask=15};
    // 依次更改同槽的边界 alpha/颜色/wrap；外侧 UV 的颜色必须随实际参数改变。
    // Re-register border alpha/color/wrap in the same slot; outside UV pixels must follow the actual parameters.
    for(unsigned i=0;i<5;i++){
        parameters.ws=i<3?NVN_WRAP_MODE_CLAMP_TO_BORDER:i==3?NVN_WRAP_MODE_REPEAT:NVN_WRAP_MODE_CLAMP_TO_EDGE;
        for(unsigned j=0;j<3;j++)parameters.border[j]=i==0?1:0;parameters.border[3]=i==2?0:1;
        mtl_reg_samp(64,&parameters);mtl_sub_begin();clear_color(&clear);draw_graphics(&triangle,NULL);mtl_sub_commit();
        const uint8_t *expected=i==0?white:i==1?black:i==2?transparent:blue;sampler_assert_target(&target,expected,expected);
    }
    uint64_t creations=sampler_creations;VkSampler shared=sampler_get(64);mtl_reg_samp(65,&parameters);assert(sampler_get(65)==shared&&sampler_creations==creations);
    state.textures[1][0]=((uint64_t)65<<20)|64;mtl_sub_begin();draw_graphics(&triangle,NULL);mtl_sub_commit();sampler_assert_target(&target,blue,blue);
    // 同批先记录白边，后重登记为黑边；旧描述符引用的 VkSampler 不得被销毁或改写。
    // Record white then re-register black in one batch; the earlier descriptor's VkSampler must remain alive and unchanged.
    state.textures[1][0]=((uint64_t)64<<20)|64;parameters.ws=NVN_WRAP_MODE_CLAMP_TO_BORDER;for(unsigned j=0;j<4;j++)parameters.border[j]=1;mtl_reg_samp(64,&parameters);
    VkSampler white_sampler=sampler_get(64);mtl_sub_begin();state.scissor=(p_scissor){0,0,8,16};draw_graphics(&triangle,NULL);
    for(unsigned j=0;j<3;j++)parameters.border[j]=0;mtl_reg_samp(64,&parameters);assert(sampler_get(64)!=white_sampler);state.scissor.x=8;draw_graphics(&triangle,NULL);mtl_sub_commit();
    sampler_assert_target(&target,white,black);for(unsigned j=0;j<4;j++)parameters.border[j]=1;mtl_reg_samp(64,&parameters);assert(sampler_get(64)==white_sampler);
    // 真实 D32 图像内部为 0.25；引用深度为 0.5，边界 1/0 和 LESS/GREATER 产生不同真值。
    // A real D32 image contains 0.25; reference 0.5, borders 1/0 and LESS/GREATER have distinct reference results.
    state.scissor=(p_scissor){0,0,16,16};state.rt.depth=&shadow;mtl_sub_begin();p_clear_ds depth_clear={.depth=.25f,.depth_mask=1};clear_depth(&depth_clear);mtl_sub_commit();state.rt.depth=NULL;
    state.program=&comparison;state.textures[1][0]=((uint64_t)64<<20)|65;parameters.cmp_mode=NVN_COMPARE_MODE_COMPARE_R_TO_TEXTURE;
    for(unsigned i=0;i<5;i++){
        parameters.ws=i<3?NVN_WRAP_MODE_CLAMP_TO_BORDER:NVN_WRAP_MODE_REPEAT;parameters.cmp_func=i==1||i==4?NVN_COMPARE_FUNC_GREATER:NVN_COMPARE_FUNC_LESS;
        for(unsigned j=0;j<3;j++)parameters.border[j]=i==2?0:1;parameters.border[3]=1;
        mtl_reg_samp(64,&parameters);mtl_sub_begin();clear_color(&clear);draw_graphics(&triangle,NULL);mtl_sub_commit();const uint8_t *expected=i==0||i==4?white:black;sampler_assert_target(&target,expected,expected);
    }
    mtl_tex_release(&source);mtl_tex_release(&shadow);mtl_tex_release(&target);free(source.mem);memset(&state,0,sizeof state);state.depth_range=(p_depth_range){0,1};
    puts("Vulkan samplers: standard border RGBA, wrap changes, shared objects, live slot re-registration and D32 shadow compare pixels PASS");
}
#endif
