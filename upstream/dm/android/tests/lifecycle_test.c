// 在独立测试进程中验证 SIGTERM 的正常退出顺序和 1.5 秒兜底。
// Verify SIGTERM shutdown ordering and the 1.5-second fallback in isolated test processes.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "dm.h"
#include "host_posix.h"
#include <assert.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
static int output;
static int stall;
static void mark(char value) { assert(write(output, &value, 1) == 1); }
void tact_prefetch_stop(void) { mark('1'); }
void granny_prof_dump(void) { mark('2'); }
void hlecount_flush(void) { mark('3'); }
void dm_audio_suspend(int on) { assert(on == 1); mark('4'); }
void dm_gfx_suspend(int on) { assert(on == 1); if (stall) usleep(3000000); mark('5'); }
uint64_t now_ns(void) { return dm_android_clock_ns(CLOCK_MONOTONIC); }
void dm_term_install(void);
static void check(int hanging) {
    int ready[2], result[2]; assert(!pipe(ready) && !pipe(result));
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        close(ready[0]); close(result[0]); output = result[1]; stall = hanging;
        dm_term_install(); mark('0'); assert(write(ready[1], "R", 1) == 1); for (;;) pause();
    }
    close(ready[1]); close(result[1]); char value; assert(read(ready[0], &value, 1) == 1);
    uint64_t start = now_ns(); assert(!kill(child, SIGTERM)); int status; assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    char markers[16] = {0}; ssize_t n = read(result[0], markers, sizeof markers - 1);
    assert(n > 0); assert(!strcmp(markers, hanging ? "01234" : "012345"));
    if (hanging) assert(now_ns() - start >= 1400000000 && now_ns() - start < 3000000000);
    close(ready[0]); close(result[0]);
}
int main(void) { check(0); check(1); puts("Android lifecycle: PASS (SIGTERM ordering, normal-thread cleanup, bounded fallback)"); return 0; }
