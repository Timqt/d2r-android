#!/usr/bin/env python3
# 壳用：把三份「从游戏文件生成、编进代码」的文件拆成两半——
#   数据   → 一份 derived.tsv（随使用者自己的游戏文件生成，放进安装包的 dm/）
#   替身   → 同名的三份源文件，里面没有数据，只有大小固定的空表和启动时从 derived.tsv 填表的几行（host/dm_derived_rt.h）
# 这样壳的可执行文件里不含这三张表的内容，而用它们的代码（diag.c、dm_vault_ui.c、DmTouchOverlay.m …）一行不用改。
# 表的条数会写进替身（数组大小）：条数随游戏版本固定，不是游戏内容。
# 用法：shell_stubs.py data  <dm 目录> <输出 derived.tsv>     读真实的三份生成文件，写数据（d2rm_pack.py 与 shell_build.sh 都用）
#       shell_stubs.py stubs <dm 目录>                       把三份生成文件原地换成替身（只在 shell_build.sh 的临时副本里做）
# For the shell: split the three files "generated from the game files and compiled into the code" into two halves --
#   data   → one derived.tsv (generated from the user's own game files, placed in dm/ of the install package)
#   stubs  → three source files of the same names, with no data inside, only fixed-size empty tables and the few lines that fill the tables from derived.tsv at startup (host/dm_derived_rt.h)
# This way the shell's executable does not contain the content of these three tables, while the code that uses them (diag.c, dm_vault_ui.c, DmTouchOverlay.m ...) needs not a single line changed.
# The entry counts of the tables are written into the stubs (array sizes): the counts are fixed by the game version and are not game content.
# Usage: shell_stubs.py data  <dm directory> <output derived.tsv>     read the three real generated files and write the data (used by both d2rm_pack.py and shell_build.sh)
#       shell_stubs.py stubs <dm directory>                       replace the three generated files in place with stubs (done only in shell_build.sh's temporary copy)
import os, re, sys

CSTR = r'"((?:[^"\\]|\\.)*)"'


def unq(s): return bytes(s, 'utf-8').decode('unicode_escape').encode('latin-1').decode('utf-8') if '\\' in s else s


def paths(dm, tables=None):
    # tables：三张表所在的目录（缺省 dm/host）；Android 打包器把真实表生成在工作目录里，用这个参数指过去。
    # 技能图标表只在 iOS 源码树里（Android 不需要它）。
    # tables: the directory holding the three tables (default dm/host); the Android packer generates the real
    # tables into its workspace and points here. The touch-art table only exists in the iOS source tree.
    t = tables or os.path.join(dm, 'host')
    return (os.path.join(t, 'dm_hud_exp.h'), os.path.join(t, 'dm_vault_tab.h'),
            os.path.join(dm, 'ios', 'Sources', 'dm_touch_native_art.c'), os.path.join(t, 'dm_bright_levels.inc'))


def parse(dm, tables=None):
    hud_path, vault_path, art_path, bright_path = paths(dm, tables)
    # 三份文件都必须是**真实表**；仓库里提交的是运行时替身（没有数据行），拿它拆表只会得到空结果，
    # 所以这里明确报错，而不是悄悄产出一个空 derived.tsv。
    # All three files must be *real* tables; the repository commits runtime stubs (no data rows), and splitting
    # those would silently produce an empty derived.tsv, so this fails loudly instead.
    for path in (hud_path, vault_path):
        if not os.path.isfile(path): sys.exit('缺少真实表：%s（先用游戏文件生成）' % path)
    hud, vault = (open(p, encoding='utf-8').read() for p in (hud_path, vault_path))
    if 'k_exp[100]' not in hud or 'vt_part_name[VT_NPART]' not in vault:
        sys.exit('这些是运行时替身而不是真实表：%s / %s（先用游戏文件生成真实表）' % (hud_path, vault_path))
    art = open(art_path, encoding='utf-8').read() if os.path.isfile(art_path) else ''
    bright = open(bright_path, encoding='utf-8').read() if os.path.isfile(bright_path) else ''
    exp = [int(x) for x in re.search(r'k_exp\[100\] = \{(.*?)\};', hud, re.S).group(1).replace('\n', ' ').split(',') if x.strip()]
    assert len(exp) == 100, len(exp)
    parts = [unq(x) for x in re.findall(CSTR, re.search(r'vt_part_name\[VT_NPART\] = \{(.*?)\};', vault).group(1))]
    rows = [(int(a), int(b), int(c), int(d), int(e), int(f), unq(k), unq(i)) for a, b, c, d, e, f, k, i in
            re.findall(r'^\s*\{ (\d+), (\d+), (\d+), (\d+), (\d+), (-?\d+), %s, %s \},$' % (CSTR, CSTR), vault, re.M)]
    sets = [unq(x) for x in re.findall(CSTR, re.search(r'vt_set_key\[\] = \{(.*?)\};', vault).group(1))]
    assert rows and sets and len(parts) == int(re.search(r'#define VT_NPART (\d+)', vault).group(1))
    assert vault.count('\n    { ') == len(rows), (vault.count('\n    { '), len(rows))
    m = re.search(r'static const struct \{ uint32_t id; int atlas,frame; \} rows\[\]=\{\n(.*?)\n\};', art, re.S) if art else None
    trows = [tuple(int(x) for x in t) for t in re.findall(r'^ \{(\d+),(-?\d+),(-?\d+)\},$', m.group(1), re.M)] if m else []
    assert not m or len(trows) == m.group(1).count('\n') + 1, (len(trows), m.group(1).count('\n') + 1 if m else 0)
    # 亮度表：每行 `{ 关卡号, 幕, DrlgType, 是否随昼夜 },`；行尾注释里是关卡名（游戏文本），不进派生数据。
    # Brightness rows: `{ level, act, DrlgType, varies },`; the trailing comment holds level names (game text),
    # which is not part of the derived data.
    lv = [tuple(int(x) for x in row) for row in re.findall(r'^\{ (\d+), (\d+), (\d+), (-?\d+) \},', bright, re.M)] if bright else []
    return exp, parts, rows, sets, trows, (hud, vault, art, m), lv


def data(dm, dst, tables=None):
    exp, parts, rows, sets, trows, _, lv = parse(dm, tables)
    L = ['EXP\t' + '\t'.join(map(str, exp[i:i + 10])) for i in range(0, 100, 10)]
    L += ['VTPART\t' + p for p in parts] + ['VTROW\t' + '\t'.join(map(str, r)) for r in rows] + ['VTSET\t' + s for s in sets]
    L += ['TAROW\t%d\t%d\t%d' % t for t in trows]
    L += ['BRT\t%d\t%d\t%d\t%d' % tuple(row) for row in lv]
    for l in L: assert '\n' not in l and l.count('\t') < 16
    for r in rows: assert '\t' not in r[6] and '\t' not in r[7]
    with open(dst, 'w', encoding='utf-8', newline='\n') as f: f.write('\n'.join(L) + '\n')
    return len(exp), len(parts), len(rows), len(sets), len(trows), len(lv)


def stubs(dm, tables=None):
    exp, parts, rows, sets, trows, (hud, vault, art, m), lv = parse(dm, tables)
    p_hud, p_vault, p_art, p_bright = paths(dm, tables)
    open(p_hud, 'w', encoding='utf-8').write('''// 壳的替身（dm/tools/shell_stubs.py 生成）：经验表不编进代码，启动时从安装包的 dm/derived.tsv 读（标签 EXP，每行 10 个）
#pragma once
#include <stdint.h>
#include "dm_derived_rt.h"
static uint32_t k_exp[100];
static void k_exp_row(char **f, int n, void *ctx) { int *i = ctx; for (int k = 0; k < n && *i < 100; k++) k_exp[(*i)++] = (uint32_t)strtoul(f[k], NULL, 10); }
// 路径在进程启动时就有的平台（Mac / iOS）在加载时填；Android 的 DM_DERIVED 由前端在 boot 里设置，晚于库加载，
// 所以那边由前端显式调 dm_hud_exp_load()。函数可重复调用（每次从文件重新填）。
// Platforms whose path exists at process start (Mac / iOS) fill on load; on Android the front end sets DM_DERIVED in
// boot(), later than library load, so it calls dm_hud_exp_load() explicitly. Calling it again re-reads the file.
void dm_hud_exp_load(void) { int i = 0; dm_derived_each("EXP", k_exp_row, &i);
    if (i != 100) fprintf(stderr, "[dm] 【红】经验表没读全：%d / 100（数据包与 APK 的游戏版本不一致？）\n", i); }
#if !defined(__ANDROID__)
__attribute__((constructor)) static void k_exp_load(void) { dm_hud_exp_load(); }
#endif
''')
    open(p_vault, 'w', encoding='utf-8').write('''// 壳的替身（dm/tools/shell_stubs.py 生成）：编年史仓库固定表不编进代码，启动时从安装包的 dm/derived.tsv 读
// （标签 VTPART / VTROW / VTSET）。条数随游戏版本固定，写成数组大小；内容全在数据文件里。
#include "dm_derived_rt.h"
#define VT_NPART %d
static const char *vt_part_name[VT_NPART];
typedef struct { uint8_t kind, part, tier; uint16_t row, lvl; int16_t set; const char *key, *icon; } vt_row;
static vt_row vt_rows[%d];
#define VT_NROW (int)(sizeof vt_rows / sizeof vt_rows[0])
static const char *vt_set_key[%d];
#define VT_NSET (int)(sizeof vt_set_key / sizeof vt_set_key[0])
static void vt_part_cb(char **f, int n, void *ctx) { int *i = ctx; if (n >= 1 && *i < VT_NPART) vt_part_name[(*i)++] = strdup(f[0]); }
static void vt_set_cb(char **f, int n, void *ctx) { int *i = ctx; if (n >= 1 && *i < VT_NSET) vt_set_key[(*i)++] = strdup(f[0]); }
static void vt_row_cb(char **f, int n, void *ctx) { int *i = ctx; if (n < 8 || *i >= VT_NROW) return;
    vt_rows[(*i)++] = (vt_row){ (uint8_t)atoi(f[0]), (uint8_t)atoi(f[1]), (uint8_t)atoi(f[2]), (uint16_t)atoi(f[3]), (uint16_t)atoi(f[4]), (int16_t)atoi(f[5]), strdup(f[6]), strdup(f[7]) }; }
void dm_vault_load(void) { int a = 0, b = 0, c = 0;
    dm_derived_each("VTPART", vt_part_cb, &a); dm_derived_each("VTROW", vt_row_cb, &b); dm_derived_each("VTSET", vt_set_cb, &c);
    for (int i = a; i < VT_NPART; i++) vt_part_name[i] = ""; for (int i = c; i < VT_NSET; i++) vt_set_key[i] = "";
    for (int i = b; i < VT_NROW; i++) vt_rows[i] = (vt_row){ .set = -1, .key = "", .icon = "" };
    if (a != VT_NPART || b != VT_NROW || c != VT_NSET) fprintf(stderr, "[dm] 【红】编年史仓库表没读全：部位 %%d / %%d、条目 %%d / %%d、套装 %%d / %%d\\n", a, VT_NPART, b, VT_NROW, c, VT_NSET); }
#if !defined(__ANDROID__)
__attribute__((constructor)) static void vt_load(void) { dm_vault_load(); }
#endif
''' % (len(parts), len(rows), len(sets)))
    if m:
        stub = art[:m.start()] + '''static struct { uint32_t id; int atlas,frame; } rows[%d];   /* 壳的替身：技能索引不编进代码，启动时从安装包的 dm/derived.tsv 读（标签 TAROW） */
static void ta_row_cb(char **f, int n, void *ctx) { int *i = ctx; if (n < 3 || *i >= (int)(sizeof rows / sizeof rows[0])) return;
    rows[*i].id = (uint32_t)strtoul(f[0], NULL, 10); rows[*i].atlas = atoi(f[1]); rows[*i].frame = atoi(f[2]); (*i)++; }
__attribute__((constructor)) static void ta_load(void) { int i = 0; dm_derived_each("TAROW", ta_row_cb, &i);
    for (int k = i; k < (int)(sizeof rows / sizeof rows[0]); k++) { rows[k].id = UINT32_MAX - 1; rows[k].atlas = rows[k].frame = -1; }
    if (i != (int)(sizeof rows / sizeof rows[0])) fprintf(stderr, "[dm] 【红】技能图标索引没读全：%%d / %%d\\n", i, (int)(sizeof rows / sizeof rows[0])); }''' % len(trows) + art[m.end():]
        stub = stub.replace('#include <stddef.h>', '#include <stddef.h>\n#include "../../host/dm_derived_rt.h"', 1); assert 'dm_derived_rt.h' in stub
        open(p_art, 'w', encoding='utf-8').write(stub)
    # 亮度表的替身只是一条"有多少行"的宏：内容不进程序，启动时从 derived.tsv 读（dm_bright.c 里填）。
    # The brightness stub is only a "how many rows" macro: the content is read from derived.tsv at startup
    # (filled inside dm_bright.c).
    open(p_bright, 'w', encoding='utf-8').write('''// 壳的替身（dm/tools/shell_stubs.py 生成）：关卡亮度表的内容不进程序，启动时从数据包的 derived.tsv 读（标签 BRT）。
// 条数随游戏版本固定，不是游戏内容；换游戏版本后要重新生成这个数字。
#define DM_BRIGHT_COUNT %d
''' % len(lv))
    return len(parts), len(rows), len(sets), len(trows), len(lv)


if __name__ == '__main__':
    args = sys.argv[1:]
    tables = None
    if '--tables' in args:
        i = args.index('--tables'); tables = args[i + 1]; del args[i:i + 2]
    if len(args) == 3 and args[0] == 'data':
        print('derived.tsv：经验 %d、部位 %d、仓库条目 %d、套装 %d、技能索引 %d、亮度 %d' % data(args[1], args[2], tables))
    elif len(args) == 2 and args[0] == 'stubs':
        print('替身已写：部位 %d、仓库条目 %d、套装 %d、技能索引 %d、亮度 %d' % stubs(args[1], tables))
    else:
        sys.exit('用法：shell_stubs.py data <dm 目录> <输出 derived.tsv> [--tables <表目录>] | '
                 'shell_stubs.py stubs <dm 目录> [--tables <表目录>]')
