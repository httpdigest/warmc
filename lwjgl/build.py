"""An LWJGL 3 program to a native macOS executable, through TeaVM and warmc.

  python3 lwjgl/build.py [--main CLASS] [-O0|-O1|-O2|-O3] SRC_DIR -o OUT

SRC_DIR holds the program's .java files, written against LWJGL 3's API, and
CLASS (default Main) is its main class.  The program is compiled against
warmc's LWJGL (lwjgl/src), TeaVM makes a wasm-gc module of both, the TeaVM
adapter (test/teavm/wasi.py) a WASI command, and warmc a native executable,
in which each GLFW and OpenGL function is a direct call of GLFW's or
OpenGL's: the module's imports of module "lwjgl" are --native-module ones,
linked against GLFW (`make glfw`), macOS's OpenGL framework and
lwjgl/native/lwjgl.c, which calls the program back for GLFW's callbacks.

It needs `make all`, `make teavm`, `make glfw` and a JDK, 21 or later.
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD = os.path.join(ROOT, os.environ.get("BUILD", "build"))
TEAVM_SRC = os.path.join(ROOT, "test", "teavm")
sys.path.insert(0, TEAVM_SRC)
import wasi  # noqa: E402

WORK = os.path.join(BUILD, "lwjgl")
# The frameworks GLFW's static library needs on macOS, and OpenGL's.
FRAMEWORKS = ["Cocoa", "IOKit", "CoreFoundation", "QuartzCore", "OpenGL"]


def pinned(name):
    with open(os.path.join(ROOT, "Makefile")) as f:
        for line in f:
            if line.startswith(name + " "):
                return line.split(":=", 1)[1].strip()
    raise SystemExit("lwjgl: no %s in the Makefile" % name)


TEAVM = os.path.join(BUILD, "tools", "teavm-" + pinned("TEAVM_VERSION"))
GLFW = os.path.join(BUILD, "tools", "glfw-" + pinned("GLFW_VERSION"))


def run(cmd, what):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit("lwjgl: %s failed:\n%s%s" % (what, r.stdout, r.stderr))
    return r


def newest(paths):
    return max((os.path.getmtime(p) for p in paths), default=0)


def stale(target, sources):
    return not os.path.exists(target) or os.path.getmtime(target) < newest(sources)


def lwjgl_classes():
    """lwjgl/src compiled against TeaVM's class library, once."""
    out = os.path.join(WORK, "classes")
    stamp = os.path.join(out, ".complete")
    sources = sorted(glob.glob(os.path.join(HERE, "src", "**", "*.java"), recursive=True))
    stubs = glob.glob(os.path.join(HERE, "stub", "**", "*.java"), recursive=True)
    if stale(stamp, sources + stubs):
        shutil.rmtree(out, ignore_errors=True)
        # java.nio.LwjglBuffers is TeaVM's to provide (lwjgl/stub)
        run(["javac", "--release", "21", "-nowarn", "-encoding", "UTF-8", "-proc:none", "-implicit:none",
             "--patch-module", "java.base=" + os.path.join(HERE, "stub"),
             "-cp", os.path.join(TEAVM, "*"), "-d", out] + sources, "javac of lwjgl/src")
        open(stamp, "w").close()
    return out


def glue():
    """lwjgl/native/lwjgl.c, compiled once."""
    src = os.path.join(HERE, "native", "lwjgl.c")
    obj = os.path.join(WORK, "lwjgl.o")
    if stale(obj, [src, os.path.join(ROOT, "rt", "warm_rt.h")]):
        run(["clang", "-O2", "-mmacosx-version-min=11.0", "-c", "-I", os.path.join(ROOT, "rt"),
             "-I", os.path.join(GLFW, "include"), "-o", obj, src], "clang of lwjgl/native/lwjgl.c")
    return obj


def driver():
    out = os.path.join(WORK, "driver")
    src = os.path.join(TEAVM_SRC, "TeaVMBuild.java")
    cls = os.path.join(out, "warmc", "teavm", "TeaVMBuild.class")
    if stale(cls, [src]):
        shutil.rmtree(out, ignore_errors=True)
        run(["javac", "--release", "17", "-nowarn", "-cp", os.path.join(TEAVM, "*"), "-d", out, src],
            "javac of TeaVMBuild.java")
    return out


def main():
    ap = argparse.ArgumentParser(description="an LWJGL 3 program through TeaVM and warmc")
    ap.add_argument("src")
    ap.add_argument("-o", dest="out", required=True)
    ap.add_argument("--main", default="Main")
    ap.add_argument("-O", dest="level", default="2", choices="0123")
    ap.add_argument("-v", action="store_true", help="print the link command")
    a = ap.parse_args()

    for need, how in ((os.path.join(TEAVM, "patched", ".complete"), "make teavm"),
                      (os.path.join(GLFW, ".complete"), "make glfw"),
                      (os.path.join(BUILD, "warmc"), "make all")):
        if not os.path.exists(need):
            raise SystemExit("lwjgl: no %s: run `%s`" % (need, how))
    os.makedirs(WORK, exist_ok=True)
    classes = lwjgl_classes()
    obj = glue()

    job = os.path.join(WORK, "job")
    shutil.rmtree(job, ignore_errors=True)
    cp = os.pathsep.join([os.path.join(TEAVM, "patched"), os.path.join(TEAVM, "*"), driver()])
    run(["java", "-cp", cp, "warmc.teavm.TeaVMBuild", "--teavm", TEAVM, "--main", a.main,
         "--classpath", classes, "--support", os.path.join(TEAVM_SRC, "FdLibm.java"),
         "%s=%s" % (a.src, job)], "TeaVM")
    with open(os.path.join(job, "main.wasm"), "rb") as f:
        module = f.read()
    adapted = os.path.join(WORK, "app.wasm")
    err = wasi.write(module, adapted, a.main, native={"lwjgl"})
    if err:
        raise SystemExit("lwjgl: the TeaVM adapter: %s" % err)

    link = [obj, os.path.join(GLFW, "lib-arm64", "libglfw3.a")]
    for fw in FRAMEWORKS:
        link += ["-framework", fw]
    cmd = [os.path.join(BUILD, "warmc"), "--target", "macos",
           "--rt", os.path.join(BUILD, "warmrt-macos.a"), "--wasi", "-O" + a.level,
           "--native-module", "lwjgl", "--link", " ".join(link), "-o", a.out, adapted]
    if a.v:
        cmd.append("-v")
    r = run(cmd, "warmc")
    sys.stderr.write("".join(l + "\n" for l in r.stderr.splitlines()
                             if "was built for newer" not in l))


if __name__ == "__main__":
    main()
