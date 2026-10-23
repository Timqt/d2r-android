// 三张从游戏文件生成的表（编年史仓库 / 经验 / 关卡亮度）的内容**不编进程序**：仓库里提交的是运行时替身
// （dm/tools/shell_stubs.py 写出的 dm_hud_exp.h、dm_vault_tab.h，以及 dm_bright.c 里的亮度表），
// 启动时从数据包的 derived.tsv 读。derived.tsv 由使用者自己的游戏文件生成（dm/tools/d2rm_pack.py、
// dm/android/tools/make_package.py）：每行「标签<TAB>字段<TAB>…」，UTF-8，字段里没有制表符与换行。
// 位置：Android 由前端设 DM_DERIVED 指到导入的数据包；Mac / iOS 缺省读可执行文件旁的 dm/derived.tsv。
// The three tables generated from the game files (chronicle vault / XP / level brightness) are **not compiled in**:
// the repository holds runtime stubs (dm_hud_exp.h and dm_vault_tab.h written by dm/tools/shell_stubs.py, plus the
// brightness table inside dm_bright.c) and they are filled from the package's derived.tsv at startup.
// derived.tsv is generated from the user's own game files (dm/tools/d2rm_pack.py, dm/android/tools/make_package.py):
// each line is "tag<TAB>field<TAB>...", UTF-8, with no tabs or newlines inside fields.
// Location: Android gets DM_DERIVED pointing into the imported package; Mac / iOS default to dm/derived.tsv next to
// the executable.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(__ANDROID__)
#include <mach-o/dyld.h>
#endif
// derived.tsv 的位置：环境变量 DM_DERIVED 优先（Android 前端在导入的数据包里找到它时设置），否则退回苹果那种
// "可执行文件旁的 dm/derived.tsv"（Mac / iOS 安装包布局）。
// Where derived.tsv lives: DM_DERIVED wins (the Android front end sets it when the imported package has one);
// otherwise fall back to "dm/derived.tsv next to the executable" (the Mac / iOS install layout).
static inline const char *dm_derived_path(char *buffer, size_t size) {
    const char *given = getenv("DM_DERIVED"); if (given && *given) return given;
#if !defined(__ANDROID__)
    uint32_t capacity = (uint32_t)size; if (_NSGetExecutablePath(buffer, &capacity)) return NULL;
    char *slash = strrchr(buffer, '/'); if (!slash || (size_t)(slash - buffer) + 24 > size) return NULL;
    strcpy(slash, "/dm/derived.tsv"); return buffer;
#else
    (void)buffer; (void)size; return NULL;
#endif
}
// 对每一行标签为 tag 的记录调一次 cb（f[0..n) 是各字段，指向本函数持有的行缓冲；要留着的字符串自己 strdup）。返回这种记录的条数，读不到文件 = -1
// Calls cb once for every record line whose tag is tag (f[0..n) are the fields, pointing into the line buffer owned by this function; strdup any strings you want to keep). Returns the number of records of this kind; file cannot be read = -1
static inline int dm_derived_each(const char *tag, void (*cb)(char **f, int n, void *ctx), void *ctx) {
    char p[4096]; const char *path = dm_derived_path(p, sizeof p);
    if (!path) { fprintf(stderr, "[dm] 【红】找不到 derived.tsv：数据包里缺少它（请用新版打包器重新生成数据包）\n"); return -1; }
    FILE *fp = fopen(path, "r"); if (!fp) { fprintf(stderr, "[dm] 【红】安装包里没有 derived.tsv（%s）：这个包没有用游戏文件做全\n", path); return -1; }
    static char ln[8192]; int cnt = 0; size_t tl = strlen(tag);
    while (fgets(ln, sizeof ln, fp)) { size_t l = strlen(ln); while (l && (ln[l - 1] == '\n' || ln[l - 1] == '\r')) ln[--l] = 0;
        if (strncmp(ln, tag, tl) || ln[tl] != '\t') continue;
        char *f[16]; int n = 0; for (char *q = ln + tl + 1; n < 16; ) { f[n++] = q; q = strchr(q, '\t'); if (!q) break; *q++ = 0; }
        cb(f, n, ctx); cnt++; }
    fclose(fp); return cnt;
}
