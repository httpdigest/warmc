#!/usr/bin/env bash
# The pinned release binaries the jobs need besides LLVM, for this host:
# wasmtime and wabt, which the test suite checks warmc against, and ccache,
# which the build compiles through, each into build/ci/<tool> and onto the
# job's PATH; and wasm-tools where `make check` looks for it, so that it is
# never built with cargo here.
#
# The versions are pinned (wabt 1.0.42 is the release whose proposals
# test/hostenv.sh expects) and every download is checked against its SHA-256
# before use.  Each tool directory holds a stamp naming its version, and a
# tool whose directory a restored cache brought back is not downloaded again.

set -euo pipefail

WASMTIME_VERSION=50.0.1
WABT_VERSION=1.0.42
CCACHE_VERSION=4.14.2
# Must match the Makefile's WASM_TOOLS_VERSION; where it does not, make builds
# the Makefile's version with cargo as it would anywhere else.
WASM_TOOLS_VERSION=1.263.0

case "$(uname -s)/$(uname -m)" in
  Darwin/arm64)
    wasmtime_asset=wasmtime-v$WASMTIME_VERSION-aarch64-macos.tar.xz
    wasmtime_sha=960e5216c7ebd7e9b45e6561d15821b8b801b7d54aee28c7b91921aeb30c65c8
    wabt_asset=wabt-$WABT_VERSION-macos-arm64.tar.gz
    wabt_sha=3f654779b436c628db3ca4323c51538815f5fbc887a6d5f3bb1c18d36281e240
    ccache_asset=ccache-$CCACHE_VERSION-darwin.tar.gz
    ccache_sha=cf240009630b605b775c7c5c7b7105a3b196c79f31af322c1557969aa28003fc
    wasm_tools_asset=wasm-tools-$WASM_TOOLS_VERSION-aarch64-macos.tar.gz
    wasm_tools_sha=b568f04746a699c4068b8d751779b60b501cf2cab6d259a3bc973eb5eeb1f62b
    ;;
  Linux/aarch64)
    wasmtime_asset=wasmtime-v$WASMTIME_VERSION-aarch64-linux.tar.xz
    wasmtime_sha=d737e077416d78fbfb0a008229ec27d6a0fd5a4924874b8c38b293c850cf4094
    wabt_asset=wabt-$WABT_VERSION-linux-arm64.tar.gz
    wabt_sha=a7632c1cc66f70886441842ecf5a2ffa91a98c79e026059458451234ad013ea8
    ccache_asset=ccache-$CCACHE_VERSION-linux-aarch64-glibc.tar.gz
    ccache_sha=bc537a1f29232be50be3219ffe80c7c1955bc80a99c75311d7fb839828c80dd0
    wasm_tools_asset=wasm-tools-$WASM_TOOLS_VERSION-aarch64-linux.tar.gz
    wasm_tools_sha=773b5e6bf957f0d93a9a337c9c5fc4c35509706301a80799f57a76fa367fd1ca
    ;;
  MINGW64_NT*/x86_64|CLANG64_NT*/x86_64|MSYS_NT*/x86_64)
    wasmtime_asset=wasmtime-v$WASMTIME_VERSION-x86_64-windows.zip
    wasmtime_sha=c3646ee2de8cfcefe532e75852e1fd2728d78c3fcc43251794803a698d1c30ca
    wabt_asset=wabt-$WABT_VERSION-windows-x64.tar.gz
    wabt_sha=62f1eb2b51aa57cf0f0b8ba333bf2c72a049930aa9c5de01dcad271c4fe48c88
    ccache_asset=ccache-$CCACHE_VERSION-windows-x86_64.zip
    ccache_sha=6993ce645e53192cec592ee23186200b574ac4c59097ecde8d9cfd681b47ae84
    wasm_tools_asset=wasm-tools-$WASM_TOOLS_VERSION-x86_64-windows.zip
    wasm_tools_sha=0803df158b4dbea6b029d11f4206c414f432f0447af91542817061e4f0bd912e
    ;;
  *)
    echo "install-tools: no pinned binaries for $(uname -s)/$(uname -m)" >&2
    exit 1
    ;;
esac

sha256() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
  else shasum -a 256 "$1" | cut -d' ' -f1
  fi
}

path_add() {
  [ -n "${GITHUB_PATH:-}" ] || return 0
  if command -v cygpath >/dev/null 2>&1; then cygpath -wa "$1"
  else (cd "$1" && pwd)
  fi >> "$GITHUB_PATH"
}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# install DIR VERSION URL SHA256: unless DIR holds VERSION's stamp, download
# URL, refuse it unless it hashes to SHA256, and unpack it into DIR with its
# top directory removed.
install() {
  local dir=$1 version=$2 url=$3 want=$4
  [ -e "$dir/.version-$version" ] && return 0
  local file="$work/${url##*/}" got
  curl -fsSL --retry 5 -o "$file" "$url"
  got=$(sha256 "$file")
  if [ "$got" != "$want" ]; then
    echo "install-tools: ${url##*/} has SHA-256 $got, expected $want" >&2
    exit 1
  fi
  rm -rf "$dir" "$work/x"
  mkdir -p "$work/x" "$dir"
  case "$file" in
    *.zip) unzip -q "$file" -d "$work/x" ;;
    *.tar.xz) tar -xJf "$file" -C "$work/x" ;;
    *) tar -xzf "$file" -C "$work/x" ;;
  esac
  mv "$work"/x/*/* "$dir/"
  touch "$dir/.version-$version"
}

install build/ci/wasmtime "$WASMTIME_VERSION" \
  "https://github.com/bytecodealliance/wasmtime/releases/download/v$WASMTIME_VERSION/$wasmtime_asset" \
  "$wasmtime_sha"
install build/ci/wabt "$WABT_VERSION" \
  "https://github.com/WebAssembly/wabt/releases/download/$WABT_VERSION/$wabt_asset" \
  "$wabt_sha"
install build/ci/ccache "$CCACHE_VERSION" \
  "https://github.com/ccache/ccache/releases/download/v$CCACHE_VERSION/$ccache_asset" \
  "$ccache_sha"
path_add build/ci/wasmtime
path_add build/ci/wabt/bin
path_add build/ci/ccache

# wasm-tools goes where the Makefile's rule installs it, with the stamp that
# rule leaves, so `make check` takes it as built.
pinned=$(sed -n 's/^WASM_TOOLS_VERSION *:= *//p' Makefile)
if [ "$pinned" != "$WASM_TOOLS_VERSION" ]; then
  echo "install-tools: the Makefile pins wasm-tools $pinned, this script" \
       "$WASM_TOOLS_VERSION; leaving it to make to build" >&2
elif [ ! -e "build/tools/.wasm-tools-$WASM_TOOLS_VERSION" ]; then
  install "$work/wasm-tools" "$WASM_TOOLS_VERSION" \
    "https://github.com/bytecodealliance/wasm-tools/releases/download/v$WASM_TOOLS_VERSION/$wasm_tools_asset" \
    "$wasm_tools_sha"
  mkdir -p build/tools/bin
  rm -f build/tools/.wasm-tools-*
  cp "$work"/wasm-tools/wasm-tools* build/tools/bin/
  touch "build/tools/.wasm-tools-$WASM_TOOLS_VERSION"
fi

build/ci/wasmtime/wasmtime --version
build/ci/wabt/bin/wat2wasm --version
build/ci/ccache/ccache --version | head -1
if [ -e "build/tools/.wasm-tools-$pinned" ]; then build/tools/bin/wasm-tools --version; fi
