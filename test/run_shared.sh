#!/usr/bin/env bash
# warmc --shared: test/shared/basic.wat built as a shared library, called by
# a C program linked against it through the header warmc writes
# (test/shared/host.c) and by Python's ctypes (test/shared/basic.py); and
# what --shared refuses to build.

set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
. test/hostenv.sh
WORK="$BUILD/testrun/shared"
rm -rf "$WORK"
mkdir -p "$WORK"

case "$WARMC_HOST" in
  native-macos) EXT=dylib; TARGET=(--target macos --rt "$BUILD/warmrt-macos.a") ;;
  native-linux) EXT=so; TARGET=(--target linux --rt "$BUILD/warmrt.a") ;;
  native-windows) EXT=dll; TARGET=(--target windows --rt "$BUILD/warmrt-windows.a") ;;
  *) echo "shared: SKIPPED (warmc cannot link a shared library for $WARMC_HOST yet)"
     exit 0 ;;
esac

fail=0
LIB="$WORK/libbasic.$EXT"
# A DLL goes by its own name; a program links against the import library
# llvm-dlltool makes of the .def warmc writes beside it.
[ "$EXT" = dll ] && LIB="$WORK/basic.dll"
if ! ./$BUILD/warmc "${TARGET[@]}" --shared -o "$LIB" test/shared/basic.wat \
       2>"$WORK/build.err"; then
  echo "FAIL[shared]: warmc --shared failed"
  sed 's/^/    /' "$WORK/build.err"
  exit 1
fi
if ! grep -q 'export "splat" gets no C symbol' "$WORK/build.err"; then
  echo "FAIL[shared]: no warning for the v128 export C cannot call"
  fail=1
fi
if [ "$EXT" = dll ] &&
   ! llvm-dlltool -m i386:x86-64 -d "$WORK/basic.def" -l "$WORK/libbasic.dll.a" \
       2>"$WORK/dlltool.err"; then
  echo "FAIL[shared]: no import library from the .def"
  sed 's/^/    /' "$WORK/dlltool.err"
  fail=1
fi

if ! cc -O2 -Wall -Werror -I "$WORK" -o "$WORK/host" test/shared/host.c \
       -L "$WORK" -lbasic -Wl,-rpath,"$WORK" -lpthread 2>"$WORK/host.err"; then
  echo "FAIL[shared]: the C host does not build against the header"
  sed 's/^/    /' "$WORK/host.err"
  fail=1
elif ! "$WORK/host"; then
  fail=1
fi

# The runtime's options reach a library through the environment as they do
# a program: a heap of 4 MB cannot hold a million live nodes.
cat > "$WORK/heap.c" <<'EOF'
#include <string.h>
#include "basic.h"
static int64_t twice(void *ctx, int64_t v) { (void)ctx; return 2 * v; }
int main(void) {
  basic_bind_import("env", "twice", (void *)twice, 0);
  return list_sum(1000000) != 0 ||
         strcmp(basic_trap_name(basic_last_trap()), "allocation failed") != 0;
}
EOF
if ! cc -Wall -Werror -I "$WORK" -o "$WORK/heap" "$WORK/heap.c" -L "$WORK" \
       -lbasic -Wl,-rpath,"$WORK" 2>"$WORK/heap.err"; then
  echo "FAIL[shared]: the heap check does not build"
  sed 's/^/    /' "$WORK/heap.err"
  fail=1
elif ! WARMC_GC_HEAP_KB=4096 "$WORK/heap" || "$WORK/heap"; then
  echo "FAIL[shared]: WARMC_GC_HEAP_KB does not reach the library"
  fail=1
fi

cat > "$WORK/cxx.cc" <<'EOF'
#include "basic.h"
static int64_t twice(void *, int64_t v) { return 2 * v; }
int main() {
  basic_bind_import("env", "twice", reinterpret_cast<void *>(twice), nullptr);
  return add(1, 2) != 3 || twice_plus_one(3) != 7;
}
EOF
if ! c++ -Wall -Werror -I "$WORK" -o "$WORK/cxx" "$WORK/cxx.cc" -L "$WORK" \
       -lbasic -Wl,-rpath,"$WORK" 2>"$WORK/cxx.err" || ! "$WORK/cxx"; then
  echo "FAIL[shared]: the header does not serve C++"
  sed 's/^/    /' "$WORK/cxx.err"
  fail=1
fi

python3 test/shared/basic.py "$LIB" || fail=1

# Java's foreign function and memory API, final since Java 22: the java of
# $JAVA_HOME, or the one on PATH.
JAVA=java
[ -n "${JAVA_HOME:-}" ] && JAVA="$JAVA_HOME/bin/java"
JV=$("$JAVA" -version 2>&1 | sed -n 's/.*version "\([0-9]*\).*/\1/p' | head -1)
if [ -z "$JV" ]; then
  echo "shared/java: SKIPPED (no java)"
elif [ "$JV" -lt 22 ]; then
  echo "shared/java: SKIPPED (java $JV predates the foreign function API of 22)"
else
  "$JAVA" --enable-native-access=ALL-UNNAMED test/shared/Basic.java "$LIB" || fail=1
fi

# What a library cannot be: one that imports anything but functions, and a
# component.
refuse() {
  local what=$1 want=$2 src=$3
  shift 3
  printf '%s' "$src" > "$WORK/refuse.wat"
  if ./$BUILD/warmc "${TARGET[@]}" --shared "$@" -o "$WORK/refuse.$EXT" \
       "$WORK/refuse.wat" 2>"$WORK/refuse.err"; then
    echo "FAIL[shared]: --shared built $what"
    fail=1
  elif ! grep -q "$want" "$WORK/refuse.err"; then
    echo "FAIL[shared]: --shared refused $what without saying $want"
    sed 's/^/    /' "$WORK/refuse.err"
    fail=1
  fi
}
refuse "a module importing memory" "which is not a function" \
  '(module (import "env" "m" (memory 1)))'
refuse "a module whose import returns two results" "more than one result" \
  '(module (import "env" "f" (func (result i32 i32))))'
refuse "a --wasi library" "no wasi host" '(module)' --wasi
refuse "a library without export names" "export names" '(module)' \
  --no-export-names

[ "$fail" = 0 ] && echo "shared: a library called from C, C++, Python and Java, its options from the environment, and four refusals"
exit $fail
