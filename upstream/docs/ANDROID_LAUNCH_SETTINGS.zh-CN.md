# Android 可复现启动设置

此模块只读取本应用 temp 目录中的 `launch-settings.conf`，用于在同一 APK 中选择已有掌机/主机运行档，以及收集游戏实际取到的手柄状态。它不改变游戏时钟、帧率、驱动、着色器精度或验证开关。

实现为 `dm/android/native/launch_settings.c/.h`，独立测试为 `dm/android/tests/launch_settings_test.c`。根智能体已审核并接入 `DM_TEMP` 设置后的启动位置及宿主源码列表，完整 APK 构建安装成功。Host及Android同App UID普通34项/环境故障40项均真实exit0；保留真实FIFO负例，不跳检查。掌机档营地主颜色/深度/UI目标实际1280×720，截图20.0FPS，游戏HID取样文件也已实测生成。

## 配置与缺省行为

示例文件：

```ini
# 与 Eden 掌机设置做可复现对照
operation_mode=handheld
input_receipts=1
```

| 键 | 允许值 | 生效方式 |
| --- | --- | --- |
| `operation_mode` | `handheld` / `console` | 设置现有 `DM_OPMODE=0` / `DM_OPMODE=1` |
| `input_receipts` | `0` / `1` | `0` 清除 `DM_HID_RECEIPT_LOG`；`1` 将其设置为调用方 temp 中固定的 `input-receipts.jsonl` |

没有文件、空文件或只含注释的文件不改变已有环境。省略一个键时保留该键此前的环境设置；只有显式 `input_receipts=0` 会关闭已有取样。模块不提前创建输入日志，也不产生测试按键。

格式是大小写敏感的 `key=value`；支持首尾空格/制表符、LF/CRLF、空行和独占一行的 `#` 注释。行内注释不支持，因而 `input_receipts=1 # note` 会报非法值。整个文件最多 4096 字节，每行最多 256 字节（不计行结束符），包含 NUL 字节、未知键、非法值、重复键、缺少等号或超长内容都会明确失败。错误配置不会半应用；结果结构也只在成功后发布。配置必须是普通文件，FIFO 不会阻塞启动。

调用方传入自身的绝对 temp 路径，配置不能指定其他路径。模块仅拼接这两个固定文件名，不提供任意环境变量设置入口。路径所有权由启动代码保证，模块不是文件系统隔离机制。

## 必须在首次读取之前接入

现有耦合已经核对：

- `frontend_activity.c::boot` 创建并设置 `DM_TEMP` 后才调用 `dm_main`。
- `hle_oe.c::dm_opmode` 第一次读取 `DM_OPMODE` 后保存运行状态，后续不再重新读取环境。
- `hle_hid.c::fill` 使用 `pthread_once` 初始化输入返回日志，首次读取 `DM_HID_RECEIPT_LOG` 后打开文件。
- `frontend_activity.c::front_game_res` 按运行档给出名义掌机 1280×720、主机 1600×900；实际创建的 GPU 目标尺寸仍须通过真实游戏日志/探针核对，不能用配置值代替实测。
- `tools/host_link.py` 显式列出 native C 源文件；新模块需要根智能体加到该源列表。模块自身不依赖 Java、JNI、Vulkan 或宿主 TLS。

建议的接入点紧跟现有 `setenv("DM_TEMP", path, 1)`，且必须在首次运行模式查询及输入采样之前，不能在游戏运行中调用：

```c
#include "launch_settings.h"

dm_android_launch_settings launch;
char launch_error[512];
if (!dm_android_launch_settings_apply(path, &launch,
                                     launch_error, sizeof launch_error))
    die("[android] launch settings: %s", launch_error);
LOG("[android] launch configuration present=%d operation_mode=%d input_receipts=%d",
    launch.present, launch.operation_mode, launch.input_receipts);
```

返回 1 表示成功，缺文件也成功；返回 0 必须停止启动并报告错误。成功结构中的 `present=0` 表示缺文件，两个设置字段为 `-1` 表示未指定、保持原设置。这些字段是本次解析结果，不是实际 GPU 分辨率或完整有效配置的证明。

全部内容和所需路径验证完成后才写环境。写入前保存两项旧值；某次环境写入失败时恢复已写项。libc 没有跨变量的原子事务，极端内存不足可能导致恢复也失败，此时错误明确包含 `environment rollback also failed; ... abort startup`，调用方必须终止，不能继续运行或声称已恢复。接口只供启动阶段单一调用方使用，不承诺与其他线程并发修改环境的原子性。

## 输入日志的真实含义

`input-receipts.jsonl` 是既有 `GetNpadState` 返回采样的逐行 JSON，不是制表分隔文件。记录含真实 `sampling`、`buttons`、左右摇杆轴、读取起止时间和手柄样式。日志只记状态变化以及最多每秒一次的活动见证；到 1024 条后明确写 `limit_reached`，不是逐帧无上限日志。关闭和再次开启需要新进程，因为现有日志初始化只执行一次。游戏首次读取时会用写模式打开，覆盖同名上次日志；测试完成应先保存副本。

本模块不会把桌面测试或注入事件当成游戏收到输入的证据；摇杆双指的闭环仍应使用真实游戏的 HID 返回、位置变化和截图。

## 2026-10-15 离线验证

在 `<LOCAL_WORKTREE>` 运行，产物均在忽略的 `dm/android/build/launch-settings/`：

```bash
mkdir -p dm/android/build/launch-settings
cc -std=c11 -O3 -Wall -Wextra -Werror -Idm/android/native \
  dm/android/native/launch_settings.c dm/android/tests/launch_settings_test.c \
  -o dm/android/build/launch-settings/test-host
dm/android/build/launch-settings/test-host

cc -std=c11 -O3 -Wall -Wextra -Werror -Idm/android/native \
  dm/android/native/launch_settings.c dm/android/tests/launch_settings_test.c \
  -DDM_LAUNCH_SETTINGS_TEST_WRAP_ENV -Wl,--wrap=setenv -Wl,--wrap=unsetenv \
  -o dm/android/build/launch-settings/test-host-env-fault
dm/android/build/launch-settings/test-host-env-fault

<LOCAL_WORK>/tools/android/ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang \
  -std=c11 -O3 -Wall -Wextra -Werror -Idm/android/native \
  dm/android/native/launch_settings.c dm/android/tests/launch_settings_test.c \
  -o dm/android/build/launch-settings/test-arm64

<LOCAL_WORK>/tools/android/ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang \
  -std=c11 -O3 -Wall -Wextra -Werror -Idm/android/native \
  dm/android/native/launch_settings.c dm/android/tests/launch_settings_test.c \
  -DDM_LAUNCH_SETTINGS_TEST_WRAP_ENV -Wl,--wrap=setenv -Wl,--wrap=unsetenv \
  -o dm/android/build/launch-settings/test-arm64-env-fault

git diff --check
```

实际结果：普通 O3 测试 34 项通过；额外环境故障注入版本 40 项通过；两个 Android 29 ARM64 PIE 离线编译成功；`git diff --check` 通过。覆盖缺文件时原环境存在/不存在、有效模式、固定 HID 路径及不提前创建日志、只设一项保留另一项、重复/非法/未知/过长/NUL 配置不半应用、长度边界、无末尾换行、CRLF、非普通文件，以及环境写入失败后的恢复和回滚失败响亮报告。

环境故障替身只在独立测试的链接器 `--wrap` 中启用，生产模块没有测试配置入口。最初 O3 故障测试暴露测试计数器重置被优化掉的问题，O0 通过但 O3 失败；已核对实际反汇编并将测试控制量声明为 `volatile`，重新运行 O3 得到 40 项通过，没有通过降低生产优化等级掩盖问题。

额外尝试 ASan/UBSan 链接未成功：当前主机缺少实际 `libasan.so.8.0.0` / `libubsan.so.1.0.0`，NDK 也没有桌面 x86_64 对应 sanitizer 静态库；不把该项列作已执行或通过。未操作 ADB，未改 frontend/build/Java/GPU，未宣称掌机档实机 FPS、实际分辨率或 HID 取样已验收。

## Android shell 的 FIFO 前提条件失败与应用身份验证准备

根智能体执行 Android shell 下的两个 ARM64 测试时均返回 134，停在 `mkfifo` 的断言；使用默认 `/tmp` 和显式 `/data/local/tmp` 均未闭环。没有删除这项检查，也没有把此前主机通过的结果代替 Android 成绩。独立测试已补输出：失败时打印实际配置路径、UID、errno、系统错误文本和 `test incomplete, not skipped`，随后仍断言失败。

Android 16 官方默认策略中，`shell` 对 `shell_data_file` 的创建许可列出普通文件和符号链接；应用域对自己的 `app_data_file` 明确拥有 FIFO 的创建许可。[shell.te](https://android.googlesource.com/platform/system/sepolicy/+/refs/tags/android-16.0.0_r1/private/shell.te)、[untrusted_app_all.te](https://android.googlesource.com/platform/system/sepolicy/+/refs/tags/android-16.0.0_r1/private/untrusted_app_all.te)

`runas_app` 使用 `untrusted_app_domain`，为使用 `run-as` 在本应用内部 temp 创建真实 FIFO 提供了官方策略依据；这是可执行的验证方案，OPPO 本机具体权限及首次 shell 失败的精确 errno 仍须实际采集。[runas_app.te](https://android.googlesource.com/platform/system/sepolicy/+/refs/tags/android-16.0.0_r1/private/runas_app.te)

已从当前源码重新编译两个独立测试组件，供下一轮 APK 从 `build/gpu-fixtures/*.so` 打包到自身 `nativeLibraryDir`：

```bash
<LOCAL_WORK>/tools/android/ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang \
  -std=c11 -O3 -g -fPIE -pie -Wall -Wextra -Werror -Idm/android/native \
  dm/android/native/launch_settings.c dm/android/tests/launch_settings_test.c \
  -Wl,-z,defs -Wl,-z,max-page-size=16384 \
  -o dm/android/build/gpu-fixtures/libdmlaunchsettingstest.so

<LOCAL_WORK>/tools/android/ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang \
  -std=c11 -O3 -g -fPIE -pie -Wall -Wextra -Werror -Idm/android/native \
  dm/android/native/launch_settings.c dm/android/tests/launch_settings_test.c \
  -DDM_LAUNCH_SETTINGS_TEST_WRAP_ENV -Wl,--wrap=setenv -Wl,--wrap=unsetenv \
  -Wl,-z,defs -Wl,-z,max-page-size=16384 \
  -o dm/android/build/gpu-fixtures/libdmlaunchsettingsfaulttest.so
```

两个组件均实际编译返回 0，ELF 解释器为 `/system/bin/linker64`，全部 LOAD 段对齐为 `0x4000`（16 KiB）。名称后缀 `.so` 供 APK 原生文件打包使用，组件含 `main`，实际是从应用 `nativeLibraryDir` 执行的 PIE 测试程序；不加载进入游戏主进程。

根智能体已通过本应用 `run-as` 实际执行各组件，以自身内部 `files/temp` 的绝对路径作为第一个参数，保留完整输出与退出码。测试在该目录下新建 `d2r-launch-settings-XXXXXX`，只操作其中的测试配置，不读写正式 `launch-settings.conf`。两项已同时取得34/40项完成标记及退出码0；日志 `dm/android/build/launch-settings/{libdmlaunchsettingstest.so,libdmlaunchsettingsfaulttest.so}.appuid.device.log`。

本次改动仅补独立测试的失败证据并生成离线诊断组件，没有修改生产模块、frontend、构建脚本、Java 或 GPU，也未操作 ADB。修订后主机 O3 的 34 / 40 项再次实际通过，日志为 `dm/android/build/launch-settings/host.log` 和 `host-env-fault.log`。
