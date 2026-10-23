#!/usr/bin/env python3
"""APK 版本折算的回归。

版本名要能区分构建（否则系统把两个构建当成同一个版本，覆盖安装只提示"重新安装"）；
版本号必须**单调递增** —— Android 会拒绝版本号倒退的安装，把正常升级当成降级挡掉。

Regression for the APK version mapping: the name has to tell builds apart, and the code has to move forward --
Android refuses an install whose version code goes backwards, which would turn a normal upgrade into a rejected
downgrade.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import build  # noqa: E402  （先把 tools 目录放进 sys.path | the tools directory must be importable first）

checks = 0


def check(condition, message):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(message)
    print("  ✓ " + message)


# 发布版本：名字带本移植的后缀，号码按 主*10000 + 次*100 + 修订 折。
check(build.version_name("v0.3") == "0.3-android-port", "v0.3 的版本名是 0.3-android-port")
check(build.version_code("v0.3") == 300, "v0.3 的版本号是 300")
check(build.version_code("v1.2.3") == 10203, "v1.2.3 的版本号是 10203")
check(build.version_code("v0.10") == 1000, "v0.10 排在 v0.3 之后")

# 单调：后一个 tag 的号码必须更大。
tags = ["v0.1", "v0.2", "v0.3", "v0.10", "v1.0", "v1.2.3", "v2.0.0"]
codes = [build.version_code(tag) for tag in tags]
check(codes == sorted(codes) and len(set(codes)) == len(codes), "发布 tag 的版本号严格递增：" + str(codes))

# 分支与 dev 构建固定为 1：按提交折出来的数字会忽大忽小，会把正常安装挡掉。
for version in ("dev", "main-78fb2612", "main-0a1b2c3d"):
    check(build.version_code(version) == 1, "%s 的版本号固定为 1（不参与降级判断）" % version)
    check(build.version_name(version) == version + "-android-port", "%s 的版本名带移植后缀" % version)

# 带前导 0 的次版本号不能按"字符串"或"小数"解释。
check(build.version_code("v0.10") > build.version_code("v0.9"), "v0.10 的号码大于 v0.9（不是按小数比）")

print("test_version： %d 项通过" % checks)
