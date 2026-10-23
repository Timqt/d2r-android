// 壳的替身（dm/tools/shell_stubs.py 生成）：编年史仓库固定表不编进代码，启动时从安装包的 dm/derived.tsv 读
// （标签 VTPART / VTROW / VTSET）。条数随游戏版本固定，写成数组大小；内容全在数据文件里。
#include "dm_derived_rt.h"
#define VT_NPART 11
static const char *vt_part_name[VT_NPART];
typedef struct { uint8_t kind, part, tier; uint16_t row, lvl; int16_t set; const char *key, *icon; } vt_row;
static vt_row vt_rows[562];
#define VT_NROW (int)(sizeof vt_rows / sizeof vt_rows[0])
static const char *vt_set_key[35];
#define VT_NSET (int)(sizeof vt_set_key / sizeof vt_set_key[0])
static void vt_part_cb(char **f, int n, void *ctx) { int *i = ctx; if (n >= 1 && *i < VT_NPART) vt_part_name[(*i)++] = strdup(f[0]); }
static void vt_set_cb(char **f, int n, void *ctx) { int *i = ctx; if (n >= 1 && *i < VT_NSET) vt_set_key[(*i)++] = strdup(f[0]); }
static void vt_row_cb(char **f, int n, void *ctx) { int *i = ctx; if (n < 8 || *i >= VT_NROW) return;
    vt_rows[(*i)++] = (vt_row){ (uint8_t)atoi(f[0]), (uint8_t)atoi(f[1]), (uint8_t)atoi(f[2]), (uint16_t)atoi(f[3]), (uint16_t)atoi(f[4]), (int16_t)atoi(f[5]), strdup(f[6]), strdup(f[7]) }; }
void dm_vault_load(void) { int a = 0, b = 0, c = 0;
    dm_derived_each("VTPART", vt_part_cb, &a); dm_derived_each("VTROW", vt_row_cb, &b); dm_derived_each("VTSET", vt_set_cb, &c);
    for (int i = a; i < VT_NPART; i++) vt_part_name[i] = ""; for (int i = c; i < VT_NSET; i++) vt_set_key[i] = "";
    for (int i = b; i < VT_NROW; i++) vt_rows[i] = (vt_row){ .set = -1, .key = "", .icon = "" };
    if (a != VT_NPART || b != VT_NROW || c != VT_NSET) fprintf(stderr, "[dm] 【红】编年史仓库表没读全：部位 %d / %d、条目 %d / %d、套装 %d / %d\n", a, VT_NPART, b, VT_NROW, c, VT_NSET); }
#if !defined(__ANDROID__)
__attribute__((constructor)) static void vt_load(void) { dm_vault_load(); }
#endif
