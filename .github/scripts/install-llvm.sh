#!/usr/bin/env bash
# The pinned LLVM release, the same on macOS and Linux arm64, unpacked into
# build/ci/llvm with only what the build and the test suite run: clang and
# its resource directory (headers and the sanitizer runtimes), lld under the
# names the tests call it by, the binary tools the encoder and Mach-O checks
# use, and on macOS the libLTO that ld64 links clang's ThinLTO objects with.
# The release is 1.6-1.8 GB, almost all of it static libraries and tools
# nothing here runs; this is about a third of that, and a restored cache
# holding this version is not downloaded again.  Windows takes MSYS2's LLVM.

set -euo pipefail

LLVM_VERSION=23.1.4

case "$(uname -s)/$(uname -m)" in
  Darwin/arm64)
    asset=LLVM-$LLVM_VERSION-macOS-ARM64.tar.xz
    sha=5d3179db39adeca5b17a34d82708fe976bc60e055b05fe557878fb3fb64d39ef
    ;;
  Linux/aarch64)
    asset=LLVM-$LLVM_VERSION-Linux-ARM64.tar.xz
    sha=9b27cf263d711aa6ebfd4045ab241d947bd09b3cd19ffa5728f48a80c9eb3a91
    ;;
  *)
    echo "install-llvm: no pinned LLVM for $(uname -s)/$(uname -m)" >&2
    exit 1
    ;;
esac

dest=build/ci/llvm

if [ ! -e "$dest/.version-$LLVM_VERSION" ]; then
  work=$(mktemp -d)
  trap 'rm -rf "$work"' EXIT
  curl -fsSL --retry 5 -o "$work/$asset" \
    "https://github.com/llvm/llvm-project/releases/download/llvmorg-$LLVM_VERSION/$asset"
  if command -v sha256sum >/dev/null 2>&1; then got=$(sha256sum "$work/$asset" | cut -d' ' -f1)
  else got=$(shasum -a 256 "$work/$asset" | cut -d' ' -f1)
  fi
  if [ "$got" != "$sha" ]; then
    echo "install-llvm: $asset has SHA-256 $got, expected $sha" >&2
    exit 1
  fi
  # Listed first, then only the members wanted that exist are unpacked: a
  # release that links its tools against libLLVM keeps that library too.
  # Files only, not their directories: GNU tar unpacks a directory named to
  # it whole, and then reports each file in it named as well as not found.
  top=${asset%.tar.xz}
  tar -tJf "$work/$asset" | grep -v '/$' |
    grep -E "^$top/(bin/(clang|clang-[0-9]+|lld|ld\\.lld|ld64\\.lld|wasm-ld|llvm-ar|llvm-ranlib|llvm-mc|llvm-objcopy|llvm-objdump|llvm-size|llvm-symbolizer)|lib/clang/.*|lib/lib(LTO|Remarks|LLVM|clang-cpp)[-.0-9A-Za-z]*\\.(dylib|so[.0-9]*))$" \
      > "$work/members"
  xargs tar -xJf "$work/$asset" -C "$work" < "$work/members"
  rm -rf "$dest"
  mkdir -p "$(dirname "$dest")"
  mv "$work/$top" "$dest"
  touch "$dest/.version-$LLVM_VERSION"
fi

if [ -n "${GITHUB_PATH:-}" ]; then
  (cd "$dest/bin" && pwd) >> "$GITHUB_PATH"
fi
# This clang is not Apple's: it finds the SDK through SDKROOT.
if [ "$(uname -s)" = Darwin ] && [ -n "${GITHUB_ENV:-}" ]; then
  echo "SDKROOT=$(xcrun --show-sdk-path)" >> "$GITHUB_ENV"
fi
for tool in clang ld.lld wasm-ld llvm-ar llvm-mc llvm-objcopy llvm-objdump; do
  "$dest/bin/$tool" --version | head -1
done
