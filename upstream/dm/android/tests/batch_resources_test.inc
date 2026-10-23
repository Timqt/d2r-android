// 在所有 GPU 批次完成后验证环资源隔离；飞行时不可改写由异步提交测试另行覆盖。
// Verify ring resource isolation after all GPU batches complete; async-submit tests separately cover ownership while in flight.
static void batch_resources_reference_test(void) {
#if DM_VK_BATCH_SLOTS >= 3
    mtl_drain();assert(!recording);uint64_t shared=immutable_buffer_bytes-uniform_arena_bytes;
    VkDescriptorBufferInfo info[3];uint8_t *mapped[3];VkDescriptorPool pools_seen[3];
    for(unsigned slot=0;slot<3;slot++){batch_resources_select(slot);info[slot]=uniform_allocate(16,&mapped[slot]);memset(mapped[slot],0x30+slot,16);
        if(!descriptor_chunks)descriptor_chunks=descriptor_current=descriptor_chunk_new();pools_seen[slot]=descriptor_chunks->pool;
        for(unsigned old=0;old<slot;old++){assert(info[slot].buffer!=info[old].buffer);assert(pools_seen[slot]!=pools_seen[old]);for(unsigned i=0;i<16;i++)assert(mapped[old][i]==0x30+old);}}
    batch_resources_select(0);uint8_t *zero;VkDescriptorBufferInfo reused=uniform_allocate(0,&zero);assert(reused.buffer==info[0].buffer&&reused.offset==info[0].offset);
    for(unsigned i=0;i<65536;i++)assert(!zero[i]);for(unsigned slot=1;slot<3;slot++)for(unsigned i=0;i<16;i++)assert(mapped[slot][i]==0x30+slot);
    uint64_t bytes=0;for(unsigned slot=0;slot<DM_VK_BATCH_SLOTS;slot++)bytes+=batch_resources_uniform_bytes(slot);assert(bytes==uniform_arena_bytes&&immutable_buffer_bytes-uniform_arena_bytes==shared);
    uint64_t released=batch_resources_uniform_bytes(1);batch_resources_release(1);assert(batch_resources_uniform_bytes(1)==0&&uniform_arena_bytes==bytes-released);assert(descriptor_chunks->pool==pools_seen[0]);
    batch_resources_select(1);uniform_allocate(0,&zero);for(unsigned i=0;i<65536;i++)assert(!zero[i]);assert(immutable_buffer_bytes-uniform_arena_bytes==shared);
    puts("Vulkan batch resources: three independent arenas/pools, preserved other-slot versions, selected-slot zero padding and balanced release PASS");
#endif
}
