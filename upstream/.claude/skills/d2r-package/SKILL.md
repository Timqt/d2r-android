---
name: d2r-package
description: 用玩家自己的游戏文件和密钥生成一个可导入的 Android 数据包（d2r-package.zip），供 D2R Android 移植的启动界面导入。当用户要求"打包/生成数据包/让手机能玩/处理游戏文件 zip/用 prod.keys 解包"，或提到游戏跑不起来是因为缺少数据包时使用。也用于把已有的解包结果重新汇编成包（--reuse）。
---

# 生成 D2R Android 数据包

手机端跑不动这条流水线里的两步：**解包解密**需要 `prod.keys` 和 hactool（密钥只应留在玩家自己电脑上），
**着色器翻译**需要 .NET 10（手机端搬不动这套工具链）。所以大块数据走一个包：电脑端生成，手机端导入。
包格式的完整说明在 `docs/ANDROID_PACKAGE.zh-CN.md`。

## 一条命令

**必须用项目的 Python 虚拟环境跑**：`patch_nso` 需要 venv 里的 lz4/capstone，而子进程继承 `sys.executable`，
用系统 `python3` 会在那一步 `ModuleNotFoundError`。

```bash
~/dm-work/tools/android-venv/bin/python dm/android/tools/make_package.py \
  <游戏包> <密钥> [--output <输出路径>] [--workspace <中间产物目录>]
```

- `<游戏包>`：玩家自备的 zip，或一个**放着本体与更新两个包**的目录 —— 两种形式里装的都是**本体与更新**，
  文件就是 `.nsz`/`.nsp`。两个都必须给：只给一个包认不出谁是本体谁是更新。
- `<密钥>`：`prod.keys`，或一个里面装着它的 zip（例如 `ProdKeys.-*.zip`）。
- 输出默认 `~/dm-work/pack-workspace/d2r-package.zip`。

## 工具会自己认出来的事（不要替它假设文件名）

- **本体 vs 更新**：更新包带**非零**版本号（如 `v2228224`），本体是剩下里**最大**的那个。
  名字里写 `DLC` 的直接排除。
  ⚠️ **本体自己的名字里也带 `[v0]`** —— 只看"有没有版本号"会把本体误判成更新。工具要求版本号非零，
  就是这个原因；改动这段判断时别退回去。
- **NSZ 要转 NSP**：解包工具只吃 NSP，工具会先调 `nsz -D` 转。这一步当年是手工做的。
- **密钥**可能是裸文件，也可能在 zip 里。
- 实在分不出本体/更新时会**明确报错并列出看到的文件**，而不是随便挑一个往下跑。这时让用户改文件名。

## 耗时与复用

最慢的两步是 **NSZ→NSP** 和 **着色器翻译（6931 个，走 .NET）**，整条流水线在一小时量级；
解包要读写成 GB 的数据。跑完中间产物留在工作目录，**重跑时加 `--reuse`** 可跳过最慢的两步，只做汇编（几分钟）：

```bash
~/dm-work/tools/android-venv/bin/python dm/android/tools/make_package.py \
  <游戏包> <密钥> --reuse ~/dm-work/pack-workspace/produce
```

`--reuse` 不做游戏版本校验 —— **复用了不匹配的数据会在游戏里暴露，而不是在这里被拦住**，
所以只在确认是同一份游戏文件时用。

## 验证产物

```bash
~/dm-work/tools/android-venv/bin/python dm/android/tools/build_package.py inspect <包路径>
```

应当看到 `format: 1`、`subgroup_size`、以及 `counts` 里 romfs 与 shaders 都非零。缺模块、缺分组档位、
包格式版本不认识都会明确报错。

## 送到手机

把包拷到手机（放 `Download/` 或任意能被文件选择器看到的位置），在**启动界面点"导入数据包"**选它。
导入会解到应用外部私有目录的 `d2r-package/`，**解完最后才写 `manifest.json`**，所以中途失败不会让游戏读到半个包。
游戏启动时按"有没有 manifest.json"决定用导入的数据还是回落到默认位置。

### 用 adb 直接装到手机（能连上数据线或无线调试时，先问用户愿不愿意）

玩家下载到的套件（`d2r-package-kit-*.zip`）根目录里就有 `D2R-Android-port.apk`；
在仓库里工作时它是 `dm/android/build/D2R-Android-port.apk`。确认其中哪一个存在后用：

```bash
# 1) 让用户在手机上开「开发者选项 → USB 调试」（或「无线调试」并给出配对码），然后确认连接：
adb devices                      # 出现 unauthorized 就让用户在手机上点「允许」
# 2) 安装应用（这个 APK 不含游戏代码，游戏数据由下面的包提供）：
adb install -r <上面确认过的 APK 路径>
# 3) 把数据包推到手机（文件选择器能看到的位置）：
adb push <包路径> /sdcard/Download/d2r-package.zip
# 4) 打开启动界面，剩下的由用户在手机上点：
adb shell am start -n org.d2r.android.port/.LauncherActivity
```

第 4 步之后，请用户在启动界面点「导入数据包」→ 选 `Download/d2r-package.zip` → 导入完成后点「启动游戏」。
**最后这两下必须由用户在手机上点**：系统文件选择器不接受 adb 代选，包要由用户选一次，应用才会把它原子换入。
**不要**用 `run-as` 手工往私有目录塞文件来"跳过导入" —— 那会绕过原子换入，失败时可能留下半个包。

如果 `adb devices` 里什么都没有：先问用户线是否插好 / 无线调试是否在同一网络，别自己反复重连。

## 边界（务必遵守）

- **密钥绝不写进日志、包或仓库**。`extract_packages.py` 已经刻意不回显密钥相关输出，别在它外面再打一遍。
- **不要把数据包、解出的游戏数据、翻译后的着色器提交进仓库**。仓库声明不含游戏内容与派生物，
  `docs/ANDROID_PACKAGE.zh-CN.md` 与 `README.zh-CN.md` 都写了这一点。
- **分组档位默认 64** 是已验证的设备配置。换 GPU 必须核对实际能力，不能照搬：
  `build_shaders.py --host-subgroup-size` 与包里的 `host-subgroup-size.txt` 必须与实际策略一致，
  否则运行时会在着色器档位检查处直接终止。

## 前置条件（缺哪个就先补哪个）

| 需要 | 默认位置 | 缺失时的表现 |
| --- | --- | --- |
| 项目 venv（lz4/capstone/nsz 等） | `~/dm-work/tools/android-venv` | 某一步 `ModuleNotFoundError` |
| hactool | `~/dm-work/tools/hactool/hactool` | 解包失败 |
| .NET 10 SDK | `~/dm-work/tools/dotnet/dotnet` | 着色器翻译失败 |
| 玩家的游戏文件与密钥 | 用户自备 | 本工具链不提供 |

三者都可用环境变量覆盖：`DM_HACTOOL` / `DM_DOTNET` / `DM_NSZ`（工具目录另有 `DM_ANDROID_TOOLS`）。
