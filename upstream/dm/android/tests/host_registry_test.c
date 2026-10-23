// 验证真实 HLE 宏的 ELF 节布局与 pthread 键镜像，包含有效槽偏移 0。
// Verify real HLE macro ELF section layout and pthread key mirroring, including valid slot offset zero.
#include "hle.h"
#include "runtime.h"
#include "host_tls.h"
#include "host_sections.h"
#include <assert.h>

HLE("test_import", uint64_t, registry_import, uint64_t value) { return value + 7; }
HLE_FORCE("test_force", "Android registry test", uint64_t, registry_force, uint64_t value) { return value + 11; }
HLE_FORCE("test_force_two", "Android registry test", uint64_t, registry_force_two, uint64_t value) { return value + 13; }
uint64_t gate_dispatch(uint64_t ret, uint64_t *regs) { (void)ret; (void)regs; abort(); }
uint64_t unimpl_c(unsigned index, uint64_t *regs) { (void)index; (void)regs; abort(); }
void dm_thread_returned(void) { abort(); }
static pthread_key_t keys[8];
static void *thread_test(void *arg) {
    uintptr_t seed = (uintptr_t)arg;
    dm_android_thread_init(NULL);
    for (unsigned i = 0; i < 8; ++i) {
        assert(!dm_android_host_setspecific(keys[i], (void *)(seed + i)));
        assert((uintptr_t)pthread_getspecific(keys[i]) == seed + i);
        assert(dm_android_context_get()->slots[i] == seed + i);
    }
    return NULL;
}
int main(void) {
    unsigned long size;
    const hle_ent *imports = dm_android_host_section(0, &size);
    assert(size == sizeof(hle_ent)); assert(!strcmp(imports[0].name, "test_import"));
    assert(((uint64_t (*)(uint64_t))imports[0].fn)(3) == 10);
    const hle_force_ent *forced = dm_android_host_section(1, &size);
    assert(size == 2 * sizeof(hle_force_ent));
    for (unsigned i = 0; i < 2; ++i) {
        assert(forced[i].name && forced[i].why);
        assert(((uint64_t (*)(uint64_t))forced[i].fn)(3) == (!strcmp(forced[i].name, "test_force") ? 14 : 16));
    }
    for (unsigned i = 0; i < 8; ++i) assert(!pthread_key_create(&keys[i], NULL));
    dm_android_bind_keys(keys);
    pthread_t a, b;
    assert(!pthread_create(&a, NULL, thread_test, (void *)100));
    assert(!pthread_create(&b, NULL, thread_test, (void *)200));
    pthread_join(a, NULL); pthread_join(b, NULL);
    for (unsigned i = 0; i < 8; ++i) pthread_key_delete(keys[i]);
    puts("Android host registry: PASS (ELF import/force tables, entry alignment, pthread key mirror, thread isolation)");
    return 0;
}
