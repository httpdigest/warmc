#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."

PREPARE=${1:-}
BUILD=${BUILD:-build}
export PATH="$PATH:$HOME/.npm-global/bin"
. test/hostenv.sh
WORK="$BUILD/testrun"
mkdir -p "$WORK"

SRCOPT=${WORKLOAD_CLANG_OPT:--O2}

SRC=${WORKLOAD_SRC:-test/bench/workload.c}
NAME=$(basename "$(basename "$SRC" .c)" .wat)

case "$SRC" in *.wat) TAG="bench_$NAME";; *) TAG="$NAME${SRCOPT#-}";; esac
WASM="$WORK/$TAG.wasm"
REF="$WORK/$TAG.ref"

FTAG=$(printf '%s' "${WARMC_FLAGS:-default}" | tr -c 'A-Za-z0-9' '_')
BIN="$WORK/$TAG$FTAG"

if [ "$NAME" = addr32 ] || [ "$NAME" = addrmulti ] || [ "$NAME" = addr64 ]; then

  CASES=()
  for k in k_sum k_axpy k_gather k_hist k_bytes k_chase; do
    for a in 0 1 2 3 4 5 15 16 17 1023 1024 2000; do CASES+=("$k $a"); done
  done
  CASES+=("startup 0")
elif [ "$NAME" = simd ]; then
  CASES=()
  for k in s_dot s_bytes s_shuf s_widen s_cmp s_mask s_lane s_i64 s_dotp s_fma; do
    for a in 0 1 2 3 4 255 256 300; do CASES+=("$k $a"); done
  done
  CASES+=("startup 0")
elif [ "$NAME" = control ]; then
  CASES=()
  for k in c_call c_indirect c_callref c_plain c_guard c_region; do
    for a in 0 1 2 3 17 100; do CASES+=("$k $a"); done
  done
  for k in c_throw c_deep; do
    for a in 0 1 2 3 17 64; do CASES+=("$k $a"); done
  done

  for k in c_tail c_tailref; do
    for a in 0 1 2 3 1000 200000; do CASES+=("$k $a"); done
  done
  CASES+=("startup 0")
elif [ "$NAME" = vecwork ]; then

  CASES=(
    "v_axpy 2"  "v_blend 2" "v_scale 3" "v_mix64 2" "v_fmul 2" "v_fsqrt 2"
    "v_sum 2"   "v_xor 2"   "v_bxor 2"  "v_bmul 3"  "v_hadd 2" "v_max 2"
    "v_clamp 2" "v_ptr 2"  "v_miv 2"  "v_wide 2" "v_down 2" "v_deint 2" "v_alt 2"   "v_cload 2" "v_cstore 2" "v_short 2"
    "v_tail 0"  "v_tail 1"  "v_tail 2"  "v_tail 3"  "v_tail 4"  "v_tail 5"
    "v_tail 7"  "v_tail 8"  "v_tail 9"  "v_tail 15" "v_tail 16" "v_tail 17"
    "v_tail 31" "v_tail 32" "v_tail 33" "v_tail 100" "v_tail 2048"
    "startup 0"
  )
else
  CASES=(
    "sha_rounds 200"  "sha_rounds 1000" "sort_check 4096" "sort_check 777"
    "matmul 40"       "matmul 7"        "mix64 20000"     "mix64 3"
    "dispatch 100000" "dispatch 1"      "nbody 5"         "nbody 200"
    "strings 100"     "strings 3"       "bitops 1000"     "bitops 7"
    "startup 0"
  )
fi

case "$SRC" in
  test/bench/addr32.wat|test/bench/addrmulti.wat|test/bench/addr64.wat)

    python3 test/gen_bench.py || exit 1 ;;
esac

if [ "$(cat "$WASM.src" 2>/dev/null)" != "$SRC $SRCOPT" ]; then rm -f "$WASM"; fi
if [ ! -s "$WASM" ] || [ "$SRC" -nt "$WASM" ]; then
  case "$SRC" in
    *.wat) wat2wasm $WABT_FLAGS -o "$WASM" "$SRC" || exit 1 ;;
    *)     clang --target=wasm32 $SRCOPT -nostdlib -ffp-contract=off \
                 -Wl,--no-entry -Wl,--export-dynamic \
                 -o "$WASM" "$SRC" || exit 1 ;;
  esac
  printf '%s %s\n' "$SRC" "$SRCOPT" > "$WASM.src"
fi

CASES_NOW=$(printf '%s\n' "${CASES[@]}")
if [ ! -s "$REF" ] || [ "$WASM" -nt "$REF" ] ||
   [ "$CASES_NOW" != "$(cat "$REF.cases" 2>/dev/null)" ]; then
  : > "$REF"
  for c in "${CASES[@]}"; do
    set -- $c
    want=$(wasm-interp $WABT_FLAGS "$WASM" -r "$1" -a "i32:$2" 2>&1 | tail -1)
    echo "${want#*=> }" >> "$REF"
  done
  printf '%s\n' "${CASES[@]}" > "$REF.cases"
fi

[ "$PREPARE" = --prepare ] && exit 0

wtimeout ./$BUILD/warmc ${WARMC_FLAGS:-} $WARMC_TARGET_FLAGS -o "$BIN" "$WASM" || exit 1

argv=()
for c in "${CASES[@]}"; do
  [ ${#argv[@]} -eq 0 ] || argv+=("--")
  argv+=($c)
done

if [ "$NAME" = vecwork ] && [ "${WARMC_FLAGS:-}" = "-O2" ]; then
  got=$(wtimeout ./$BUILD/warmc -O2 --stats -c -o /dev/null "$WASM" 2>&1 |
        sed -n 's/.*vectorised=\([0-9]*\).*/\1/p')
  if [ "${got:-0}" -lt "${VEC_FLOOR:-22}" ]; then
    echo "FAIL[vecwork]: ${got:-0} loops vectorised, expected ${VEC_FLOOR:-22}"
    exit 1
  fi
fi

if [ "$NAME" = addr64 ] && [ "${WARMC_FLAGS:-}" = "-O2" ]; then
  stats=$(wtimeout ./$BUILD/warmc -O2 --stats -c -o /dev/null "$WASM" 2>&1)
  got=$(printf '%s' "$stats" | sed -n 's/.*checks_removed=\([0-9]*\).*/\1/p')
  if [ "${got:-0}" -lt "${BCE_FLOOR:-19}" ]; then
    echo "FAIL[addr64]: ${got:-0} bounds checks removed, expected ${BCE_FLOOR:-19}"
    exit 1
  fi

  got=$(printf '%s' "$stats" | sed -n 's/.*vectorised=\([0-9]*\).*/\1/p')
  if [ "${got:-0}" -lt "${VEC64_FLOOR:-5}" ]; then
    echo "FAIL[addr64]: ${got:-0} loops vectorised, expected ${VEC64_FLOOR:-5}"
    exit 1
  fi
fi

wruntimeout $WARMC_RUN "$BIN" "${argv[@]}" > "$BIN.got" 2>/dev/null

case "$SRC" in
  *.wat) WHENCE="$NAME.wat" ;;
  *)     WHENCE="clang $SRCOPT wasm32 output, $NAME" ;;
esac
python3 - "$REF" "$BIN.got" "$WHENCE" "${CASES[@]}" <<'PY'
import sys
ref, got, srcopt = sys.argv[1], sys.argv[2], sys.argv[3]
cases = sys.argv[4:]

def norm(tok):
    """Both sides to unsigned hex, so the two printers cannot disagree on
    formatting alone."""
    tok = tok.strip()
    if ":" not in tok:
        return None
    t, v = tok.split(":", 1)
    m = (1 << 32) - 1 if t in ("i32", "f32") else (1 << 64) - 1
    try:
        return "%s:%x" % (t, int(v, 0) & m)
    except ValueError:
        return None

want = [norm(l) for l in open(ref)]
have = [norm(l) for l in open(got)]
p = f = 0
for i, c in enumerate(cases):
    w = want[i] if i < len(want) else None
    g = have[i] if i < len(have) else None
    if w is not None and w == g:
        p += 1
    else:
        f += 1
        print("FAIL[workload] %s: want %s got %s" % (c, w, g))
print("workload: %d passed, %d failed (%s)" % (p, f, srcopt))
sys.exit(1 if f else 0)
PY
