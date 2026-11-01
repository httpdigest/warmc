#!/usr/bin/env bash
# Prints what .github/msys2-packages.txt holds: every package file that the
# fresh MSYS2 setup-msys2 unpacks from its pinned installer takes to become an
# upgraded one with the Windows job's packages, as the repository has them
# today, one "SHA-256  URL" line each.  install-msys2.sh installs exactly
# those, so that the Windows job's toolchain (clang, lld, the LLVM tools), its
# Python and the rest are pinned like the other jobs' rather than whatever
# MSYS2 held the morning of the run.
#
# Run it in such a fresh MSYS2 -- the base installer that setup-msys2's
# release names, unpacked, and started once -- not in one that has been
# upgraded, which would leave out what it already holds:
#
#   bash .github/scripts/pin-msys2.sh > .github/msys2-packages.txt

set -euo pipefail

# The Windows job's packages.  Of LLVM only the tools (llvm-ar, llvm-mc,
# llvm-objcopy) are wanted, not the 440 MB of headers and libraries the llvm
# package is.  diffutils is for cmp, which a Git for Windows on the PATH would
# otherwise lend.  The Python is MSYS2's own, not CLANG64's native one: the
# test scripts run /bin/bash, call ld.lld by that name and write files for
# bash to read, and a native Python finds neither, nor writes them with bare
# newlines.
PACKAGES="make diffutils unzip perl python
          mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-lld
          mingw-w64-clang-x86_64-llvm-tools"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

pacman -Sy >&2
# One transaction, the system upgrade and the packages together, so that what
# the packages need is resolved against the upgraded system.
# shellcheck disable=SC2086
pacman -Sup --needed --print-format '%r %f' $PACKAGES |
  sort -u > "$work/files"
if [ ! -s "$work/files" ]; then
  echo "pin-msys2: pacman lists nothing to install" >&2
  exit 1
fi

echo "# Written by .github/scripts/pin-msys2.sh: the package files"
echo "# install-msys2.sh installs, each checked against its SHA-256."
while read -r repo file; do
  case "$repo" in
    msys) url="https://repo.msys2.org/msys/x86_64/$file" ;;
    *)    url="https://repo.msys2.org/mingw/$repo/$file" ;;
  esac
  curl -fsSL --retry 5 -o "$work/pkg" "$url"
  echo "$(sha256sum "$work/pkg" | cut -d' ' -f1)  $url"
done < "$work/files"
