// 经 dlopen 加载的 TLS 模块：覆盖新线程首次解析与热路径，完整对拍寄存器并验证线程隔离。
// A dlopen-loaded TLS module exercises first resolution and warm paths, all registers, and thread isolation.
#include "runtime.h"
#include "runtime_context_cache.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t x[31], sp;
    unsigned char q[32][16];
    uint64_t nzcv, fpsr, fpcr, padding;
} snapshot;
_Static_assert(sizeof(snapshot) == 800, "assembly snapshot size");
_Static_assert(offsetof(dm_android_context,tls)==DM_NSLOTS*sizeof(uint64_t),"guest TLS offset");
_Static_assert(offsetof(dm_android_context,tlr)==DM_NSLOTS*sizeof(uint64_t)+0x10000,"guest TLR offset");
extern void dm_android_context_register_probe(snapshot output[2]);
uint64_t gate_dispatch(uint64_t ret, uint64_t *regs) { (void)ret; (void)regs; abort(); }
uint64_t unimpl_c(unsigned index, uint64_t *regs) { (void)index; (void)regs; abort(); }
void dm_thread_returned(void) { abort(); }

static pthread_barrier_t barrier;
typedef struct { unsigned id; dm_android_context *context; uintptr_t tp; uint64_t value; } thread_case;
static _Atomic int fail_key_create,fail_set_specific;
static _Atomic unsigned key_create_calls,set_specific_calls;
static _Atomic unsigned late_destructor_calls;
static pthread_key_t late_context_key;
static void (*cache_destructor)(void *);
int dm_android_context_cache_key_create(pthread_key_t *key,void (*destructor)(void *)) {
    cache_destructor=destructor;
    atomic_fetch_add(&key_create_calls,1);return atomic_load(&fail_key_create)?EAGAIN:pthread_key_create(key,destructor);
}
int dm_android_context_cache_set_specific(pthread_key_t key,const void *value) {
    atomic_fetch_add(&set_specific_calls,1);return atomic_load(&fail_set_specific)?EAGAIN:pthread_setspecific(key,value);
}

static dm_android_context *check_registers(void) {
    snapshot state[2] = {0};
    uintptr_t tp = (uintptr_t)__builtin_thread_pointer();
    dm_android_context_register_probe(state);
    for (unsigned i = 0; i < 31; ++i) if (i != 16) {
        if (state[0].x[i] != state[1].x[i]) fprintf(stderr, "context entry corrupted x%u\n", i);
        assert(state[0].x[i] == state[1].x[i]);
    }
    assert(!memcmp(state[0].q, state[1].q, sizeof state[0].q));
    assert(state[0].sp == state[1].sp && state[0].nzcv == state[1].nzcv);
    assert(state[0].fpsr == state[1].fpsr && state[0].fpcr == state[1].fpcr);
    assert(tp == (uintptr_t)__builtin_thread_pointer());
    assert(state[1].x[16] == (uintptr_t)dm_android_context_get());
    return (void *)(uintptr_t)state[1].x[16];
}

static void late_context_destroy(void *value) {
    dm_android_context *context=value;uintptr_t tp=(uintptr_t)__builtin_thread_pointer();
    // 真实 public TSD 清理中模拟其他析构晚于 cache 析构，次序不依赖 libc 的键遍历顺序。
    // Model a later destructor during real public TSD cleanup, independently of libc's key iteration order.
    assert(cache_destructor);cache_destructor(context);assert(context->cache_terminating);
    assert(!dm_android_context_cache_lookup(tp));
    assert(dm_android_context_get()==context&&context->slots[DM_K18]==0xc001d00d);
    assert(check_registers()==context&&!dm_android_context_cache_lookup(tp));
    assert(dm_android_thread_init(NULL)==context&&context->cache_terminating);
    assert(dm_android_context_get()==context&&!dm_android_context_cache_lookup(tp));
    atomic_fetch_add(&late_destructor_calls,1);
}

static void *run_thread(void *arg) {
    thread_case *test = arg;
    test->tp=(uintptr_t)__builtin_thread_pointer();
    // 此线程尚未通过 C 访问 context；第一次汇编调用须自己处理延迟分配。
    // This thread has not accessed context from C; its first assembly call handles lazy allocation.
    test->context = check_registers();
    assert(test->context->slots[DM_K18] == 0);
    dm_android_context *context = dm_android_thread_init(NULL);
    assert(context == test->context);
    context->slots[DM_K18] = 0x12340000u + test->id;
    context->slots[DM_KTLS] = 0xabcd0000u + test->id;
    context->slots[DM_KTLR] = 0xef000000u + test->id;
    pthread_barrier_wait(&barrier);
    for (unsigned i = 0; i < 1000; ++i) {
        assert(check_registers() == context);
        assert(context->slots[DM_K18] == 0x12340000u + test->id);
        assert(context->slots[DM_KTLS] == 0xabcd0000u + test->id);
        assert(context->slots[DM_KTLR] == 0xef000000u + test->id);
        sched_yield();
    }
    return NULL;
}

static void fixture_publish(dm_android_context_cache_entry *entry,uintptr_t tp,uintptr_t context) {
    uint64_t sequence=atomic_load(&entry->sequence);assert(!(sequence&1));atomic_store(&entry->sequence,sequence+1);
    atomic_thread_fence(memory_order_seq_cst);atomic_store(&entry->tp,tp);atomic_store(&entry->context,context);atomic_store(&entry->sequence,sequence+2);
}

static void *run_failures_and_collisions(void *arg) {
    thread_case *test=arg;test->tp=(uintptr_t)__builtin_thread_pointer();
    atomic_store(&fail_key_create,1);dm_android_context *context=dm_android_context_get();assert(context&&context->slots[DM_K18]==0);
    context->slots[DM_K18]=0xc001d00d;assert(!dm_android_context_cache_lookup(test->tp));assert(check_registers()==context);
    atomic_store(&fail_key_create,0);assert(dm_android_context_get()==context&&context->slots[DM_K18]==0xc001d00d);assert(dm_android_context_cache_lookup(test->tp)==context);
    dm_android_context_cache_forget(test->tp,context);assert(!dm_android_context_cache_lookup(test->tp));
    atomic_store(&fail_set_specific,1);assert(dm_android_context_get()==context);assert(!dm_android_context_cache_lookup(test->tp));assert(check_registers()==context);
    atomic_store(&fail_set_specific,0);assert(dm_android_context_get()==context&&context->slots[DM_K18]==0xc001d00d);assert(dm_android_context_cache_lookup(test->tp)==context);
    dm_android_context_cache_forget(test->tp,(const dm_android_context*)((uintptr_t)context+8));assert(dm_android_context_cache_lookup(test->tp)==context);
    // 同桶错误 TP/context 必须回退；失败的 C 注册也不能驱逐另一个登记。
    // A wrong TP/context in the same bucket must fall back; failed C registration cannot evict another registration.
    uintptr_t other_tp=test->tp^((uintptr_t)1<<63);assert(dm_android_context_cache_bucket(other_tp)==dm_android_context_cache_bucket(test->tp));
    dm_android_context_cache_entry *entry=&dm_android_context_cache[dm_android_context_cache_bucket(test->tp)];
    fixture_publish(entry,other_tp,(uintptr_t)context+8);assert(check_registers()==context&&context->slots[DM_K18]==0xc001d00d);
    assert(atomic_load(&entry->tp)==other_tp);fixture_publish(entry,0,0);assert(dm_android_context_get()==context);
    uint64_t sequence=atomic_load(&entry->sequence);atomic_store(&entry->sequence,sequence+1);assert(check_registers()==context);assert(atomic_load(&entry->sequence)==sequence+1);
    atomic_store(&entry->sequence,sequence+2);assert(check_registers()==context&&dm_android_context_cache_lookup(test->tp)==context);
    assert(!pthread_setspecific(late_context_key,context));test->context=context;return NULL;
}

static void *run_reuse(void *arg) {
    thread_case *test=arg;test->tp=(uintptr_t)__builtin_thread_pointer();test->context=check_registers();
    assert(test->context->slots[DM_K18]==0);test->context->slots[DM_K18]=0xf0000000+test->id;
    assert(check_registers()==test->context);test->value=test->context->slots[DM_K18];return NULL;
}

__attribute__((visibility("default"))) int dm_android_context_test(void) {
    // 单独工作线程故障注入，不把 dlopen 模块的析构挂到仍存活的 driver 主线程。
    // Inject failures on a worker, avoiding a dlopen module's destructor on the still-live driver main thread.
    assert(!pthread_key_create(&late_context_key,late_context_destroy));
    pthread_t fault_thread;thread_case fault={0};assert(!pthread_create(&fault_thread,NULL,run_failures_and_collisions,&fault));assert(!pthread_join(fault_thread,NULL));
    assert(atomic_load(&late_destructor_calls)==1);assert(!pthread_key_delete(late_context_key));
    assert(atomic_load(&key_create_calls)>=2&&atomic_load(&set_specific_calls)>=3);assert(!dm_android_context_cache_lookup(fault.tp));
    pthread_t threads[4]; thread_case tests[4] = {0};
    assert(!pthread_barrier_init(&barrier, NULL, 4));
    for (unsigned i = 0; i < 4; ++i) {
        tests[i].id = i + 1;
        assert(!pthread_create(&threads[i], NULL, run_thread, &tests[i]));
    }
    for (unsigned i = 0; i < 4; ++i) assert(!pthread_join(threads[i], NULL));
    for (unsigned i = 0; i < 4; ++i) for (unsigned j = 0; j < i; ++j)
        assert(tests[i].context != tests[j].context);
    for(unsigned i=0;i<4;i++)assert(!dm_android_context_cache_lookup(tests[i].tp));
    assert(!pthread_barrier_destroy(&barrier));
    uintptr_t previous_tp=0;unsigned reused=0;
    for(unsigned i=0;i<128;i++){
        thread_case test={.id=i+1};pthread_t thread;assert(!pthread_create(&thread,NULL,run_reuse,&test));assert(!pthread_join(thread,NULL));
        assert(test.value==0xf0000000+test.id&&!dm_android_context_cache_lookup(test.tp));reused+=test.tp==previous_tp;previous_tp=test.tp;
    }
    printf("Context cache: PASS (getter-only registration, preserved slots, pthread key/create failures, collision/odd fallback, stale destructor token, late getter/init during public TSD cleanup, 128 new threads, TP reused %u times)\n",reused);
    puts("Context TLSDESC/cache: PASS (dlopen, cold and warm, 4 threads x 1001, x0-x30 except output x16, q0-q31, SP, NZCV, FPSR, FPCR, Android x18/TP, isolated guest slots)");
    return 0;
}
