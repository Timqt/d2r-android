// 前端（Mac 键盘 / iOS 触屏）→ nn::hid 的输入状态。按钮位与 nn::hid::NpadButton 一致：
// A0 B1 X2 Y3 StickL4 StickR5 L6 R7 ZL8 ZR9 Plus10 Minus11 Left12 Up13 Right14 Down15；摇杆 ±32767。
// Front end (Mac keyboard / iOS touchscreen) -> nn::hid input state. Button bits match nn::hid::NpadButton:
// A0 B1 X2 Y3 StickL4 StickR5 L6 R7 ZL8 ZR9 Plus10 Minus11 Left12 Up13 Right14 Down15; sticks ±32767.
#pragma once
#include <stdint.h>
// 按钮位（= nn::hid::NpadButton 位序；hle_hid.c 的 DM_KEYS 键名表由这里生成）。前端一律用这些名字组 buttons。
// Button bits (= nn::hid::NpadButton bit order; the DM_KEYS key-name table in hle_hid.c is generated from here). Frontends always compose buttons from these names.
enum {
    DM_BTN_A = 1u << 0, DM_BTN_B = 1u << 1, DM_BTN_X = 1u << 2, DM_BTN_Y = 1u << 3,
    DM_BTN_LS = 1u << 4, DM_BTN_RS = 1u << 5, DM_BTN_L = 1u << 6, DM_BTN_R = 1u << 7,
    DM_BTN_ZL = 1u << 8, DM_BTN_ZR = 1u << 9, DM_BTN_PLUS = 1u << 10, DM_BTN_MINUS = 1u << 11,
    DM_BTN_LEFT = 1u << 12, DM_BTN_UP = 1u << 13, DM_BTN_RIGHT = 1u << 14, DM_BTN_DOWN = 1u << 15,
};
#define DM_STICK_MAX 32767
void dm_input_set(uint64_t buttons, int32_t lx, int32_t ly, int32_t rx, int32_t ry);
uint64_t dm_input_samples(void);   // 游戏已取手柄的累计次数 | Cumulative number of times the game has sampled the controller
// 实体 / 虚拟手柄（iOS GameController，dm_gamepad.m）：与触屏输入分开存，游戏取手柄时按键按位或、摇杆手柄非零就用手柄的。没连手柄时全 0，取值与原来逐位相同
// Physical / virtual controller (iOS GameController, dm_gamepad.m): stored separately from touch input; when the game reads the controller, buttons are
// ORed bitwise and a stick uses the controller's value if it is non-zero. All 0 when no controller is connected, so values are bit-for-bit the same as before
void dm_pad_set(uint64_t buttons, int32_t lx, int32_t ly, int32_t rx, int32_t ry);

// 十字键可能是按键事件，也可能是 HAT 轴（不少手柄，含 Xbox，在安卓上报成 HAT）。这里只做轴到按键位的映射，
// 好让它能在主机上单独验证：HAT_Y 负为上、正为下；阈值 ±0.5 容忍模拟量；两轴同时超阈值给斜向两位。
// The D-pad arrives either as key events or as HAT axes (many pads, Xbox included, report it as HAT on Android).
// This only maps axes to button bits so it can be verified on the host on its own: HAT_Y is negative up and
// positive down, ±0.5 tolerates analogue hats, and both axes over the threshold give two diagonal bits.
static inline uint64_t dm_pad_hat_buttons(float hat_x, float hat_y) {
    uint64_t buttons = 0;
    if (hat_x <= -0.5f) buttons |= DM_BTN_LEFT; else if (hat_x >= 0.5f) buttons |= DM_BTN_RIGHT;
    if (hat_y <= -0.5f) buttons |= DM_BTN_UP; else if (hat_y >= 0.5f) buttons |= DM_BTN_DOWN;
    return buttons;
}

// 扳机（ZL/ZR）：Xbox 手柄在安卓上把 LT/RT 报成**模拟轴**（`AXIS_LTRIGGER`/`AXIS_RTRIGGER`，0–1）而不是
// L2/R2 键码 —— 与十字键报成 HAT 轴是同一类问题，键码分支永远收不到它。阈值 0.5 的两个理由：轴的 flat 区
// 在 0.06 上下抖动（实测 flat=0.062），0.5 离噪声足够远；也和上面的 HAT 用同一个阈值，手感一致。
// 要调手感只改这一个数。
// Triggers (ZL/ZR): on Android an Xbox pad reports LT/RT as **analogue axes** (`AXIS_LTRIGGER`/`AXIS_RTRIGGER`,
// 0-1) rather than L2/R2 keycodes -- the same kind of gap as the D-pad arriving as HAT axes, and the keycode
// branch can never see them. The 0.5 threshold has two reasons: the axis flutters inside a flat region of about
// 0.06 (measured flat=0.062), so 0.5 is well clear of the noise, and it matches the HAT threshold above so the
// feel is consistent. Tuning the feel means changing this one number.
static inline uint64_t dm_pad_trigger_buttons(float left, float right) {
    uint64_t buttons = 0;
    if (left >= 0.5f) buttons |= DM_BTN_ZL;
    if (right >= 0.5f) buttons |= DM_BTN_ZR;
    return buttons;
}

// 可选诊断DM_HID_RECEIPT_LOG=<可写JSONL路径>：真实GetNpadState返回收样。
// 字段read_start_ns/time_ns同宿主now_ns()；前端publish窗口必须同helper，读取区间跨发布不得强配。
// 状态变化+最多每秒一次活动见证，1024条后limit_reached；未设环境变量不写文件。
// 实际返回含id/style/sampling/buttons/四轴/attributes/script_mask，不把前端输出当收到。
// 无新行不证明空白触点读到，须有效连接/采样活动/原UI与命令包证据；日志有IO开销，不作计时。
// Optional diagnostic DM_HID_RECEIPT_LOG=<writable JSONL path>: received samples of real GetNpadState returns.
// Fields read_start_ns/time_ns use the same host now_ns(); the front-end publish window must use the same helper, and a read interval that spans a publish must not be force-matched.
// State changes + at most one activity witness per second, limit_reached after 1024 entries; no file is written when the environment variable is not set.
// The actual return includes id/style/sampling/buttons/four axes/attributes/script_mask; front-end output is not treated as received.
// No new line does not prove that a blank touch point was read; a valid connection / sampling activity / evidence from the original UI and command packets is required; the log has IO overhead and is not used for timing.

// 私有skill-aim-v1：只取已实际GetNpad返回的右轴成对收样，非前端尚未消费值。
// Private skill-aim-v1: takes only paired received samples of the right axis that GetNpad has actually returned, not values the front end has not yet had consumed.
void dm_input_sampled_right(int32_t *rx,int32_t *ry);

// ---------- 手柄 / 触屏两套输入的接管权（iOS 前端 dm_gamepad.m 同语义，纯函数）----------
// 谁有明确输入谁接管：
//   · 手柄按下任何键、或把摇杆推出来 → 手柄模式（屏幕虚拟手柄隐藏，实体手柄接管）；
//   · 碰了屏幕 → 触屏模式（虚拟手柄显示，实体手柄的摇杆不交给游戏）；
//   · 碰过屏幕之后，摇杆要先回中、再推出来才重新接管（一直偏着的摇杆不会把接管权抢回来）；按键随时接管。
// 摇杆带迟滞：推过 25% 才算推出，回到 15% 以内才算回中 —— 停在阈值附近的漂移不会一会儿算一会儿不算。
// 这套规则放在纯函数里，安卓前端（frontend_activity.c）与宿主单测（t_padmode）用同一份，不各写一套。
// Controller / touch takeover (same rules as the iOS front end, pure functions): whoever has explicit input takes over.
//   - Controller: any button press or a stick pushed out -> controller mode (the on-screen controls hide);
//   - the screen is touched -> touch mode (the on-screen controls show, the controller sticks are not handed to the game);
//   - after the screen was touched, a stick has to return to centre and be pushed out again before it takes over; buttons always take over.
// The sticks have hysteresis: past 25% counts as pushed out, back within 15% counts as centred, so drift near the threshold
// does not count one moment and not the next. The rules live in pure functions so the Android front end and the host test share them.
typedef struct {
    int stick_on[2];   // 两根摇杆是否推出来了（带迟滞） | whether each stick is pushed out (with hysteresis)
    int need_center;   // 碰过屏幕后，摇杆要回中再推出来才重新接管 | after a screen touch, a stick must recentre before it takes over again
} dm_pad_mode_state;

// 摇杆迟滞：没推出时要过 25% 才算推出，已经推出时回到 15% 以内才算回中。
// Stick hysteresis: not pushed out yet, it has to pass 25% to count as pushed out; once pushed out, back within 15% counts as centred.
static inline int dm_pad_mode_stick(int was_on, int32_t x, int32_t y) {
    double limit = (was_on ? 0.15 : 0.25) * (double)DM_STICK_MAX;
    return (double)x * x + (double)y * y >= limit * limit;
}
// 更新一根摇杆的推出状态。
// Update one stick's pushed-out state.
static inline void dm_pad_mode_update(dm_pad_mode_state *state, int index, int32_t x, int32_t y) {
    state->stick_on[index] = dm_pad_mode_stick(state->stick_on[index], x, y);
}
// 两根摇杆都回中了 → 解除"要先回中"的限制（之后再推出来就能接管）。
// Both sticks centred -> the "must recentre first" restriction is lifted (pushing out again takes over from then on).
static inline void dm_pad_mode_centered(dm_pad_mode_state *state) {
    if (!state->stick_on[0] && !state->stick_on[1]) state->need_center = 0;
}
// 接管权判定：按下了新的键，或摇杆推出来了（且不在"要先回中"的限制里）。
// Takeover decision: a new button press, or a stick pushed out (and not under the "must recentre first" restriction).
static inline int dm_pad_mode_takeover(const dm_pad_mode_state *state, uint64_t buttons, uint64_t previous_buttons) {
    if (buttons & ~previous_buttons) return 1;
    if ((state->stick_on[0] || state->stick_on[1]) && !state->need_center) return 1;
    return 0;
}

// 手柄状态注入点：GetNpadState 填完原状态后、伪键覆盖前调用。Mac 宿主触点脚本（touch_mac.c，Mac 专用、按 _mac 约定不进 iOS 包）启动时注册；iOS 不带该文件，保持空
// Controller state injection point: called after GetNpadState has filled in the original state and before the pseudo-key override. The Mac host touch point script (touch_mac.c, Mac only, kept out of the iOS app by the _mac convention) registers at startup; iOS does not carry that file and it stays empty
extern void (*dm_pad_hook)(uint64_t now, uint64_t *buttons, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry);
