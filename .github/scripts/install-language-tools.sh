#!/usr/bin/env bash
# The producers of the language corpora that no setup action provides, for
# the nightly run: the Kotlin/Wasm compiler, GHC's wasm backend and the
# AssemblyScript compiler, each pinned.  Go, TinyGo, Zig, Node and Rust come
# from their setup actions and install-rust.sh.  A corpus whose producer is
# missing skips itself, so a host without one of these runs the others.
#
# Usage: install-language-tools.sh TOOL...  where TOOL is kotlin, ghc or asc.
# Kotlin and AssemblyScript go into build/ci/kotlinc and build/ci/asc, and
# GHC into ~/.ghc-wasm, where test/gobench.py looks for it; each directory
# holds a stamp naming its version, and is not installed again when a
# restored cache brought it back.  Kotlin wants a JDK on the PATH
# (actions/setup-java) and AssemblyScript an npm (actions/setup-node).

set -euo pipefail

KOTLIN_VERSION=2.4.21
KOTLIN_SHA=7cc140e76daf416a0424a5557f99afd7ca89ebd463ea1101261e3e01c33e75cd
# ghc-wasm-meta's bootstrap.sh fetches whatever its master branch holds;
# this pins one commit of it instead, and runs its setup.sh directly.
GHC_WASM_META_COMMIT=8079eb96bbc63823d99ad8acaff0765532625474
GHC_WASM_FLAVOUR=9.14
ASC_VERSION=0.28.20

dest=build/ci
mkdir -p "$dest"

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

kotlin() {
  # The compiler distribution, which carries kotlinc-wasm beside kotlinc.
  if [ ! -e "$dest/kotlinc/.version-$KOTLIN_VERSION" ]; then
    local zip="$work/kotlin-compiler-$KOTLIN_VERSION.zip" got
    curl -fsSL --retry 5 -o "$zip" \
      "https://github.com/JetBrains/kotlin/releases/download/v$KOTLIN_VERSION/kotlin-compiler-$KOTLIN_VERSION.zip"
    got=$(sha256 "$zip")
    if [ "$got" != "$KOTLIN_SHA" ]; then
      echo "install-language-tools: $zip has SHA-256 $got, expected $KOTLIN_SHA" >&2
      exit 1
    fi
    rm -rf "$dest/kotlinc"
    unzip -q "$zip" -d "$dest"
    touch "$dest/kotlinc/.version-$KOTLIN_VERSION"
  fi
  path_add "$dest/kotlinc/bin"
  "$dest/kotlinc/bin/kotlinc" -version
}

ghc() {
  local stamp="$HOME/.ghc-wasm/.warmc-ci-$GHC_WASM_META_COMMIT-$GHC_WASM_FLAVOUR"
  if [ ! -e "$stamp" ]; then
    mkdir -p "$work/ghc-wasm-meta"
    curl -fsSL --retry 5 \
      "https://gitlab.haskell.org/haskell-wasm/ghc-wasm-meta/-/archive/$GHC_WASM_META_COMMIT/ghc-wasm-meta-$GHC_WASM_META_COMMIT.tar.gz" |
      tar -xz -C "$work/ghc-wasm-meta" --strip-components=1
    (cd "$work/ghc-wasm-meta" && FLAVOUR=$GHC_WASM_FLAVOUR ./setup.sh)
    touch "$stamp"
  fi
  "$HOME/.ghc-wasm/wasm32-wasi-ghc/bin/wasm32-wasi-ghc" --version
}

asc() {
  # A prefix of its own rather than npm's global one, so that it is cached.
  if [ ! -e "$dest/asc/.version-$ASC_VERSION" ]; then
    rm -rf "$dest/asc"
    npm install --prefix "$dest/asc" --no-audit --no-fund \
      "assemblyscript@$ASC_VERSION"
    touch "$dest/asc/.version-$ASC_VERSION"
  fi
  path_add "$dest/asc/node_modules/.bin"
  "$dest/asc/node_modules/.bin/asc" --version
}

for tool in "$@"; do
  case "$tool" in
    kotlin|ghc|asc) "$tool" ;;
    *) echo "install-language-tools: unknown tool $tool" >&2; exit 1 ;;
  esac
done
