/*
 * libd2rvlk.so for the D2R Android port (Adreno 650 / Snapdragon 865).
 *
 * Why this exists
 * ---------------
 * gpu_driver.c:open_system() ends at:
 *     d->library = dlopen("libd2rvlk.so", RTLD_NOW | RTLD_LOCAL);   <- renamed
 * and from there the port resolves EVERY Vulkan entry point through
 * dlsym(handle, "vkGetInstanceProcAddr") followed by GIPA -- DM_VK_GLOBAL_FUNCTIONS
 * expands to exactly one symbol (vkCreateInstance), and load_globals/load_instance/
 * load_device all go through the same handle. Nothing ever links a Vk entry point
 * directly (the source comments say so explicitly).
 *
 * The soname cannot stay "libvulkan.so": that is a platform library
 * (/system/lib64/libvulkan.so), and the linker resolves a platform soname to the
 * platform copy, which then picks the 2021 ICD from /vendor/lib64/hw/ and fails with
 *     FATAL Vulkan pipeline_create_compute failed: -13
 * Verified empirically: shipping a libvulkan.so of our own changed nothing. An app
 * only soname is resolved from the app's own lib/arm64-v8a, so libdmandroid.so's
 * "libvulkan.so" string is rewritten (length-preserving) to this name.
 *
 * What this shim actually has to do
 * ---------------------------------
 * Forwarding straight to vulkan.adreno.so does NOT work. That library exports no
 * unmangled Vulkan symbol at all: its .dynsym is 434 C++ mangled qglinternal::
 * definitions over 270 undefined ones, with no vkGetInstanceProcAddr, no
 * vkCreateInstance and not a single vk_icd* entry point. It is meant to be found by
 * the Android Vulkan loader, never dlopen'd and dlsym'd by hand.
 *
 * The loader is what finds it. libadrenotools -- shipped inside this very APK as
 * lib/arm64-v8a/libadrenotools.so -- installs libhook_impl/libmain_hook, which make
 * the loader enumerate the app's nativeLibraryDir for ICDs. So the real job here is
 * to ask libadrenotools for a loader handle, exactly the way the port's own
 * custom-driver branch does at gpu_driver.c:133:
 *     open(RTLD_NOW, 0, NULL, hookLibDir, NULL, NULL, NULL, NULL)
 * and then forward GIPA to that handle.
 *
 * This is what removes the need for gpu-driver.conf. That file exists only so the
 * port takes the custom branch, and the custom branch is the only place that assigns
 * d->adrenotools (gpu_driver.c:152). Calling libadrenotools from here hands the system
 * branch the same loader, so no config file, no chattr +i and no Magisk module are
 * needed. The driver files (vulkan.adreno.so, notgsl.so, notllvm-glnext.so,
 * notllvm-qgl.so, notadreno_utils.so, libdmabufheap.so) ride along in the same
 * lib/arm64-v8a directory, which is the hookLibDir the loader searches.
 *
 * Finding hookLibDir
 * ------------------
 * libadrenotools' header is explicit that hookLibDir MUST be what
 * getApplicationInfo().nativeLibraryDir returns, or the hook silently falls back to
 * the vendor driver. The port already resolved and published that value at
 * frontend_activity.c:219 (dladdr on its own boot() symbol, then setenv), so
 * DM_ANDROID_LIBDIR is already in the environment by the time the GPU driver opens --
 * it carries NO trailing slash there and gpu_driver.c:108 adds one. dladdr is kept as
 * a fallback because the port itself uses it, so it is guaranteed resolvable here.
 *
 * Do NOT use syscall() to read /proc/self/maps: bionic does not export syscall(), so a
 * weak reference resolves to NULL and the first call jumps to address 0
 * (observed: CRASH sig=11 addr=0 pc=0 inside vkGetInstanceProcAddr).
 *
 * Loading is lazy on purpose. Doing it from an ELF constructor breaks the linker: the
 * constructor already runs INSIDE dlopen("libd2rvlk.so"), and dlopen()ing
 * libadrenotools.so from there re-enters it (observed as CRASH sig=11 addr=0 pc=0 with
 * nativeLibraryDir live in the registers, backtrace dlopen+0x18 -> our text). The port
 * calls dlsym() and then GIPA right after dlopen returns, so the first
 * vkGetInstanceProcAddr() call is early enough.
 *
 * dlopen/dlsym/dladdr/getenv are weak plus a DT_NEEDED on libdl.so: -nostdlib leaves
 * them with no scope to resolve against, and a STRONG undefined symbol makes RTLD_NOW
 * refuse to load the library ("cannot locate symbol dlopen").
 */
#define __WEAK __attribute__((weak))
typedef unsigned long dm_size;

extern void *__WEAK dlopen(const char *, int);
extern void *__WEAK dlsym(void *, const char *);
extern char *__WEAK getenv(const char *);
extern int __WEAK __android_log_print(int, const char *, const char *, ...);
extern int __WEAK dladdr(void *, void *);

/* first four members of bionic's Dl_info: fname, fbase, sname, saddr */
struct dm_dl_info { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; };

#define DM_RTLD_NOW    2
#define DM_RTLD_LOCAL  0

#define DM_LOG_TAG     "d2rshim"
#define DM_ANDROID_LOGD 3

typedef void *(*dm_open_fn)(int, int, const char *, const char *,
                            const char *, const char *, const char *, void **);
typedef void *(*dm_gipa_fn)(void *, const char *);

static const char dm_ATOOLS[] = "libadrenotools.so";

/* dladdr needs an address that lives in this object; forward declare the export. */
void *vkGetInstanceProcAddr(void *instance, const char *name);

static dm_gipa_fn dm_loader_gipa;
static char dm_libdir[1024];
static int dm_in_load;

static void dm_log(const char *fmt, const char *a, const char *b) {
    if (__android_log_print) __android_log_print(DM_ANDROID_LOGD, DM_LOG_TAG, fmt, a, b);
}

static dm_size dm_strlen(const char *s) { dm_size n = 0; while (s[n]) n++; return n; }

/* nativeLibraryDir plus the trailing slash the port adds at gpu_driver.c:108. */
static void dm_locate_libdir(void) {
    const char *env;
    struct dm_dl_info info;
    dm_size n;
    if (dm_libdir[0]) return;
    env = getenv ? getenv("DM_ANDROID_LIBDIR") : 0;
    if (env) {
        n = dm_strlen(env);
        if (n + 2 > sizeof dm_libdir) return;
        for (dm_size i = 0; i < n; i++) dm_libdir[i] = env[i];
        dm_libdir[n] = '/';
        dm_libdir[n + 1] = 0;
        dm_log("shim: libdir from env //", dm_libdir, "");
        return;
    }
    if (!dladdr) return;
    if (!dladdr((void *)(void *)&vkGetInstanceProcAddr, &info) || !info.dli_fname) return;
    n = dm_strlen(info.dli_fname);
    while (n > 0 && info.dli_fname[n - 1] != '/') n--;
    if (!n || n + 2 > sizeof dm_libdir) return;
    for (dm_size i = 0; i < n; i++) dm_libdir[i] = info.dli_fname[i];
    dm_libdir[n] = 0;
    dm_log("shim: libdir from dladdr //", dm_libdir, "");
}

static void dm_load_loader(void) {
    char path[1200];
    dm_open_fn open;
    void *atools, *loader;
    dm_size n, m;
    if (dm_loader_gipa || dm_in_load) return;
    dm_in_load = 1;
    dm_log("shim: start dlopen=%s //", (const char *)dlopen, "");
    if (!dlopen || !dlsym) { dm_in_load = 0; return; }
    dm_locate_libdir();
    dm_log("shim: libdir=%s //", dm_libdir, "");
    if (!dm_libdir[0]) { dm_in_load = 0; return; }
    n = dm_strlen(dm_libdir);
    if (n + sizeof dm_ATOOLS > sizeof path) { dm_in_load = 0; return; }
    for (m = 0; m < n; m++) path[m] = dm_libdir[m];
    for (m = 0; dm_ATOOLS[m]; m++) path[n + m] = dm_ATOOLS[m];
    path[n + m] = 0;

    atools = dlopen(path, DM_RTLD_NOW | DM_RTLD_LOCAL);
    dm_log("shim: adrenotools=%s //", (const char *)atools, "");
    if (!atools) { dm_in_load = 0; return; }
    open = (dm_open_fn)dlsym(atools, "adrenotools_open_libvulkan");
    dm_log("shim: open=%s //", (const char *)open, "");
    if (!open) { dm_in_load = 0; return; }
    /* featureFlags = 0: no custom driver, the hooks alone make the loader pick up the
     * ICD that ships next to this file. tmpLibDir = NULL is fine on API 31 -- the
     * memfd path -- libadrenotools only needs a writable directory below API 29. */
    loader = open(DM_RTLD_NOW, 0, (const char *)0, dm_libdir,
                  (const char *)0, (const char *)0, (const char *)0, (void **)0);
    dm_log("shim: loader=%s //", (const char *)loader, "");
    if (!loader) { dm_in_load = 0; return; }
    dm_loader_gipa = (dm_gipa_fn)dlsym(loader, "vkGetInstanceProcAddr");
    dm_log("shim: gipa=%s //", (const char *)dm_loader_gipa, "");
    dm_in_load = 0;
}

__attribute__((visibility("default")))
void *vkGetInstanceProcAddr(void *instance, const char *name) {
    if (!dm_loader_gipa) dm_load_loader();
    if (!dm_loader_gipa) return 0;
    return dm_loader_gipa(instance, name);
}

/* Some paths read physical device properties straight off the handle; forward it so a
 * caller that dlsym()s this name does not silently get NULL. */
typedef void (*dm_gdp2_fn)(void *, void *);
__attribute__((visibility("default")))
void vkGetPhysicalDeviceProperties2(void *physicalDevice, void *properties) {
    dm_gdp2_fn fn;
    if (!dm_loader_gipa) dm_load_loader();
    if (!dm_loader_gipa) return;
    fn = (dm_gdp2_fn)dm_loader_gipa(0, "vkGetPhysicalDeviceProperties2");
    if (fn) fn(physicalDevice, properties);
}
