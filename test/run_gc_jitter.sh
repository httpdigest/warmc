#!/usr/bin/env bash

# Sweep GC schedule seeds through the jitter probe, all seeds concurrently.
#
# The full mode matrix already runs once per battery (run_gc_modes.sh in
# run_all.sh); what a seed sweep adds is schedule-space coverage, and the
# schedule only matters to the modes that inject jitter.  So the default
# sweep gives each seed ONE of the three jitter modes, round-robin, running
# JOBS seeds at a time: 64 seeds in seconds, not minutes.  The binaries are
# shared across every instance (they key on WARMC_FLAGS alone), so past the
# first seed the sweep is pure run time.
#
# JITTER_FULL=1 restores the old shape -- the entire matrix under every
# seed, GC_JITTER_JOBS-wide inside a seed and GC_JITTER_SEED_JOBS seeds at
# a time -- for when a jitter suspect needs the deep net.
set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
export BUILD

SEEDS=${JITTER_SEEDS:-64}
RATE=${JITTER_RATE:-8}
if [ -n "${JITTER_FULL:-}" ]; then
  SJOBS=${GC_JITTER_SEED_JOBS:-4}
else
  # Each instance is ~70% CPU (the rest is process spawn), so the sweet
  # spot is past the core count; wall is flat from 24 up on 12 cores.
  _cores=$( (sysctl -n hw.logicalcpu 2>/dev/null || nproc 2>/dev/null) || echo 8)
  SJOBS=${GC_JITTER_SEED_JOBS:-$((_cores * 2))}
fi

OUT=$(mktemp -d) || exit 1
trap 'rm -rf "$OUT"' EXIT

SMALL="GC_N=5000 GC_M=500 GC_L=20"

run_seed() {
  local s="$1" out rc
  if [ -n "${JITTER_FULL:-}" ]; then
    out=$(GC_MODES_JOBS=${GC_JITTER_JOBS:-6} WARMC_GC_JITTER=$RATE \
          WARMC_GC_JITTER_SEED=$s ./test/run_gc_modes.sh 2>&1); rc=$?
  else
    local mode envs
    case $((s % 3)) in
      0) mode=conc-jitter
         envs="WARMC_GC_MIN_KB=4 $SMALL WARMC_GC_VERIFY=1";;
      1) mode=reloc-conc-jitter
         envs="WARMC_GC_MIN_KB=4 $SMALL WARMC_GC_RELOC=8 WARMC_GC_VERIFY=1";;
      2) mode=rounds-jitter
         envs="WARMC_GC_MIN_KB=64 $SMALL WARMC_GC_ROUNDS=1";;
    esac
    out=$(env $envs WARMC_GC_JITTER=$RATE WARMC_GC_JITTER_SEED=$s \
          WARMC_FLAGS=-O2 ./test/run_gc.sh 2>&1); rc=$?
    [ $rc -ne 0 ] && out="[$mode] $out"
  fi
  if [ $rc -ne 0 ]; then
    { echo "SEED $s"; echo "$out"; } > "$OUT/$s.log"
    echo x > "$OUT/$s.fail"
  fi
}

s=1
while [ "$s" -le "$SEEDS" ]; do
  run_seed "$s" &
  while [ "$(jobs -rp | wc -l)" -ge "$SJOBS" ]; do wait -n 2>/dev/null || break; done
  s=$((s+1))
done
wait

fail=0
for f in "$OUT"/*.fail; do [ -e "$f" ] && fail=1; break; done
if [ "$fail" -ne 0 ]; then
  for f in "$OUT"/*.log; do [ -e "$f" ] && cat "$f"; done
  exit 1
fi
echo "gc-jitter: $SEEDS seeds clean"
