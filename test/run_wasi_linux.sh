#!/bin/bash
# The WASI host's sockets and its flags on stdin and stdout, on Linux, where
# rt/linuxposix.c answers fcntl, poll, accept and the rest over raw system
# calls, and where a connection does not take its listener's O_NONBLOCK as on
# macOS: test/wasi-driven/echoserver and stdioflags, and the Go HTTP server
# (test/gohttp, with go 1.22 or later on PATH), linked by warmc for Linux and
# driven by the same drivers as on macOS, inside a Debian container.

set -u
BUILD=${BUILD:-build}
WORK="$BUILD/wasilinux"
# tagged with the Debian release it is built on, so that a new one builds anew
# rather than finding the old image under the name
IMAGE=localhost/warmc-wasi-linux:trixie

skip() { echo "wasi-linux: SKIPPED ($1)"; exit 0; }

[ "$(uname -s)" = Darwin ] || skip "it links on macOS for a Linux container"
PODMAN=$(command -v podman || command -v docker) || skip "no podman or docker"
arch=$("$PODMAN" info --format '{{.Version.OsArch}}' 2>/dev/null) ||
  skip "$(basename "$PODMAN") is installed but not running"
[ "$arch" = linux/arm64 ] ||
  skip "the container runs $arch, and warmc emits aarch64"

rm -rf "$WORK"; mkdir -p "$WORK"

if ! "$PODMAN" image exists "$IMAGE" 2>/dev/null; then
  cat > "$WORK/Dockerfile" <<'EOF'
FROM docker.io/library/debian:trixie-slim
RUN apt-get -qq update && apt-get -qq install -y --no-install-recommends \
      python3 && rm -rf /var/lib/apt/lists/*
EOF
  "$PODMAN" build -q -t "$IMAGE" -f "$WORK/Dockerfile" "$WORK" \
    >"$WORK/image.log" 2>&1 || skip "cannot build the $IMAGE image; see $WORK/image.log"
fi

link() {   # name wasm level
  ./$BUILD/warmc --wasi "$3" --target=linux --rt "$BUILD/warmrt.a" \
      -o "$WORK/$1" "$2" 2>"$WORK/$1.err" || {
    echo "FAIL[wasi-linux/$1]: warmc could not link it for Linux"
    sed 's/^/    /' "$WORK/$1.err"; exit 1; }
}

for name in echoserver stdioflags; do
  ./$BUILD/warmc --emit-wasm -o "$WORK/$name.wasm" "test/wasi-driven/$name.wat" \
    2>"$WORK/$name.err" || {
    echo "FAIL[wasi-linux/$name]: warmc could not assemble it"
    sed 's/^/    /' "$WORK/$name.err"; exit 1; }
  link "$name" "$WORK/$name.wasm" -O2
done

gohttp=0
if command -v go >/dev/null 2>&1 &&
   go env GOVERSION | awk -F'[go.]' '{ exit !($3 > 1 || ($3 == 1 && $4 >= 22)) }'; then
  GOOS=wasip1 GOARCH=wasm go build -o "$WORK/gohttp.wasm" test/gohttp/main.go \
    2>"$WORK/gohttp.go.err" || {
    echo "FAIL[wasi-linux/gohttp]: go build failed"
    sed 's/^/    /' "$WORK/gohttp.go.err"; exit 1; }
  link gohttp-O2 "$WORK/gohttp.wasm" -O2
  gohttp=1
else
  echo "wasi-linux/gohttp: SKIPPED (no go 1.22 or later on PATH)"
fi

cat > "$WORK/inside.sh" <<EOF
rc=0
/t/wasi-driven/echoserver.sh /w/echoserver /w && echo "PASS echoserver" || rc=1
python3 /t/wasi-driven/stdioflags.py /w/stdioflags && echo "PASS stdioflags" || rc=1
if [ $gohttp = 1 ]; then
  python3 /t/run_gohttp.py --exe /w/gohttp-O2 && echo "PASS gohttp" || rc=1
fi
exit \$rc
EOF
"$PODMAN" run --rm -v "$PWD/test:/t:ro,Z" -v "$PWD/$WORK:/w:Z" "$IMAGE" \
  bash /w/inside.sh >"$WORK/run.log" 2>&1
rc=$?
grep -v '^PASS' "$WORK/run.log" | sed 's/^/    /'
if [ "$rc" != 0 ]; then
  echo "FAIL[wasi-linux]: see above"
  exit 1
fi
echo "wasi-linux: $(grep -c '^PASS' "$WORK/run.log") case(s) pass in a Linux container"
exit 0
