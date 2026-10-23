#!/system/bin/sh
PKG=org.d2r.android.port
F=/data/user/0/$PKG/files
T=$F/temp
# 待装的 APK 路径。默认是 README 里 adb push 用的那个；想装别的包就带 D2R_APK 跑。
APK=${D2R_APK:-/data/local/tmp/d2r-new.apk}

am force-stop $PKG
sleep 2
pm install -r $APK
echo "install rc=$?  ($APK)"
echo "save dirs : $(ls $F/save 2>/dev/null | tr '\n' ' ')"
echo "d2r-pkg   : $(du -sh $F/d2r-package 2>/dev/null | cut -f1)"

[ -f $T/gpu-driver.conf ] && chattr -i $T/gpu-driver.conf 2>/dev/null
rm -f $T/gpu-driver.conf
rm -rf $T/driver
[ -d /data/adb/modules/d2r_driver ] && touch /data/adb/modules/d2r_driver/disable

logcat -c 2>/dev/null
: > $F/game.log
am start -n $PKG/.D2RActivity
sleep 35

echo "=== process ==="
PID=$(pidof $PKG)
echo "  pid=$PID"
[ -n "$PID" ] && grep -E 'libd2rvlk|vulkan\.adreno|libadrenotools' /proc/$PID/maps | awk '{print $6}' | sort -u
echo "  gpu-driver.conf after launch: $([ -f $T/gpu-driver.conf ] && echo YES || echo NO)"

echo "=== d2rshim logcat ==="
logcat -d -s d2rshim:D 2>/dev/null | tail -20

echo "=== game.log vulkan lines ==="
grep -nE 'vulkan-loader|Driver Build|actual device|FATAL|CRASH|identity|no vk' $F/game.log | tail -20
