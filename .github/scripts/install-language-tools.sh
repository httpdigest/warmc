#!/usr/bin/env bash
# The producers of the language corpora that no setup action provides, for
# the nightly run, each pinned and every download checked against a hash: a
# JDK (Temurin), the Kotlin/Wasm compiler, GHC's wasm backend and the
# AssemblyScript compiler.  Go, TinyGo, Zig, Node and Rust come from their
# setup actions and install-rust.sh.  A corpus whose producer is missing skips
# itself, so a host without one of these runs the others.
#
# Usage: install-language-tools.sh TOOL...  where TOOL is jdk, kotlin, ghc or
# asc.  The JDK, Kotlin and AssemblyScript go into build/ci/{jdk,kotlinc,asc},
# and GHC into ~/.ghc-wasm, where test/gobench.py looks for it; each directory
# holds a stamp naming its version, and is not installed again when a
# restored cache brought it back.  Kotlin wants the JDK, so `jdk` comes first,
# and AssemblyScript an npm (actions/setup-node).

set -euo pipefail

# Temurin's newest release for each host: 27.0.1 was built again for macOS and
# Linux (27.0.1.0.2) and not yet for Windows, whose newest is 27's first.
JDK_VERSION=27.0.1.0.2+1
JDK_WINDOWS_VERSION=27+35
# Must match the Makefile's KOTLIN_VERSION, whose stdlib klib it fetches.
KOTLIN_VERSION=2.4.21
KOTLIN_SHA=7cc140e76daf416a0424a5557f99afd7ca89ebd463ea1101261e3e01c33e75cd
# ghc-wasm-meta's bootstrap.sh fetches whatever its master branch holds;
# this pins one commit of it instead, and runs its setup.sh directly.
GHC_WASM_META_COMMIT=8079eb96bbc63823d99ad8acaff0765532625474
GHC_WASM_FLAVOUR=9.14
# Installed with npm ci from .github/asc, whose lockfile pins it and what it
# loads (binaryen, long) by their sha512.
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

env_add() {
  [ -n "${GITHUB_ENV:-}" ] || return 0
  echo "$1=$2" >> "$GITHUB_ENV"
}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Fetches $1 to $2 and checks it against the SHA-256 $3.
fetch() {
  local got
  curl -fsSL --retry 5 -o "$2" "$1"
  got=$(sha256 "$2")
  if [ "$got" != "$3" ]; then
    echo "install-language-tools: $1 has SHA-256 $got, expected $3" >&2
    exit 1
  fi
}

jdk() {
  local v=$JDK_VERSION host sha ext=tar.gz home=
  case "$(uname -s)/$(uname -m)" in
    Darwin/arm64)
      host=aarch64_mac home=/Contents/Home
      sha=c2619cb73ff1d296edd0644d92ae8ed94039bc1bdd43282398835b7f88134fb6 ;;
    Linux/aarch64)
      host=aarch64_linux
      sha=5ddacf925244a00b60cac828b61ff2f67a68cd32b8dfe5cee1405e333b4589e5 ;;
    MINGW64_NT*/x86_64|CLANG64_NT*/x86_64|MSYS_NT*/x86_64)
      v=$JDK_WINDOWS_VERSION host=x64_windows ext=zip
      sha=2031c9c5c624737ed29a0624caaf5bcc25e74235834c28b4bfa88f7ba7a40e4a ;;
    *)
      echo "install-language-tools: no pinned JDK for $(uname -s)/$(uname -m)" >&2
      exit 1 ;;
  esac
  if [ ! -e "$dest/jdk/.version-$v" ]; then
    local feature=${v%%[.+]*}
    local file="OpenJDK${feature}U-jdk_${host}_hotspot_${v//+/_}.$ext"
    fetch "https://github.com/adoptium/temurin$feature-binaries/releases/download/jdk-${v//+/%2B}/$file" \
      "$work/$file" "$sha"
    rm -rf "$dest/jdk"
    mkdir -p "$dest/jdk"
    if [ "$ext" = zip ]; then
      unzip -q "$work/$file" -d "$work/jdk"
      mv "$work"/jdk/*/* "$dest/jdk/"
    else
      tar -xzf "$work/$file" -C "$dest/jdk" --strip-components=1
    fi
    touch "$dest/jdk/.version-$v"
  fi
  local java_home
  java_home=$(cd "$dest/jdk$home" && pwd)
  export JAVA_HOME=$java_home PATH="$java_home/bin:$PATH"
  path_add "$java_home/bin"
  if command -v cygpath >/dev/null 2>&1; then
    env_add JAVA_HOME "$(cygpath -wa "$java_home")"
  else
    env_add JAVA_HOME "$java_home"
  fi
  java -version
}

kotlin() {
  # The compiler distribution, which carries kotlinc-wasm beside kotlinc.
  if [ ! -e "$dest/kotlinc/.version-$KOTLIN_VERSION" ]; then
    local zip="$work/kotlin-compiler-$KOTLIN_VERSION.zip"
    fetch "https://github.com/JetBrains/kotlin/releases/download/v$KOTLIN_VERSION/kotlin-compiler-$KOTLIN_VERSION.zip" \
      "$zip" "$KOTLIN_SHA"
    rm -rf "$dest/kotlinc"
    unzip -q "$zip" -d "$dest"
    touch "$dest/kotlinc/.version-$KOTLIN_VERSION"
  fi
  path_add "$dest/kotlinc/bin"
  "$dest/kotlinc/bin/kotlinc" -version
}

# ghc-wasm-meta at the pinned commit, which git checks the tree against, and
# its setup.sh run with a curl that checks every file it downloads against the
# hash the commit's autogen.json records for the URL: setup.sh itself checks
# none.  Its `wasm32-wasi-cabal update` is left out, which would fetch
# Hackage's index as it is that day, and which nothing here reads.
ghc() {
  local stamp="$HOME/.ghc-wasm/.warmc-ci-$GHC_WASM_META_COMMIT-$GHC_WASM_FLAVOUR"
  if [ ! -e "$stamp" ]; then
    local meta="$work/ghc-wasm-meta"
    git clone --quiet https://gitlab.haskell.org/haskell-wasm/ghc-wasm-meta.git "$meta"
    git -C "$meta" checkout --quiet --detach "$GHC_WASM_META_COMMIT"
    if [ "$(git -C "$meta" rev-parse HEAD)" != "$GHC_WASM_META_COMMIT" ]; then
      echo "install-language-tools: ghc-wasm-meta is not at $GHC_WASM_META_COMMIT" >&2
      exit 1
    fi
    local update='^"\$PREFIX/wasm32-wasi-cabal/bin/wasm32-wasi-cabal" update$'
    if [ "$(grep -c "$update" "$meta/setup.sh")" != 1 ]; then
      echo "install-language-tools: setup.sh does not run cabal update as expected" >&2
      exit 1
    fi
    sed -i.orig "\#$update#d" "$meta/setup.sh"
    mkdir -p "$work/shim"
    cat > "$work/shim/curl" <<SHIM
#!/usr/bin/env bash
set -euo pipefail
out= url=
args=("\$@")
for ((i = 0; i < \${#args[@]}; i++)); do
  case "\${args[i]}" in
    -o) out=\${args[i+1]} ;;
    http://*|https://*) url=\${args[i]} ;;
  esac
done
if [ -z "\$out" ] || [ -z "\$url" ]; then
  echo "install-language-tools: setup.sh ran curl \$* , which is no pinned download" >&2
  exit 1
fi
"$(command -v curl)" "\$@"
python3 - "$meta/autogen.json" "\$url" "\$out" <<'PY'
import base64, hashlib, json, sys
autogen, url, out = sys.argv[1:]
pins = [v["hash"] for v in json.load(open(autogen)).values()
        if isinstance(v, dict) and v.get("url") == url]
if not pins:
    sys.exit("install-language-tools: setup.sh fetched %s, which autogen.json has no hash for" % url)
algo, want = pins[0].split("-", 1)
with open(out, "rb") as f:
    got = base64.b64encode(hashlib.new(algo, f.read()).digest()).decode()
if got != want:
    sys.exit("install-language-tools: %s has %s %s, autogen.json says %s" % (url, algo, got, want))
PY
SHIM
    chmod +x "$work/shim/curl"
    (cd "$meta" && PATH="$work/shim:$PATH" FLAVOUR=$GHC_WASM_FLAVOUR ./setup.sh)
    touch "$stamp"
  fi
  "$HOME/.ghc-wasm/wasm32-wasi-ghc/bin/wasm32-wasi-ghc" --version
}

asc() {
  # A prefix of its own rather than npm's global one, so that it is cached.
  # npm ci installs the lockfile exactly, checking each package's sha512, and
  # none of them runs a script.
  local lock
  lock=$(sha256 .github/asc/package-lock.json)
  if [ ! -e "$dest/asc/.lock-$lock" ]; then
    rm -rf "$dest/asc"
    mkdir -p "$dest/asc"
    cp .github/asc/package.json .github/asc/package-lock.json "$dest/asc/"
    npm ci --prefix "$dest/asc" --ignore-scripts --no-audit --no-fund
    touch "$dest/asc/.lock-$lock"
  fi
  if [ "$("$dest/asc/node_modules/.bin/asc" --version)" != "Version $ASC_VERSION" ]; then
    echo "install-language-tools: .github/asc does not install asc $ASC_VERSION" >&2
    exit 1
  fi
  path_add "$dest/asc/node_modules/.bin"
  "$dest/asc/node_modules/.bin/asc" --version
}

for tool in "$@"; do
  case "$tool" in
    jdk|kotlin|ghc|asc) "$tool" ;;
    *) echo "install-language-tools: unknown tool $tool" >&2; exit 1 ;;
  esac
done
