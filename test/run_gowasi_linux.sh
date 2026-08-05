#!/bin/bash

set -u
BUILD=${BUILD:-build}
WORK="$BUILD/gowasilinux"
IMAGE=localhost/warmc-gowasi-linux

skip() { echo "gowasi-linux: SKIPPED ($1)"; exit 0; }

[ "$(uname -s)" = Darwin ] || skip "the reference side is a macOS run"
command -v tinygo >/dev/null 2>&1 || skip "tinygo not on PATH"
PODMAN=$(command -v podman || command -v docker) || skip "no podman or docker"
arch=$("$PODMAN" info --format '{{.Version.OsArch}}' 2>/dev/null) ||
  skip "$(basename "$PODMAN") is installed but not running"
[ "$arch" = linux/arm64 ] ||
  skip "the container runs $arch, and warmc emits aarch64"

rm -rf "$WORK"; mkdir -p "$WORK"

if ! "$PODMAN" image exists "$IMAGE" 2>/dev/null; then
  cat > "$WORK/Dockerfile" <<'EOF'
FROM docker.io/library/debian:bookworm-slim
RUN apt-get -qq update && apt-get -qq install -y --no-install-recommends \
      gcc libc6-dev && rm -rf /var/lib/apt/lists/*
EOF
  "$PODMAN" build -q -t "$IMAGE" -f "$WORK/Dockerfile" "$WORK" \
    >"$WORK/image.log" 2>&1 || skip "cannot build the $IMAGE image; see $WORK/image.log"
fi

cp "$BUILD/warmrt.a" "$BUILD/warmwasi.a" "$WORK/" || exit 1

printf 'alpha\nbeta\ngamma\n' > "$WORK/stdin.txt"
printf 'secret\n' > "$WORK/escape.secret"

seed_dir() {
  rm -rf "$1"; mkdir -p "$1"
  printf 'seed\n' > "$1/seed.txt"
}
snapshot() {
  ( cd "$1" && find . | LC_ALL=C sort && echo -- &&
    find . -type f | LC_ALL=C sort | while read -r f; do
      echo "== $f"; cat "$f"
    done ) 2>/dev/null
}

names=()
for src in test/gowasi/*.go; do
  name=$(basename "$src" .go)
  tflags=$(sed -n 's|^// tinygo: *||p' "$src" | head -1)
  if ! tinygo build -o "$WORK/$name.wasm" -target=wasip1 $tflags "$src" \
       2>"$WORK/$name.tinygo.err"; then
    echo "FAIL[gowasi-linux/$name]: tinygo could not build it"
    sed 's/^/    /' "$WORK/$name.tinygo.err"
    exit 1
  fi

  ./$BUILD/warmc --wasi -O2 --target=macos --rt "$BUILD/warmrt-macos.a" \
      -o "$WORK/$name.mac" "$WORK/$name.wasm" 2>"$WORK/$name.mac.err" || {
    echo "FAIL[gowasi-linux/$name]: warmc could not build for macOS"
    sed 's/^/    /' "$WORK/$name.mac.err"; exit 1; }
  ./$BUILD/warmc --wasi -O2 -c --target=linux \
      -o "$WORK/$name.o" "$WORK/$name.wasm" 2>"$WORK/$name.lx.err" || {
    echo "FAIL[gowasi-linux/$name]: warmc could not build for Linux"
    sed 's/^/    /' "$WORK/$name.lx.err"; exit 1; }
  names+=("$name")
done

for name in "${names[@]}"; do
  d="$WORK/$name.mac.dir"; seed_dir "$d"
  ( cd "$WORK" && env -i "./$name.mac" --dir="$name.mac.dir::/d" \
      >"$name.mac.out" 2>"$name.mac.err2" <stdin.txt )
  echo "exit=$?" >> "$WORK/$name.mac.out"
  snapshot "$d" > "$WORK/$name.mac.tree"
done

cat > "$WORK/inside.sh" <<'EOF'
set -u
cd /w
for o in *.o; do
  n=$(basename "$o" .o)
  gcc -nostartfiles -no-pie -u warmc_driver_hook -o "$n.lx" "$o" \
      warmwasi.a warmrt.a || { echo "LINKFAIL $n"; continue; }
  rm -rf "$n.lx.dir"; mkdir -p "$n.lx.dir"; printf 'seed\n' > "$n.lx.dir/seed.txt"
  env -i "./$n.lx" --dir="$n.lx.dir::/d" \
    >"$n.lx.out" 2>"$n.lx.err2" <stdin.txt
  echo "exit=$?" >> "$n.lx.out"

  ( cd "$n.lx.dir" && find . | LC_ALL=C sort && echo -- &&
    find . -type f | LC_ALL=C sort | while read -r f; do
      echo "== $f"; cat "$f"
    done ) > "$n.lx.tree" 2>/dev/null
done
EOF
if ! "$PODMAN" run --rm -v "$PWD/$WORK:/w:Z" "$IMAGE" \
     sh /w/inside.sh >"$WORK/run.log" 2>&1; then
  echo "FAIL[gowasi-linux]: the container run failed"
  sed 's/^/    /' "$WORK/run.log" | head -20
  exit 1
fi

fail=0
pass=0
for name in "${names[@]}"; do
  bad=""
  [ -f "$WORK/$name.lx.out" ] || { echo "FAIL[gowasi-linux/$name]: did not run"; fail=1; continue; }
  cmp -s "$WORK/$name.mac.out"  "$WORK/$name.lx.out"  || bad="$bad stdout"
  cmp -s "$WORK/$name.mac.err2" "$WORK/$name.lx.err2" || bad="$bad stderr"
  cmp -s "$WORK/$name.mac.tree" "$WORK/$name.lx.tree" || bad="$bad tree"
  if [ -n "$bad" ]; then
    echo "FAIL[gowasi-linux/$name]: differs from the macOS run:$bad"
    for k in $bad; do
      case $k in
        stdout) diff "$WORK/$name.mac.out"  "$WORK/$name.lx.out"  | head -8 ;;
        stderr) diff "$WORK/$name.mac.err2" "$WORK/$name.lx.err2" | head -8 ;;
        tree)   diff "$WORK/$name.mac.tree" "$WORK/$name.lx.tree" | head -8 ;;
      esac
    done | sed 's/^/    /'
    fail=1
  else
    pass=$((pass + 1))
  fi
done

if [ "$fail" != 0 ]; then
  echo "gowasi-linux: FAILED"
  exit 1
fi
echo "gowasi-linux: $pass program(s) agree with the macOS run"
