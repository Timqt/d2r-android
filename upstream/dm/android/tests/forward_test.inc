// 两个测试专用 timeline semaphore 固定 producer/consumer 时序，验证引用跨发布保持。
// Two test-only timeline semaphores control producer/consumer timing and verify references survive publication.
static void forward_test_host_visibility(void) {
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
}
static void forward_reference_test(void) {
    mtl_drain();uint32_t guest[8]={0};mtl_pool_add(guest,sizeof guest);uint32_t patch=7;
    mtl_sub_begin();forward_reset();post_op *const *first_scope=post_copy_tail;post_op *producer=post_new(0);
    producer->destination=guest;producer->size=sizeof guest;producer->output=buffer_create(sizeof guest);post_register_range(producer);memset(producer->output.mapped,0,sizeof guest);forward_slice slice;
    assert(forward_find((uintptr_t)guest,sizeof guest,1,first_scope,&slice)==FORWARD_NONE);
    assert(forward_find((uintptr_t)guest,sizeof guest,1,NULL,&slice)==FORWARD_UNRECORDED);
    vkCmdFillBuffer(command,producer->output.buffer,0,sizeof guest,0xcafebabe);forward_mark_gpu_recorded(producer,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,1);
    assert(forward_find((uintptr_t)(guest+1),8,4,NULL,&slice)==FORWARD_FULL&&slice.offset==4&&slice.size==8);
    assert(forward_find((uintptr_t)(guest+1),8,16,NULL,&slice)==FORWARD_ALIGNMENT);
    post_op *const *consumer_scope=post_copy_tail;post_op *consumer=post_new(0);consumer->destination=guest;consumer->size=sizeof guest;consumer->output=buffer_create(sizeof guest);post_register_range(consumer);memset(consumer->output.mapped,0,sizeof guest);
    assert(forward_find((uintptr_t)guest,sizeof guest,1,NULL,&slice)==FORWARD_UNRECORDED);
    assert(forward_find((uintptr_t)guest,sizeof guest,1,consumer_scope,&slice)==FORWARD_FULL&&slice.producer==producer);
    uint64_t retained=forward_references;forward_hold(&slice);forward_hold(&slice);assert(forward_references==retained+1&&atomic_load(&slice.backing.owner->references)==2);
    forward_read_barrier(&slice,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);uint64_t barriers=forward_barriers;forward_read_barrier(&slice,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);assert(forward_barriers==barriers);
    VkBufferCopy copy={.srcOffset=slice.offset,.size=sizeof guest};vkCmdCopyBuffer(command,slice.buffer,consumer->output.buffer,1,&copy);forward_mark_gpu_recorded(consumer,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,1);
    mtl_sub_copy((uintptr_t)&patch,(uintptr_t)(guest+1),sizeof patch);assert(forward_find((uintptr_t)guest,sizeof guest,1,NULL,&slice)==FORWARD_PARTIAL);
    assert(forward_find((uintptr_t)(guest+1),sizeof patch,1,NULL,&slice)==FORWARD_FULL&&slice.host_written);
    mtl_sub_event((volatile uint32_t*)guest,0,42);assert(forward_find((uintptr_t)guest,sizeof guest,1,NULL,&slice)==FORWARD_HOST_EFFECT);
    forward_test_host_visibility();uint64_t ticket=mtl_sub_commit();assert(mtl_wait(ticket,5000000000ull));assert(guest[0]==42&&guest[1]==7);for(unsigned i=2;i<8;i++)assert(guest[i]==0xcafebabe);mtl_pool_del(guest);
    // semaphore 等待位于提交边界，不使用可能阻塞 host submit 的 VkEvent 等待。
    // Semaphore waits sit at submission boundaries rather than VkEvent waits that may block host submission.
#ifdef DM_VK_REFERENCE_GATES
    if(!reference_gates_supported()){puts("Vulkan forwarding: timeline backing lifetime check unavailable on this device");return;}
    uint32_t result[4]={0},destination[4]={0};mtl_pool_add(result,sizeof result);mtl_pool_add(destination,sizeof destination);
    VkSemaphore gates[2]={reference_gate_create(),reference_gate_create()};reference_gate_watchdog watchdog[2];
    mtl_sub_begin();forward_reset();reference_gate_arm(gates[0],1);
    producer=post_new(0);producer->destination=result;producer->size=sizeof result;producer->output=buffer_create(sizeof result);post_register_range(producer);memset(producer->output.mapped,0,sizeof result);
    vkCmdFillBuffer(command,producer->output.buffer,0,sizeof result,0x12345678);forward_mark_gpu_recorded(producer,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,1);forward_test_host_visibility();uint64_t produced=async_raw_commit();
    for(unsigned i=0;i<2;i++)reference_gate_watchdog_start(&watchdog[i],gates[i],1,5);
    mtl_sub_begin();forward_reset();post_op *const *scope=post_copy_tail;assert(forward_find((uintptr_t)result,sizeof result,1,scope,&slice)==FORWARD_FULL&&slice.producer==producer);
    forward_hold(&slice);assert(atomic_load(&slice.backing.owner->references)==2);reference_gate_arm(gates[1],1);forward_read_barrier(&slice,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    consumer=post_new(0);consumer->destination=destination;consumer->size=sizeof destination;consumer->output=buffer_create(sizeof destination);post_register_range(consumer);memset(consumer->output.mapped,0,sizeof destination);
    copy=(VkBufferCopy){.srcOffset=slice.offset,.size=sizeof destination};vkCmdCopyBuffer(command,slice.buffer,consumer->output.buffer,1,&copy);forward_mark_gpu_recorded(consumer,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,1);forward_test_host_visibility();uint64_t consumed=async_raw_commit();
    int fired=reference_gate_watchdog_stop(&watchdog[0]);reference_gate_signal(gates[0],1);assert(mtl_wait(produced,5000000000ull));for(unsigned i=0;i<4;i++)assert(result[i]==0x12345678&&destination[i]==0);
    assert(!mtl_wait(consumed,0));assert(atomic_load(&slice.backing.owner->references)==1);
    pthread_mutex_lock(&buffer_cache_lock);for(cached_buffer *entry=buffer_cache;entry;entry=entry->next)assert(entry->buffer.buffer!=slice.buffer);pthread_mutex_unlock(&buffer_cache_lock);
    fired|=reference_gate_watchdog_stop(&watchdog[1]);reference_gate_signal(gates[1],1);assert(mtl_wait(consumed,5000000000ull));for(unsigned i=0;i<4;i++)assert(destination[i]==0x12345678);
    for(unsigned i=0;i<2;i++)reference_gate_close(gates[i],1);assert(!fired);mtl_pool_del(result);mtl_pool_del(destination);
    puts("Vulkan forwarding: future outputs excluded, exact offsets/alignment, partial/event fallback, ordered GPU copies, retain dedup and producer-published/consumer-pending backing lifetime PASS");
#endif
}
