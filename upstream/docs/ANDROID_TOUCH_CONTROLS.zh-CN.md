# Android 虚拟手柄

日期：2026-10-15。用户要求独立子智能体实现虚拟摇杆，同时继续处理渲染。实现已完成离线检查，主智能体已构建、安装并在真正的游戏营地执行三组多指验证，确认移动和 A 技能输入到达游戏、两种顺序独立抬手、真实两指 CANCEL 后控件归零。第一轮路径遇到帐篷阻挡，尚不能据此宣称单独释放 A 后持续移动整整五秒；后台/文字输入生命周期及实体手柄混合输入仍待单独验证。

## 实现

新增 `dm/android/java/org/d2r/android/port/TouchControls.java`、`TouchPadState.java`，接入现有 `D2RActivity.java` 与 `native/frontend_activity.c`。

原生 Vulkan 直接拥有 Activity 窗口，因此普通 `addContentView` 会被游戏覆盖。控件沿用已验证可见的应用内 `PopupWindow`，分成六个小窗口：左摇杆、A/B/X/Y、左肩键 ZL/L、右肩键 R/ZR、方向键、−/+/L3/R3。没有全屏透明触控窗口；控件矩形之外仍交给原游戏触屏入口。

每个窗口非焦点、非模态，开启 `setSplitTouchEnabled(true)`。系统可将不同手指分别送往摇杆、按键窗口和游戏窗口。控件中的整个手势由其窗口消费，摇杆的抬起不经过原生屏幕点按 A 的入口。原有游戏画面点按行为保留。

输入用 Android pointer ID 而非随抬手改变的 pointer index 记录归属。左摇杆只允许第一根有效手指持有，第二根手指不能抢走方向。每根按键手指独立持有按钮，两个手指同时按一个键，抬起其中一个仍保持按下。拖出按键圆即释放，拖回圆内再次按下。`ACTION_CANCEL` 只释放发生取消的窗口，不清空其他手指。

摇杆上方向为正，输出范围 ±32767；使用 15% 圆形静止区，剩余距离连续映射，斜向推满仍限制总长度。无效坐标归零。通过新增 JNI `onTouchPad(long, int, int, int, int)` 送到真正的 `dm_pad_set`，沿用 HLE 已有短按锁存，因此快点按不会在两次游戏取样之间消失。

原生实体键盘/手柄与虚拟手柄分别存储。按钮按位合并；实体键盘/摇杆有非零方向时优先用实体方向，否则用屏幕摇杆。释放虚拟手柄不会清除仍按住的实体按键。Java 和 `AInputQueue` 都在 Activity 主 Looper 上更新状态。

暂停、失去窗口焦点、打开文字输入框、销毁控件均释放虚拟输入并隐藏窗口；恢复焦点后按现有窗口大小显示。原生暂停/输入队列销毁仍清空整个手柄状态。尺寸变化先清零再重排，不从上次手势恢复旧方向。销毁会移除布局监听器；每次 Activity 只创建一套控件，重复恢复用已有窗口更新位置。

新增键帽与无障碍文字均在 `dm/i18n/ui_text.tsv`，重新生成 `dm/host/dm_i18n_tab.h`。Android 构建既有 `strings.py` 自动生成中英文资源；未增加图像资源或直接写中文字面量。

## 布局

参考同一台 OPPO 平板的只读数据：物理 2400×3392，横屏 3392×2400，420 dpi（2.625 像素/dp）。布局按屏幕、16:9 居中游戏画面及系统安全区域动态计算，避免左右返回手势和底部系统手势条。小按键优先放黑边；黑边不够时放上边附近，摇杆和技能键放在下侧游戏 HUD 上方。

按 3392×2400、左右系统手势各 78 像素、底部安全区 42 像素估算：游戏范围 y=246–2154；控件矩形如下。实际窗口 Insets 或 Decor 尺寸变化时，位置会重新计算，部署后应以截图为准。

| 控件 | 左上角 | 宽×高 |
| --- | --- | --- |
| 左摇杆 | 120,1389 | 441×441 |
| A/B/X/Y | 2789,1347 | 483×483 |
| ZL/L | 162,1200 | 357×147 |
| R/ZR | 2852,1158 | 357×147 |
| 方向键排 | 1066,2162 | 588×147 |
| −/+/L3/R3 | 1738,2162 | 588×147 |

摇杆中心约 341,1609；A 3188,1588，B 3031,1746，X 3031,1431，Y 2873,1588。已对照现有营地截图复核：生命球、魔法球、技能栏和腰带位于画面下沿，摇杆/技能键在其上方，小键排在下黑边。矩形窗口内部的透明间隙会被该小控件窗口消费，未宣称圆外逐像素穿透。

主智能体安装后读取的真实 WindowManager 数据已确认六组位置与表格一致，每组包含 `NOT_FOCUSABLE NOT_TOUCH_MODAL SPLIT_TOUCH`，没有覆盖全屏的虚拟手柄触控窗口。生命球、魔法球、技能栏和腰带在真实截图中可见；调试窗口单独保持 `NOT_TOUCHABLE`。证据：[初次营地截图](<LOCAL_WORKTREE>/dm/android/build/device-touch-controls-first.png)、[实际窗口记录](<LOCAL_WORK>/tools/android-touch-controls-windows.log)。

## 真机部署与第一轮多指证据

设备是同一台 OPPO 平板、Android API 36。主智能体统一完成实际 APK 构建和增量安装，安装日志显示 `Success`，随后运行真正的游戏宿主并进入现有 AndroidProbe 营地存档。

- APK 检查点：[virtual-pad-system-first.apk](<LOCAL_WORK>/build/android/checkpoints/virtual-pad-system-first.apk)。
- [完整构建日志](<LOCAL_WORK>/tools/android-build-touch-controls.log)、[安装日志](<LOCAL_WORK>/tools/android-install-touch-controls.log)。
- 工具与准确部署命令：[独立多指工具说明](<LOCAL_WORK>/research/android-touch-controls/README.zh-CN.md)。该工具只存于研究目录，不进入游戏或 APK 业务代码。

主智能体先执行零输入 API 检查，实际输出 `sdk=36`、`InputManagerGlobal`、`sync_mode=2`、`device_id=6`、`display_id=0` 与 `API PASS input_events_sent=0`：[设备 API 日志](<LOCAL_WORK>/research/android-touch-controls/device-api.log)。

随后通过 Android 系统窗口分发器注入真正的两根手指：pointer 0 在摇杆中心 341,1609 按下并逐步移动至 341,1509（本次实际 `dx=0, dy=-100`），pointer 1 在 A 中心 3188,1588 按下。保持同一段 downTime，第二根手指用 `ACTION_POINTER_DOWN(1)` 和 `ACTION_POINTER_UP(1)`，最后以单指 `ACTION_UP` 结束。12 次输入均返回 `accepted=true`，序列用时 19,328 毫秒；原始证据：[事件与阶段日志](<LOCAL_WORK>/research/android-touch-controls/device-sequence/injection.log)。

真正的游戏画面、游戏 HUD 坐标和游戏聊天均响应。第一轮证据如下，阶段开始时间来自注入日志，截图在各阶段内部截取：

| 阶段 | 开始时间 | 真机画面与游戏响应 |
| --- | --- | --- |
| PREPARE | 0 秒 | root 先用虚拟 B 关闭卡夏小菜单；基线坐标 4018,4944 |
| STICK_ONLY | 1.299 秒 | 左摇杆处于上推按下状态 |
| STICK_AND_BUTTON | 6.308 秒 | 摇杆与 A 同时高亮，游戏坐标 4009,4927；游戏聊天出现 AndroidProbe 的“城镇里不行”响应 |
| STICK_ONLY_AFTER_BUTTON_UP | 11.325 秒 | A 归中立、摇杆仍上推，游戏坐标 4009,4922 |
| ALL_RELEASED | 16.328 秒 | 摇杆与 A 都归中立，游戏坐标 4009,4922 |
| FINAL_NEUTRAL | root 追加中立截图 | 坐标仍为 4009,4922，摇杆与 A 继续中立 |

关键截图：[双指与游戏技能响应](<LOCAL_WORK>/research/android-touch-controls/device-sequence/STICK_AND_BUTTON.png)、[仅释放 A](<LOCAL_WORK>/research/android-touch-controls/device-sequence/STICK_ONLY_AFTER_BUTTON_UP.png)、[全部释放后](<LOCAL_WORK>/research/android-touch-controls/device-sequence/ALL_RELEASED.png)、[追加中立](<LOCAL_WORK>/research/android-touch-controls/device-sequence/FINAL_NEUTRAL.png)。

证据边界：`accepted=true` 只证明 Android 接受输入；游戏坐标由 4018,4944 到 4009,4927/4922 证明实际移动，游戏聊天证明 A 技能请求已到游戏，控件独立高亮/归零证明跨两窗口的手指归属和释放行为。角色随后来到帐篷前，碰撞阻挡会让坐标停止，不能把这份路径当成“只松 A 后还持续移动五秒”的完整证明，也不能用一个中立截图代替后台或实体手柄的验证。后续反向抬手与两指 CANCEL 的独立记录如下。

## 反向独立抬手与两指取消

主智能体在同一已安装版本补做两组真实系统触屏事件，均正常退出 0，各次注入均 `accepted=true`，没有改成直接调用游戏状态函数。

`release-stick-first` 使用 `dx=0, dy=+100`，pointer 0 由 341,1609 下推至 341,1709，用时 19,325 毫秒。11.303 秒发出的 `ACTION_POINTER_UP(0)` 包含两根手指，之后只保留 pointer ID 1；11.320 秒进入 `BUTTON_ONLY`，保持五秒，16.321 秒最后发单指 `ACTION_UP`，该单指 ID 仍是 1。真实截图显示左摇杆归中立而 A 仍高亮，游戏坐标 4058,4969；全部释放与 root 追加三秒后的中立截图仍是 4058,4969，两个控件均中立。这验证先抬摇杆不会错误清掉另一根 A 手指，并覆盖 pointer index 改变后仍按 ID 归属的路径。证据：[原始事件日志](<LOCAL_WORK>/research/android-touch-controls/device-release-stick-first/injection.log)、[只剩 A 的截图](<LOCAL_WORK>/research/android-touch-controls/device-release-stick-first/BUTTON_ONLY.png)、[全部释放](<LOCAL_WORK>/research/android-touch-controls/device-release-stick-first/ALL_RELEASED.png)、[追加中立](<LOCAL_WORK>/research/android-touch-controls/device-release-stick-first/FINAL_NEUTRAL.png)。

`cancel` 使用 `dx=0, dy=-100`，总用时 14,303 毫秒。11.301 秒向 Android 分发器发送真正的 `ACTION_CANCEL`，事件包含 pointer 0/1，两指取消返回 `accepted=true`；11.303 秒进入三秒 `AFTER_CANCEL`。取消后与 root 追加中立截图都显示摇杆/A 归中立、游戏坐标 4009,4922，没有继续保留旧控件按下外观。证据：[原始事件日志](<LOCAL_WORK>/research/android-touch-controls/device-cancel/injection.log)、[取消后截图](<LOCAL_WORK>/research/android-touch-controls/device-cancel/AFTER_CANCEL.png)、[追加中立](<LOCAL_WORK>/research/android-touch-controls/device-cancel/FINAL_NEUTRAL.png)。

本次两指 CANCEL 覆盖真实窗口分发与取消处理入口，没有通过假游戏取样或前端状态日志宣称游戏收到全部四轴。它也不替代系统通知、切后台、文字输入框或实际连接实体手柄时的生命周期与混合输入验证。三组截图/日志保留在研究目录，后续可直接复查；独立探针源码、Dex JAR 和原 APK 检查点均保留。

## 离线验证

以下检查均在本机执行，没有用它们代替真机运行结论：

```bash
mkdir -p dm/android/build/touch-check
<LOCAL_WORK>/tools/android/jdk/bin/javac --release 8 \
  -classpath <LOCAL_WORK>/tools/android/platform/android.jar \
  -d dm/android/build/touch-check \
  dm/android/java/org/d2r/android/port/D2RActivity.java \
  dm/android/java/org/d2r/android/port/TouchControls.java \
  dm/android/java/org/d2r/android/port/TouchPadState.java \
  dm/android/tests/TouchPadStateTest.java
<LOCAL_WORK>/tools/android/jdk/bin/java \
  -cp dm/android/build/touch-check org.d2r.android.port.TouchPadStateTest
<LOCAL_WORK>/tools/android/ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang \
  -c -fPIC -O3 -fno-strict-aliasing -fno-omit-frame-pointer -g -D_GNU_SOURCE \
  -I dm/host -I dm/android/native -I dm/gfx/nvn/include -I dm/android/native/compat \
  dm/android/native/frontend_activity.c -o dm/android/build/touch-check/frontend_activity.o
<LOCAL_WORK>/tools/android/jdk/bin/javap \
  -classpath dm/android/build/touch-check -private -s org.d2r.android.port.D2RActivity
python3 dm/ios/tools/dm_i18n.py --check
python3 dm/i18n/scan_literals.py
git diff --check
```

结果：Android Java 编译通过；ARM64 前端编译通过（原有 `_GNU_SOURCE` 重复定义警告）；JNI 签名 `(JIIII)V` 与实际 Java 类一致；中英表 347 条通过，中文字面量检查通过，差异格式检查通过。

`TouchPadStateTest` 的 16 项行为检查全部通过：移动同时按技能、分别抬起两根手指、同键双指、只取消一组窗口、第二根摇杆手指不抢输入、错误窗口不释放输入、静止区、斜向限幅、无效坐标、拖出/拖回按键、暂停清空、取消后旧 MOVE 不恢复状态、合法 pointer ID 边界、取消后复用手指 ID。该测试只验证真实使用的纯 Java 状态模型，没有虚构游戏收到输入或实际画面。

## 主智能体真机验证建议

1. 统一构建安装后截图，确认六组控件位于原生游戏之上，中央游戏 UI 和 HUD 可见。读取 `dumpsys window windows` 检查控件窗口大小有限、带 `SPLIT_TOUCH` 和 `NOT_TOUCH_MODAL`。
2. 用 A 进入现有 AndroidProbe 存档，避免 Y 的删除角色行为。拖住左摇杆至少两秒，结合 HUD 游戏坐标变化证明角色确实移动；抬手后坐标停止变化，不能额外触发 A。
3. 两指操作：保持移动同时按 A；先松 A 应继续移动，先松摇杆应停止移动而保留另一根按键手指。手动操作或能生成真实多 pointer MotionEvent 的工具验证，普通 `adb input swipe` 只证明单指。
4. B 返回、+ 打开/关闭菜单、− 切地图，检查快点按确实响应。其余肩键和方向键按对应游戏功能确认。
5. 保持摇杆或按钮期间切后台、打开文字框/系统提示再返回，确认不会卡住移动/技能；恢复后的控件不能重复出现。
6. 保持实体键盘/手柄输入，再按/松屏幕控件，确认实体输入保留。真实按键取样如需证据，可使用项目已有可选 `DM_HID_RECEIPT_LOG`，不要把前端状态日志当游戏实际收到的取样。

## 修改清单

- `dm/android/java/org/d2r/android/port/TouchControls.java`：新控件窗口和绘制、手势分发、重排。
- `dm/android/java/org/d2r/android/port/TouchPadState.java`：新多指状态模型。
- `dm/android/java/org/d2r/android/port/D2RActivity.java`：JNI 接入及生命周期/文字框显隐。
- `dm/android/native/frontend_activity.c`：仅虚拟输入存储、合并、清空和 JNI 注册。
- `dm/android/tests/TouchPadStateTest.java`：状态行为回归。
- `dm/i18n/ui_text.tsv`、`dm/host/dm_i18n_tab.h`：新增资源及生成表。
- 本说明文件。

## 游戏实际HID取样补证

root随后在真实掌机720p营地启用已有 `input_receipts=1`，重新执行由Android系统分发的双指序列。保存目录 `~/dm-work/research/android-touch-controls/device-hid-receipts`，含原始 `input-receipts.jsonl`、`real-state-changes.json`、阶段截图与注入日志。

这次证据来自游戏实际 `GetNpadState` 返回值：sample4238–4242轴逐步到 `(lx=102,ly=20573)`；sample4363为buttons1且轴仍保持；sample4477释放A后buttons0、轴仍保持，间隔约五秒；sample4591最后释放摇杆后buttons和轴全部0。两根手指的独立释放已在游戏实际取样中确认，未用控件高亮或注入accepted冒充HID。角色在帐篷前碰撞导致坐标停住的限制仍保留；本次没有补做后台、文字输入或实体手柄混合输入测试。
