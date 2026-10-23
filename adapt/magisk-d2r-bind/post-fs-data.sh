#!/system/bin/sh
# Keep the phone awake: long display timeout + stay awake while charging.
settings put system screen_off_timeout 1800000 2>/dev/null
settings put global stay_on_while_plugged_in 7 2>/dev/null
