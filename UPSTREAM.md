# Upstream

本仓库的 `upstream/` 是**上游安卓移植的快照**，上面再叠一层本项目的真机适配。
先说清三层关系：

| 层 | 仓库 | 许可 | 角色 |
| --- | --- | --- | --- |
| **灵感来源** | [jhaizhou-ops/d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile) | MIT，Copyright (c) 2026 jhaizhou | iPhone / iPad / Mac / Android / 鸿蒙 的兼容层。**本项目最初的念头来自这里**：自己买的 Switch 版游戏，在自己的手机/平板上原生跑起来 |
| **直接上游** | [sydeEvans/d2r-android](https://github.com/sydeEvans/d2r-android) | MIT，Copyright (c) 2026 jhaizhou | 把 d2r-mobile 移植到安卓的版本。**`upstream/` 就是它的快照**（固定 commit `5d8e32210b6d54bc0f8140865332a8616766779f`） |
| **本项目** | 本仓库 | MIT | 在上面的基础上做**真机适配**：Adreno 650 的驱动注入与转发 shim、Mali（天玑/Immortalis）的整套兼容与踩坑修复、触屏/手柄/帧率等 |

## `upstream/` 里到底是什么

它**不再与上游逐字节相同**：安卓端的改动已经直接落在树里，所以 clone 本仓库就能构建，
不需要额外打补丁。

| 改了什么 | 在哪 |
| --- | --- |
| 手柄接管 / 帧率档 / 崩溃落盘等安卓端改动 | `adapt/android-gamepad-and-frame-rate.patch`（15 个文件） |
| **Mali（天玑 / Immortalis）适配**：BC 贴图走 CPU 解码、`subgroup=16`、LocalSize Z 折叠 | `upstream/dm/android/native/`、`upstream/dm/gfx/tex/nvn_bcdec.c` |
| **Mali 上 3D 场景全黑的修复**：折叠时同步改写着色器手算的 z 索引 | `upstream/dm/android/native/nvn_vulkan_zfold.inc`（新）、`upstream/dm/android/native/nvn_vulkan_shader.inc`（改） |
| 上述修复的独立测试 | `upstream/dm/android/tests/zfold_test.c`（新） |
| Mali 的实测限制与踩坑记录 | `upstream/docs/ANDROID_MALI_LIMITS.zh-CN.md`（新） |

驱动注入、Vulkan 转发 shim、APK 改包这些**不在** `upstream/` 里，而在 `adapt/`，
最后进到打好的 APK。

## 怎么看出与上游的差异

```bash
git clone https://github.com/sydeEvans/d2r-android /tmp/upstream
diff -ru /tmp/upstream upstream/
```

## 怎么从干净的上游重新生成

```bash
cd /tmp/upstream
git apply /path/to/this/repo/adapt/android-gamepad-and-frame-rate.patch
```

该补丁对上游 `5d8e322` 适用，对本仓库的 `upstream/` 反向也能干净应用，
所以两者保证一致。**本项目在这之上的改动没有单独的补丁文件** —— 它们就在树里，
用上面的 `diff -ru` 就能全部看出来（比逐个补丁更容易读）。
