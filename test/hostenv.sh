#!/usr/bin/env bash

: "${BUILD:=build}"

# wabt 1.0.42 turned most of these proposals on by default and removed their
# --enable flags, which it now rejects; pass only the ones this wat2wasm still
# lists.  test/hostbuild.py's wabt_flags() makes the same choice.
WABT_FLAGS=""
_wabt_help=$(wat2wasm --help 2>/dev/null)
for _f in exceptions function-references tail-call gc memory64 multi-memory \
          extended-const relaxed-simd annotations; do
  case "$_wabt_help" in *"--enable-$_f "*) WABT_FLAGS="$WABT_FLAGS --enable-$_f" ;; esac
done
WABT_FLAGS=${WABT_FLAGS# }
unset _wabt_help _f
export WABT_FLAGS

case "$(uname -s)/$(uname -m)" in
  Darwin/arm64)

    if ! command -v wasm-ld >/dev/null 2>&1; then
      for d in /opt/homebrew/opt/lld/bin /opt/homebrew/opt/lld@*/bin /usr/local/opt/lld/bin; do
        if [ -x "$d/wasm-ld" ]; then PATH="$PATH:$d"; export PATH; break; fi
      done
    fi

    if [ -z "${WARMC_MACOS_LIBDIR:-}" ]; then
      sdk=$(xcrun --show-sdk-path 2>/dev/null)
      [ -n "$sdk" ] && WARMC_MACOS_LIBDIR="$sdk/usr/lib"
    fi
    export WARMC_MACOS_LIBDIR
    WARMC_TARGET_FLAGS="--target=macos --rt $BUILD/warmrt-macos.a"
    [ -n "${WARMC_MACOS_LD:-}" ] &&
      WARMC_TARGET_FLAGS="$WARMC_TARGET_FLAGS --ld $WARMC_MACOS_LD"
    WARMC_RUN=""
    WARMC_HOST=native-macos
    ;;
  Linux/aarch64)
    WARMC_TARGET_FLAGS="--rt $BUILD/warmrt.a"
    WARMC_RUN=""
    WARMC_HOST=native-linux
    ;;
  MINGW64_NT*/x86_64|CLANG64_NT*/x86_64|MSYS_NT*/x86_64)
    WARMC_TARGET_FLAGS="--target=windows --rt $BUILD/warmrt-windows.a"
    WARMC_RUN=""
    WARMC_HOST=native-windows
    # Accepted NaN policy: x64 hardware generates the canonical quiet NaN
    # with the sign bit SET; the comparison folds exactly that pattern.
    WARMC_X64_NAN=1
    export WARMC_X64_NAN
    ;;
  *)
    WARMC_TARGET_FLAGS="--rt $BUILD/warmrt.a"
    WARMC_RUN="qemu-aarch64"
    WARMC_HOST=qemu
    ;;
esac

export WARMC_TARGET_FLAGS WARMC_RUN WARMC_HOST

: "${WARMC_TIMEOUT:=6}"
: "${WARMC_RUN_TIMEOUT:=30}"
wtimeout() {
  perl -e 'my $t = shift; alarm $t; exec @ARGV or exit 127' "$WARMC_TIMEOUT" "$@"
}
wruntimeout() {
  perl -e 'my $t = shift; alarm $t; exec @ARGV or exit 127' "$WARMC_RUN_TIMEOUT" "$@"
}
export WARMC_TIMEOUT WARMC_RUN_TIMEOUT

# The name of one configuration, for a working directory or an output file
# that a parallel row must not share with another.
#
# The flags alone are not enough to name one.  warmc reads about a hundred
# WARMC_* variables that change what it emits, and two rows can share a flag
# set while differing only in one of them: cases-O2 and cases-farbr both ran
# at -O2 and differed only in WARMC_FAR_BRANCH, so both wrote
# build/testrun/_O2 -- 48 of the 59 objects differ between those two modes,
# and each row linked and checked whatever mixture of them won the race.  The
# far-branch row passed that way while two thirds of it held no far branch at
# all, which is the worse half of the bug: the collision is loud only when the
# relink and the exec happen to overlap.
#
# So anything that reaches the compiler is part of the name.  The plumbing
# above is not: a timeout, how to run the result, and where the SDK lives
# change nothing about the object warmc writes.
WFTAG_IGNORE=" WARMC_FLAGS WARMC_TIMEOUT WARMC_RUN_TIMEOUT WARMC_RUN"
WFTAG_IGNORE="$WFTAG_IGNORE WARMC_HOST WARMC_TARGET_FLAGS"
WFTAG_IGNORE="$WFTAG_IGNORE WARMC_MACOS_LIBDIR WARMC_MACOS_LD "

wftag() {
  local parts="${WARMC_FLAGS:-}" n
  for n in $(compgen -e | grep '^WARMC_' | sort); do
    case "$WFTAG_IGNORE" in *" $n "*) continue ;; esac
    parts="${parts:+$parts }${n}_${!n}"
  done
  printf '%s' "${parts:-default}" | tr -c 'A-Za-z0-9' '_'
}
