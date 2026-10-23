// 使用合法的 timeline future host signal 门控 GPU，确定性检查在途快照、FIFO 发布和满环等待。
// Gate the real GPU on a legal future host signal of a timeline semaphore to verify in-flight snapshots, FIFO publication and a full ring.
typedef struct async_reference_job {
    volatile uint32_t *event;
    uint32_t *source,*destination;
    _Atomic int entered,acquired,finished;
    uint64_t ticket;
    int read_overlap;
} async_reference_job;

static void async_reference_gpu_write(uint32_t *destination,uint32_t value) {
    graphics_pass_end();post_op *write=post_new(0);write->destination=destination;write->size=4;write->output=buffer_create(4);post_register_range(write);
    memset(write->output.mapped,0,4);vkCmdFillBuffer(command,write->output.buffer,0,4,value);
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
}

static void *async_reference_thread(void *argument) {
    async_reference_job *job=argument;atomic_store(&job->entered,1);mtl_sub_begin();atomic_store(&job->acquired,1);
    if(job->read_overlap)mtl_sub_copy((uintptr_t)job->source,(uintptr_t)job->destination,4);
    else mtl_sub_event(job->event,0,444);
    job->ticket=async_raw_commit();assert(mtl_wait(job->ticket,2000000000));atomic_store(&job->finished,1);return NULL;
}

static void async_reference_test(void) {
#if DM_VK_BATCH_SLOTS >= 3 && defined(DM_VK_REFERENCE_GATES)
    mtl_drain();assert(!recording);tracked_vertices=NULL;protected_vertices=NULL;
    if(!reference_gates_supported()){puts("Vulkan async: timeline gate unsupported on this test device; gated reference skipped");return;}
    uint32_t guest[5]={0},first_source=17,last_source=29;float uniform[4]={0},red[4]={1,0,0,1},green[4]={0,1,0,1};
    volatile uint32_t event=0;uint64_t reports[3][2]={{0}};mtl_pool_add(guest,sizeof guest);mtl_pool_add(uniform,sizeof uniform);
    int pixels_available=getenv("DM_VK_SHADERS")!=NULL;
    tex_t targets[2]={{.serial=960001,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}},
                      {.serial=960002,.d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.width=16,.height=16,.depth=1,.levels=1}}};
    for(unsigned i=0;i<2;i++)nvn_tex_layout(&targets[i].d,&targets[i].L);
    uint8_t controls[2][0x880]={{1},{4}};prog_t solid={.nstage=2,.sd={{.control=controls[0]},{.control=controls[1]}}};
    p_draw triangle={.prim=NVN_DRAW_PRIMITIVE_TRIANGLES,.count=3};p_clear_color clear={.mask=15};
    memset(&state,0,sizeof state);state.program=&solid;state.rt.n=1;state.mask.mask=15;state.scissor=(p_scissor){0,0,16,16};state.ubo[1][0]=(p_bind_buf){.addr=(uintptr_t)uniform,.size=sizeof uniform};
    VkSemaphore gate=reference_gate_create();reference_gate_watchdog watchdog;reference_gate_watchdog_start(&watchdog,gate,1,5);uint64_t base=mtl_last_fence();

    mtl_sub_begin();async_batch *first_batch=async_current;VkCommandBuffer first_command=command;VkFence first_fence=gpu_fence;VkQueryPool first_queries=draw_queries;
    reference_gate_arm(gate,1);post_new(4);mtl_sub_copy((uintptr_t)&first_source,(uintptr_t)guest,4);
    if(pixels_available){state.rt.color[0]=&targets[0];mtl_sub_copy((uintptr_t)red,(uintptr_t)uniform,sizeof red);clear_color(&clear);draw_graphics(&triangle,NULL);}
    async_reference_gpu_write(guest+3,16);mtl_sub_counter((uintptr_t)reports[0],1);mtl_sub_event(&event,0,111);
    uint64_t first_ticket=async_raw_commit();assert(first_ticket==base+1&&!mtl_wait(first_ticket,0));
    assert(event==0&&guest[0]==0&&guest[3]==0&&reports[0][0]==0);

    mtl_sub_begin();async_batch *second_batch=async_current;
    assert(second_batch!=first_batch&&command!=first_command&&gpu_fence!=first_fence&&draw_queries!=first_queries);
    assert(async_have_unknown()&&dependencies_unknown((uintptr_t)(guest+3),4));
    uint32_t forwarded=0;assert(snapshot_pending((uintptr_t)guest,4));assert(!snapshot_read((uintptr_t)guest,&forwarded,4)&&forwarded==17);
    mtl_sub_copy((uintptr_t)guest,(uintptr_t)(guest+1),4);mtl_sub_copy((uintptr_t)&last_source,(uintptr_t)guest,4);
    assert(!snapshot_read((uintptr_t)guest,&forwarded,4)&&forwarded==29);
    assert(!snapshot_read((uintptr_t)(guest+1),&forwarded,4)&&forwarded==17);
    post_new(4);
    if(pixels_available){state.rt.color[0]=&targets[1];mtl_sub_copy((uintptr_t)green,(uintptr_t)uniform,sizeof green);clear_color(&clear);draw_graphics(&triangle,NULL);}
    mtl_sub_counter((uintptr_t)reports[1],1);mtl_sub_event(&event,0,222);
    uint64_t second_ticket=async_raw_commit();assert(second_ticket==base+2&&!mtl_wait(second_ticket,0));
    assert(event==0&&guest[0]==0&&guest[1]==0);

    mtl_sub_begin();assert(async_current!=first_batch&&async_current!=second_batch);
    assert(!snapshot_read((uintptr_t)guest,&forwarded,4)&&forwarded==29);
    assert(!snapshot_read((uintptr_t)(guest+1),&forwarded,4)&&forwarded==17);
    mtl_sub_copy((uintptr_t)(guest+1),(uintptr_t)(guest+2),4);post_new(4);mtl_sub_counter((uintptr_t)reports[2],1);mtl_sub_event(&event,0,333);
    uint64_t third_ticket=async_raw_commit();assert(third_ticket==base+3&&!mtl_wait(third_ticket,0));

    async_reference_job fourth={.event=&event};pthread_t thread;pthread_mutex_lock(&queue_lock);uint64_t stalls_before=async_ring_stalls;pthread_mutex_unlock(&queue_lock);
    assert(!pthread_create(&thread,NULL,async_reference_thread,&fourth));
    uint64_t deadline=dm_android_clock_ns(CLOCK_MONOTONIC)+1000000000;int stalled=0;
    do{pthread_mutex_lock(&queue_lock);stalled=async_ring_stalls>stalls_before;pthread_mutex_unlock(&queue_lock);if(stalled)break;usleep(1000);}while(dm_android_clock_ns(CLOCK_MONOTONIC)<deadline);
    assert(atomic_load(&fourth.entered)&&stalled&&!atomic_load(&fourth.acquired)&&!atomic_load(&fourth.finished));
    assert(!mtl_wait(first_ticket,0)&&event==0&&guest[0]==0&&guest[1]==0&&guest[2]==0&&guest[3]==0);
    reference_gate_signal(gate,1);assert(!pthread_join(thread,NULL));assert(atomic_load(&fourth.finished)&&fourth.ticket==base+4);
    assert(mtl_wait(third_ticket,0)&&mtl_wait(fourth.ticket,0));
    assert(event==444&&guest[0]==29&&guest[1]==17&&guest[2]==17&&guest[3]==16);
    assert(reports[0][0]==(pixels_available?256:0)&&reports[1][0]==(pixels_available?256:0)&&reports[2][0]==0);
    assert(!reference_gate_watchdog_stop(&watchdog));reference_gate_close(gate,1);

    if(pixels_available){
        uint8_t pixels[16*16*4];NVNcopyRegion region={0,0,0,16,16,1};const uint8_t expected[2][4]={{255,0,0,255},{0,255,0,255}};
        for(unsigned i=0;i<2;i++){mtl_read_texels(&targets[i],0,0,&region,pixels,64,0);for(unsigned p=0;p<256;p++)assert(!memcmp(pixels+p*4,expected[i],4));mtl_tex_release(&targets[i]);}
    }

    // 读取上一批未知 GPU 输出必须真等完成；不提前把 output.mapped 当作已发布数据。
    // Reading an earlier batch's unknown GPU output must wait for real completion, never treat output.mapped as already published.
    memset(&state,0,sizeof state);guest[3]=9;guest[4]=0;event=0;gate=reference_gate_create();reference_gate_watchdog_start(&watchdog,gate,1,5);base=mtl_last_fence();
    mtl_sub_begin();reference_gate_arm(gate,1);async_reference_gpu_write(guest+3,16);mtl_sub_event(&event,0,555);first_ticket=async_raw_commit();
    async_reference_job reader={.source=guest+3,.destination=guest+4,.read_overlap=1};assert(!pthread_create(&thread,NULL,async_reference_thread,&reader));
    deadline=dm_android_clock_ns(CLOCK_MONOTONIC)+1000000000;
    while(mtl_last_fence()<base+2&&dm_android_clock_ns(CLOCK_MONOTONIC)<deadline)usleep(1000);
    assert(atomic_load(&reader.entered)&&atomic_load(&reader.acquired)&&!atomic_load(&reader.finished));
    assert(mtl_last_fence()==base+2&&!mtl_wait(first_ticket,0)&&guest[3]==9&&guest[4]==0&&event==0);
    reference_gate_signal(gate,1);assert(!pthread_join(thread,NULL));assert(atomic_load(&reader.finished)&&guest[3]==16&&guest[4]==16&&event==555);
    assert(reader.ticket==base+3&&mtl_wait(reader.ticket,0));assert(!reference_gate_watchdog_stop(&watchdog));reference_gate_close(gate,1);
    mtl_pool_del(uniform);mtl_pool_del(guest);
    puts("Vulkan async: three real in-flight command/fence/query contexts, no early guest/event publication, FIFO copies/reports, cross-batch known-copy forwarding, full-ring reuse, isolated uniform pixels and unknown GPU-output dependency waits PASS");
#else
    puts("Vulkan async: gated reference skipped for configured smaller ring or non-reference build");
#endif
}
