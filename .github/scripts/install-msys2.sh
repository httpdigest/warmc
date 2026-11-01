#!/usr/bin/env bash
# The Windows job's MSYS2 packages, pinned: exactly the files
# .github/msys2-packages.txt lists (pin-msys2.sh writes it), each checked
# against its SHA-256, installed into the fresh MSYS2 that setup-msys2 makes of
# its pinned installer.  The files are kept in build/ci/msys2-pkgs, which the
# job caches under the list's hash.
#
# Usage: install-msys2.sh 1|2, the two passes setup-msys2 makes of an upgrade.
# The first can replace the MSYS2 runtime under the very shell running it, so
# its exit status is not trusted, as setup-msys2 does not trust its own; the
# job then ends every MSYS2 process, and the second pass, from a new shell,
# installs whatever the first left out.  It must succeed, and checks that
# every listed package is installed at its pinned version.

set -euo pipefail

list=.github/msys2-packages.txt
cache=build/ci/msys2-pkgs
mkdir -p "$cache"

files=()
while read -r sha url; do
  case "$sha" in ''|'#'*) continue ;; esac
  f="$cache/${url##*/}"
  if [ ! -e "$f" ] || [ "$(sha256sum "$f" | cut -d' ' -f1)" != "$sha" ]; then
    rm -f "$f.part"
    curl -fsSL --retry 5 -o "$f.part" "$url"
    got=$(sha256sum "$f.part" | cut -d' ' -f1)
    if [ "$got" != "$sha" ]; then
      rm -f "$f.part"
      echo "install-msys2: $url has SHA-256 $got, $list says $sha" >&2
      exit 1
    fi
    mv "$f.part" "$f"
  fi
  files+=("$f")
done < "$list"

case "${1:-}" in
  1)
    pacman -U --noconfirm --overwrite '*' "${files[@]}" || true
    if [ -e /etc/pacman.conf.pacnew ]; then
      mv -f /etc/pacman.conf.pacnew /etc/pacman.conf
    fi
    ;;
  2)
    pacman -U --noconfirm --needed --overwrite '*' "${files[@]}"
    bad=0
    for f in "${files[@]}"; do
      want=$(pacman -Qp "$f")
      have=$(pacman -Q "${want%% *}" 2>/dev/null || echo "${want%% *} missing")
      if [ "$have" != "$want" ]; then
        echo "install-msys2: $have is installed, the pin is $want" >&2
        bad=1
      fi
    done
    exit $bad
    ;;
  *)
    echo "usage: install-msys2.sh 1|2" >&2
    exit 2
    ;;
esac
