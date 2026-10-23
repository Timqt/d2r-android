# 在 Windows 上构建上游 APK（上游 build.py 是 Linux-only）

日期：2026-10-20。为了把安卓端的手柄 / 触屏接管权改动（`adapt/android-gamepad-and-frame-rate.patch`）编进 APK，
在这台 Windows 机器上把上游 `dm/android/tools/build.py` 跑通了。上游的构建脚本按 Linux 写死，
下面是把差异全部挪到**本机这一层**、不改上游任何文件的做法。

## 为什么不能直接跑

上游 `build.py`（及 `host_link.py` / `build_gpu_driver.py` / `build_gpu_fixtures.py`）里写死了
Linux 形态的工具路径与脚本形态：

| 上游写死的东西 | Linux 上是什么 | Windows 上要怎么办 |
| --- | --- | --- |
| `ndk/toolchains/llvm/prebuilt/linux-x86_64/bin` | Linux 版 NDK 目录 | 建一个同名**假目录**：bin 里放 Windows NDK 的工具副本（连 DLL 一起，工具启动要靠旁边的 DLL），`lib/` 与 `sysroot/` 用 junction 指回真 NDK |
| `build-tools/d8`、`build-tools/apksigner` | 无扩展名的 shell 脚本 | Windows 上是 `.bat`；`.bat` 本身能被 CreateProcess 直接调起（Python subprocess 验证过），只要路径写对后缀 |
| `Path.exists()` 查工具 | 有就是有 | Windows 上**不带扩展名**的路径 `exists()` 是 False，而 CreateProcess 会自动补 `.exe` → 每个工具都要额外放一份**无扩展名**的副本 |
| `import fcntl` | POSIX 才有 | 给 Python 一个 `fcntl` 替身（上游只用 `flock` 做构建互斥锁） |
| 源码与日志里的中文 | UTF-8 | Windows 上 Python 默认编码是 GBK → 整个构建要开 `PYTHONUTF8=1` |

## 目录

下面三样东西在**本仓库之外**（在你的工作区根目录 `<WORK>/`），是 Windows 上的本机布局：

```
<WORK>/build-tools-win/                 假工具树（DM_ANDROID_TOOLS 指向这里）
├── ndk/toolchains/llvm/prebuilt/linux-x86_64/
│   ├── bin/                           Windows NDK 的工具副本 + 各目标三元组的副本（clang 按 argv[0] 定目标）
│   ├── lib/ → junction → 真 NDK 的 lib
│   └── sysroot/ → junction → 真 NDK 的 sysroot
├── ndk/shader-tools/linux-x86_64/glslc.exe
├── jdk/                               Temurin 21 的**拷贝**（不是 junction：要在旁边放无扩展名副本）
├── platform/android.jar               API 35
├── build-tools/                       aapt2 / d8.bat / zipalign / apksigner.bat + 无扩展名副本
├── platform-tools/adb
└── pyshim/fcntl.py                    Windows 上的 fcntl 替身
<WORK>/build-win.py                    把 build.py 当模块加载、拦一层 run() 补 .bat 后缀；上游文件一个字不动
<WORK>/pack-workspace/setup-win-tools.ps1  生成上面那棵假工具树
```

## 跑法

```powershell
$env:DM_ANDROID_TOOLS = '<WORK>\build-tools-win'
$env:PYTHONUTF8 = '1'
$env:PYTHONPATH   = '<WORK>\build-tools-win\pyshim'
& '<WORK>\tools\android-venv\Scripts\python.exe' `
  '<WORK>\build-win.py' --game-run --version v0.6.57
```

> `--version` 给 `0.6.57` 这样的**纯数字**，构建脚本会自己拼上 `-android-port` 后缀；
> `versionCode` 按 `0*10000 + 6*100 + 57 = 657` 算，比已装的旧版本号大才能 `pm install -r` 原地升级。

`--game-run` 但不给 `--game-source`：**宿主与前端是真的、游戏模块由手机端导入的数据包提供** ——
这正是本仓库发布的那种通用 APK（仓库里的 `apk/D2R-Android-port.apk` 就是它，
原构建输出在 `releases/v0.6.1/`）。

## NDK 必须是 r28c

上游 `bootstrap.py` 钉的是 **r28c**。用 NDK 26.1（LLVM 17）会在这里断掉：

```
ld.lld: error: undefined hidden symbol: dm_android_context_storage
```

原因：`dm/android/native/runtime.c` 里那个 `__thread` 变量带 hidden 可见性，LLVM 17 把它编成
**模拟 TLS**（符号名变成 `__emutls_v.dm_android_context_storage`），而汇编入口
（`generate_entry.py` 生成的 `entry_elf.S`）按 **TLS 描述符模型**引用它
（`:tlsdesc:` / `.tlsdesccall`）→ 链接时找不到那个符号。r28c（LLVM 19）下同一处是原生 TLS：

```
28: 0000000000000000 66632 TLS     GLOBAL HIDDEN      5 dm_android_context_storage
```

所以 Windows 版 NDK 要下 `android-ndk-r28c-windows.zip`（约 713 MB），
`build-tools-win/ndk28/android-ndk-r28c/`。

## Windows 上的两个坑

- **PowerShell 5.1 按 GBK 读无 BOM 的 UTF-8 脚本**，中文字节会被吃掉换行（脚本行号对不上、
  变量莫名其妙为空）。`.ps1` 一律**带 BOM 写**（或全 ASCII）。
- **删除命令受限**：永久删除被安全策略挡住；可恢复删除要用顶层 `rm -- "<字面量路径>"`
  （本仓库内的路径才行，变量形式会被拒）。
