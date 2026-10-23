# 第三方许可与出处

本文件列出本仓库**直接使用、修改或再分发**的第三方组件，以及**只作参考、代码未并入**的项目。
本仓库自身的代码以 MIT 许可发布（见 `LICENSE`）。

---

## 一、作为代码来源（vendor 进 `upstream/`，MIT）

### d2r-mobile — 本项目最初的灵感来源

- 仓库：<https://github.com/jhaizhou-ops/d2r-mobile>
- 许可：MIT，Copyright (c) 2026 jhaizhou
- 说明：iPhone / iPad / Mac / Android / 鸿蒙 上的《暗黑破坏神 II：狱火重生》兼容层。
  **本项目的思路源于它**，NVN 截获、宿主结构、触屏与手柄框架都源自这条线。
- 许可文件：`upstream/LICENSE`

### d2r-android — 本仓库 `upstream/` 的直接来源

- 仓库：<https://github.com/sydeEvans/d2r-android>
- 固定 commit：`5d8e32210b6d54bc0f8140865332a8616766779f`
- 许可：MIT，Copyright (c) 2026 jhaizhou
- 说明：把 d2r-mobile 移植到安卓的版本。**`upstream/` 就是它的快照**（已含安卓端改动，
  不需要额外打补丁），本项目在它之上继续做真机适配。
- 对照关系见 [`UPSTREAM.md`](UPSTREAM.md)

---

## 二、随包分发的二进制（`upstream/dm/android/prebuilt/turnip/`）

### Mesa Turnip

- 仓库：<https://gitlab.freedesktop.org/mesa/mesa>
- 许可：MIT（Mesa）
- 构建：T30 Adreno 版本，作者 [@Mr_Purple_666](https://gitlab.freedesktop.org/mesa/mesa)，
  文件 `vulkan.purple.so`，版本 `26.3.0-T30-1.4.359`
- 说明：**原样**放在 `upstream/dm/android/prebuilt/turnip/` 并附其 `meta.json`。
  启动界面可以在「系统驱动 / 内置 Turnip」之间切。
- 许可文件：`upstream/dm/android/prebuilt/turnip/`（见 `meta.json` 指向的上游许可）

### AdrenoTools（`libadrenotools.so` / `libhook_impl.so` / `libmain_hook.so`）

- 仓库：<https://gitlab.freedesktop.org/mesa/mesa> 之外的独立项目
  AdrenoTools（<https://github.com/obfusk/adrenotools>）
- 许可：MIT
- 说明：APK 内的 hook 库，让 Android Vulkan loader 去枚举应用的 `nativeLibraryDir` 找 ICD。
  是"驱动做进 APK"这一步的关键，源码不在本仓库（随上游二进制提供）。

---

## 三、只作参考、代码未并入

### Eden（Switch 模拟器）

- 仓库：<https://git.eden-emu.dev/eden-emu/eden>
- 许可：**GPL-3.0**
- 说明：**仅作设计参考**。本项目参考了它的异步调度（`vk_scheduler.cpp`）、完成序号
  （`vk_master_semaphore.cpp`）、上传缓冲池（`vk_staging_buffer_pool.cpp`）与 Android 驱动装载
  （`android/app/src/main/jni/native.cpp`）。
  **它的着色器翻译是 GPL-3.0，没有并入本 MIT 项目** —— 这一点在仓库里是刻意守住的边界。
- 参考的具体入口记在 `upstream/docs/ANDROID_GPU_REVIEW.zh-CN.md`

### LASRecomp

- 仓库：<https://github.com/GPUOpen-Effects/LASRecomp>
- 许可：Apache-2.0
- 说明：超分/重建的思路参考，代码未并入。

### AMD FSR 1.0 / 高通 SGSR

- FSR：<https://github.com/GPUOpen-Effects/FidelityFX-FSR>（MIT）
- SGSR：<https://github.com/SnapdragonGameStudios/snapdragon-gsr>（BSD-3-Clause）
- 说明：超分方案调研（`upstream/docs/ANDROID_UPSCALING_RESEARCH.zh-CN.md`），代码未并入。

---

## 四、`upstream/dm/third_party/` 下的库

| 目录 | 上游 | 许可 |
| --- | --- | --- |
| `ryujinx/` | <https://github.com/Ryujinx/Ryujinx> | MIT |
| `libdeflate/` | <https://github.com/ebiggers/libdeflate> | MIT |
| `bcdec/` | <https://github.com/iOrange/bcdec> | MIT（部分函数参考 <https://gist.github.com/rygorous/2144712>） |
| `flip/` | <https://github.com/Flipboard/FLIP> | MIT（工具目录里的 stb_image / tinyexr / miniz 各自保留原许可） |

---

## 五、本仓库自带的第三方工具

| 文件 | 来源 | 许可 |
| --- | --- | --- |
| `adapt/zipalign.py` | 本仓库自己写的纯 Python ZIP 重写器（对齐到 4096，padding 放进 extra field `0xD935`） | MIT |
| `adapt/vk_shim3.c` | 本仓库自己写的转发 shim | MIT |
| `build-tools-win/sources/libadrenotools-*` | AdrenoTools（见上） | MIT |
| `build-tools-win/sources/liblinkernsbypass-*` | <https://github.com/libxdp/liblinkernsbypass> | Apache-2.0 / MIT |

---

## 六、**不**随本仓库分发的内容

| 内容 | 原因 |
| --- | --- |
| 任何游戏资源、RomFS、`.nsp` / `.nsz` / `.xci`、游戏密钥（`prod.keys` / `title.keys`） | 版权与许可证；玩家必须用自己的正版介质生成数据包 |
| 高通 Adreno 840 驱动的专有二进制（`drivers/Qualcomm_840/*.so`） | 专有二进制，无再分发授权。见 `drivers/Qualcomm_840/README.md` |
| 包含上述专有驱动的成品 APK | 同上 |

`.gitignore` 里对 `*.keys` / `prod.keys` / `title.keys` / `switch key/` / `*.apk`（构建产物）/
`*.nsp` / `*.nsz` 有明确排除。

---

## 七、商标

《暗黑破坏神 II：狱火重生》与《术士君临》是暴雪娱乐的商标与作品；
《任天堂 Switch》是任天堂的商标。本项目是**非官方**的兼容层，
与暴雪娱乐、任天堂及其关联方无隶属关系，也未获得它们的背书。
