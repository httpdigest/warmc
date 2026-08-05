#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
export PATH="$PATH:$HOME/.npm-global/bin"
. test/hostenv.sh
WORK="$BUILD/testrun/pressure"
mkdir -p "$WORK"

export WARMC_VERIFY_RA=1

ARGS="1 2 5 17"
LEVELS=${PRESSURE_LEVELS:-"-O0 -O1 -O2 -O2_-fno-cse -O2_-fno-regalloc -O2_-fno-guard-pages -O2_-fno-inline"}

names=$(python3 test/gen_pressure.py "$WORK") || exit 1
wat="$WORK/pressure.wat"
wasm="$WORK/pressure.wasm"
wat2wasm $WABT_FLAGS "$wat" -o "$wasm" || { echo "FAIL[pressure]: wat2wasm"; exit 1; }

sum=$(shasum "$wasm" | cut -d' ' -f1)
cache="$WORK/want.$sum"
nwant=0
for name in $names; do for n in $ARGS; do nwant=$((nwant + 1)); done; done
if [ ! -s "$cache" ] || [ "$(wc -l < "$cache")" -ne "$nwant" ]; then
  rm -f "$WORK"/want.*
  : > "$cache.partial"
  for name in $names; do
    for n in $ARGS; do
      w=$(wasm-interp $WABT_FLAGS "$wasm" -r "$name" -a "i32:$n" 2>&1 | tail -1)
      w=${w#*=> i32:}
      case $w in
        ''|*[!0-9-]*) echo "FAIL[pressure]: wasm-interp gave no i32 for $name($n): $w"; exit 1;;
      esac
      echo "$(( w & 0xffffffff ))" >> "$cache.partial"
    done
  done
  mv "$cache.partial" "$cache"
fi
want=()
while IFS= read -r line; do want[${#want[@]}]="$line"; done < "$cache"

invoke=()
for name in $names; do
  for n in $ARGS; do
    [ ${#invoke[@]} -eq 0 ] || invoke+=("--")
    invoke+=("$name" "$n")
  done
done

pass=0; fail=0
for lv in $LEVELS; do
  flags=${lv//_/ }
  bin="$WORK/bin_${lv}"
  if ! wtimeout ./$BUILD/warmc $flags $WARMC_TARGET_FLAGS -o "$bin" "$wasm" 2>"$bin.err"; then
    echo "FAIL[pressure] $flags: $(head -2 "$bin.err")"
    fail=$((fail + ${#want[@]}))
    continue
  fi
  lines=()
  while IFS= read -r line; do lines[${#lines[@]}]="$line"; done < <(
    wruntimeout $WARMC_RUN "$bin" "${invoke[@]}" 2>/dev/null)
  i=0
  for name in $names; do
    for n in $ARGS; do
      g=${lines[$i]:-}
      g=${g#i32:}
      if [ -n "$g" ] && [ "$(( g & 0xffffffff ))" = "${want[$i]}" ]; then
        pass=$((pass+1))
      else
        [ "$fail" -lt 20 ] && echo "FAIL[pressure] $name n=$n $flags: want ${want[$i]} got ${g:-<none>}"
        fail=$((fail+1))
      fi
      i=$((i+1))
    done
  done
  rm -f "$bin" "$bin.err"
done

echo "pressure: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
