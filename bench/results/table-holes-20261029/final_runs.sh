#!/usr/bin/env bash
# Final paired timings: base (dev without holes) against holes, alternating, five rounds each.
export TIDE_HIDDEN=1
B=$(cygpath -w "$PWD/build/final/base")
H=$(cygpath -w "$PWD/build/final/holes")
ab() { python build/flip/scripts/ab.py 5 "$1" "$B\\$2.exe" "$H\\$2.exe" -- "${@:3}" 2>&1 | tail -1; }

echo "## Sleep, 100k bodies, 90% asleep, one thread (ms per 12-tick client period)"
for t in 0 10 30 100 300 1000; do
  ab "sparse toggles=$t" versus_sleep --toggles $t --threads 1
  ab "moving toggles=$t" versus_sleep_lasting --toggles $t --threads 1
done
echo "## Sleep, other sizes"
for b in 10000 1000000; do
  for t in 0 10 100; do
    ab "sparse bodies=$b toggles=$t" versus_sleep --bodies $b --toggles $t --threads 1
    ab "moving bodies=$b toggles=$t" versus_sleep_lasting --bodies $b --toggles $t --threads 1
  done
done
echo "## Regression checks (ms per tick)"
ab "particles 1M" versus_particles --count 1000000 --threads 1
ab "wide 1M" versus_wide --count 1000000 --threads 1
ab "wide 1M 50% frozen" versus_wide --count 1000000 --tagged 1 --frozen 50 --threads 1
ab "wide lasting 1M 95% frozen" versus_wide_lasting --count 1000000 --tagged 1 --frozen 95 --threads 1
ab "churn 2k" versus_churn --rate 2000 --threads 1
ab "churn 2k kept" versus_churn --rate 2000 --threads 1 --kept 1
ab "rollback" versus_rollback --threads 1
