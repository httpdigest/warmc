#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
export PATH="$PATH:$HOME/.npm-global/bin"
. test/hostenv.sh
WORK="$BUILD/testrun/bincases"
mkdir -p "$WORK"

LEVELS=${BINCASE_LEVELS:-"-O0 -O1 -O2"}

checks=$(python3 test/gen_binary_cases.py "$WORK") || exit 1
cases=$(printf '%s\n' "$checks" | awk '{print $1}' | sort -u)

pass=0; fail=0
for lv in $LEVELS; do
  flags=${lv//_/ }
  for case in $cases; do
    wasm="$WORK/$case.wasm"
    bin="$WORK/$case$(printf '%s' "$lv" | tr -c 'A-Za-z0-9' '_')"
    wasi=""
    case "$case" in wasi_*) wasi="--wasi";; esac
    if ! wtimeout ./$BUILD/warmc $flags $WARMC_TARGET_FLAGS $wasi -o "$bin" "$wasm" 2>"$bin.err"; then
      echo "FAIL[bincases] $case $flags: $(head -2 "$bin.err")"
      fail=$((fail + 1))
      continue
    fi

    invoke=()
    wants=()
    while read -r c export want; do
      [ "$c" = "$case" ] || continue
      case "$want" in
        trap:*)
          out=$(wruntimeout $WARMC_RUN "$bin" "$export" 2>&1); rc=$?
          if [ "$rc" -eq 1 ] && [[ "$out" == *"${want#trap:}"* ]]; then
            pass=$((pass + 1))
          else
            echo "FAIL[bincases] $case.$export $flags: want trap '${want#trap:}' got rc=$rc: $out"
            fail=$((fail + 1))
          fi
          continue
          ;;
      esac
      [ ${#invoke[@]} -eq 0 ] || invoke+=("--")
      invoke+=("$export")
      wants[${#wants[@]}]="$want"
    done <<< "$checks"
    [ ${#invoke[@]} -eq 0 ] && { rm -f "$bin" "$bin.err"; continue; }

    lines=()
    while IFS= read -r line; do lines[${#lines[@]}]="$line"; done < <(
      wruntimeout $WARMC_RUN "$bin" "${invoke[@]}" 2>/dev/null)

    i=0
    for export in "${invoke[@]}"; do
      [ "$export" = "--" ] && continue
      got=${lines[$i]:-}
      if [ "$got" = "${wants[$i]}" ]; then
        pass=$((pass + 1))
      else
        echo "FAIL[bincases] $case.$export $flags: want ${wants[$i]} got ${got:-<none>}"
        fail=$((fail + 1))
      fi
      i=$((i + 1))
    done
    rm -f "$bin" "$bin.err"
  done
done

echo "bincases: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
