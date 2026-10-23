#!/system/bin/sh
PKG=org.d2r.android.port
F=/data/user/0/$PKG/files

echo "=== [1] back up then remove the d2r_driver Magisk module ==="
tar -cf /data/local/tmp/d2r_driver-backup.tar -C /data/adb/modules d2r_driver 2>/dev/null
echo "  backup: $(stat -c %s /data/local/tmp/d2r_driver-backup.tar 2>/dev/null) bytes"
rm -rf /data/adb/modules/d2r_driver
echo "  d2r_driver dir exists? $([ -d /data/adb/modules/d2r_driver ] && echo YES || echo NO)"
echo "  d2r_bindmount kept?    $([ -d /data/adb/modules/d2r_bindmount ] && echo YES || echo NO)"

echo "=== [2] cold start (force-stop, no leftover config) ==="
am force-stop $PKG
sleep 3
[ -f $F/temp/gpu-driver.conf ] && chattr -i $F/temp/gpu-driver.conf 2>/dev/null
rm -f $F/temp/gpu-driver.conf
rm -rf $F/temp/driver
logcat -c 2>/dev/null
: > $F/game.log
am start -n $PKG/.D2RActivity
sleep 45

echo "=== [3] result ==="
PID=$(pidof $PKG)
echo "  pid=$PID  $([ -n "$PID" ] && echo ALIVE || echo DEAD)"
[ -n "$PID" ] && grep -E 'libd2rvlk|vulkan\.adreno' /proc/$PID/maps | awk '{print $6}' | sort -u
echo "  FATAL=$(grep -c FATAL $F/game.log)  CRASH=$(grep -c CRASH $F/game.log)"
grep -E 'actual device|selected=' $F/game.log | tail -3
echo "  gpu-driver.conf after launch: $([ -f $F/temp/gpu-driver.conf ] && echo YES || echo NO)"
screencap -p /data/local/tmp/v7-cold.png
echo "  shot: $(stat -c %s /data/local/tmp/v7-cold.png) bytes"
