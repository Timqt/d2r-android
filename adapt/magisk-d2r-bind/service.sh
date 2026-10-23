#!/system/bin/sh
# D2R Android: move the game package dir off sdcardfs.
# /data/media/0 stays encrypted until the user unlocks, so wait for it.
REAL=/data/media/0/Android/data/org.d2r.android.port/files/d2r-package
MID=/data/user/0/org.d2r.android.port/files/d2r-ext
VIEW=/storage/emulated/0/Android/data/org.d2r.android.port/files/d2r-package
LOG=/data/local/tmp/d2r-bind.log
: > "$LOG"
# Storage stays locked until the user physically unlocks the phone, which can
# happen long after boot, so keep retrying instead of giving up on a timer.
i=0
while [ "$i" -lt 7200 ]; do
  if [ -d "$REAL/romfs/data" ]; then
    echo "waited ${i}x5s, source present" >> "$LOG"
    break
  fi
  sleep 5
  i=$((i+1))
done
if [ ! -d "$REAL/romfs/data" ]; then
  echo "ABORT: $REAL/romfs/data never appeared" >> "$LOG"
  exit 0
fi
mkdir -p "$MID"
if mount --bind "$REAL" "$MID" >> "$LOG" 2>&1; then
  echo "bind1 ok" >> "$LOG"
else
  echo "bind1 FAILED" >> "$LOG"
  exit 0
fi
if mount --bind "$MID" "$VIEW" >> "$LOG" 2>&1; then
  echo "bind2 ok" >> "$LOG"
else
  echo "bind2 FAILED" >> "$LOG"
fi
stat -c '%d %n' "$VIEW/romfs/data" >> "$LOG" 2>&1
stat -c '%d %n' "$REAL/romfs/data" >> "$LOG" 2>&1
