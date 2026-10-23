#!/system/bin/sh
PKG=org.d2r.android.port
F=/data/user/0/$PKG/files

echo "=== let it run 60s ==="
sleep 60

PID=$(pidof $PKG)
echo "=== process ==="
echo "  pid=$PID  $([ -n "$PID" ] && echo ALIVE || echo DEAD)"

echo "=== driver identity ==="
grep -E 'actual device|selected=|identity|requested=' $F/game.log | tail -5

echo "=== FATAL / CRASH count ==="
echo "  FATAL : $(grep -c 'FATAL' $F/game.log)"
echo "  CRASH : $(grep -c 'CRASH' $F/game.log)"

echo "=== frame counter (twice, 8s apart) ==="
A=$(grep -oE 'frame[= ]+[0-9]+' $F/game.log | tail -1)
sleep 8
B=$(grep -oE 'frame[= ]+[0-9]+' $F/game.log | tail -1)
echo "  $A  ->  $B"

echo "=== last log lines ==="
tail -12 $F/game.log

echo "=== gpu faults (kgsl) ==="
dumpsys gpu 2>/dev/null | grep -iE 'kgsl|hang|fault' | head -5
echo "  (none above = clean)"

echo "=== screenshot ==="
screencap -p /data/local/tmp/v7.png
ls -l /data/local/tmp/v7.png

echo "=== still no config file? ==="
echo "  gpu-driver.conf: $([ -f $F/temp/gpu-driver.conf ] && echo YES || echo NO)"
echo "  temp/driver    : $([ -d $F/temp/driver ] && echo YES || echo NO)"
