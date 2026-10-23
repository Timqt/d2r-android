// 用普通宿主可执行文件 dlopen 测试库，确保 TLS 是动态模块而非启动时的静态 TLS。
// Load the test library with dlopen so its TLS belongs to a dynamic module rather than initial static TLS.
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>

int main(int argc, char **argv) {
    assert(argc == 2);
    void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!module) { fprintf(stderr, "context test dlopen: %s\n", dlerror()); return 1; }
    int (*run)(void) = (int (*)(void))dlsym(module, "dm_android_context_test");
    assert(run);
    int result = run();
    assert(!dlclose(module));
    return result;
}
