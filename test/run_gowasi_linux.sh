#!/bin/bash

set -u
BUILD=${BUILD:-build}
WORK="$BUILD/gowasilinux"
# tagged with the Debian release it is built on, so that a new one builds anew
# rather than finding the old image under the name
IMAGE=localhost/warmc-gowasi-linux:trixie

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
FROM docker.io/library/debian:trixie-slim
RUN apt-get -qq update && apt-get -qq install -y --no-install-recommends \
      gcc libc6-dev && rm -rf /var/lib/apt/lists/*
EOF
  "$PODMAN" build -q -t "$IMAGE" -f "$WORK/Dockerfile" "$WORK" \
    >"$WORK/image.log" 2>&1 || skip "cannot build the $IMAGE image; see $WORK/image.log"
fi

cp "$BUILD/warmrt.a" "$BUILD/warmwasi.a" "$WORK/" || exit 1

# Each program is linked for Linux twice.  `.st` is warmc's own link: a static
# ELF with no libc, the WASI host's POSIX calls answered by rt/linuxposix.c
# over raw system calls -- what `warmc --wasi --target linux` gives a user.
# `.lx` is the object linked by gcc against glibc, the way an embedder's driver
# would be: the host must keep to its own warmc_lx_* names and leave that
# libc's malloc and stdio alone.  Both have to match the macOS run, save
# where the two hosts answer differently by design (below).

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
  ./$BUILD/warmc --wasi -O2 --target=linux --rt "$BUILD/warmrt.a" \
      -o "$WORK/$name.st" "$WORK/$name.wasm" 2>"$WORK/$name.st.err" || {
    echo "FAIL[gowasi-linux/$name]: warmc could not link it for Linux itself"
    sed 's/^/    /' "$WORK/$name.st.err"; exit 1; }
  names+=("$name")
done

# The WASI 0.2 host carries the same POSIX layer, so the tinygo wasip2 corpus
# runs through it too -- linked by warmc alone, as a component has no -c path
# that a C driver could take over.
for src in test/wasip2/*.go; do
  name=p2-$(basename "$src" .go)
  if ! tinygo build -o "$WORK/$name.wasm" -target=wasip2 "$src" \
       2>"$WORK/$name.tinygo.err"; then
    echo "FAIL[gowasi-linux/$name]: tinygo could not build it"
    sed 's/^/    /' "$WORK/$name.tinygo.err"
    exit 1
  fi
  ./$BUILD/warmc --wasi -O2 --target=macos --rt "$BUILD/warmrt-macos.a" \
      -o "$WORK/$name.mac" "$WORK/$name.wasm" 2>"$WORK/$name.mac.err" || {
    echo "FAIL[gowasi-linux/$name]: warmc could not build for macOS"
    sed 's/^/    /' "$WORK/$name.mac.err"; exit 1; }
  ./$BUILD/warmc --wasi -O2 --target=linux --rt "$BUILD/warmrt.a" \
      -o "$WORK/$name.st" "$WORK/$name.wasm" 2>"$WORK/$name.st.err" || {
    echo "FAIL[gowasi-linux/$name]: warmc could not link it for Linux itself"
    sed 's/^/    /' "$WORK/$name.st.err"; exit 1; }
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
      warmwasi.a warmrt.a || echo "LINKFAIL $n"
done
for exe in *.lx *.st; do
  [ -x "$exe" ] || continue
  rm -rf "$exe.dir"; mkdir -p "$exe.dir"; printf 'seed\n' > "$exe.dir/seed.txt"
  env -i "./$exe" --dir="$exe.dir::/d" \
    >"$exe.out" 2>"$exe.err2" <stdin.txt
  echo "exit=$?" >> "$exe.out"

  ( cd "$exe.dir" && find . | LC_ALL=C sort && echo -- &&
    find . -type f | LC_ALL=C sort | while read -r f; do
      echo "== $f"; cat "$f"
    done ) > "$exe.tree" 2>/dev/null
done
EOF
if ! "$PODMAN" run --rm -v "$PWD/$WORK:/w:Z" "$IMAGE" \
     sh /w/inside.sh >"$WORK/run.log" 2>&1; then
  echo "FAIL[gowasi-linux]: the container run failed"
  sed 's/^/    /' "$WORK/run.log" | head -20
  exit 1
fi

# A create through a name that ends in a slash fails EISDIR on Linux and
# EINVAL on macOS, each host's own answer, so a program whose
# $src.linux.out is recorded beside its source -- what wasmtime prints on
# Linux -- has its Linux builds held to that, and the exit status and the
# rest still to the macOS run.
for name in "${names[@]}"; do
  case $name in
    p2-*) rec="test/wasip2/${name#p2-}.linux.out" ;;
    *)    rec="test/gowasi/$name.linux.out" ;;
  esac
  if [ -f "$rec" ]; then
    { cat "$rec"; tail -n 1 "$WORK/$name.mac.out"; } > "$WORK/$name.ref.out"
  else
    cp "$WORK/$name.mac.out" "$WORK/$name.ref.out"
  fi
done

fail=0
pass=0
for name in "${names[@]}"; do
  variants="st"
  [ -f "$WORK/$name.o" ] && variants="st lx"
  for v in $variants; do
    x="$WORK/$name.$v"
    bad=""
    [ -f "$x.out" ] || { echo "FAIL[gowasi-linux/$name.$v]: did not run"; fail=1; continue; }
    cmp -s "$WORK/$name.ref.out"  "$x.out"  || bad="$bad stdout"
    cmp -s "$WORK/$name.mac.err2" "$x.err2" || bad="$bad stderr"
    cmp -s "$WORK/$name.mac.tree" "$x.tree" || bad="$bad tree"
    if [ -n "$bad" ]; then
      echo "FAIL[gowasi-linux/$name.$v]: differs from the macOS run:$bad"
      for k in $bad; do
        case $k in
          stdout) diff "$WORK/$name.ref.out"  "$x.out"  | head -8 ;;
          stderr) diff "$WORK/$name.mac.err2" "$x.err2" | head -8 ;;
          tree)   diff "$WORK/$name.mac.tree" "$x.tree" | head -8 ;;
        esac
      done | sed 's/^/    /'
      fail=1
    else
      pass=$((pass + 1))
    fi
  done
done

if [ "$fail" != 0 ]; then
  echo "gowasi-linux: FAILED"
  exit 1
fi
echo "gowasi-linux: $pass Linux build(s) agree with the macOS run"
