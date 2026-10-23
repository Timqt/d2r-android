# 适配层说明

驱动注入、转发 shim、APK 重打包都在这个目录；安卓端对上游源码的改动则由
`android-gamepad-and-frame-rate.patch` 记录（补丁已打进 `upstream/` 树，
仓库克隆下来即可直接构建）。

## 文件

| 文件 | 作用 |
|---|---|
| `vk_shim3.c` | 转发 shim 源码。`dlopen` 应用目录里的 `libadrenotools.so`，调 `adrenotools_open_libvulkan(RTLD_NOW, 0, NULL, hookLibDir, ...)` 拿到真 loader，导出 `vkGetInstanceProcAddr` 转发。 |
| `build-vkshim3.sh` | 在 debian 容器里交叉编译 shim 为 `libd2rvlk.so`（SONAME 同名，`DT_NEEDED libdl.so` 由空壳 stub 骗出）。 |
| `prebuilt/libd2rvlk.so` | 上面的编译产物（66,832 字节），已随仓库带上，省掉交叉编译这一步。 |
| `patch-apk.py` | 对上游打出来的原版 APK 做三件事：删 `assets/turnip/`、在 `lib/arm64-v8a/` 注入 6 个驱动文件、把 `libdmandroid.so` 偏移处的 `libvulkan.so\0` **等长**替换为 `libd2rvlk.so\0`。路径默认取本仓库布局，可用 `D2R_SRC_APK` / `D2R_DRIVER_DIR` / `D2R_SHIM` / `D2R_OUT` 覆盖。 |
| `zipalign.py` | 纯 Python 的 zipalign 等价实现：把每个条目的数据偏移对齐到 4096，padding 放本地头 extra field（id `0xD935`）。zipalign 是 Linux 二进制，容器里装 openjdk 太慢，所以自己写。 |
| `elfsym.py` | ELF 动态符号表分析工具。本仓库里它是最关键的取证工具——没有它就看不出 `vulkan.adreno.so` 一个裸 `vk*` 都没有。 |
| `install-and-verify.sh` | 装机 → 清掉旧机制的残留（`gpu-driver.conf`、`temp/driver`、Magisk `d2r_driver`）→ 冷启动 → 校验驱动身份与进程存活。 |
| `verify-runtime.sh` | 运行期健康检查：进程、FATAL/CRASH 计数、帧计数增量、kgsl 故障、音频状态。 |
| `remove-magisk-driver-module.sh` | 备份并删除旧的 `d2r_driver` Magisk 模块（备份 tar 留在手机 `/data/local/tmp/`）。 |
| `print-state.sh` | 打印当前完整状态：已装 APK、数据完整性、旧机制残留、bind mount 的 `st_dev`、运行时状态。 |
| `android-gamepad-and-frame-rate.patch` | 安卓端对上游源码的全部改动（15 个文件）：手柄 / 触屏接管权、帧率档、崩溃现场落盘。文件头有逐条说明。 |
| `magisk-d2r-bind/` | **仍需保留在手机上的** Magisk 模块。只做 f2fs 双层 bind mount，跟驱动无关；应用无 root 做不了。 |

## shim 的三个关键约束

1. **弱符号全判空**。`-nostdlib` 下所有 libc 符号必须 `__attribute__((weak))` 且不定义；
   强未定义符号会让 `RTLD_NOW` 直接拒绝加载（`cannot locate symbol "dlopen"`）。
2. **不能在 ELF 构造函数里 `dlopen`**。构造函数本身就跑在 `dlopen` 内部，再嵌一层会重入链接器
   （`CRASH sig=11 addr=0 pc=0`）。必须惰性初始化。
3. **不要用 `syscall()`**。bionic 根本不导出它，弱声明会解析成 NULL，第一次调用就跳到地址 0。
   要拿库目录用 `getenv("DM_ANDROID_LIBDIR")`（端口在 `frontend_activity.c:219` 设好的）
   或 `dladdr`（端口自己也在用）。

## 驱动完整性校验

```bash
python elfsym.py drivers/Qualcomm_840/vulkan.adreno.so "^(vk[A-Z]|_vk)"
```

输出只有表头，没有一条匹配：

```
=== drivers/Qualcomm_840/vulkan.adreno.so ===
  size=5797912  SONAME/DT_NEEDED=libcutils.so, libz.so, notadreno_utils.so, ...
  total dynamic symbols: 704
  DEFINED: 434   UNDEFINED: 270
```

**一条都没打出来，就是证据本身**：这个库里没有任何裸 `vk*`、没有任何 `vk_icd*`，
所以它只能被 Vulkan loader 发现，不能被手写 shim 直接 dlsym —— shim 必须借道
`libadrenotools` 让 loader 去加载它。

（换个能匹配上的过滤串就能看到那 434 个定义确实是 C++ mangled 的
`_ZN11qglinternal...`，例如 `^_ZN11qglinternal`。）

