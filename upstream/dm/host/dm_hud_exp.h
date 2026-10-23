// 壳的替身（dm/tools/shell_stubs.py 生成）：经验表不编进代码，启动时从安装包的 dm/derived.tsv 读（标签 EXP，每行 10 个）
#pragma once
#include <stdint.h>
#include "dm_derived_rt.h"
static uint32_t k_exp[100];
static void k_exp_row(char **f, int n, void *ctx) { int *i = ctx; for (int k = 0; k < n && *i < 100; k++) k_exp[(*i)++] = (uint32_t)strtoul(f[k], NULL, 10); }
// 路径在进程启动时就有的平台（Mac / iOS）在加载时填；Android 的 DM_DERIVED 由前端在 boot 里设置，晚于库加载，
// 所以那边由前端显式调 dm_hud_exp_load()。函数可重复调用（每次从文件重新填）。
// Platforms whose path exists at process start (Mac / iOS) fill on load; on Android the front end sets DM_DERIVED
// in boot(), later than library load, so it calls dm_hud_exp_load() explicitly. Calling it again re-reads the file.
void dm_hud_exp_load(void) { int i = 0; dm_derived_each("EXP", k_exp_row, &i);
    if (i != 100) fprintf(stderr, "[dm] 【红】经验表没读全：%d / 100（数据包与 APK 的游戏版本不一致？）\n", i); }
#if !defined(__ANDROID__)
__attribute__((constructor)) static void k_exp_load(void) { dm_hud_exp_load(); }
#endif
