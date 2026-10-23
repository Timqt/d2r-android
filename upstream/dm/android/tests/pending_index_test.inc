// 真实 GPU 写入和原链 oracle 验证精确区间、动态 scope、未编码输出与发布后的退役。
// Real GPU writes and the original-chain oracle verify exact ranges, dynamic scopes, unencoded outputs and retirement after publication.
static post_op *pending_index_test_output(uint32_t *destination,uint64_t bytes,uint32_t value) {
    post_op *p=post_new(0);p->destination=destination;p->size=bytes;p->output=buffer_create(bytes);post_register_range(p);
    assert(async_unknown((uintptr_t)destination,bytes));
    vkCmdFillBuffer(command,p->output.buffer,0,bytes,value);forward_mark_gpu_recorded(p,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,1);return p;
}
static void pending_index_reference_test(void) {
    mtl_drain();tracked_vertices=NULL;protected_vertices=NULL;memset(&state,0,sizeof state);
    uint32_t guest[16]={0},known=31;uint64_t report[4]={0};mtl_pool_add(guest,sizeof guest);mtl_pool_add(report,sizeof report);
    mtl_sub_begin();mtl_sub_copy((uintptr_t)&known,(uintptr_t)(guest+4),sizeof known);
    assert(post_copy_first&&!async_have_unknown());
    assert(!async_current->pending_ranges.collections[DM_PENDING_UNKNOWN].count&&!async_current->pending_ranges.collections[DM_PENDING_HOST_EFFECT].count);
    post_op *const *scope=post_copy_tail;mtl_sub_event(guest,0,7);
    assert(!*scope&&forward_host_effects(async_current,(uintptr_t)guest,4,*scope));
    // 首个未来 copy 建立动态排除边界；它尚未 encoded 时也必须属于全批 unknown 集合。
    // The first future copy establishes the dynamic exclusion boundary; while unencoded it must already belong to the whole-batch unknown set.
    post_op *future=post_new(0);future->destination=guest+8;future->size=16;future->output=buffer_create(16);post_register_range(future);
    assert(*scope==future&&async_unknown((uintptr_t)(guest+8),16));forward_slice slice;
    assert(forward_find((uintptr_t)(guest+8),16,1,scope,&slice)==FORWARD_NONE);
    assert(forward_find((uintptr_t)(guest+8),16,1,NULL,&slice)==FORWARD_UNRECORDED);
    mtl_sub_event(guest+12,0,9);mtl_sub_counter((uintptr_t)report,0);
    assert(forward_host_effects(async_current,(uintptr_t)guest,4,*scope));
    assert(!forward_host_effects(async_current,(uintptr_t)(guest+12),4,*scope));
    assert(!forward_host_effects(async_current,(uintptr_t)report,16,*scope));
    assert(async_unknown((uintptr_t)(guest+12),4)&&async_unknown((uintptr_t)report,16));
    assert(!async_unknown((uintptr_t)(guest+5),12)&&!async_unknown((uintptr_t)(guest+13),12));
    assert(async_unknown((uintptr_t)report+15,1)&&!async_unknown((uintptr_t)report+16,1));
    vkCmdFillBuffer(command,future->output.buffer,0,16,42);forward_mark_gpu_recorded(future,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,1);
    forward_test_host_visibility();uint64_t ticket=async_raw_commit();assert(mtl_wait(ticket,5000000000ull));
    assert(guest[0]==7&&guest[4]==31&&guest[12]==9&&report[0]>0&&report[1]>0);
    for(unsigned i=8;i<12;i++)assert(guest[i]==42);
    mtl_sub_begin();assert(!async_have_unknown()&&!async_unknown((uintptr_t)guest,sizeof guest));
    // 两个真实输出间的空洞不能阻塞；半开区间边界亦不能多算一个字节。
    // A hole between two real outputs must not block, and half-open boundaries must not include one extra byte.
    pending_index_test_output(guest,8,17);pending_index_test_output(guest+4,8,29);
    assert(!async_unknown((uintptr_t)(guest+2),8));
    assert(async_unknown((uintptr_t)guest+7,2)&&async_unknown((uintptr_t)(guest+4)-1,2));
    assert(!async_unknown((uintptr_t)(guest+6),4));
    forward_test_host_visibility();ticket=async_raw_commit();assert(mtl_wait(ticket,5000000000ull));
    assert(guest[0]==17&&guest[1]==17&&guest[2]==0&&guest[3]==0&&guest[4]==29&&guest[5]==29);
    mtl_sub_begin();assert(!async_have_unknown());
    for(unsigned i=0;i<DM_VK_BATCH_SLOTS;i++)assert(!async_batches[i].pending_ranges.collections[0].count&&!async_batches[i].pending_ranges.collections[1].count&&!async_batches[i].pending_ranges.live_nodes);
    ticket=async_raw_commit();assert(mtl_wait(ticket,5000000000ull));mtl_pool_del(report);mtl_pool_del(guest);
    puts("Vulkan pending index: real GPU holes/boundaries, known-only publication chain, dynamic copy scope, intervening/later event and counter, unencoded output and fence-published slot retirement PASS");
}
