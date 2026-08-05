#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
. test/hostenv.sh
WORK="$BUILD/testrun/reject"
mkdir -p "$WORK"

cases=$(python3 test/gen_reject_cases.py "$WORK") || exit 1

pass=0; fail=0
while read -r name cls frag; do
  [ -n "$name" ] || continue
  err="$WORK/$name.err"
  # A case may be text (.wat) rather than binary (.wasm); the generator writes
  # whichever it is and warmc detects the kind from the bytes, not the name.
  src="$WORK/$name.wasm"
  [ -f "$WORK/$name.wat" ] && src="$WORK/$name.wat"
  wtimeout ./$BUILD/warmc --validate-only "$src" >/dev/null 2>"$err"
  rc=$?
  case "$cls" in
    malformed) want=2 ;;
    invalid)   want=3 ;;
    accept)    want=0 ;;
    *) echo "FAIL[reject] $name: unknown class $cls"; fail=$((fail + 1)); continue ;;
  esac
  if [ "$rc" != "$want" ]; then
    printf 'FAIL[reject] %s: want %s (exit %s), got exit %s: %s\n' \
      "$name" "$cls" "$want" "$rc" "$(head -1 "$err")"
    fail=$((fail + 1))
    continue
  fi
  if [ -n "$frag" ] && ! grep -qF -- "$frag" "$err"; then
    printf 'FAIL[reject] %s: verdict right, message missing %s: %s\n' \
      "$name" "$frag" "$(head -1 "$err")"
    fail=$((fail + 1))
    continue
  fi
  pass=$((pass + 1))
done <<< "$cases"

echo "reject: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
