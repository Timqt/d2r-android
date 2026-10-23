// 在独立进程的临时目录验证启动配置；环境写入故障替身仅在测试链接时启用。
// Verify launch configuration in a temporary directory of a standalone process; environment-write fault substitutes are enabled only when linking the test.
#define _POSIX_C_SOURCE 200809L
#include "launch_settings.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char temporary[4096], config_path[4096], receipt_path[4096];
static unsigned tests;

#ifdef DM_LAUNCH_SETTINGS_TEST_WRAP_ENV
// 链接器 --wrap 在编译后替换 libc 调用；volatile 保证 O3 不消掉编译期不可见的替身控制写入。
// The linker applies --wrap after compilation; volatile keeps O3 from removing control writes for substitutes invisible at compile time.
static volatile unsigned environment_writes, fail_write_mask;
int __real_setenv(const char *, const char *, int);
int __real_unsetenv(const char *);
static int write_fails(void) {
    environment_writes++;
    if (environment_writes < 32 && (fail_write_mask & (1u << environment_writes))) { errno = ENOMEM; return 1; }
    return 0;
}
int __wrap_setenv(const char *key, const char *value, int overwrite) {
    return write_fails() ? -1 : __real_setenv(key, value, overwrite);
}
int __wrap_unsetenv(const char *key) { return write_fails() ? -1 : __real_unsetenv(key); }
#endif

static void expect_env(const char *name, const char *expected) {
    const char *actual = getenv(name);
    assert(expected ? actual && !strcmp(actual, expected) : !actual);
}

static void reset_environment(void) {
#ifdef DM_LAUNCH_SETTINGS_TEST_WRAP_ENV
    environment_writes = 0; fail_write_mask = 0;
#endif
    assert(!setenv("DM_OPMODE", "old-operation-mode", 1));
    assert(!setenv("DM_HID_RECEIPT_LOG", "old-receipt-path", 1));
    assert(!setenv("DM_DEBUG_OVERLAY", "old-overlay-mode", 1));
    assert(!setenv("DM_GAME_RES", "old-resolution", 1));
}

static void write_bytes(const void *text, size_t length) {
    FILE *file = fopen(config_path, "wb");
    assert(file && fwrite(text, 1, length, file) == length && !fclose(file));
}

static void write_config(const char *text) { write_bytes(text, strlen(text)); }

static dm_android_launch_settings apply_ok(const char *directory) {
    dm_android_launch_settings result = {-99, -99, -99, -99, -99, -99};
    char error[512] = "stale error";
    assert(dm_android_launch_settings_apply(directory, &result, error, sizeof error));
    assert(!error[0]);
    return result;
}

static void expect_bad(const char *text, const char *message) {
    reset_environment();
    write_config(text);
    dm_android_launch_settings result = {17, 18, 19, 20, 21, 22, 23};
    char error[512];
    assert(!dm_android_launch_settings_apply(temporary, &result, error, sizeof error));
    assert(strstr(error, message));
    assert(result.present == 17 && result.operation_mode == 18 && result.input_receipts == 19 && result.debug_overlay == 20 && result.resolution_width == 21 && result.resolution_height == 22 && result.present_hz == 23);
    expect_env("DM_OPMODE", "old-operation-mode");
    expect_env("DM_HID_RECEIPT_LOG", "old-receipt-path");
    expect_env("DM_DEBUG_OVERLAY", "old-overlay-mode");
    expect_env("DM_GAME_RES", "old-resolution");
    tests++;
}

static void missing_defaults(void) {
    reset_environment();
    dm_android_launch_settings result = apply_ok(temporary);
    assert(!result.present && result.operation_mode == -1 && result.input_receipts == -1 && result.debug_overlay == -1 && result.resolution_width == -1 && result.resolution_height == -1 && result.present_hz == -1);
    expect_env("DM_OPMODE", "old-operation-mode"); expect_env("DM_HID_RECEIPT_LOG", "old-receipt-path");
    tests++;
    assert(!unsetenv("DM_OPMODE") && !unsetenv("DM_HID_RECEIPT_LOG"));
    result = apply_ok(temporary);
    assert(!result.present && result.operation_mode == -1 && result.input_receipts == -1 && result.debug_overlay == -1 && result.resolution_width == -1 && result.resolution_height == -1 && result.present_hz == -1);
    expect_env("DM_OPMODE", NULL); expect_env("DM_HID_RECEIPT_LOG", NULL);
    tests++;
}

static void valid_settings(void) {
    reset_environment(); write_config("");
    dm_android_launch_settings result = apply_ok(temporary);
    assert(result.present && result.operation_mode == -1 && result.input_receipts == -1 && result.debug_overlay == -1 && result.resolution_width == -1 && result.resolution_height == -1 && result.present_hz == -1);
    expect_env("DM_OPMODE", "old-operation-mode"); expect_env("DM_HID_RECEIPT_LOG", "old-receipt-path");
    tests++;

    write_config("# 掌机与实际输入取样\r\n\toperation_mode = handheld \r\n input_receipts = 1\r\n");
    result = apply_ok(temporary);
    assert(result.present && result.operation_mode == 0 && result.input_receipts == 1);
    expect_env("DM_OPMODE", "0"); expect_env("DM_HID_RECEIPT_LOG", receipt_path);
    // 解析器只配置路径，不提前创建日志或采集假的 HID 输入。
    // The parser only configures the path; it does not create the log early or sample fake HID input.
    assert(access(receipt_path, F_OK) && errno == ENOENT);
    tests++;

    write_config("operation_mode=console\ninput_receipts=0");
    result = apply_ok(temporary);
    assert(result.operation_mode == 1 && result.input_receipts == 0);
    expect_env("DM_OPMODE", "1"); expect_env("DM_HID_RECEIPT_LOG", NULL);
    tests++;

    reset_environment(); write_config("operation_mode=handheld\n");
    result = apply_ok(temporary);
    assert(result.operation_mode == 0 && result.input_receipts == -1);
    expect_env("DM_OPMODE", "0"); expect_env("DM_HID_RECEIPT_LOG", "old-receipt-path");
    tests++;

    reset_environment(); write_config("input_receipts=1\n");
    result = apply_ok(temporary);
    assert(result.operation_mode == -1 && result.input_receipts == 1);
    expect_env("DM_OPMODE", "old-operation-mode"); expect_env("DM_HID_RECEIPT_LOG", receipt_path);
    tests++;

    reset_environment(); write_config("input_receipts=0\n");
    result = apply_ok(temporary);
    assert(result.operation_mode == -1 && result.input_receipts == 0);
    expect_env("DM_OPMODE", "old-operation-mode"); expect_env("DM_HID_RECEIPT_LOG", NULL);
    tests++;

    reset_environment(); write_config("# comment\n\t \r\n");
    result = apply_ok(temporary);
    assert(result.present && result.operation_mode == -1 && result.input_receipts == -1 && result.debug_overlay == -1 && result.resolution_width == -1 && result.resolution_height == -1 && result.present_hz == -1);
    expect_env("DM_OPMODE", "old-operation-mode"); expect_env("DM_HID_RECEIPT_LOG", "old-receipt-path");
    tests++;

    write_config("input_receipts=1\n");
    char trailing[4096];
    int n = snprintf(trailing, sizeof trailing, "%s///", temporary); assert(n > 0 && n < (int)sizeof trailing);
    result = apply_ok(trailing);
    assert(result.input_receipts == 1); expect_env("DM_HID_RECEIPT_LOG", receipt_path);
    tests++;

    write_config("operation_mode=console\n");
    assert(dm_android_launch_settings_apply(temporary, NULL, NULL, 0));
    expect_env("DM_OPMODE", "1");
    tests++;
}

static void overlay_settings(void) {
    // 关掉浮层是一个显式设置，必须写成 "0" 而不是清空变量，否则“没指定”和“关掉”就区分不出来。
    // Turning the overlay off is an explicit setting, so it writes "0" rather than unsetting the variable;
    // otherwise "unspecified" and "off" would be indistinguishable.
    reset_environment(); write_config("debug_overlay=0\n");
    dm_android_launch_settings result = apply_ok(temporary);
    assert(result.present && result.operation_mode == -1 && result.input_receipts == -1 && result.debug_overlay == 0);
    expect_env("DM_OPMODE", "old-operation-mode");
    expect_env("DM_HID_RECEIPT_LOG", "old-receipt-path");
    expect_env("DM_DEBUG_OVERLAY", "0");
    tests++;

    reset_environment(); write_config("debug_overlay=1\n");
    result = apply_ok(temporary);
    assert(result.debug_overlay == 1);
    expect_env("DM_DEBUG_OVERLAY", "1");
    tests++;

    // 省略该键时不动已有环境值，与另外两个键同一约定。
    // Omitting the key leaves the existing environment value untouched, matching the other two keys.
    reset_environment(); write_config("operation_mode=handheld\n");
    result = apply_ok(temporary);
    assert(result.debug_overlay == -1);
    expect_env("DM_DEBUG_OVERLAY", "old-overlay-mode");
    tests++;

    // 三个键一起写：第三项环境写入必须真的发生，且前面两项不受影响。
    // All three keys together: the third environment write must actually happen without disturbing the first two.
    reset_environment(); write_config("operation_mode=console\ninput_receipts=0\ndebug_overlay=0\n");
    result = apply_ok(temporary);
    assert(result.operation_mode == 1 && result.input_receipts == 0 && result.debug_overlay == 0);
    expect_env("DM_OPMODE", "1"); expect_env("DM_HID_RECEIPT_LOG", NULL); expect_env("DM_DEBUG_OVERLAY", "0");
    tests++;
}

static void resolution_settings(void) {
    reset_environment(); write_config("resolution=1920x1080\n");
    dm_android_launch_settings result = apply_ok(temporary);
    assert(result.resolution_width == 1920 && result.resolution_height == 1080);
    expect_env("DM_GAME_RES", "1920x1080");
    tests++;

    // 省略该键时不动已有环境值，与其它键同一约定。
    // Omitting the key leaves the existing environment value untouched, like the other keys.
    reset_environment(); write_config("operation_mode=console\n");
    result = apply_ok(temporary);
    assert(result.resolution_width == -1 && result.resolution_height == -1);
    expect_env("DM_GAME_RES", "old-resolution");
    tests++;

    // 上界与下界都接受。
    // Both bounds are accepted.
    reset_environment(); write_config("resolution=320x240\n");
    result = apply_ok(temporary);
    expect_env("DM_GAME_RES", "320x240");
    tests++;
    reset_environment(); write_config("resolution=7680x4320\n");
    result = apply_ok(temporary);
    expect_env("DM_GAME_RES", "7680x4320");
    tests++;

    // 四个键一起写：第四项环境写入必须真的发生，且前三项不受影响。
    // All four keys together: the fourth environment write must actually happen without disturbing the first three.
    reset_environment(); write_config("operation_mode=handheld\ninput_receipts=0\ndebug_overlay=0\nresolution=2560x1440\n");
    result = apply_ok(temporary);
    assert(result.resolution_width == 2560 && result.resolution_height == 1440);
    expect_env("DM_OPMODE", "0"); expect_env("DM_HID_RECEIPT_LOG", NULL);
    expect_env("DM_DEBUG_OVERLAY", "0"); expect_env("DM_GAME_RES", "2560x1440");
    tests++;
}

static void present_hz_settings(void) {
    // 0 = 跟随屏幕最高刷新率；其余是固定帧率。
    // 0 = follow the screen's maximum refresh rate; anything else is a fixed rate.
    reset_environment(); write_config("present_hz=0\n");
    dm_android_launch_settings result = apply_ok(temporary);
    assert(result.present_hz == 0);
    tests++;
    reset_environment(); write_config("present_hz=120\n");
    result = apply_ok(temporary);
    assert(result.present_hz == 120);
    tests++;
    // 省略这个键时是"未指定"（-1），前端保持原本的 60 Hz。
    // Omitting the key leaves it unspecified (-1) and the front end keeps the original 60 Hz.
    reset_environment(); write_config("operation_mode=console\n");
    result = apply_ok(temporary);
    assert(result.present_hz == -1);
    tests++;
    // 范围外与非法值都明确失败，不半应用。
    // Out-of-range and malformed values fail loudly without applying anything.
    expect_bad("present_hz=23\n", "present_hz must be 0 or 24-240");
    expect_bad("present_hz=241\n", "present_hz must be 0 or 24-240");
    expect_bad("present_hz=abc\n", "present_hz must be 0 or 24-240");
    expect_bad("present_hz=60x\n", "present_hz must be 0 or 24-240");
    expect_bad("present_hz=0\npresent_hz=60\n", "duplicate launch settings key on line 2");
}

static void malformed_settings(void) {
    expect_bad("operation_mode=handheld\nDM_VK_SHADERS=/tmp/fake\n", "unknown launch settings key on line 2");
    expect_bad("input_receipts=1\noperation_mode=handheld\noperation_mode=console\n", "duplicate launch settings key on line 3");
    expect_bad("operation_mode=handheld\ninput_receipts=1\ninput_receipts=1\n", "duplicate launch settings key on line 3");
    expect_bad("operation_mode=0\n", "operation_mode must be handheld or console");
    expect_bad("input_receipts=1\noperation_mode=Handheld\n", "operation_mode must be handheld or console");
    expect_bad("operation_mode=console\ninput_receipts=true\n", "input_receipts must be 0 or 1");
    expect_bad("operation_mode=console\ninput_receipts=2\n", "input_receipts must be 0 or 1");
    expect_bad("operation_mode=console\ninput_receipts=\n", "input_receipts must be 0 or 1");
    expect_bad("operation_mode=handheld\ninput_receipts\n", "line 2 requires key=value");
    expect_bad("operation_mode=handheld\n=1\n", "unknown launch settings key on line 2");
    expect_bad("operation_mode=handheld\ninput_receipts=1=0\n", "input_receipts must be 0 or 1");
    expect_bad("operation_mode=handheld # note\n", "operation_mode must be handheld or console");
    expect_bad("debug_overlay=2\n", "debug_overlay must be 0 or 1");
    expect_bad("debug_overlay=true\n", "debug_overlay must be 0 or 1");
    expect_bad("debug_overlay=\n", "debug_overlay must be 0 or 1");
    expect_bad("operation_mode=handheld\ndebug_overlay=0\ndebug_overlay=1\n", "duplicate launch settings key on line 3");
    expect_bad("resolution=1920\n", "resolution must be <width>x<height>");
    expect_bad("resolution=1920x1080p\n", "resolution must be <width>x<height>");
    expect_bad("resolution=319x1080\n", "resolution must be <width>x<height>");
    expect_bad("resolution=1920x4321\n", "resolution must be <width>x<height>");
    expect_bad("resolution=\n", "resolution must be <width>x<height>");
    expect_bad("resolution=1920x1080\nresolution=1280x720\n", "duplicate launch settings key on line 2");
}

static void size_limits(void) {
    char content[4200];
    memset(content, 'x', 256); content[0] = '#'; content[256] = 0;
    reset_environment(); write_config(content);
    dm_android_launch_settings result = apply_ok(temporary);
    assert(result.present && result.operation_mode == -1 && result.input_receipts == -1 && result.debug_overlay == -1 && result.resolution_width == -1 && result.resolution_height == -1 && result.present_hz == -1);
    expect_env("DM_OPMODE", "old-operation-mode"); expect_env("DM_HID_RECEIPT_LOG", "old-receipt-path");
    tests++;

    strcpy(content, "operation_mode=handheld\n"); size_t prefix = strlen(content);
    memset(content + prefix, 'x', 257); content[prefix] = '#'; content[prefix + 257] = 0;
    expect_bad(content, "line 2 exceeds 256 bytes");

    memset(content, '\n', 4096); content[4096] = 0;
    reset_environment(); write_config(content); result = apply_ok(temporary);
    assert(result.present && result.operation_mode == -1 && result.input_receipts == -1 && result.debug_overlay == -1 && result.resolution_width == -1 && result.resolution_height == -1 && result.present_hz == -1);
    expect_env("DM_OPMODE", "old-operation-mode"); expect_env("DM_HID_RECEIPT_LOG", "old-receipt-path");
    tests++;

    content[4096] = '\n'; content[4097] = 0;
    expect_bad(content, "exceed 4096 bytes");

    reset_environment();
    const char nul_content[] = "operation_mode=handheld\n\0input_receipts=1\n";
    write_bytes(nul_content, sizeof nul_content - 1);
    result = (dm_android_launch_settings){17, 18, 19, 20, 21, 22}; char error[512];
    assert(!dm_android_launch_settings_apply(temporary, &result, error, sizeof error));
    assert(strstr(error, "NUL byte") && result.present == 17 && result.operation_mode == 18 && result.input_receipts == 19 && result.debug_overlay == 20 && result.resolution_width == 21 && result.resolution_height == 22);
    expect_env("DM_OPMODE", "old-operation-mode"); expect_env("DM_HID_RECEIPT_LOG", "old-receipt-path");
    tests++;
}

static void invalid_paths(void) {
    const char *paths[] = {NULL, "", "relative/temp"};
    char error[512];
    reset_environment();
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
        dm_android_launch_settings result = {17, 18, 19, 20, 21, 22};
        assert(!dm_android_launch_settings_apply(paths[i], &result, error, sizeof error));
        assert(strstr(error, "absolute application temp directory") && result.present == 17);
        expect_env("DM_OPMODE", "old-operation-mode"); expect_env("DM_HID_RECEIPT_LOG", "old-receipt-path");
        tests++;
    }
    char long_path[4097]; memset(long_path, 'x', 4096); long_path[0] = '/'; long_path[4096] = 0;
    assert(!dm_android_launch_settings_apply(long_path, NULL, error, sizeof error));
    assert(strstr(error, "temp path is too long"));
    expect_env("DM_OPMODE", "old-operation-mode"); expect_env("DM_HID_RECEIPT_LOG", "old-receipt-path");
    tests++;

    assert(!unlink(config_path)); assert(!mkdir(config_path, 0700));
    assert(!dm_android_launch_settings_apply(temporary, NULL, error, sizeof error));
    assert(strstr(error, "regular file"));
    expect_env("DM_OPMODE", "old-operation-mode"); expect_env("DM_HID_RECEIPT_LOG", "old-receipt-path");
    assert(!rmdir(config_path)); tests++;

    int fifo_result = mkfifo(config_path, 0600);
    if (fifo_result) {
        int saved = errno;
        fprintf(stderr, "launch settings FIFO fixture creation failed: path=%s uid=%lu errno=%d (%s); test incomplete, not skipped\n",
                config_path, (unsigned long)getuid(), saved, strerror(saved));
    }
    assert(!fifo_result);
    alarm(5);
    assert(!dm_android_launch_settings_apply(temporary, NULL, error, sizeof error));
    alarm(0);
    assert(strstr(error, "regular file"));
    expect_env("DM_OPMODE", "old-operation-mode"); expect_env("DM_HID_RECEIPT_LOG", "old-receipt-path");
    assert(!unlink(config_path)); tests++;
}

#ifdef DM_LAUNCH_SETTINGS_TEST_WRAP_ENV
static void environment_failures(void) {
    char error[512];
    for (int absent = 0; absent < 2; absent++) {
        for (int receipt = 0; receipt < 2; receipt++) {
            reset_environment();
            if (absent) assert(!unsetenv("DM_OPMODE") && !unsetenv("DM_HID_RECEIPT_LOG"));
            write_config(receipt ? "operation_mode=handheld\ninput_receipts=1\n" : "operation_mode=handheld\ninput_receipts=0\n");
            // 精确让第二项环境写失败；旧值存在和不存在都必须恢复，result 不发布。
            // Fail precisely the second environment write; restore both present and absent old values, and do not publish result.
            environment_writes = 0; fail_write_mask = 1u << 2;
            dm_android_launch_settings result = {17, 18, 19, 20, 21, 22};
            assert(!dm_android_launch_settings_apply(temporary, &result, error, sizeof error));
            fail_write_mask = 0;
            assert(strstr(error, "previous environment restored"));
            assert(result.present == 17 && result.operation_mode == 18 && result.input_receipts == 19 && result.debug_overlay == 20 && result.resolution_width == 21 && result.resolution_height == 22);
            expect_env("DM_OPMODE", absent ? NULL : "old-operation-mode");
            expect_env("DM_HID_RECEIPT_LOG", absent ? NULL : "old-receipt-path");
            tests++;
        }
    }
    reset_environment(); write_config("operation_mode=handheld\ninput_receipts=1\n");
    environment_writes = 0; fail_write_mask = 1u << 1;
    assert(!dm_android_launch_settings_apply(temporary, NULL, error, sizeof error));
    fail_write_mask = 0;
    assert(strstr(error, "previous environment restored"));
    expect_env("DM_OPMODE", "old-operation-mode"); expect_env("DM_HID_RECEIPT_LOG", "old-receipt-path");
    tests++;

    reset_environment(); environment_writes = 0; fail_write_mask = (1u << 2) | (1u << 3);
    assert(!dm_android_launch_settings_apply(temporary, NULL, error, sizeof error));
    fail_write_mask = 0;
    // 真实 libc 回滚也可能分配失败：必须明确报 abort startup，不能伪称恢复成功。
    // Real libc rollback can also fail allocation: explicitly report abort startup instead of falsely claiming successful restoration.
    if (!strstr(error, "environment rollback also failed")) fprintf(stderr, "rollback-failure test returned: %s; writes=%u\n", error, environment_writes);
    assert(strstr(error, "environment rollback also failed") && strstr(error, "abort startup"));
    expect_env("DM_OPMODE", "0"); expect_env("DM_HID_RECEIPT_LOG", "old-receipt-path");
    tests++;
}
#endif

int main(int argc, char **argv) {
    const char *base = argc > 1 ? argv[1] : "/tmp";
    int n = snprintf(temporary, sizeof temporary, "%s/d2r-launch-settings-XXXXXX", base); assert(n > 0 && n < (int)sizeof temporary);
    assert(mkdtemp(temporary));
    n = snprintf(config_path, sizeof config_path, "%s/launch-settings.conf", temporary); assert(n > 0 && n < (int)sizeof config_path);
    n = snprintf(receipt_path, sizeof receipt_path, "%s/input-receipts.jsonl", temporary); assert(n > 0 && n < (int)sizeof receipt_path);
    missing_defaults(); valid_settings(); overlay_settings(); resolution_settings(); present_hz_settings(); malformed_settings(); size_limits(); invalid_paths();
#ifdef DM_LAUNCH_SETTINGS_TEST_WRAP_ENV
    environment_failures();
#endif
    if (!access(config_path, F_OK)) assert(!unlink(config_path));
    assert(!rmdir(temporary));
    printf("Android launch settings: PASS (%u cases; fixed whitelist, defaults, strict parse, no partial parse application", tests);
#ifdef DM_LAUNCH_SETTINGS_TEST_WRAP_ENV
    printf(", environment rollback and explicit rollback-failure reporting");
#endif
    puts(")");
    return 0;
}
