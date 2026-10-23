// 纹理视图清除的真实字节真值：整数视图不能按底层 UNORM 的浮点值清除。
// True GPU bytes for texture-view clears: integer views must not clear through the base UNORM float interpretation.
#ifndef DM_ANDROID_TYPED_CLEAR_TEST_INCLUDED
#define DM_ANDROID_TYPED_CLEAR_TEST_INCLUDED
static void typed_clear_u16_pixels(tex_t *texture,unsigned mip,unsigned layer,uint16_t outside,uint16_t inside,int x0,int y0,int width,int height,const char *label) {
    unsigned w=dimension(texture->d.width,mip),h=dimension(texture->d.height,mip);uint16_t pixels[64];assert(w*h<=64);
    NVNcopyRegion region={0,0,0,w,h,1};mtl_read_texels(texture,mip,layer,&region,(uint8_t*)pixels,w*2,0);
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
        uint16_t expected=(int)x>=x0&&(int)x<x0+width&&(int)y>=y0&&(int)y<y0+height?inside:outside;
        if(pixels[y*w+x]!=expected){fprintf(stderr,"Typed clear %s mip%u layer%u pixel%u,%u raw=%04x expected=%04x\n",label,mip,layer,x,y,pixels[y*w+x],expected);assert(0);}
    }
}
static void typed_clear_submit(p_clear_tex *clear) {mtl_sub_begin();clear_texture(clear);mtl_sub_commit();}
static void typed_clear_raw_pixels(tex_t *texture,const void *expected,unsigned bytes,const char *label) {
    uint8_t pixels[1024];assert(texture->d.width*texture->d.height*bytes<=sizeof pixels);NVNcopyRegion region={0,0,0,texture->d.width,texture->d.height,1};mtl_read_texels(texture,0,0,&region,pixels,texture->d.width*bytes,0);
    for(unsigned i=0;i<texture->d.width*texture->d.height;i++)if(memcmp(pixels+i*bytes,expected,bytes)){fprintf(stderr,"Typed clear %s raw pixel%u differs\n",label,i);assert(0);}
}
static void typed_clear_reference_test(void) {
    mtl_drain();tex_t cube={.serial=880401,.d={.target=NVN_TEXTURE_TARGET_CUBEMAP,.format=NVN_FORMAT_R16,.width=8,.height=8,.depth=6,.levels=2}};nvn_tex_layout(&cube.d,&cube.L);
    p_clear_tex clear={.tex=&cube,.has_view=1,.is_uint=1,.view={.set_format=1,.format=NVN_FORMAT_R16UI},.r={0,0,0,8,8,6},.mask=15,.c.u={65535}};
    // 固定重现旧路径：同一 raw 65535 在 R16_UNORM clear 的 float 成员里约为 9e-41，六面都变成 0000。
    // Reproduce the old path: raw 65535 is about 9e-41 through the R16_UNORM clear's float member; all six faces become 0000.
    mtl_sub_begin();image_entry *image=image_get(&cube);VkClearColorValue old_value;memcpy(&old_value,&clear.c,sizeof old_value);
    VkImageSubresourceRange old_range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,6};image_access(image,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    vkCmdClearColorImage(command,image->image,VK_IMAGE_LAYOUT_GENERAL,&old_value,1,&old_range);image_gpu_written(image);mtl_sub_commit();
    for(unsigned layer=0;layer<6;layer++)typed_clear_u16_pixels(&cube,0,layer,0,0,0,0,8,8,"old ignored view produces zero");
    typed_clear_submit(&clear);for(unsigned layer=0;layer<6;layer++)typed_clear_u16_pixels(&cube,0,layer,65535,65535,0,0,8,8,"UINT view six faces FFFF");
    clear.view.set_levels=1;clear.view.base_level=1;clear.view.nlevels=1;clear.r=(NVNcopyRegion){0,0,0,4,4,6};clear.c.u[0]=0x1357;typed_clear_submit(&clear);
    clear.view.base_level=0;clear.r=(NVNcopyRegion){0,0,0,8,8,6};clear.c.u[0]=0;typed_clear_submit(&clear);
    for(unsigned layer=0;layer<6;layer++){typed_clear_u16_pixels(&cube,0,layer,0,0,0,0,8,8,"UINT zero");typed_clear_u16_pixels(&cube,1,layer,0x1357,0x1357,0,0,4,4,"other mip preserved");}
    clear.c.u[0]=0x8001;typed_clear_submit(&clear);for(unsigned layer=0;layer<6;layer++)typed_clear_u16_pixels(&cube,0,layer,0x8001,0x8001,0,0,8,8,"middle integer bits");
    // view.min_layer 和 region.zoffset 都有效，只覆盖 layer3 的矩形；另一 mip 和所有未覆盖像素/面保留。
    // Both view.min_layer and region.zoffset apply: cover only a rectangle in layer3 and preserve the other mip and all untouched pixels/faces.
    clear.view.set_layers=1;clear.view.min_layer=2;clear.view.nlayers=3;clear.r=(NVNcopyRegion){2,1,1,3,4,1};clear.c.u[0]=0x2468;typed_clear_submit(&clear);
    for(unsigned layer=0;layer<6;layer++){
        typed_clear_u16_pixels(&cube,0,layer,0x8001,layer==3?0x2468:0x8001,2,1,3,4,"partial view-layer rectangle");
        typed_clear_u16_pixels(&cube,1,layer,0x1357,0x1357,0,0,4,4,"partial clear preserves mip1");
    }
    clear.view.base_level=1;clear.view.min_layer=1;clear.view.nlayers=2;clear.r=(NVNcopyRegion){0,0,1,4,4,1};clear.c.u[0]=0x5678;typed_clear_submit(&clear);
    for(unsigned layer=0;layer<6;layer++)typed_clear_u16_pixels(&cube,1,layer,layer==2?0x5678:0x1357,layer==2?0x5678:0x1357,0,0,4,4,"nonzero mip and view-layer");
    // float16 视图清 1.0 应存 3c00，而不是底层 UNORM 清 1.0 存的 ffff；-0.5 应存 b800。
    // A float16 view clear of 1.0 stores 3c00 rather than the base UNORM 1.0's ffff; -0.5 stores b800.
    clear.is_uint=0;clear.view.format=NVN_FORMAT_R16F;clear.view.base_level=0;clear.view.min_layer=0;clear.view.nlayers=1;clear.r=(NVNcopyRegion){0,0,0,8,8,1};clear.c.f[0]=1;typed_clear_submit(&clear);
    typed_clear_u16_pixels(&cube,0,0,0x3c00,0x3c00,0,0,8,8,"float16 view one raw bits");
    clear.c.f[0]=-.5f;typed_clear_submit(&clear);typed_clear_u16_pixels(&cube,0,0,0xb800,0xb800,0,0,8,8,"float16 negative raw bits");
    clear.view.format=NVN_FORMAT_R16;clear.view.base_level=1;clear.view.min_layer=4;clear.view.nlayers=1;clear.r=(NVNcopyRegion){0,0,0,4,4,1};clear.c.f[0]=.25f;typed_clear_submit(&clear);
    for(unsigned layer=0;layer<6;layer++){uint16_t expected=layer==4?0x4000:layer==2?0x5678:0x1357;typed_clear_u16_pixels(&cube,1,layer,expected,expected,0,0,4,4,"native float clear preserves other view layers");}
    tex_t rgba={.serial=880402,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8_SRGB,.width=8,.height=8,.depth=1,.levels=1}};nvn_tex_layout(&rgba.d,&rgba.L);
    p_clear_tex rgba_clear={.tex=&rgba,.has_view=1,.is_uint=1,.view={.set_format=1,.format=NVN_FORMAT_RGBA8UI},.r={0,0,0,8,8,1},.mask=15,.c.u={17,83,201,255}};typed_clear_submit(&rgba_clear);
    uint8_t pixels[256];const uint8_t rgba_expected[4]={17,83,201,255};NVNcopyRegion rgba_region={0,0,0,8,8,1};mtl_read_texels(&rgba,0,0,&rgba_region,pixels,32,0);
    for(unsigned i=0;i<64;i++)assert(!memcmp(pixels+i*4,rgba_expected,4));
    // 32 位清除必须保留所有位；多通道 8 字节图样验证非 FillBuffer 分支，不只测两字节特例。
    // Preserve all 32-bit clear bits; an eight-byte multichannel pattern exercises the non-FillBuffer branch, beyond the two-byte case.
    tex_t r32={.serial=880403,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_R32F,.width=8,.height=8,.depth=1,.levels=1}};nvn_tex_layout(&r32.d,&r32.L);
    p_clear_tex r32_clear={.tex=&r32,.has_view=1,.is_uint=1,.view={.set_format=1,.format=NVN_FORMAT_R32UI},.r={0,0,0,8,8,1},.mask=15};
    const uint32_t words[]={0,0x3f800000,0x7f7fffff,0x12345678};for(unsigned i=0;i<4;i++){r32_clear.c.u[0]=words[i];typed_clear_submit(&r32_clear);typed_clear_raw_pixels(&r32,&words[i],4,"UINT view over float32");}
    tex_t rgba16={.serial=880404,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA16F,.width=8,.height=8,.depth=1,.levels=1}};nvn_tex_layout(&rgba16.d,&rgba16.L);
    const uint16_t rgba16_expected[]={0x3c00,0xbc00,0x3800,0x3c00};p_clear_tex rgba16_clear={.tex=&rgba16,.has_view=1,.is_uint=1,.view={.set_format=1,.format=NVN_FORMAT_RGBA16UI},.r={0,0,0,8,8,1},.mask=15,.c.u={0x3c00,0xbc00,0x3800,0x3c00}};typed_clear_submit(&rgba16_clear);typed_clear_raw_pixels(&rgba16,rgba16_expected,8,"UINT four-channel float16 backing");
    // 打包源初次部分清除先恢复所有 CPU 子资源；普通 GPU 目标则不能被旧 CPU 字节回灌。
    // A first partial clear restores all packaged CPU subresources; an ordinary GPU target must retain GPU contents over stale CPU bytes.
    tex_t packaged={.serial=880405,.has_layout=1,.d={.target=NVN_TEXTURE_TARGET_2D_ARRAY,.format=NVN_FORMAT_R16,.width=8,.height=8,.depth=2,.levels=2}};nvn_tex_layout(&packaged.d,&packaged.L);packaged.mem=calloc(1,packaged.L.size);assert(packaged.mem);mtl_pool_add(packaged.mem,packaged.L.size);
    const uint16_t initial[2][2]={{0x1111,0x2222},{0x3333,0x4444}};uint16_t linear[64];for(unsigned layer=0;layer<2;layer++)for(unsigned mip=0;mip<2;mip++){unsigned w=dimension(packaged.d.width,mip),h=dimension(packaged.d.height,mip);for(unsigned i=0;i<w*h;i++)linear[i]=initial[layer][mip];assert(!nvn_swizzle(&packaged.d,mip,layer,linear,w*2,packaged.mem));}
    p_clear_tex packaged_clear={.tex=&packaged,.has_view=1,.is_uint=1,.view={.set_format=1,.format=NVN_FORMAT_R16UI,.set_layers=1,.min_layer=1,.nlayers=1},.r={2,2,0,2,2,1},.mask=15,.c.u={0xaaaa}};typed_clear_submit(&packaged_clear);
    for(unsigned layer=0;layer<2;layer++)for(unsigned mip=0;mip<2;mip++)typed_clear_u16_pixels(&packaged,mip,layer,initial[layer][mip],layer==1&&mip==0?0xaaaa:initial[layer][mip],2,2,2,2,"packaged first clear preserves initial source");
    tex_t ordinary={.serial=880406,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_R16,.width=8,.height=8,.depth=1,.levels=1}};nvn_tex_layout(&ordinary.d,&ordinary.L);ordinary.mem=calloc(1,ordinary.L.size);assert(ordinary.mem);for(unsigned i=0;i<64;i++)linear[i]=0x5555;assert(!nvn_swizzle(&ordinary.d,0,0,linear,16,ordinary.mem));
    p_clear_tex ordinary_clear={.tex=&ordinary,.has_view=1,.is_uint=1,.view={.set_format=1,.format=NVN_FORMAT_R16UI},.r={0,0,0,8,8,1},.mask=15,.c.u={0x8888}};typed_clear_submit(&ordinary_clear);ordinary_clear.r=(NVNcopyRegion){1,2,0,2,3,1};ordinary_clear.c.u[0]=0xaaaa;typed_clear_submit(&ordinary_clear);typed_clear_u16_pixels(&ordinary,0,0,0x8888,0xaaaa,1,2,2,3,"ordinary GPU target ignores stale CPU source");
    mtl_tex_release(&cube);mtl_tex_release(&rgba);mtl_tex_release(&r32);mtl_tex_release(&rgba16);mtl_tex_release(&packaged);mtl_tex_release(&ordinary);mtl_pool_del(packaged.mem);free(packaged.mem);free(ordinary.mem);
    puts("Vulkan typed texture clear: old UINT/UNORM zero reproduced; six-face FFFF/zero/middle bits, mip/view-layer/partial rectangle preservation, float16 and float32 raw bits, UINT-over-sRGB bytes, wide patterns, packaged initial preservation and GPU authority PASS");
}
#endif
