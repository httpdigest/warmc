#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
. test/hostenv.sh
WORK="$BUILD/tinygorun"
WT="$BUILD/tools/bin/wasm-tools"
mkdir -p "$WORK"

if ! command -v tinygo >/dev/null 2>&1; then
  echo "tinygo: SKIPPED (tinygo not on PATH)"
  exit 0
fi
if [ ! -x "$WT" ]; then
  echo "run_tinygo: $WT not found (run \`make wasm-tools\`)" >&2
  exit 1
fi

fail=0

GOLD="$WORK/golden.txt"

cat > "$GOLD" <<'EOF'
hello from tinygo
fib(30) = 832040
sorted head = 1 tail = 9 sum = 28
map alpha 1
map beta 2
map gamma 3
reversed: em esrever
areas = 37
counted to 3
deferred: 42
1.5^10 * 1024 = 59049
EOF

APP="$WORK/app.wasm"
if [ ! -s "$APP" ] || [ test/tinygo/main.go -nt "$APP" ]; then
  tinygo build -o "$APP" -target=wasip1 -scheduler=none test/tinygo/main.go \
    || { echo "FAIL[tinygo]: tinygo build"; exit 1; }
fi

if command -v wasmtime >/dev/null 2>&1; then
  if ! wasmtime run "$APP" > "$WORK/oracle.txt" 2>"$WORK/oracle.err"; then
    echo "FAIL[tinygo]: wasmtime rejected the module"
    sed -n 1,5p "$WORK/oracle.err"
    fail=1
  elif ! cmp -s "$GOLD" "$WORK/oracle.txt"; then
    echo "FAIL[tinygo]: wasmtime disagrees with the golden text"
    diff "$GOLD" "$WORK/oracle.txt" | head -10
    fail=1
  fi
fi

"$WT" parse test/tinygo/wasi_stub.wat -o "$WORK/wasistub.wasm" || exit 1

python3 test/hostbuild.py cc test/tinygo/driver.c "$WORK/driver.o" \
  || { echo "FAIL[tinygo]: driver"; exit 1; }

for lv in -O0 -O1 -O2; do
  wtimeout ./$BUILD/warmc $lv $WARMC_TARGET_FLAGS --symbol-prefix=app \
      -c -o "$WORK/app$lv.o" "$APP" \
    || { echo "FAIL[tinygo]: compiling app at $lv"; fail=1; continue; }
  wtimeout ./$BUILD/warmc $lv $WARMC_TARGET_FLAGS --symbol-prefix=wasistub \
      -c -o "$WORK/stub$lv.o" "$WORK/wasistub.wasm" \
    || { echo "FAIL[tinygo]: compiling stub at $lv"; fail=1; continue; }
  BUILD="$BUILD" python3 test/hostbuild.py link "$WORK/prog$lv" \
      "$WORK/driver.o" "$WORK/app$lv.o" "$WORK/stub$lv.o" \
    || { echo "FAIL[tinygo]: link at $lv"; fail=1; continue; }
  PROG=$(python3 test/hostbuild.py exe "$WORK/prog$lv")
  if ! wruntimeout $WARMC_RUN "$PROG" > "$WORK/out$lv.txt" 2>"$WORK/err$lv.txt"; then
    echo "FAIL[tinygo]: prog$lv exited nonzero"
    sed -n 1,5p "$WORK/err$lv.txt"
    fail=1
    continue
  fi
  if ! cmp -s "$GOLD" "$WORK/out$lv.txt"; then
    echo "FAIL[tinygo]: output at $lv disagrees with the golden text"
    diff "$GOLD" "$WORK/out$lv.txt" | head -10
    fail=1
  fi
done

for lv in -O0 -O1 -O2; do
  if ! wtimeout ./$BUILD/warmc $lv --wasi $WARMC_TARGET_FLAGS \
        -o "$WORK/wasi$lv" "$APP" 2>"$WORK/wasi$lv.link"; then
    echo "FAIL[tinygo]: --wasi build at $lv"
    sed -n 1,5p "$WORK/wasi$lv.link"
    fail=1
    continue
  fi
  if ! wruntimeout $WARMC_RUN "$WORK/wasi$lv" > "$WORK/wasiout$lv.txt" \
        2>"$WORK/wasierr$lv.txt"; then
    echo "FAIL[tinygo]: --wasi at $lv exited nonzero"
    sed -n 1,5p "$WORK/wasierr$lv.txt"
    fail=1
    continue
  fi
  if ! cmp -s "$GOLD" "$WORK/wasiout$lv.txt"; then
    echo "FAIL[tinygo]: --wasi output at $lv disagrees with the golden text"
    diff "$GOLD" "$WORK/wasiout$lv.txt" | head -10
    fail=1
  fi
done

if [ "$fail" -eq 0 ]; then
  echo "tinygo: golden, wasmtime, three warmc levels with a driver and three" \
       "with --wasi all agree"
fi
exit $fail
