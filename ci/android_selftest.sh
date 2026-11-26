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
for i in $(seq 1 240); do # up to 20 minutes
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

grep -E "selftest: (ok|FAIL|PASS)|boot [0-9]+/10|deps:|display:|audio:|fatal|AndroidRuntime|FATAL" "$OUT/logcat.txt" | tail -n 200
if [ -f "$OUT/selftest/selftest_report.txt" ]; then cat "$OUT/selftest/selftest_report.txt"; fi
echo "self-test result: ${result:-TIMEOUT}"
[ "$result" = PASS ]
