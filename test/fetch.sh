#!/usr/bin/env bash
# Downloads a URL to a file, checked against the SHA-256 that test/fetch.sha256
# records for that URL.  Every download the Makefile makes goes through here,
# so each is pinned by its contents as well as by the version in its URL: a
# URL with no line there, or whose bytes do not match the line, fails and
# leaves nothing at the destination.  Raising a version changes the URL, so
# its sums have to be recorded with it; `test/fetch.sh --record URL...` prints
# the lines to add, which are worth checking against what the publisher lists.
#
#   test/fetch.sh URL OUT
#   test/fetch.sh --record URL...

set -euo pipefail

SUMS="$(cd "$(dirname "$0")" && pwd)/fetch.sha256"

sha256() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
  else shasum -a 256 "$1" | cut -d' ' -f1
  fi
}

if [ "${1:-}" = --record ]; then
  shift
  tmp=$(mktemp)
  trap 'rm -f "$tmp"' EXIT
  for url in "$@"; do
    curl -sSLf --retry 5 -o "$tmp" "$url"
    echo "$(sha256 "$tmp")  $url"
  done
  exit 0
fi

if [ $# -ne 2 ]; then
  echo "usage: test/fetch.sh URL OUT | --record URL..." >&2
  exit 2
fi
url=$1 out=$2

want=$(awk -v u="$url" '$1 !~ /^#/ && $2 == u { print $1 }' "$SUMS")
if [ -z "$want" ]; then
  echo "fetch: test/fetch.sha256 has no SHA-256 for $url" >&2
  echo "fetch: \`test/fetch.sh --record $url\` prints the line" >&2
  exit 1
fi

rm -f "$out.part"
if ! curl -sSLf --retry 5 -o "$out.part" "$url"; then
  rm -f "$out.part"
  echo "fetch: cannot fetch $url" >&2
  exit 1
fi
got=$(sha256 "$out.part")
if [ "$got" != "$want" ]; then
  rm -f "$out.part"
  echo "fetch: $url has SHA-256 $got, test/fetch.sha256 says $want" >&2
  exit 1
fi
mv "$out.part" "$out"
