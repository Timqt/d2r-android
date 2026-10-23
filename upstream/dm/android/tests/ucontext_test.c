// 用真实写保护缺页验证 Android 信号现场和 ESR 写入位。
// Validate Android signal context and the ESR write bit using an actual write-protection fault.
#define _GNU_SOURCE
#include "dm_ucontext.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
static void *page;
static size_t size;
static volatile sig_atomic_t passed;
static void fault(int signal, siginfo_t *info, void *context) {
    ucontext_t *uc = context;
    if (signal != SIGSEGV || info->si_addr != page || !dm_uc_is_write(uc) || !DM_UC_PC(uc) || !DM_UC_SP(uc)) _exit(2);
    if (mprotect(page, size, PROT_READ | PROT_WRITE)) _exit(3);
    passed = 1;
}
int main(void) {
    size = sysconf(_SC_PAGESIZE); page = mmap(NULL, size, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(page != MAP_FAILED);
    struct sigaction action = {.sa_sigaction = fault, .sa_flags = SA_SIGINFO}; sigemptyset(&action.sa_mask);
    assert(!sigaction(SIGSEGV, &action, NULL));
    *(volatile int *)page = 42;
    assert(passed && *(volatile int *)page == 42); munmap(page, size);
    puts("Android signal context: PASS (real write fault, ESR WnR, PC, SP, resumed instruction)");
    return 0;
}
