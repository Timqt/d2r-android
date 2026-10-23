#!/usr/bin/env python3
# 把上游 APK 改成 Adreno 650 的成品。对 APK 做三件事：
#   1. 删掉 assets/turnip/（启动器不再提供 Mesa Turnip 这一档）
#   2. 把 6 个闭源 Adreno 840 驱动文件注入 lib/arm64-v8a/
#   3. 在 libdmandroid.so 里把 "libvulkan.so" **等长**换成 "libd2rvlk.so"
#      —— 端口的 dlopen("libvulkan.so") 因此落到我们自己的转发 shim，而不是系统 loader
#
# 路径默认取本仓库的布局；可用环境变量覆盖：
#   D2R_SRC_APK     待改的基线 APK   默认 apk/D2R-Android-port.apk
#   D2R_DRIVER_DIR  驱动文件目录      默认 drivers/Qualcomm_840
#   D2R_SHIM        转发 shim         默认 adapt/prebuilt/libd2rvlk.so
#   D2R_OUT         改完的产物        默认 apk-patch/patched-unsigned.apk
import os, re, sys, zipfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SRC_APK = os.environ.get("D2R_SRC_APK", os.path.join(REPO, "apk", "D2R-Android-port.apk"))
QDIR = os.environ.get("D2R_DRIVER_DIR", os.path.join(REPO, "drivers", "Qualcomm_840"))
SHIM = os.environ.get("D2R_SHIM", os.path.join(REPO, "adapt", "prebuilt", "libd2rvlk.so"))
OUT = os.environ.get("D2R_OUT", os.path.join(REPO, "apk-patch", "patched-unsigned.apk"))

# "libvulkan.so" is 12 characters; the replacement MUST be 12 as well, otherwise the
# string would no longer fit in place and every relocation after it would be wrong.
OLD = b"libvulkan.so\x00"          # 12 chars + NUL = 13 bytes
NEW = b"libd2rvlk.so\x00"         # 12 chars + NUL = 13 bytes
TARGET = "lib/arm64-v8a/libdmandroid.so"

DRIVER_FILES = [
    "vulkan.adreno.so", "notgsl.so", "notllvm-glnext.so",
    "notllvm-qgl.so", "notadreno_utils.so", "libdmabufheap.so",
]
DROP_PREFIX = "assets/turnip/"
DROP_EXACT = {"META-INF/MANIFEST.MF", "META-INF/ANDROIDD.SF", "META-INF/ANDROIDD.RSA"}

for f in DRIVER_FILES:
    if not os.path.exists(os.path.join(QDIR, f)):
        print("  MISSING %s (in %s)" % (f, QDIR), file=sys.stderr)
        sys.exit(1)
if not os.path.exists(SHIM):
    print("  MISSING shim %s" % SHIM, file=sys.stderr)
    sys.exit(1)
if not os.path.exists(SRC_APK):
    print("  MISSING base APK %s" % SRC_APK, file=sys.stderr)
    sys.exit(1)
assert len(OLD) == len(NEW), "patch must be length preserving"

os.makedirs(os.path.dirname(OUT), exist_ok=True)

patched_lib = None
src_zip = zipfile.ZipFile(SRC_APK, "r")
kept = dropped = added = 0
with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED) as out:
    for info in src_zip.infolist():
        n = info.filename
        if n in DROP_EXACT or n.startswith(DROP_PREFIX):
            dropped += 1
            continue
        data = src_zip.read(n)
        if n == TARGET:
            hits = [m.start() for m in re.finditer(re.escape(OLD), data)]
            if len(hits) != 1:
                print("  ABORT: %d occurrences of %r, expected exactly 1" % (len(hits), OLD), file=sys.stderr)
                sys.exit(1)
            off = hits[0]
            data = data[:off] + NEW + data[off + len(OLD):]
            patched_lib = (off, len(data))
            print("=== patched %s ===" % n)
            print("  offset=%d  %r -> %r" % (off, OLD, NEW))
        ct = zipfile.ZIP_STORED if n.startswith("lib/") and n.endswith(".so") else info.compress_type
        ni = zipfile.ZipInfo(n, date_time=info.date_time)
        ni.compress_type = ct
        ni.external_attr = info.external_attr
        out.writestr(ni, data)
        kept += 1
    for f in DRIVER_FILES + ["libd2rvlk.so"]:
        path = os.path.join(QDIR, f) if f != "libd2rvlk.so" else SHIM
        blob = open(path, "rb").read()
        ni = zipfile.ZipInfo("lib/arm64-v8a/" + f, date_time=(2026, 10, 19, 0, 0, 0))
        ni.compress_type = zipfile.ZIP_STORED
        ni.external_attr = 0o755 << 16
        out.writestr(ni, blob)
        added += 1
src_zip.close()

if patched_lib is None:
    print("  ABORT: target library never seen", file=sys.stderr)
    sys.exit(1)
print("  libdmandroid.so size unchanged: %d" % patched_lib[1])
print()
print("=== patched ===")
print("  kept=%d dropped=%d added=%d size=%d" % (kept, dropped, added, os.path.getsize(OUT)))
print("  out=%s" % OUT)
print("  next: zipalign -> sign (see README)")
