// 游戏真实反序 OR 元数据必须取到材质贴图；正序、combined 和常量 sampler 同样以像素验证。
// Real reversed-OR game metadata must bind the material texture; normal, combined and immediate samplers also have pixel references.
#ifndef DM_ANDROID_HANDLE_DECODE_TEST_INCLUDED
#define DM_ANDROID_HANDLE_DECODE_TEST_INCLUDED
static void handle_decode_reference_test(void) {
    tex_t source={.serial=850011,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=8,.height=8,.depth=1,.levels=1}};
    tex_t target={.serial=850012,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};
    nvn_tex_layout(&source.d,&source.L);nvn_tex_layout(&target.d,&target.L);source.mem=calloc(1,source.L.size);assert(source.mem);
    const uint8_t blue[4]={10,80,220,255},white[4]={255,255,255,255};uint8_t pixels[16*16*4],linear[8*8*4];for(unsigned i=0;i<64;i++)memcpy(linear+i*4,blue,4);assert(!nvn_swizzle(&source.d,0,0,linear,8*4,source.mem));
    mtl_set_tex_pool_n(128);mtl_reg_tex(96,&source,NULL);samp_par repeat={.ws=NVN_WRAP_MODE_REPEAT,.wt=NVN_WRAP_MODE_CLAMP_TO_EDGE,.wr=NVN_WRAP_MODE_CLAMP_TO_EDGE,.lod_max=1000};
    samp_par border=repeat;border.ws=NVN_WRAP_MODE_CLAMP_TO_BORDER;for(unsigned i=0;i<4;i++)border.border[i]=1;mtl_reg_samp(96,&repeat);mtl_reg_samp(97,&border);
    uint8_t controls[2][0x880]={{1},{14}};prog_t program={.nstage=2,.sd={{.control=controls[0]},{.control=controls[1]}}};
    memset(&state,0,sizeof state);state.program=&program;state.rt.n=1;state.rt.color[0]=&target;state.mask.mask=15;state.depth_range=(p_depth_range){0,1};state.textures[1][10]=96;state.samplers[1][0]=(uint64_t)96<<20;
    shader_entry *fragment=program_shader(&program,1);shader_binding *binding=NULL;for(unsigned i=0;i<fragment->count;i++)if(fragment->bindings[i].kind==3)binding=&fragment->bindings[i];assert(binding&&binding->handle==0x101b815a&&binding->cb_slot==0x30002);
    shader_binding original=*binding;p_draw triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};NVNcopyRegion region={0,0,0,16,16,1};
    for(unsigned case_index=0;case_index<5;case_index++){
        *binding=original;state.textures[1][10]=96;state.samplers[1][0]=(uint64_t)96<<20;
        if(case_index==1)state.samplers[1][0]=(uint64_t)97<<20;
        else if(case_index==2)binding->handle=0x1056806e;
        else if(case_index==3){binding->handle=0x6e;binding->cb_slot=UINT32_MAX;state.textures[1][10]=96|((uint64_t)96<<20);}
        else if(case_index==4){binding->handle=(3u<<28)|(97u<<14)|0x6e;binding->cb_slot=2;}
        mtl_sub_begin();draw_graphics(&triangle,NULL);mtl_sub_commit();mtl_read_texels(&target,0,0,&region,pixels,16*4,0);
        const uint8_t *expected=case_index==1||case_index==4?white:blue;
        for(unsigned i=0;i<256;i++)if(memcmp(pixels+4*i,expected,4)){fprintf(stderr,"Handle case %u pixel %u observed %u,%u,%u,%u expected %u,%u,%u,%u\n",case_index,i,pixels[4*i],pixels[4*i+1],pixels[4*i+2],pixels[4*i+3],expected[0],expected[1],expected[2],expected[3]);assert(0);}
    }
    *binding=original;mtl_tex_release(&source);mtl_tex_release(&target);free(source.mem);memset(&state,0,sizeof state);state.depth_range=(p_depth_range){0,1};
    puts("Vulkan texture handles: real reversed OR, sampler selection, normal OR, combined and immediate sampler pixels PASS");
}
#endif
