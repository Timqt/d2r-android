// 用真实 Vulkan / 安全 reader 验证完整自有快照目标；可另链真实 HLE，不能用假的 heap helper 报命中。
// Verify full owned snapshot targets with real Vulkan/safe reads; link actual HLE separately, never fake heap-helper hits.
#define _GNU_SOURCE
#define DM_VK_REFERENCE_GATES 1
#include "../native/nvn_vulkan.c"
#include <assert.h>
#include <sys/mman.h>
#include <unistd.h>

static _Atomic uint64_t test_dirty_generation=1;
void dm_guest_write_prepare(void *address,size_t size) {(void)address;(void)size;atomic_fetch_add(&test_dirty_generation,1);}
void nvn_dirty_protect(const void *address,size_t size) {(void)address;(void)size;}
int nvn_dirty_tracked(const void *address,size_t size) {(void)address;(void)size;return 0;}
int nvn_dirty_since(const void *address,size_t size,uint64_t stamp) {(void)address;(void)size;(void)stamp;return 1;}
uint64_t nvn_dirty_now(void) {return atomic_load(&test_dirty_generation);}
int dm_fps_setting(void) {return 60;}
void dm_shot_written(const char *path,int ok) {(void)path;(void)ok;}
void *front_metal_layer(void) {return NULL;}
extern uint64_t svc_dispatch(int number,uint64_t *registers,uint64_t origin);
#pragma weak svc_dispatch

#ifdef DM_OWNED_SNAPSHOT_TEST_CLOCK_WRAP
static _Atomic int test_clock_mode;
static _Atomic uint64_t test_clock_calls;
extern uint64_t __real_dm_android_clock_ns(clockid_t clock);
uint64_t __wrap_dm_android_clock_ns(clockid_t clock) {
    atomic_fetch_add(&test_clock_calls,1);int mode=atomic_load(&test_clock_mode);uint64_t value=mode==2?0:__real_dm_android_clock_ns(clock);
    if(mode)errno=ENOTTY;return value;
}
#endif

enum {TEST_LARGE=65540,TEST_HEAP_SIZE=2<<20};
static void pattern(uint8_t *data,size_t size,unsigned seed) {
    for(size_t i=0;i<size;i++)data[i]=(uint8_t)(i*37u+(i>>7)+seed);
}
static dm_android_read_stats safe_stats(void) {dm_android_read_stats stats;dm_android_current_thread_read_stats(&stats);return stats;}
static void check_bytes(const void *got,const void *expected,size_t size) {assert(!memcmp(got,expected,size));}
static void check_fill(const void *bytes,size_t size,uint8_t expected) {const uint8_t *p=bytes;for(size_t i=0;i<size;i++)assert(p[i]==expected);}
static vk_buffer gpu_clone(vk_buffer input) {
    vk_buffer output=buffer_create(input.size);
    VkBufferCopy copy={.size=input.size};vkCmdCopyBuffer(command,input.buffer,output.buffer,1,&copy);
    VkBufferMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=output.buffer,.size=output.size};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&barrier,0,NULL);
    post_new(3)->source=input;return output;
}
static void finish_and_check(vk_buffer *output,const void *expected) {
    uint64_t ticket=mtl_sub_commit();assert(mtl_wait(ticket,5000000000ull));check_bytes(output->mapped,expected,output->size);buffer_destroy(output);
}
static void target_contract_test(const uint8_t *source) {
    mtl_sub_begin();vk_buffer target=buffer_create(TEST_LARGE);memset(target.mapped,0xa5,TEST_LARGE);
    dm_android_read_stats before=safe_stats();uint64_t calls=owned_snapshot_stats.calls,logical=snapshot_bytes;
    assert(snapshot_read_owned((uintptr_t)source,NULL,TEST_LARGE)==-1);
    vk_buffer invalid=target;invalid.owner=NULL;assert(snapshot_read_owned((uintptr_t)source,&invalid,TEST_LARGE)==-1);
    invalid=target;invalid.capacity=TEST_LARGE-1;assert(snapshot_read_owned((uintptr_t)source,&invalid,TEST_LARGE)==-1);
    invalid=target;invalid.size=TEST_LARGE-1;assert(snapshot_read_owned((uintptr_t)source,&invalid,TEST_LARGE)==-1);
    vk_buffer retained=buffer_borrow(target);assert(snapshot_read_owned((uintptr_t)source,&target,TEST_LARGE)==-1);buffer_destroy(&retained);
    assert(snapshot_read_owned(UINTPTR_MAX-3,&target,TEST_LARGE)==-1);
    assert(safe_stats().calls==before.calls&&owned_snapshot_stats.calls==calls&&snapshot_bytes==logical);check_fill(target.mapped,TEST_LARGE,0xa5);
    buffer_destroy(&target);uint64_t ticket=mtl_sub_commit();assert(mtl_wait(ticket,5000000000ull));
    puts("Owned snapshot: missing/shared owner, short size/capacity and overflow reject before any destination write or reader call PASS");
}
static void fallback_and_generic_test(const uint8_t *source) {
    mtl_sub_begin();uint64_t fast=owned_snapshot_stats.heap_calls,safe=owned_snapshot_stats.safe_calls;
    uint64_t absent=owned_snapshot_stats.absent_fallback,refused=owned_snapshot_stats.refused_fallback,logical=snapshot_bytes;
    dm_android_read_stats before=safe_stats();vk_buffer input=buffer_create(TEST_LARGE);
    assert(!snapshot_read_owned((uintptr_t)source,&input,TEST_LARGE));
    assert(owned_snapshot_stats.heap_calls==fast&&owned_snapshot_stats.safe_calls==safe+1&&safe_stats().calls==before.calls+1);
    assert(owned_snapshot_stats.absent_fallback==absent+!snapshot_heap_api_available());
    assert(owned_snapshot_stats.refused_fallback==refused+!!snapshot_heap_api_available()&&snapshot_bytes==logical+TEST_LARGE);
    vk_buffer output=gpu_clone(input);finish_and_check(&output,source);
    uint8_t *shadow=malloc(TEST_LARGE);assert(shadow);before=safe_stats();fast=owned_snapshot_stats.heap_calls;
    mtl_sub_begin();assert(!snapshot_read((uintptr_t)source,shadow,TEST_LARGE));
    assert(safe_stats().calls==before.calls+1&&owned_snapshot_stats.heap_calls==fast);check_bytes(shadow,source,TEST_LARGE);
    uint64_t ticket=mtl_sub_commit();assert(mtl_wait(ticket,5000000000ull));free(shadow);
    puts("Owned snapshot: actual safe-reader fallback and exact Vulkan transfer bytes; generic shadow reader stays on safe path PASS");
}
static void threshold_test(const uint8_t *source) {
    mtl_sub_begin();uint64_t small=owned_snapshot_stats.small_fallback,fast=owned_snapshot_stats.heap_calls;dm_android_read_stats before=safe_stats();
    vk_buffer input=buffer_create(65536);assert(!snapshot_read_owned((uintptr_t)source,&input,65536));
    assert(owned_snapshot_stats.small_fallback==small+1&&owned_snapshot_stats.heap_calls==fast&&safe_stats().calls==before.calls+1);
    vk_buffer output=gpu_clone(input);finish_and_check(&output,source);
    puts("Owned snapshot: exact 65536-byte threshold retains actual safe read and exact GPU bytes PASS");
}
static void known_and_partial_test(uint8_t *guest,const uint8_t *full_source,int expect_heap) {
    uint8_t *initial=malloc(TEST_LARGE),*expected=malloc(TEST_LARGE);assert(initial&&expected);pattern(initial,TEST_LARGE,19);memcpy(guest,initial,TEST_LARGE);mtl_pool_add(guest,TEST_LARGE);
    mtl_sub_begin();mtl_sub_copy((uintptr_t)full_source,(uintptr_t)guest,TEST_LARGE);
    dm_android_read_stats before=safe_stats();uint64_t known=owned_snapshot_stats.known_calls,fast=owned_snapshot_stats.heap_calls,logical=snapshot_bytes;
    vk_buffer input=buffer_create(TEST_LARGE);assert(!snapshot_read_owned((uintptr_t)guest,&input,TEST_LARGE));
    assert(owned_snapshot_stats.known_calls==known+1&&owned_snapshot_stats.heap_calls==fast&&safe_stats().calls==before.calls&&snapshot_bytes==logical+TEST_LARGE);
    check_bytes(guest,initial,TEST_LARGE);vk_buffer output=gpu_clone(input);finish_and_check(&output,full_source);check_bytes(guest,full_source,TEST_LARGE);
    puts("Owned snapshot: known-full-copy precedes heap/API/syscall, immutable source and guest publication order, exact GPU bytes PASS");

    uint8_t patch[4096];pattern(patch,sizeof patch,233);memcpy(guest,initial,TEST_LARGE);memcpy(expected,initial,TEST_LARGE);memcpy(expected+4096,patch,sizeof patch);
    mtl_sub_begin();mtl_sub_copy((uintptr_t)patch,(uintptr_t)(guest+4096),sizeof patch);before=safe_stats();known=owned_snapshot_stats.known_calls;fast=owned_snapshot_stats.heap_calls;logical=snapshot_bytes;
    input=buffer_create(TEST_LARGE);assert(!snapshot_read_owned((uintptr_t)guest,&input,TEST_LARGE));
    assert(owned_snapshot_stats.known_calls==known&&owned_snapshot_stats.heap_calls==fast+expect_heap&&safe_stats().calls==before.calls+!expect_heap&&snapshot_bytes==logical+TEST_LARGE);
    check_bytes(guest,initial,TEST_LARGE);output=gpu_clone(input);finish_and_check(&output,expected);check_bytes(guest+4096,patch,sizeof patch);
    puts("Owned snapshot: partial pending copy keeps actual baseline read and ordered overlay, exact Vulkan bytes outside/inside patch PASS");

    memcpy(guest,initial,TEST_LARGE);memcpy(expected,full_source,TEST_LARGE);memcpy(expected+4096,patch,sizeof patch);
    mtl_sub_begin();mtl_sub_copy((uintptr_t)full_source,(uintptr_t)guest,TEST_LARGE);mtl_sub_copy((uintptr_t)patch,(uintptr_t)(guest+4096),sizeof patch);
    before=safe_stats();known=owned_snapshot_stats.known_calls;fast=owned_snapshot_stats.heap_calls;input=buffer_create(TEST_LARGE);
    assert(!snapshot_read_owned((uintptr_t)guest,&input,TEST_LARGE));assert(owned_snapshot_stats.known_calls==known&&owned_snapshot_stats.heap_calls==fast+expect_heap&&safe_stats().calls==before.calls+!expect_heap);
    output=gpu_clone(input);finish_and_check(&output,expected);check_bytes(guest,expected,TEST_LARGE);
    puts("Owned snapshot: latest partial rejects older full shortcut; full then partial overlay preserves exact Vulkan output PASS");
    mtl_pool_del(guest);free(initial);free(expected);
}
static uint8_t *real_heap_start(void) {
    assert(snapshot_heap_api_available()&&svc_dispatch);uint64_t registers[32]={0};registers[1]=TEST_HEAP_SIZE;svc_dispatch(1,registers,0);
    assert(registers[0]==0&&registers[1]);return (uint8_t*)(uintptr_t)registers[1];
}
static void real_heap_test(uint8_t *heap) {
    uint8_t *expected=malloc(TEST_LARGE);assert(expected);pattern(expected,TEST_LARGE,71);memcpy(heap,expected,TEST_LARGE);
    mtl_sub_begin();dm_android_read_stats before=safe_stats();uint64_t fast=owned_snapshot_stats.heap_calls,bytes=owned_snapshot_stats.heap_bytes;
    vk_buffer input=buffer_create(TEST_LARGE);assert(!snapshot_read_owned((uintptr_t)heap,&input,TEST_LARGE));
    assert(owned_snapshot_stats.heap_calls==fast+1&&owned_snapshot_stats.heap_bytes==bytes+TEST_LARGE&&safe_stats().calls==before.calls);
    vk_buffer output=gpu_clone(input);finish_and_check(&output,expected);
    puts("Owned snapshot: actual HLE heap lease copies directly into real Vulkan mapping with no safe syscall; exact GPU bytes PASS");
#ifdef DM_OWNED_SNAPSHOT_TEST_CLOCK_WRAP
    mtl_sub_begin();input=buffer_create(TEST_LARGE);before=safe_stats();uint64_t unavailable=owned_snapshot_stats.heap_clock_unavailable,valid_wall=owned_snapshot_stats.heap_valid_wall_ns;
    atomic_store(&test_clock_mode,2);errno=EBUSY;assert(!snapshot_read_owned((uintptr_t)heap,&input,TEST_LARGE));assert(errno==EBUSY);atomic_store(&test_clock_mode,0);
    assert(owned_snapshot_stats.heap_clock_unavailable==unavailable+1&&owned_snapshot_stats.heap_valid_wall_ns==valid_wall&&safe_stats().calls==before.calls);
    output=gpu_clone(input);finish_and_check(&output,expected);
    puts("Owned snapshot: actual heap copy survives failed/clobbering diagnostic clocks, errno retained and wall marked unavailable PASS");
#endif
    threshold_test(heap);known_and_partial_test(heap,expected,1);
    uint64_t registers[32]={0};svc_dispatch(1,registers,0);assert(registers[0]==0);
    mtl_sub_begin();input=buffer_create(TEST_LARGE);memset(input.mapped,0xa5,TEST_LARGE);before=safe_stats();fast=owned_snapshot_stats.heap_calls;
    uint64_t errors=owned_snapshot_stats.safe_errors,logical=snapshot_bytes;assert(snapshot_read_owned((uintptr_t)heap,&input,TEST_LARGE)==-1);
    assert(owned_snapshot_stats.heap_calls==fast&&safe_stats().calls==before.calls+1&&owned_snapshot_stats.safe_errors==errors+1&&snapshot_bytes==logical);check_fill(input.mapped,TEST_LARGE,0xa5);
    buffer_destroy(&input);uint64_t ticket=mtl_sub_commit();assert(mtl_wait(ticket,5000000000ull));free(expected);
    puts("Owned snapshot: actual SetHeapSize shrink refuses lease, actual safe read fails without fake bytes/publication or logical accounting PASS");
}
static void clock_and_real_partial_test(void) {
#ifdef DM_OWNED_SNAPSHOT_TEST_CLOCK_WRAP
    for(int mode=1;mode<=2;mode++){atomic_store(&test_clock_mode,mode);errno=EBUSY;uint64_t value=snapshot_diagnostic_clock();assert(errno==EBUSY);assert(mode==2?!value:!!value);}
    atomic_store(&test_clock_mode,0);
    puts("Owned snapshot: actual diagnostic clock guard retains errno under clock clobber and failure PASS");
#endif
    size_t page=(size_t)getpagesize(),valid=(65536+page-1)&~(page-1),mapped=valid+page;
    uint8_t *source=mmap(NULL,mapped,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(source!=MAP_FAILED);
    pattern(source,valid,91);assert(!mprotect(source+valid,page,PROT_NONE));
    // 用真实 readv 部分返回作独立 errno / 字节期望，不假定内核失败是否改变 errno。
    // Use the actual partial readv result as the independent errno/byte reference, without assuming whether a kernel partial return changes errno.
    uint8_t *reference=malloc(TEST_LARGE);assert(reference);memset(reference,0xa5,TEST_LARGE);errno=EBUSY;
    assert(dm_android_read_memory((uintptr_t)source,reference,TEST_LARGE)==-1);int expected_errno=errno;check_bytes(reference,source,valid);check_fill(reference+valid,TEST_LARGE-valid,0xa5);
    for(int mode=1;mode<=2;mode++){
        mtl_sub_begin();vk_buffer target=buffer_create(TEST_LARGE);memset(target.mapped,0xa5,TEST_LARGE);dm_android_read_stats before=safe_stats();uint64_t logical=snapshot_bytes;
#ifdef DM_OWNED_SNAPSHOT_TEST_CLOCK_WRAP
        uint64_t clock_calls=atomic_load(&test_clock_calls);atomic_store(&test_clock_mode,mode);
#endif
        errno=EBUSY;assert(snapshot_read_owned((uintptr_t)source,&target,TEST_LARGE)==-1);assert(errno==expected_errno);
#ifdef DM_OWNED_SNAPSHOT_TEST_CLOCK_WRAP
        atomic_store(&test_clock_mode,0);
        assert(atomic_load(&test_clock_calls)==clock_calls+2*snapshot_heap_api_available());
#endif
        dm_android_read_stats after=safe_stats();assert(after.calls==before.calls+1&&after.partial==before.partial+1&&snapshot_bytes==logical);check_bytes(target.mapped,reference,TEST_LARGE);
        buffer_destroy(&target);uint64_t ticket=mtl_sub_commit();assert(mtl_wait(ticket,5000000000ull));
    }
    munmap(source,mapped);free(reference);
    printf("Owned snapshot: actual partial readv preserves original prefix/suffix/errno/accounting; diagnostic clocks inside heap-refusal path exercised=%u PASS\n",(unsigned)snapshot_heap_api_available());
}
int main(int argc,char **argv) {
    int expect_heap=argc==2&&!strcmp(argv[1],"--expect-real-heap");assert(argc==1||expect_heap);
    if(expect_heap&&(!snapshot_heap_api_available()||!svc_dispatch)){fputs("Actual Android HLE symbols required; weak/Host fallback is not fastpath validation\n",stderr);return 2;}
    if(!expect_heap)assert(!snapshot_heap_api_available());
    uint8_t *source=malloc(TEST_LARGE),*guest=malloc(TEST_LARGE);assert(source&&guest);pattern(source,TEST_LARGE,7);
    printf("Owned snapshot fixture: actual Vulkan + actual safe reader, heap_api=%u expect_real_heap=%d dirty_policy_fixture_only=1\n",(unsigned)snapshot_heap_api_available(),expect_heap);
    target_contract_test(source);fallback_and_generic_test(source);threshold_test(source);known_and_partial_test(guest,source,0);
    clock_and_real_partial_test();if(expect_heap)real_heap_test(real_heap_start());
    else assert(owned_snapshot_stats.heap_calls==0&&owned_snapshot_stats.absent_fallback>0);
    mtl_drain();free(source);free(guest);printf("Owned snapshot fixture: heap_fast_calls=%llu heap_fast_bytes=%llu safe_calls=%llu known_calls=%llu final=PASS\n",
        (unsigned long long)owned_snapshot_stats.heap_calls,(unsigned long long)owned_snapshot_stats.heap_bytes,(unsigned long long)owned_snapshot_stats.safe_calls,(unsigned long long)owned_snapshot_stats.known_calls);
    return 0;
}
