#!/usr/bin/env bash
# Installs the debug APK on the running emulator, runs spindle's four-station self-test inside it and
# collects the report, the log and the screenshots.  Usage: android_selftest.sh APK OUTDIR
set -u
APK="$1"
OUT="$2"
PKG=io.itch.chayce22315.spindle
mkdir -p "$OUT"
adb wait-for-device
adb install -r "$APK" || exit 1
adb logcat -c
adb shell am start -W -n "$PKG/.SpindleActivity" --es args "--selftest"

result=""
for i in $(seq 1 480); do # up to 40 minutes (the self-test runs in real time; emulators are slow)
    sleep 5
    log=$(adb logcat -d 2>/dev/null)
    if grep -q "selftest: PASS" <<<"$log"; then result=PASS; break; fi
    if grep -q "selftest: FAIL" <<<"$log"; then result=FAIL; break; fi
    if [ "$i" -gt 6 ] && ! adb shell pidof "$PKG" >/dev/null 2>&1; then result=EXITED; break; fi
done
sleep 3

adb logcat -d >"$OUT/logcat.txt"
# The app's private files (settings, log, self-test report and screenshots); readable because debuggable.
adb exec-out run-as "$PKG" tar cf - -C files . >"$OUT/files.tar" 2>/dev/null || true
(cd "$OUT" && tar xf files.tar 2>/dev/null; rm -f files.tar) || true

echo "--- spindle's log (SDL/APP) and SDL's activity messages"
grep -E "SDL/APP|[ /]SDL *:|SDLActivity" "$OUT/logcat.txt" | grep -v "selftest: ok " | tail -n 250
echo "--- relaunches and configuration changes (event log)"
adb logcat -b events -d 2>/dev/null | grep -E -i "relaunch|wm_.*$PKG|am_.*$PKG|configuration_changed" | head -n 60
echo "--- the system's lines about the app"
grep -E "$PKG|SpindleActivity" "$OUT/logcat.txt" | grep -v AppsFilter | head -n 80
echo "--- Android's view of the app"
grep -E "ActivityTaskManager|ActivityManager|AndroidRuntime|libc *:|DEBUG *:|Fatal|FATAL|$PKG" "$OUT/logcat.txt" | tail -n 120
if [ -f "$OUT/selftest/selftest_report.txt" ]; then cat "$OUT/selftest/selftest_report.txt"; fi
echo "self-test result: ${result:-TIMEOUT}"
[ "$result" = PASS ]
