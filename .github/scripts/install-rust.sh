#!/usr/bin/env bash
# Rust, pinned, with the WASI targets the Rust corpora build for.  The
# workflows cache rustup's toolchains under this version, so a restored cache
# installs nothing.  GitHub's runner images carry rustup; where one does not,
# it is installed first.

set -euo pipefail

RUST_VERSION=1.99.1

if ! command -v rustup >/dev/null 2>&1; then
  # a restored cache may have brought rustup back without it being on PATH
  if [ ! -x "$HOME/.cargo/bin/rustup" ]; then
    curl --proto '=https' --tlsv1.2 -fsSL https://sh.rustup.rs |
      sh -s -- -y --profile minimal --default-toolchain none --no-modify-path
  fi
  export PATH="$HOME/.cargo/bin:$PATH"
  [ -n "${GITHUB_PATH:-}" ] && echo "$HOME/.cargo/bin" >> "$GITHUB_PATH"
fi
rustup toolchain install "$RUST_VERSION" --profile minimal \
  --target wasm32-wasip1 --target wasm32-wasip2
rustup default "$RUST_VERSION"
rustc --version
