#!/usr/bin/env bash
# Rust, pinned, with the WASI targets the Rust corpora build for.  The
# workflows cache rustup's toolchains under this version, so a restored cache
# installs nothing.  rustup itself is pinned too, and installed from its
# release's rustup-init, checked against its SHA-256, rather than taken from
# whatever the runner image carries; rustup checks every toolchain component
# it downloads against the release manifest's hashes.

set -euo pipefail

RUST_VERSION=1.99.1
RUSTUP_VERSION=1.29.2

exe=
case "$(uname -s)/$(uname -m)" in
  Darwin/arm64)
    triple=aarch64-apple-darwin
    sha=f7c7e11eb2e07f006e532a8aaac77b32f4527d305ac386c0c633ca6133d35ca9 ;;
  Linux/aarch64)
    triple=aarch64-unknown-linux-gnu
    sha=a9c168dbea81a84671c6b7a5c1795cb0eb699020110d107ddfca427bd797e697 ;;
  MINGW64_NT*/x86_64|CLANG64_NT*/x86_64|MSYS_NT*/x86_64)
    triple=x86_64-pc-windows-msvc exe=.exe
    sha=00f139da614dced87d3c3d4121c43eebdac3c1bf8a5ee86b9d80dfd71c8d70fd ;;
  *)
    echo "install-rust: no pinned rustup for $(uname -s)/$(uname -m)" >&2
    exit 1 ;;
esac

sha256() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
  else shasum -a 256 "$1" | cut -d' ' -f1
  fi
}

# rustup's homes, where the workflows cache them: the Windows user profile
# under MSYS2, whose own $HOME is elsewhere.
if command -v cygpath >/dev/null 2>&1; then
  home=$(cygpath -u "$USERPROFILE")
else
  home=$HOME
fi
export CARGO_HOME="${CARGO_HOME:-$home/.cargo}" RUSTUP_HOME="${RUSTUP_HOME:-$home/.rustup}"
export PATH="$CARGO_HOME/bin:$PATH"
if [ -n "${GITHUB_PATH:-}" ]; then
  if command -v cygpath >/dev/null 2>&1; then cygpath -wa "$CARGO_HOME/bin"
  else echo "$CARGO_HOME/bin"
  fi >> "$GITHUB_PATH"
fi

have=$("$CARGO_HOME/bin/rustup$exe" --version 2>/dev/null | awk '{ print $2; exit }' || true)
if [ "$have" != "$RUSTUP_VERSION" ]; then
  work=$(mktemp -d)
  trap 'rm -rf "$work"' EXIT
  init="$work/rustup-init$exe"
  curl --proto '=https' --tlsv1.2 -fsSL --retry 5 -o "$init" \
    "https://static.rust-lang.org/rustup/archive/$RUSTUP_VERSION/$triple/rustup-init$exe"
  got=$(sha256 "$init")
  if [ "$got" != "$sha" ]; then
    echo "install-rust: rustup-init $RUSTUP_VERSION has SHA-256 $got, expected $sha" >&2
    exit 1
  fi
  chmod +x "$init"
  "$init" -y --profile minimal --default-toolchain none --no-modify-path
fi
rustup set auto-self-update disable
rustup toolchain install "$RUST_VERSION" --profile minimal \
  --target wasm32-wasip1 --target wasm32-wasip2
rustup default "$RUST_VERSION"
rustup --version
rustc --version
