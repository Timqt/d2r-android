# Android 构建与部署

English: [README.md](ANDROID_BUILD.md)。实际状态见[源码冻结说明](ANDROID_RELEASE_SNAPSHOT.zh-CN.md)。

## 环境

构建脚本当前面向 Linux x86_64，使用 Android NDK r28c、API35平台/构建工具和 JDK，无需 Gradle。工具默认安装至 `~/dm-work/tools/android`，可用 `DM_ANDROID_TOOLS` 指定。

```bash
python3 dm/android/tools/bootstrap.py
python3 -m venv ~/dm-work/tools/android-venv
source ~/dm-work/tools/android-venv/bin/activate
python -m pip install lz4 capstone unicorn numpy pillow zstandard nsz
```

工具首次下载需要网络。游戏内容、提取工具及 .NET 着色器翻译环境需自行准备，仓库不是开箱即玩的游戏发行包。已有测试/诊断代码仍随源码保留；此次整理不要求执行测试。

## 准备游戏模块和着色器

先按[原项目资源说明](GAME_FILES.zh-CN.md)准备自己的合法游戏文件。已有提取工具支持本体与更新合并：

```bash
python dm/android/tools/extract_packages.py BASE.nsp UPDATE.nsp PROD.keys   ~/dm-work/data/switch/3.3.94012/Update --hactool ~/dm-work/tools/hactool/hactool
```

`BASE.nsp`、`UPDATE.nsp`、`PROD.keys` 是本地路径占位符，不随仓库提供。随后对 main、sdk、subsdk0 分别按同一规则改写：

```bash
for module in main sdk subsdk0; do
  DM_TARGET=android DM_RULESET=touch-run-v4 python dm/tools/patch_nso.py     "$HOME/dm-work/data/switch/3.3.94012/Update/ExeFS/$module"     "$HOME/dm-work/build/android/game" "$module"
done
```

仅当输入确实已经应用同一登记补丁时才使用 `DM_ALLOW_PREPATCHED=1`。着色器转换需要 .NET 10 SDK（默认 `~/dm-work/tools/dotnet/dotnet`，可用官方 dotnet-install 脚本装到该路径；首次 `dotnet restore` 需要网络）：

```bash
python dm/android/tools/build_shaders.py   ~/dm-work/data/switch/3.3.94012/Update/RomFS   ~/dm-work/data/switch/3.3.94012/Update/ExeFS/main   ~/dm-work/build/android/shaders --host-subgroup-size 64
```

这里的64对应本次已验证设备配置，其他GPU必须核对实际能力。转换结果在输出目录的 `spirv/`。工具也支持 `--dotnet` / `--translator` 指定环境；详细参数由 `--help` 查看。

## 构建与安装

首次在干净源码目录构建前，需要从本地 RomFS 生成仓库、经验和场景光照表。激活上述 Python 虚拟环境后运行（`ROMFS` 指向自己的资源目录）：

```bash
make -C dm/host dm_vault_tab.h dm_hud_exp.h dm_bright_levels.inc \
  ROMFS="$HOME/dm-work/data/switch/3.3.94012/Update/RomFS"
```

这些生成文件被 Git 忽略，不随源码发布。更换游戏资源后需删除这三个生成文件，再运行上面的命令重新生成。

```bash
python dm/android/tools/build.py --game-run   --game-source ~/dm-work/build/android/game   --shader-cache ~/dm-work/build/android/shaders/spirv
```

输出 `dm/android/build/D2R-Android-port.apk`，包名 `org.d2r.android.port`。真实游戏APK包含本机派生的游戏模块，不随源码发布。

```bash
export ANDROID_SERIAL='你的设备序列号'
adb -s "$ANDROID_SERIAL" install -r dm/android/build/D2R-Android-port.apk
adb -s "$ANDROID_SERIAL" shell mkdir -p /sdcard/Android/data/org.d2r.android.port/files/romfs
adb -s "$ANDROID_SERIAL" push ~/dm-work/data/switch/3.3.94012/Update/RomFS/. /sdcard/Android/data/org.d2r.android.port/files/romfs/
adb -s "$ANDROID_SERIAL" push ~/dm-work/build/android/shaders/spirv /sdcard/Android/data/org.d2r.android.port/files/shaders
adb -s "$ANDROID_SERIAL" shell am start -n org.d2r.android.port/.LauncherActivity
```

RomFS和着色器仅首次或资源版本变化时部署；更新APK保留应用数据。首次无存档会进入建角界面。存档位于应用私有 `files/save/`，不要用其他设备存档覆盖现有角色。驱动与启动配置见[驱动说明](ANDROID_GPU_DRIVER.zh-CN.md)和[启动设置](ANDROID_LAUNCH_SETTINGS.zh-CN.md)。

## 自动流水线（GitHub Actions）

`.github/workflows/build.yml` 在每次 push / PR 上跑，产出两个可下载的制品：

| 制品 | 内容 |
| --- | --- |
| `D2R-Android-port.apk` | **通用 APK**：宿主与前端完整，但**不含游戏代码**（15 MB 量级）。游戏模块由手机端导入的数据包提供；启动界面本来就要求先导入才能启动。真机验证过：装上它 + 导入数据包即可进游戏。 |
| `d2r-package-kit-<版本>.zip` | 给玩家电脑用的**打包套件**：项目源码（打包工具在里面）+ `.claude/skills/d2r-package/` 技能 + 上面那个 APK + 一页说明。解压后把文件夹交给 AI 助手即可。 |

流程：`checkout → pip 装 lz4/capstone（make_probe 会调 patch_nso）→ bootstrap.py 装工具链（NDK/JDK/build-tools，约 800 MB，按 bootstrap.py 内容缓存）→ ci_checks.py → build.py --game-run（不带 --game-source，即"通用包"）→ make_kit.py → 上传制品`。

**发布给玩家**：推一个 tag 即可，流水线会建 Release 并把 APK 与套件挂上去：

```bash
git tag v0.2 && git push origin v0.2
```

**为什么 CI 的 APK 不含游戏数据（壳模式）**：游戏模块和三张派生表（编年史仓库 / 经验 / 关卡亮度）都得从玩家自己的游戏文件生成，仓库里没有、也不该进 CI（见 `CONTRIBUTING.zh-CN.md` 的数据边界）。所以 APK 走**壳模式**：宿主里只带"表有多大"的条数（`dm/host/dm_vault_tab.h`、`dm_hud_exp.h`、`dm_bright_levels.inc` 是随源码提交的替身），内容在启动时从数据包的 `derived.tsv` 读（路径由前端经 `DM_DERIVED` 交给宿主）。数据包由玩家那边的 `make_package.py` 生成，三张表的内容自动打进包里。这样同一个 APK 谁都能装，数据各用各的。

本机要构建内含游戏代码的包（不依赖导入），仍按上面的「构建与安装」加 `--game-source`；三张表依旧从数据包读。

**本地跑同一组检查**：`python3 dm/android/tools/ci_checks.py`（Linux 上能跑的那部分；`t_thread_leak` / `t_i18n` 依赖 macOS 专有头文件，脚本里会明确列出跳过，而不是假装全绿）。

## 启动界面

包的入口是 `org.d2r.android.port.LauncherActivity`（Java），游戏本体是它启动的第二个页面 `D2RActivity`（不导出）。清单由 `build.py` 从仓库清单改写而来：替换必须正好命中一个 NativeActivity 块，否则直接报错退出；探针构建没有 dex，所以仍用纯原生清单、不出现 Java 页面。

界面目前可用的设置是调试浮层开关与画面档位（掌机1280×720 / 主机1600×900），点“启动游戏”时写进应用私有 `files/temp/launch-settings.conf`，由 native 严格解析（键白名单、失败即中止启动，且不接受任意路径）。游戏文件与密钥两项**尚未接入**，只如实显示“未设置”。

启动画面背景图随源码发布：`dm/android/artwork/launcher-background.*`，构建时打进 APK 的 `assets/`，所以新电脑 clone 后能重建出同样的 APK。想换自己的图，把同名文件放进 `dm/android/local/`（该目录已 gitignore）即可覆盖，不必改源码。素材明显小于屏幕时代码会先缩小再放大，让放大本身把它柔化成氛围底图；换更大的图就自动变清晰。

应用图标同理但不用改源码：仓库自带一枚纯矢量自适应图标（`dm/android/res/`，深色底 + 金色菱框 + 暗红宝石，项目自画、不含游戏美术）；把 `ic_launcher.png` / `.jpg` / `.webp` 放进 `dm/android/local/` 就会在构建时替换成你自己的图（同目录已 gitignore，图只留在本机）。位图会作为自适应图标的前景层铺满 108dp、再由系统裁成圆角/圆形，所以**请用方形图**，非方图会被拉伸。构建脚本会把 `dm/android/res/` 复制进构建目录后一起交给 aapt2。

构建必须在项目虚拟环境里跑：`build.py` 用 `sys.executable` 派生子进程，而 `patch_nso.py` 需要 venv 里的 `lz4`，用系统 `python3` 会在那一步 `ModuleNotFoundError`。

## 输入与日志

支持虚拟摇杆、按键、系统键盘命名、键盘和手柄。画面按16:9等比适配，手机两侧可有留白。调试浮层显示FPS、CPU/GPU时间和等待，GPU时间是批次时间跨度，不能与CPU简单相加。浮层还显示电池行：电流、电压、按二者算出的功率和电量；数值来自框架（应用不需要 root），读不到就显示 `--`。浮层可以在启动界面关掉，关掉后调试线程跳过整块显示工作，但首帧卡死检测照常运行。

```bash
adb -s "$ANDROID_SERIAL" shell run-as org.d2r.android.port tail -100 files/game.log
```

管线缓存位于应用私有 `files/pipeline-cache/`，与存档分离；驱动缓存的重启加载与逐条预热都已在真机验证（`load=accepted`，预热清单后台重建），当前缺口是首次踏入没去过的地面时该处着色器组合仍会现编。

不带 `--game-run` / `--game-source` 的 `build.py` 构建运行时诊断APK，不能以该诊断画面当作真实游戏运行证据。
