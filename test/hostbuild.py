"""Building and linking host objects against the warmc runtime.

Both harnesses that link several compiled modules into one program -- the spec
suite and the case suite -- need the same two steps, and they need them to
match what the runtime archive was built with.  One copy, so they cannot drift.
"""
import os
import platform
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DARWIN = platform.system() == "Darwin"
WINDOWS = os.name == "nt" or "NT" in platform.system()

def sh(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)

def cc_host(src, obj, includes=()):
    """Compile a host object for the same target the runtime is built for."""
    if DARWIN:
        cmd = ["clang", "--target=arm64-apple-macos11", "-DWARMC_DARWIN=1"]
    elif WINDOWS:
        cmd = ["clang", "--target=x86_64-pc-windows-gnu", "-DWARMC_WINDOWS=1"]
    else:
        cmd = ["clang", "--target=aarch64-linux-gnu"]
    cmd += ["-O1", "-ffreestanding", "-fno-stack-protector", "-fno-builtin",
            "-nostdlib", "-Wall", "-I", os.path.join(ROOT, "rt")]
    for inc in includes:
        cmd += ["-I", inc]
    cmd += ["-c", "-o", obj, src]
    return sh(cmd)

def link(objs, out, build):
    """Link host objects and compiled modules against the runtime archive."""
    if DARWIN:
        libdir = os.environ.get("WARMC_MACOS_LIBDIR", "")
        if not libdir:
            sdk = sh(["xcrun", "--show-sdk-path"]).stdout.strip()
            libdir = os.path.join(sdk, "usr", "lib") if sdk else ""
        cmd = ["/usr/bin/ld", "-arch", "arm64", "-platform_version", "macos",
               "11.0", "11.0", "-o", out] + list(objs) + \
              [os.path.join(build, "warmrt-macos.a"), "-lSystem"]
        if libdir:
            cmd += ["-L" + libdir]
    elif WINDOWS:
        # Must match warmc's internal PE contract: the warmc_win_entry
        # entry point and a fully COMMITTED stack (the generated code
        # never emits stack probes).  The object list goes through a
        # response file: the spec corpus links hundreds of objects and
        # Windows caps a command line at 32K characters.  MSYS2 converts
        # /c/-style paths only on a command line, so the file's contents
        # must already be Windows-shaped.
        import re
        def winpath(p):
            p = p.replace("\\", "/")
            m = re.match(r"^/([A-Za-z])/(.*)$", p)
            return m.group(1) + ":/" + m.group(2) if m else p
        rsp = out + ".rsp"
        with open(rsp, "w") as f:
            f.write("\n".join('"%s"' % winpath(o) for o in objs))
        cmd = ["ld.lld", "-m", "i386pep", "--entry", "warmc_win_entry",
               "--subsystem", "console", "--stack", "8388608,8388608",
               "-o", out + ".exe", "@" + rsp,
               os.path.join(build, "warmrt-windows.a"),
               "-L", "/clang64/lib",
               "-lkernel32", "-lsynchronization", "-lbcryptprimitives"]
    else:
        cmd = ["ld.lld", "-static", "-e", "_start", "-o", out] + list(objs) + \
              [os.path.join(build, "warmrt.a")]
    return sh(cmd)

def exe(path):
    """The on-disk name link() actually produced for `path`."""
    return path + ".exe" if WINDOWS else path

def wabt_bigstack(build):
    """A PATH entry with big-stack copies of the wabt tools, or None.

    wabt parses and validates by recursion, and the MSYS2 binaries carry
    the 1 MiB default Windows stack -- 4095 nested blocks (cases/brtable)
    overflow it.  Rewriting SizeOfStackReserve in a copy is the same fix
    the warmc host build applies to itself with -Wl,--stack.
    """
    if not WINDOWS:
        return None
    import shutil, struct
    outdir = os.path.join(build, "wabt-bigstack")
    os.makedirs(outdir, exist_ok=True)
    for tool in ("wat2wasm", "wasm-interp"):
        src = shutil.which(tool)
        if not src:
            continue
        dst = os.path.join(outdir, tool + ".exe")
        if os.path.exists(dst) and \
           os.path.getmtime(dst) >= os.path.getmtime(src):
            continue
        with open(src, "rb") as f:
            data = bytearray(f.read())
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        magic = struct.unpack_from("<H", data, pe + 24)[0]
        assert magic == 0x20B, "not PE32+"
        struct.pack_into("<Q", data, pe + 24 + 0x48, 64 << 20)  # reserve
        with open(dst, "wb") as f:
            f.write(data)
    return outdir

# The proposals every harness asks wabt for.  wabt 1.0.42 turned most of them
# on by default and removed their --enable flags, which it now rejects, so
# only the ones the installed wat2wasm still lists are passed.  test/hostenv.sh
# makes the same choice for the shell runners.
WABT_FEATURES = ("exceptions", "function-references", "tail-call", "gc",
                 "memory64", "multi-memory", "extended-const", "relaxed-simd",
                 "annotations")

_wabt_flags = None

def wabt_flags():
    """The --enable flags for WABT_FEATURES that this wabt accepts."""
    global _wabt_flags
    if _wabt_flags is None:
        import re
        try:
            known = set(re.findall(r"--enable-[\w-]+", sh(["wat2wasm", "--help"]).stdout))
        except OSError:
            known = set()
        _wabt_flags = [f for f in ("--enable-" + x for x in WABT_FEATURES)
                       if f in known]
    return list(_wabt_flags)

# A command line over the same two steps, for the shell runners.  They used to
# carry their own `case $(uname -s)` with a Darwin arm and an aarch64-Linux
# fallback, which made every one of them wrong on a third target; going through
# here means a new target is added once.
if __name__ == "__main__":
    import sys
    usage = ("usage: hostbuild.py cc SRC OBJ [INCLUDE...]\n"
             "       hostbuild.py link OUT OBJ...\n"
             "       hostbuild.py exe PATH")
    if len(sys.argv) < 3:
        sys.exit(usage)
    what = sys.argv[1]
    if what == "cc":
        r = cc_host(sys.argv[2], sys.argv[3], sys.argv[4:])
    elif what == "link":
        r = link(sys.argv[3:], sys.argv[2],
                 os.environ.get("BUILD", os.path.join(ROOT, "build")))
    elif what == "exe":
        print(exe(sys.argv[2]))
        sys.exit(0)
    else:
        sys.exit(usage)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
    sys.exit(r.returncode)
