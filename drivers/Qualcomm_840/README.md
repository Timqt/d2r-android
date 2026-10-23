# Qualcomm Adreno 840 驱动（**不随仓库分发**）

这个目录是 `adapt/patch-apk.py` 的**驱动输入位**。里面应该有 6 个文件：

| 文件 | 作用 | 大小 |
| --- | --- | --- |
| `vulkan.adreno.so` | Vulkan ICD 本体 | 5,797,912 |
| `notllvm-qgl.so` | 高通 GL→Vulkan 实现 | 27,074,080 |
| `notllvm-glnext.so` | | 2,343,640 |
| `notgsl.so` | | 2,243,184 |
| `notadreno_utils.so` | | 130,616 |
| `libdmabufheap.so` | 安卓 12 起从系统里移除，需单独补回 | 87,488 |

`meta.json` 是驱动包自带的元数据（保留在仓库里，用于说明这是哪个包）。

## 为什么这些二进制不在仓库里

它们是**高通的专有二进制**（`meta.json` 写明了出处：Meta Ray-Ban Display 的驱动包，作者 Hugo），
**没有再分发授权**。公开发布它们会带来版权风险，所以本仓库只保留**使用它们所需的全部机制**
（转发 shim 源码、改包脚本、验证脚本、文档），二进制文件请自行准备。

## 怎么用起来

1. 拿到自己有权使用的这 6 个文件，按上表的文件名放进本目录（`drivers/Qualcomm_840/`）。
   `libdmabufheap.so` 不在驱动包里，是从安卓系统侧单独取的，安卓 12 之后系统里已经没有。
2. 校验驱动身份 —— 两份驱动的 `driverName` 都叫 Qualcomm，**必须核对 build 字符串**：

   ```bash
   # 仓库内置（本目录提供的那份）与手机系统自带的，build 字符串不同
   | 驱动 | Driver Build |
   | --- | --- |
   | 本目录提供 | `823f27cb7c, Ic944e09b26, 1753207019` |
   | 手机系统自带（2021 版） | `db3d445dbc, Ia06b22fa1a, 1633355031` |
   ```

3. 按仓库根目录 [README](../README.md) 的「从零复现」跑一遍产线。

## 备选：不用专有驱动

端口本身支持**随包分发 Mesa Turnip**（MIT 许可，见 `upstream/dm/android/prebuilt/turnip/`），
启动界面里可以在「系统驱动 / 内置 Turnip」之间切。Turnip 在 Adreno 650 上会崩
（`VK_ERROR_UNKNOWN`，`FATAL Vulkan pipeline_create_compute failed: -13`），
所以 650 这台机器上只能走上面的专有驱动路线；**Mali 设备则反过来，只能用系统驱动**
（`mali_skip_3d_compute` 相关的 Mali 专有路径见 `upstream/docs/ANDROID_MALI_LIMITS.zh-CN.md`）。

## 另一条路：Mesa 自己的 Adreno 驱动

如果 Turnip 在你的 GPU 上能跑，直接用内置 Turnip 即可，**完全不需要上面这些专有文件**。
