// 两批真实绘制和 timeline gate：前批的 GPU 查询、event/counter 发布不等后批。
// Two real draws and timeline gates: earlier GPU queries and guest event/counter publication never wait for a later batch.
static uint64_t query_readback_test_serial=990000;
static void query_readback_test_state(const prog_t *program,const tex_t *target,const float rgba[4]) {
    memset(&state,0,sizeof state);state.program=program;state.rt.n=1;state.rt.color[0]=target;
    state.mask.mask=UINT32_MAX;state.polygon.mode=2;state.depth_range=(p_depth_range){0,1};state.scissor=(p_scissor){0,0,16,16};
    state.ubo[1][0]=(p_bind_buf){.addr=(uintptr_t)rgba,.size=4*sizeof(float)};
}
static void query_readback_test_result(const async_batch *batch,uint64_t expected) {
    // 发布已退役 metadata，持久 backing 仍归本 slot 所有。
    // Publication retires metadata while the persistent backing remains owned by this slot.
    assert(batch->query_count==0&&batch->status==ASYNC_FREE);
    uint64_t count;memcpy(&count,(const uint8_t*)batch->query_readback.mapped+ASYNC_TIMESTAMP_BYTES,sizeof count);assert(count==expected);
    if(batch->times){uint64_t times[2];memcpy(times,batch->query_readback.mapped,sizeof times);assert(times[0]!=0xa5a5a5a5a5a5a5a5ull&&times[1]!=0xa5a5a5a5a5a5a5a5ull);assert(timestamp_delta(times[0],times[1],gpu_timestamp_bits)>0);}
}
static void query_readback_reference_test(void) {
#ifdef DM_VK_REFERENCE_GATES
    mtl_drain();
    if(!reference_gates_supported()){puts("Vulkan GPU query readback: timeline case unavailable");return;}
    tracked_vertices=NULL;protected_vertices=NULL;__typeof__(state) saved_state=state;
    float red[4]={1,0,0,1},blue[4]={0,0,1,1};uint64_t reports[2][2]={{0}};volatile uint32_t events[2]={0};
    uint8_t controls[2][0x880]={{1},{4}};prog_t graphics={.nstage=2,.serial=++query_readback_test_serial,.sd={{.control=controls[0]},{.control=controls[1]}}};
    tex_t target={.serial=++query_readback_test_serial,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}};nvn_tex_layout(&target.d,&target.L);
    VkSemaphore gates[2]={reference_gate_create(),reference_gate_create()};reference_gate_watchdog watchdog[2];
    p_draw triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};p_clear_color clear={.mask=15,.rgba={0,0,0,1}};async_batch *batches[2];
    double gpu_before=gpu_total_ns;uint64_t immutable_before=immutable_buffer_bytes;
    for(unsigned i=0;i<2;i++){
        mtl_sub_begin();batches[i]=async_current;assert(batches[i]->query_readback.buffer&&batches[i]->query_readback.owner);
        for(unsigned old=0;old<i;old++)assert(batches[i]->query_readback.buffer!=batches[old]->query_readback.buffer);
        memset(batches[i]->query_readback.mapped,0xa5,ASYNC_QUERY_READBACK_BYTES);reference_gate_arm(gates[i],1);
        query_readback_test_state(&graphics,&target,i?blue:red);post_new(4);clear_color(&clear);if(i)state.scissor.w=8;
        draw_graphics(&triangle,NULL);mtl_sub_counter((uintptr_t)reports[i],1);mtl_sub_event(&events[i],0,17+i);
        assert(used_queries==1);uint64_t ticket=async_raw_commit();assert(ticket==batches[i]->ticket);
        if(!i)for(unsigned j=0;j<2;j++)reference_gate_watchdog_start(&watchdog[j],gates[j],1,5);
    }
    assert(immutable_buffer_bytes>=immutable_before&&!events[0]&&!events[1]&&!reports[0][0]&&!reports[1][0]);
    uint64_t first=batches[0]->ticket,second=batches[1]->ticket;assert(second==first+1);
    int fired=reference_gate_watchdog_stop(&watchdog[0]);reference_gate_signal(gates[0],1);assert(mtl_wait(first,5000000000ull));
    assert(events[0]==17&&reports[0][0]==256&&!events[1]&&!reports[1][0]&&!mtl_wait(second,0));
    pthread_mutex_lock(&queue_lock);query_readback_test_result(batches[0],256);assert(batches[1]->status==ASYNC_PENDING&&vkGetFenceStatus(device,batches[1]->fence)==VK_NOT_READY);assert(!batches[0]->times||gpu_total_ns>gpu_before);pthread_mutex_unlock(&queue_lock);
    fired|=reference_gate_watchdog_stop(&watchdog[1]);reference_gate_signal(gates[1],1);assert(mtl_wait(second,5000000000ull));assert(!fired);
    assert(events[1]==18&&reports[1][0]==128);pthread_mutex_lock(&queue_lock);query_readback_test_result(batches[1],128);pthread_mutex_unlock(&queue_lock);
    for(unsigned i=0;i<2;i++)reference_gate_close(gates[i],1);
    uint8_t pixels[16*16*4];NVNcopyRegion region={0,0,0,16,16,1};mtl_read_texels(&target,0,0,&region,pixels,16*4,0);
    const uint8_t black[4]={0,0,0,255},blue_pixel[4]={0,0,255,255};for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++)assert(!memcmp(pixels+(y*16+x)*4,x<8?blue_pixel:black,4));
    // 重复绕环，旧帧计数与初始化 poison 均不能污染下一次 counter。
    // Reuse the ring: neither an earlier count nor initialization poison may contaminate the next guest counter.
    uint64_t persistent=immutable_buffer_bytes-uniform_arena_bytes;VkBuffer backing[DM_VK_BATCH_SLOTS];
    for(unsigned i=0;i<DM_VK_BATCH_SLOTS;i++)backing[i]=async_batches[i].query_readback.buffer;
    for(unsigned i=0;i<DM_VK_BATCH_SLOTS+1;i++){
        uint64_t report[2]={0};mtl_sub_begin();async_batch *batch=async_current;memset(batch->query_readback.mapped,0xa5,ASYNC_QUERY_READBACK_BYTES);
        query_readback_test_state(&graphics,&target,red);post_new(4);state.scissor.w=1+i%7;draw_graphics(&triangle,NULL);mtl_sub_counter((uintptr_t)report,1);
        uint64_t expected=state.scissor.w*16,ticket=async_raw_commit();assert(mtl_wait(ticket,5000000000ull)&&report[0]==expected);
        pthread_mutex_lock(&queue_lock);query_readback_test_result(batch,expected);pthread_mutex_unlock(&queue_lock);assert(immutable_buffer_bytes-uniform_arena_bytes==persistent);
        for(unsigned slot=0;slot<DM_VK_BATCH_SLOTS;slot++)assert(async_batches[slot].query_readback.buffer==backing[slot]);
    }
    mtl_tex_release(&target);mtl_prog_release(&graphics);state=saved_state;
    puts("Vulkan GPU query readback: first timestamp/256-sample counter/event truly publish while later gated 128-sample draw remains pending; exact pixels, independent backing and ring reuse PASS");
#endif
}
