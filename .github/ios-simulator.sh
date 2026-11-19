#!/bin/bash
# Boots the newest iPhone the runner's simulators have, and prints its identifier once it's ready to start programs
# (xcrun simctl spawn <identifier> ...).
set -euo pipefail
device=$(xcrun simctl list devices available --json | node -e '
  const all = JSON.parse(require("node:fs").readFileSync(0, "utf8")).devices;
  const runtimes = Object.keys(all).filter((name) => /SimRuntime[.]iOS-/.test(name)).sort((a, b) =>
    a.localeCompare(b, undefined, { numeric: true }));
  for (const runtime of runtimes.reverse()) {
    const phone = all[runtime].find((d) => d.isAvailable && /^iPhone/.test(d.name));
    if (phone) { console.log(phone.udid); process.exit(0); }
  }
  console.error("no iPhone simulator is installed");
  process.exit(1);')
xcrun simctl boot "$device" >&2 || true # Booted already
xcrun simctl bootstatus "$device" >&2
echo "$device"
