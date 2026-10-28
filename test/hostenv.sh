#!/usr/bin/env bash

: "${BUILD:=build}"

# TinyGo's wasip2 target runs wasm-tools itself to make the component, and a
# CI runner has none on PATH: give it the pinned one `make wasm-tools` builds.
if [ -x "$BUILD/tools/bin/wasm-tools" ]; then
  case "$BUILD" in
    /*) PATH="$BUILD/tools/bin:$PATH" ;;
    *)  PATH="$PWD/$BUILD/tools/bin:$PATH" ;;
  esac
  export PATH
fi

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
WFTAG_IGNORE="$WFTAG_IGNORE WARMC_MACOS_LIBDIR WARMC_MACOS_LD"
# the caches (warmc's WARMC_CACHE, test/testcache.py's), which change no output
WFTAG_IGNORE="$WFTAG_IGNORE WARMC_CACHE WARMC_NO_TESTCACHE WARMC_TESTCACHE_MB"
WFTAG_IGNORE="$WFTAG_IGNORE WARMC_TESTCACHE_REFS_ONLY "

# Compile to OUT by way of a directory of its own: what warmc writes is under
# OUT's own name, which a macOS program's signature carries, so that the same
# compile makes the same bytes run after run (and warmc's WARMC_CACHE finds
# it), and it takes OUT's place whole.  wbuild OUT WARMC-ARGS...
wbuild() {
  local out="$1" d
  shift
  d=$(mktemp -d "${out%/*}/.build.XXXXXX") || return 1
  # (where OUT is already the same file, a hard link of warmc's cache,
  # mv leaves the new name in place: the directory goes whole)
  if wtimeout "$@" -o "$d/${out##*/}"; then
    mv -f "$d/${out##*/}" "$out"
    rm -rf "$d"
  else
    rm -rf "$d"
    return 1
  fi
}

# warmc keeps what each compile made under what it made it of (src/main.c's
# WARMC_CACHE): a suite compiles every program every time, and the ones
# nothing changed for come back as hard links, by their contents -- never by
# which file is newer.
export WARMC_CACHE="${WARMC_CACHE:-$PWD/$BUILD/warmc-cache}"

wftag() {
  local parts="${WARMC_FLAGS:-}" n
  for n in $(compgen -e | grep '^WARMC_' | sort); do
    case "$WFTAG_IGNORE" in *" $n "*) continue ;; esac
    parts="${parts:+$parts }${n}_${!n}"
  done
  local tag
  tag=$(printf '%s' "${parts:-default}" | tr -c 'A-Za-z0-9' '_')
  # a file name holds 255 bytes: a long configuration, such as one of
  # test/run_combos.py's, is named by its start and a checksum of the whole
  [ ${#tag} -gt 80 ] && tag="${tag:0:40}_$(printf '%s' "$tag" | cksum | cut -d' ' -f1)"
  printf '%s' "$tag"
}
