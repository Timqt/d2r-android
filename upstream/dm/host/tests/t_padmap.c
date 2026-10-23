// 实体手柄面键映射（dm_padmap.h）：各类型 × 四个面键 → 游戏键。期望值按规则手写：印什么字母就是游戏的什么键；任天堂键帽 右 A 下 B 上 X 左 Y，Xbox 键帽 下 A 右 B 左 X 上 Y；别的按 Xbox 排。
// Physical controller face button mapping (dm_padmap.h): each type x four face buttons -> game keys. Expected values are hand-written from
// the rule: whatever letter is printed is that game key; Nintendo glyphs are right A, bottom B, top X, left Y, Xbox glyphs are bottom A,
// right B, left X, top Y; everything else is laid out as Xbox.
#include <stdio.h>
#include "../dm_padmap.h"
#include "../dm_input.h"
static int checks, fails;
#define CHECK(c, ...) do { checks++; if (c) printf("  ✓ "); else { fails++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
int main(void) {
    struct { const char *cat, *ven; int kind; } id[] = {
        { "Switch Pro Controller", "Pro Controller", DM_PADKIND_NINTENDO }, { "Nintendo Switch Joy-Con (L/R)", "Joy-Con (L/R)", DM_PADKIND_NINTENDO },
        { "Nintendo Switch Joy-Con (L)", "Joy-Con (L)", DM_PADKIND_NINTENDO }, { "MFi", "NINTENDO compatible pad", DM_PADKIND_NINTENDO },
        { "Xbox One", "Xbox Wireless Controller", DM_PADKIND_XBOX }, { "DualSense", "DualSense Wireless Controller", DM_PADKIND_XBOX },
        { "DualShock 4", "DUALSHOCK 4 Wireless Controller", DM_PADKIND_XBOX }, { "MFi", "SteelSeries Nimbus", DM_PADKIND_XBOX }, { NULL, NULL, DM_PADKIND_XBOX }, { "", "", DM_PADKIND_XBOX } };
    for (unsigned i = 0; i < sizeof id / sizeof *id; i++) CHECK(dm_padmap_kind(id[i].cat, id[i].ven) == id[i].kind, "类别「%s」厂商名「%s」→ %s 排法", id[i].cat ? id[i].cat : "(空)", id[i].ven ? id[i].ven : "(空)", id[i].kind ? "任天堂" : "Xbox");
    // 下 右 左 上
    // bottom right left top
    CHECK(dm_padmap_face(DM_PADKIND_NINTENDO, 1, 0, 0, 0) == DM_BTN_B, "任天堂：下 = B"); CHECK(dm_padmap_face(DM_PADKIND_NINTENDO, 0, 1, 0, 0) == DM_BTN_A, "任天堂：右 = A");
    CHECK(dm_padmap_face(DM_PADKIND_NINTENDO, 0, 0, 1, 0) == DM_BTN_Y, "任天堂：左 = Y"); CHECK(dm_padmap_face(DM_PADKIND_NINTENDO, 0, 0, 0, 1) == DM_BTN_X, "任天堂：上 = X");
    CHECK(dm_padmap_face(DM_PADKIND_XBOX, 1, 0, 0, 0) == DM_BTN_A, "Xbox：下 = A"); CHECK(dm_padmap_face(DM_PADKIND_XBOX, 0, 1, 0, 0) == DM_BTN_B, "Xbox：右 = B");
    CHECK(dm_padmap_face(DM_PADKIND_XBOX, 0, 0, 1, 0) == DM_BTN_X, "Xbox：左 = X"); CHECK(dm_padmap_face(DM_PADKIND_XBOX, 0, 0, 0, 1) == DM_BTN_Y, "Xbox：上 = Y");
    CHECK(dm_padmap_face(DM_PADKIND_XBOX, 1, 1, 0, 0) == (DM_BTN_A | DM_BTN_B), "两颗同时按：各算各的"); CHECK(dm_padmap_face(DM_PADKIND_NINTENDO, 0, 0, 0, 0) == 0, "都没按 = 0");
    // 十字键的 HAT 轴映射（dm_input.h）：先按安卓惯例 —— HAT_Y 负为上、正为下；阈值 ±0.5 容忍模拟量；两轴同过阈值给斜向两位。
    // 上下方向若在真机上相反，看 [android-pad] 那行日志即可确认，改这里一处。
    // HAT-axis mapping for the D-pad (dm_input.h), starting from the Android convention: HAT_Y negative is up and
    // positive is down; ±0.5 tolerates analogue hats; both axes over the threshold give two diagonal bits. If up
    // and down turn out swapped on a real pad, the [android-pad] log line shows it and only this needs changing.
    CHECK(dm_pad_hat_buttons(0, 0) == 0, "HAT 居中 = 无方向");
    CHECK(dm_pad_hat_buttons(-1, 0) == DM_BTN_LEFT, "HAT 左 = Left");
    CHECK(dm_pad_hat_buttons(1, 0) == DM_BTN_RIGHT, "HAT 右 = Right");
    CHECK(dm_pad_hat_buttons(0, -1) == DM_BTN_UP, "HAT 上（Y 负）= Up");
    CHECK(dm_pad_hat_buttons(0, 1) == DM_BTN_DOWN, "HAT 下（Y 正）= Down");
    CHECK(dm_pad_hat_buttons(-1, -1) == (DM_BTN_LEFT | DM_BTN_UP), "HAT 左上 = Left+Up");
    CHECK(dm_pad_hat_buttons(1, 1) == (DM_BTN_RIGHT | DM_BTN_DOWN), "HAT 右下 = Right+Down");
    CHECK(dm_pad_hat_buttons(0.4f, -0.4f) == 0, "低于阈值不触发（容忍模拟量漂移）");
    CHECK(dm_pad_hat_buttons(-0.5f, 0) == DM_BTN_LEFT, "恰好 -0.5 算触发（边界含）");
    // 扳机 ZL/ZR 的模拟轴映射（dm_input.h）：Xbox 的 LT/RT 在安卓上是 0–1 的轴、不是键码；阈值 0.5（实测轴的
    // flat 区约 0.06，离噪声足够远）。真机若手感偏重/偏轻，改 dm_input.h 里那一个阈值即可。
    // Analogue-axis mapping for the ZL/ZR triggers (dm_input.h): an Xbox pad reports LT/RT as 0-1 axes, not
    // keycodes; the threshold is 0.5 (the axis flat region measures about 0.06, well clear of the noise). If the
    // feel is too heavy or too light on a real pad, the single threshold in dm_input.h is the place to change.
    CHECK(dm_pad_trigger_buttons(0, 0) == 0, "扳机松开 = 不按");
    CHECK(dm_pad_trigger_buttons(1, 0) == DM_BTN_ZL, "LT 到底 = ZL");
    CHECK(dm_pad_trigger_buttons(0, 1) == DM_BTN_ZR, "RT 到底 = ZR");
    CHECK(dm_pad_trigger_buttons(1, 1) == (DM_BTN_ZL | DM_BTN_ZR), "两个扳机各给一位");
    CHECK(dm_pad_trigger_buttons(0.4f, 0.4f) == 0, "半按不到阈值不算按下");
    CHECK(dm_pad_trigger_buttons(0.5f, 0) == DM_BTN_ZL, "恰好 0.5 算按下（边界含）");
    CHECK(dm_pad_trigger_buttons(0.06f, 0.06f) == 0, "flat 区抖动不触发");
    printf("== %d 项，失败 %d ==\n", checks, fails);
    return fails != 0;
}
