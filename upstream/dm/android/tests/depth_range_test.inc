// 在真实 D32 深度附件上验证反向/常值范围，避免把合法 NVN 范围修正成 1。
// Verify reversed/constant ranges against a real D32 depth attachment instead of rewriting legal NVN ranges to 1.
#ifndef DM_ANDROID_DEPTH_RANGE_TEST_INCLUDED
#define DM_ANDROID_DEPTH_RANGE_TEST_INCLUDED
static void depth_range_reference_test(void) {
    tex_t color={.serial=820011,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};
    tex_t depth={.serial=820012,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_DEPTH32F,.width=16,.height=16,.depth=1,.levels=1}};
    nvn_tex_layout(&color.d,&color.L);nvn_tex_layout(&depth.d,&depth.L);
    uint8_t controls[2][0x880]={{11},{4}};prog_t program={.nstage=2,.sd={{.control=controls[0]},{.control=controls[1]}}};
    float red[4]={1,0,0,1};const uint8_t expected_red[4]={255,0,0,255},expected_clear[4]={0,0,0,0};uint8_t pixels[16*16*4];
    memset(&state,0,sizeof state);state.program=&program;state.rt.n=1;state.rt.color[0]=&color;state.rt.depth=&depth;state.mask.mask=15;
    state.polygon.mode=NVN_POLYGON_MODE_FILL;state.depth=(ds_st){.dt=1,.dw=1,.df=NVN_COMPARE_FUNC_LESS};
    state.ubo[1][0]=(p_bind_buf){.addr=(uintptr_t)red,.size=sizeof red};state.scissor=(p_scissor){0,0,16,16};
    p_draw triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};p_clear_color clear_color_value={.mask=15};p_clear_ds clear_depth_value={.depth=.75f,.depth_mask=1};
    NVNcopyRegion region={0,0,0,16,16,1};
    // VS 深度为 0.5；反向范围映射 0.5，常值范围也为 0.5，均通过 LESS 0.75。
    // Vertex Z is 0.5; reversed and constant ranges both map it to 0.5 and pass LESS against 0.75.
    const p_depth_range ranges[]={{1,0},{.5f,.5f},{0,1},{.875f,1}};
    for(unsigned case_index=0;case_index<sizeof ranges/sizeof *ranges;case_index++){
        state.depth_range=ranges[case_index];mtl_sub_begin();graphics_pass_end();post_new(4);
        clear_color(&clear_color_value);clear_depth(&clear_depth_value);draw_graphics(&triangle,NULL);mtl_sub_commit();
        mtl_read_texels(&color,0,0,&region,pixels,16*4,0);
        const uint8_t *expected=case_index==3?expected_clear:expected_red;
        for(unsigned i=0;i<256;i++)if(memcmp(pixels+i*4,expected,4)){
            fprintf(stderr,"Depth range (%g,%g) pixel %u: observed %u,%u,%u,%u expected %u,%u,%u,%u\n",ranges[case_index].n,ranges[case_index].f,i,pixels[i*4],pixels[i*4+1],pixels[i*4+2],pixels[i*4+3],expected[0],expected[1],expected[2],expected[3]);assert(0);
        }
        assert(samples_passed==(case_index==3?0:256));
    }
    mtl_tex_release(&color);mtl_tex_release(&depth);memset(&state,0,sizeof state);state.depth_range=(p_depth_range){0,1};
    puts("Vulkan depth range: reversed, constant and ordinary D32 LESS tests retain true pixels and counts PASS");
}
#endif
