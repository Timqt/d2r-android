#!/system/bin/sh
PKG=org.d2r.android.port
F=/data/user/0/$PKG/files
echo "=== FINAL STATE ==="
echo "-- installed apk --"
pm path $PKG
echo "  userId: $(dumpsys package $PKG | grep -o 'userId=[0-9]*' | head -1)"
echo
echo "-- data intact --"
echo "  save      : $(ls $F/save | tr '\n' ' ')"
echo "  save files: $(find $F/save -type f | wc -l)"
echo "  data pack : $(du -sh $F/d2r-package | cut -f1)"
echo
echo "-- no leftovers of the old mechanism --"
echo "  files/temp/gpu-driver.conf : $([ -f $F/temp/gpu-driver.conf ] && echo PRESENT || echo absent)"
echo "  files/temp/driver          : $([ -d $F/temp/driver ] && echo PRESENT || echo absent)"
echo "  magisk d2r_driver          : $([ -d /data/adb/modules/d2r_driver ] && echo PRESENT || echo removed)"
echo "  magisk d2r_bindmount       : $([ -d /data/adb/modules/d2r_bindmount ] && echo PRESENT || echo MISSING)"
echo "  rollback tar               : $([ -f /data/local/tmp/d2r_driver-backup.tar ] && echo PRESENT || echo missing)"
echo
echo "-- data package still on f2fs (bind mount alive) --"
stat -c '  st_dev=%d %n' /storage/emulated/0/Android/data/org.d2r.android.port/files/d2r-package/romfs/data 2>/dev/null
echo "  (expect st_dev=64522)"
echo
echo "-- runtime --"
PID=$(pidof $PKG)
echo "  pid=$PID  $([ -n "$PID" ] && echo ALIVE || echo DEAD)"
echo "  FATAL=$(grep -c FATAL $F/game.log) CRASH=$(grep -c CRASH $F/game.log)"
grep -E 'actual device' $F/game.log | tail -1
echo
echo "-- portrait lock still on (rotation kills the game silently) --"
echo "  accelerometer_rotation=$(settings get system accelerometer_rotation)  user_rotation=$(settings get system user_rotation)"
