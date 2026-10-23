// 手柄 / 触屏两套输入的接管权（dm_input.h 的纯函数）：迟滞、按下沿、碰过屏幕后要先回中。
// 规则与 iOS 前端 dm_gamepad.m 同源，这里逐条钉住，免得两端各写一份后走样。
// The controller / touch takeover rules (the pure functions in dm_input.h): hysteresis, button press edges and the
// "must recentre first" rule after a screen touch. Same source as the iOS front end; pinned here so the two copies
// cannot drift apart.
#include <stdio.h>
#include <stdint.h>
#include "../dm_input.h"

static int checks, fails;
#define CHECK(c, ...) do { checks++; if (c) printf("  ✓ "); else { fails++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)

int main(void) {
    // 摇杆迟滞：没推出时要过 25% 才算推出，推出来之后回到 15% 以内才算回中。
    // Stick hysteresis: not pushed out yet, it has to pass 25% to count as pushed out; once pushed out, back within
    // 15% counts as centred. (DM_STICK_MAX = 32767, so 25% = 8192 and 15% = 4915.)
    CHECK(dm_pad_mode_stick(0, 0, 0) == 0, "摇杆居中 = 没推出");
    CHECK(dm_pad_mode_stick(0, 4000, 0) == 0, "推 12%% 不算推出（没过 25%%）");
    CHECK(dm_pad_mode_stick(0, 9000, 0) == 1, "推 27%% 算推出");
    CHECK(dm_pad_mode_stick(0, 6000, 6000) == 1, "斜推 26%%（合起来）算推出");
    CHECK(dm_pad_mode_stick(1, 9000, 0) == 1, "已经推出，27%% 仍算推出");
    CHECK(dm_pad_mode_stick(1, 6000, 0) == 1, "已经推出，18%% 仍算推出（迟滞：回中要到 15%% 以内）");
    CHECK(dm_pad_mode_stick(1, 4000, 0) == 0, "已经推出，回到 12%% 才算回中");

    // 接管权：按下的那一刻（新的按下沿）→ 手柄接管；一直按着不放手 → 不重复接管。
    // Takeover: the moment a button goes down (a new press edge) -> controller mode; holding it does not re-trigger.
    dm_pad_mode_state state = { {0, 0}, 0 };
    CHECK(dm_pad_mode_takeover(&state, DM_BTN_A, 0) == 1, "按下 A → 手柄接管");
    CHECK(dm_pad_mode_takeover(&state, DM_BTN_A, DM_BTN_A) == 0, "按住 A 不放 → 没有新的按下沿，不接管");
    CHECK(dm_pad_mode_takeover(&state, DM_BTN_A | DM_BTN_B, DM_BTN_A) == 1, "再按下 B → 接管");

    // 摇杆推出来也算明确输入。
    // A stick pushed out is explicit input as well.
    dm_pad_mode_state pushed = { {0, 0}, 0 };
    dm_pad_mode_update(&pushed, 0, 9000, 0);
    CHECK(pushed.stick_on[0] == 1, "左摇杆记为推出");
    CHECK(dm_pad_mode_takeover(&pushed, 0, 0) == 1, "摇杆推出来 → 手柄接管");

    // 碰过屏幕之后：一直偏着的摇杆不夺回接管权（漂移大的手柄不能把界面来回切）。
    // After the screen was touched, a stick that stays deflected does not take the takeover back (a controller with
    // heavy drift must not flip the UI back and forth).
    dm_pad_mode_state recentred = { {0, 0}, 1 };
    CHECK(dm_pad_mode_takeover(&recentred, 0, 0) == 0, "碰过屏幕后，摇杆没动 → 不接管");
    dm_pad_mode_update(&recentred, 0, 9000, 0);
    CHECK(dm_pad_mode_takeover(&recentred, 0, 0) == 0, "碰过屏幕后，摇杆仍偏着 → 不接管");
    // 回中 → 解除限制 → 再推出来 → 接管。
    // Back to centre -> the restriction is lifted -> pushed out again -> it takes over.
    dm_pad_mode_update(&recentred, 0, 1000, 0);
    dm_pad_mode_centered(&recentred);
    CHECK(recentred.need_center == 0, "两根摇杆都回中 → 解除『要先回中』");
    dm_pad_mode_update(&recentred, 0, 9000, 0);
    CHECK(dm_pad_mode_takeover(&recentred, 0, 0) == 1, "回中后再推出来 → 接管");
    // 按键随时接管：「要先回中」只管摇杆。
    // Buttons always take over: the "must recentre first" rule only applies to the sticks.
    CHECK(dm_pad_mode_takeover(&recentred, DM_BTN_A, 0) == 1, "碰过屏幕后按键照样接管");

    // 摇杆还偏着时，「要先回中」不解除。
    // While a stick is still deflected the restriction stays.
    dm_pad_mode_state held = { {0, 0}, 1 };
    dm_pad_mode_update(&held, 0, 9000, 0);
    dm_pad_mode_centered(&held);
    CHECK(held.need_center == 1, "摇杆还偏着 → 『要先回中』不解除");
    // 右摇杆单独推出也算（它是技能方向用的那根）。
    // The right stick counts on its own as well (it is the one used for skill direction).
    dm_pad_mode_state right = { {0, 0}, 0 };
    dm_pad_mode_update(&right, 1, 0, 9000);
    CHECK(right.stick_on[0] == 0 && right.stick_on[1] == 1, "只更新右摇杆那一路");
    CHECK(dm_pad_mode_takeover(&right, 0, 0) == 1, "右摇杆推出来 → 手柄接管");

    printf("== %d 项，失败 %d ==\n", checks, fails);
    return fails != 0;
}
