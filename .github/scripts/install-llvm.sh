#!/usr/bin/env bash
# The pinned LLVM release for macOS and Linux arm64 -- for each the newest
# that has a tarball for it, which 23.1.6 has not for macOS -- unpacked into
# build/ci/llvm with only what the build and the test suite run: clang and
# its resource directory (headers and the sanitizer runtimes), lld under the
# names the tests call it by, the binary tools the encoder and Mach-O checks
# use, llvm-nm, which lists the runtime's symbols for the JIT, and on macOS the libLTO that ld64 links clang's ThinLTO objects with.
# The release is 1.6-1.8 GB, almost all of it static libraries and tools
# nothing here runs; this is about a third of that, and a restored cache
# holding this version is not downloaded again.  Windows takes MSYS2's LLVM.

set -euo pipefail

LLVM_VERSION=23.1.6
LLVM_MACOS_VERSION=23.1.5

case "$(uname -s)/$(uname -m)" in
  Darwin/arm64)
    LLVM_VERSION=$LLVM_MACOS_VERSION
    asset=LLVM-$LLVM_VERSION-macOS-ARM64.tar.xz
    sha=aba592b4a15e8f90c634c5367b51ec8418b8bcd2829865db331a17dc9418dcff
    ;;
  Linux/aarch64)
    asset=LLVM-$LLVM_VERSION-Linux-ARM64.tar.xz
    sha=e9daa167da28b9e31d30046f05f98e3e7da3647cdbe1c1863e8b223c07ccf291
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
    grep -E "^$top/(bin/(clang|clang-[0-9]+|lld|ld\\.lld|ld64\\.lld|wasm-ld|llvm-ar|llvm-ranlib|llvm-mc|llvm-nm|llvm-objcopy|llvm-objdump|llvm-size|llvm-symbolizer)|lib/clang/.*|lib/lib(LTO|Remarks|LLVM|clang-cpp)[-.0-9A-Za-z]*\\.(dylib|so[.0-9]*))$" \
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
