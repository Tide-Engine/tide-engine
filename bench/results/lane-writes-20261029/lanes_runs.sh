#!/usr/bin/env bash
export TIDE_HIDDEN=1
B=$(cygpath -w "$PWD/build/final/holes")
H=$(cygpath -w "$PWD/build/final/lanes")
ab() { python build/flip/scripts/ab.py 5 "$1" "$B/$2.exe" "$H/$2.exe" -- "${@:3}" 2>&1 | tail -1; }
for th in 1 4; do
  ab "wide lasting 50% kept, $th threads" versus_wide_lasting --count 1000000 --tagged 1 --frozen 50 --kept 1 --threads $th
  ab "wide lasting 50%, $th threads" versus_wide_lasting --count 1000000 --tagged 1 --frozen 50 --threads $th
  ab "wide dense kept, $th threads" versus_wide --count 1000000 --kept 1 --threads $th
  ab "wide dense, $th threads" versus_wide --count 1000000 --threads $th
done
for t in 0 10 100; do
  ab "sleep moving toggles=$t (client)" versus_sleep_lasting --toggles $t --threads 1
  ab "sleep sparse toggles=$t (client)" versus_sleep --toggles $t --threads 1
done
ab "particles 1M" versus_particles --count 1000000 --threads 1
ab "particles 1M kept" versus_particles --count 1000000 --threads 1 --kept 1
ab "churn 2k" versus_churn --rate 2000 --threads 1
ab "churn 2k kept" versus_churn --rate 2000 --threads 1 --kept 1
ab "rollback" versus_rollback --threads 1
