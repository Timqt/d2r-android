// 多指输入行为的回归：按住走路同时施放技能，独立抬手与系统取消都不能卡键。
// Input behavior regression: movement plus a held skill, independent releases and system cancellation must not stick.
package org.d2r.android.port;

public final class TouchPadStateTest {
    private static int checks;
    private static void check(boolean condition, String message) {
        checks++;
        if (!condition) throw new AssertionError(message);
    }

    public static void main(String[] args) {
        TouchPadState pad = new TouchPadState();
        pad.pressStick(7, 0, 1, 0);
        pad.pressButton(19, 1, TouchPadState.A);
        check(pad.leftX() == 32767 && pad.leftY() == 0 && pad.buttons() == TouchPadState.A,
            "Movement and skill must stay held together");
        pad.release(19, 1);
        check(pad.leftX() == 32767 && pad.buttons() == 0, "Lifting skill finger must keep movement");
        pad.pressButton(19, 1, TouchPadState.B);
        pad.release(7, 0);
        check(pad.leftX() == 0 && pad.leftY() == 0 && pad.buttons() == TouchPadState.B,
            "Lifting movement finger must keep skill and never synthesize A");
        pad.pressButton(11, 1, TouchPadState.B);
        pad.release(19, 1);
        check(pad.buttons() == TouchPadState.B, "Two fingers on one button need independent ownership");
        pad.pressButton(26, 3, TouchPadState.ZR);
        pad.cancelSource(1);
        check(pad.buttons() == TouchPadState.ZR, "Cancelling face-button window must retain shoulder window");
        pad.pressStick(3, 0, 0, 1);
        pad.pressStick(4, 0, -1, 0);
        check(pad.leftX() == 0 && pad.leftY() == 32767 && pad.stickPointer() == 3,
            "Second stick finger must not steal the first finger");
        pad.release(3, 9);
        check(pad.leftY() == 32767, "Another window must not release a held finger");
        pad.moveStick(3, 0, 0.1f, 0.1f);
        check(pad.leftX() == 0 && pad.leftY() == 0, "Small movement inside radial dead zone must stay still");
        pad.moveStick(3, 0, 4, 4);
        check(pad.leftX() > 23000 && pad.leftY() > 23000 &&
            Math.hypot(pad.leftX(), pad.leftY()) <= 32768, "Diagonal movement must clamp its magnitude");
        pad.moveStick(3, 0, Float.NaN, 0);
        check(pad.leftX() == 0 && pad.leftY() == 0, "Invalid coordinates must stop movement");
        pad.moveButton(26, 3, 0);
        check(pad.buttons() == 0, "Dragging out of a shoulder button must release it");
        pad.moveButton(26, 3, TouchPadState.R);
        check(pad.buttons() == TouchPadState.R, "Dragging back onto a button must press it again");
        pad.clear();
        check(pad.buttons() == 0 && pad.leftX() == 0 && pad.leftY() == 0 && pad.stickPointer() == -1,
            "Pause and destruction must release every source");
        pad.moveStick(3, 0, 1, 0);
        pad.moveButton(26, 3, TouchPadState.A);
        check(pad.buttons() == 0 && pad.leftX() == 0, "Old move events after cancellation must not resurrect input");
        pad.pressButton(31, 4, TouchPadState.DOWN);
        pad.pressButton(-1, 4, TouchPadState.Y);
        pad.pressButton(32, 4, TouchPadState.X);
        check(pad.buttons() == TouchPadState.DOWN, "All legal Android pointer IDs, and only those IDs, must work");
        pad.cancelSource(4);
        pad.pressStick(7, 0, -1, -1);
        check(pad.leftX() < 0 && pad.leftY() < 0, "Reused finger IDs must work after cancellation");
        System.out.println("Touch pad state PASS " + checks + " behavior checks");
    }
}
