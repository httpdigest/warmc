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

def sh(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)

def cc_host(src, obj, includes=()):
    """Compile a host object for the same target the runtime is built for."""
    if DARWIN:
        cmd = ["clang", "--target=arm64-apple-macos11", "-DWARMC_DARWIN=1"]
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
    else:
        cmd = ["ld.lld", "-static", "-e", "_start", "-o", out] + list(objs) + \
              [os.path.join(build, "warmrt.a")]
    return sh(cmd)
