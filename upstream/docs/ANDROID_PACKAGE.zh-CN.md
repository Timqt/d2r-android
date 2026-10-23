# Android 游戏数据包格式

这个包是"电脑端"与"手机端"之间的唯一契约：电脑端从玩家自己的游戏文件和密钥派生出全部运行数据，打成一个 zip；手机端在启动界面导入它就能玩。

## 为什么需要这个包

手机端跑不动流水线里的两步：

- **解包解密**需要玩家的 `prod.keys` 和 hactool。密钥只应留在玩家自己的电脑上。
- **着色器翻译**需要 .NET 10（现场 `dotnet publish` 一个引用 Ryujinx 源码的工程）。手机端搬不动这套工具链。

而**不能把全部内容塞进 APK**：RomFS 约 8.4 GB，而 `zipalign` 打不开超过 4 GB 的 zip、`apksigner` 也会在 4 GB 边界报 `Malformed ZIP Central Directory record`（都是实测）。所以 APK 保持精简，大数据走这个包。

## 目录结构

```
d2r-package.zip
├── manifest.json                 包元信息（见下）
├── game/                         派生后的游戏模块与布局
│   ├── libdmguest_main.so
│   ├── libdmguest_sdk.so
│   ├── libdmguest_subsdk0.so
│   ├── main.fixups.tsv           ┐
│   ├── main.gates.tsv            │ 模块加载布局，三套各一份
│   ├── sdk.fixups.tsv            │
│   ├── sdk.gates.tsv             │
│   ├── subsdk0.fixups.tsv        │
│   └── subsdk0.gates.tsv         ┘
├── romfs/                        游戏资源（原样）
│   └── data/*.data, *.psv
├── shaders/                      翻译后的着色器
│   ├── host-subgroup-size.txt    分组档位，必须与实际 GPU 策略一致
│   └── <key>.spv / <key>.meta
└── derived.tsv                   三张游戏派生表的内容（壳模式的 APK 里只有空表与条数）
```

`manifest.json` 示例：

```json
{
  "format": 1,
  "created": "2026-10-17T01:20:00Z",
  "source": "user-supplied game files",
  "subgroup_size": 64,
  "counts": { "game": 9, "romfs": 45, "shaders": 20795, "derived": 746 },
  "bytes": { "game": 79436432, "romfs": 8400000000, "shaders": 507000000, "derived": 24000 }
}
```

`format` 是包格式版本。手机端遇到不认识的版本必须**明确报错并拒绝导入**，不能猜。

## derived.tsv：三张游戏派生表（壳模式）

APK 里**不含**三张从游戏文件生成的表的内容（编年史仓库物品表、经验表、关卡亮度表），只带"表有多大"的条数；
内容全在这份 `derived.tsv` 里，游戏启动时按标签读进去（`dm/host/dm_derived_rt.h`，由前端把路径经 `DM_DERIVED` 交给宿主）：

| 标签 | 内容 |
| --- | --- |
| `EXP` | 每行 10 个等级的经验值，共 100 个（经验条） |
| `VTPART` / `VTROW` / `VTSET` | 编年史仓库的部位名、条目行（部位/档次/行号/等级/套装/图标键）、套装键 |
| `BRT` | 每行 `关卡号 幕 DrlgType 是否随昼夜`（动态曝光的判据） |
| `TAROW` | 技能图标索引（iOS 用；Android 包通常没有） |

包**没有** `derived.tsv` 也不会拒收（老包仍能进游戏），但那三块功能会是空的，日志里会有明确的红色告警
（"数据包没有 derived.tsv" / "表没读全"）。`build_package.py assemble --derived <文件>` 负责把它放进包，
`make_package.py` 会自动生成它。

## 手机端怎么用它

导入时**按大小分开落盘**，因为两边的限制不同：

```
内部目录（可执行）  <internalDataPath>/d2r-package/game/          模块与加载布局，约 80 MB
外部目录（大容量）  <externalDataPath>/d2r-package/romfs/         游戏资源，约 8.4 GB
                    <externalDataPath>/d2r-package/shaders/       翻译后的着色器，约 0.5 GB
                    <externalDataPath>/d2r-package/manifest.json  完成标记
```

**为什么必须分开**：外部存储是 `noexec` 挂载（实测 `/storage/emulated` 带 `noexec`），从那里 `dlopen` 会失败；
而模块必须以动态库形式装入。内部存储没有这个限制 —— 图形驱动就是从 `files/temp/driver/` 加载的，那是已验证的路径。
反过来，内部数据目录放不下 8.4 GB 的资源，所以大数据走外部。

启动时如果外部目录里有 `manifest.json`，就用它指向的这三份数据，否则回落到 APK 自带与手工推送的默认位置。
这样"导入过"和"没导入过"两条路都成立，且**不需要在配置里写任意路径**（现有配置解析器的键白名单与"不接受任意路径"的设计保持不变）。

**导入是原子的**：旧的 `manifest.json` 先删、新的最后才写。中途失败（空间不足、取消、zip 损坏）留下的是一个
没有 manifest 的目录，游戏会回落到默认数据，而不是读到半个包。

## 包不会做的事

- **不含密钥**。密钥只在电脑端解包时使用，不写进包里、不写进日志。
- **不含游戏本体**。包里是派生结果和原样资源，需要玩家自备游戏文件才能生成。
- **不校验游戏版本**。流水线刻意不做版本或摘要校验；用错版本的文件会在运行时暴露，而不是在打包时被拦住。

## 电脑端怎么生成

见 `dm/android/tools/build_package.py`（同一套代码既是命令行工具，也被 `dm/android/tools/pack_server.py` 的网页服务调用），步骤：

1. 从游戏 zip 里认出本体与更新（NSZ 先转 NSP），用玩家密钥解出 ExeFS 与 RomFS
2. 改写三个 NSO（`dm/tools/patch_nso.py`，`DM_TARGET=android`）并打成 ELF 动态库（`guest_elf.py`）
3. 从 RomFS 生成三张宿主表
4. 翻译全部着色器（`build_shaders.py`，需要 .NET 10）
5. 按上面的结构打成 zip

第 1、4 步各要几分钟到十几分钟，第 2、3 步很快；同一个工作目录里已有的产物会被复用，所以中断后重跑不必从头再来。
