#!/bin/sh
# Build the in-APK forwarder under a name that is NOT a platform library.
#
# Shipping libvulkan.so does not work: Android resolves a platform soname to the
# platform copy, so dlopen("libvulkan.so") still returned /system/lib64/libvulkan.so
# and pulled the 2021 ICD (Driver Build db3d445dbc, FATAL pipeline_create_compute -13).
# Renaming the string inside libdmandroid.so to an app-only soname makes the linker
# resolve it from the app's own lib/arm64-v8a directory instead.
#
# The replacement MUST be the same length as "libvulkan.so": that name is 12
# characters, so the replacement is 12 too. libd2rvlk.so = lib(3)+d2rvlk(6)+.so(3).
set -e
NAME=libd2rvlk.so
SRC=vk_shim3.c
[ ${#NAME} -eq 12 ] || { echo "NAME must be 12 chars, got ${#NAME}"; exit 1; }

cat > /etc/apt/sources.list <<'EOF'
deb http://mirrors.tuna.tsinghua.edu.cn/debian bookworm main
EOF
rm -f /etc/apt/sources.list.d/debian.sources

apt-get update -qq
apt-get install -y -qq --no-install-recommends gcc-aarch64-linux-gnu libc6-dev-arm64-cross >/dev/null

cd /w

# A stub libdl.so purely to get a DT_NEEDED entry for it: -nostdlib leaves dlopen /
# dlsym with no scope, and a strong undefined symbol makes RTLD_NOW refuse the load
# ("cannot locate symbol dlopen"). The stub is never shipped -- at run time the
# linker resolves the soname "libdl.so" to Android's real libdl.
mkdir -p /tmp/stub
aarch64-linux-gnu-gcc -shared -fPIC -nostdlib -Wl,-soname,libdl.so \
  -o /tmp/stub/libdl.so -x c /dev/null

aarch64-linux-gnu-gcc -shared -fPIC -nostdlib \
  -Wl,-soname,$NAME -Wl,--build-id=none -s \
  -Wl,--no-as-needed -L/tmp/stub -ldl -Wl,--as-needed \
  -o $NAME $SRC

echo "=== built ==="
ls -l $NAME
aarch64-linux-gnu-readelf -d $NAME | grep -E "SONAME|NEEDED"
echo "--- exported / undefined ---"
aarch64-linux-gnu-nm -D $NAME | grep -Ei "vkGet|dlopen|dlsym|syscall"
echo "--- any strong (non-weak) undefined left? must be empty ---"
aarch64-linux-gnu-nm -D --undefined-only $NAME | grep -v " w " || echo "(none)"
echo SHIM3_BUILD_DONE
