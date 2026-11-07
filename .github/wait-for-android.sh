#!/usr/bin/env bash
# Waits until the emulator check.yml started ("Start the Android emulator")
# says Android booted. An emulator that ended first, or hasn't booted 15
# minutes after it started, fails the step with what it said.
set -euo pipefail
adb="$ANDROID_HOME/platform-tools/adb"
until [ "$("$adb" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ]; do
  if ! kill -0 "$ANDROID_EMULATOR_PID" 2>/dev/null || [ "$(date +%s)" -gt "$ANDROID_EMULATOR_DEADLINE" ]; then
    cat "$RUNNER_TEMP/emulator.log"
    exit 1
  fi
  sleep 2
done
