"""warmc's LWJGL (lwjgl/): LWJGL 3 programs through TeaVM and warmc, natively.

  * lwjgl/demo/hello, LWJGL's HelloWorld, builds;
  * test/lwjgl/bluewindow runs: GLFW's version, an error GLFW reports through
    the error callback and one a callback throws coming out of the GLFW call
    that ran it, a window and what GLFW and OpenGL say about it, and the
    pixels of a few frames cleared to blue read back with glReadPixels -- its
    output expected.txt's, line for line.

It needs macOS, a JDK, `make teavm` and `make glfw`, and a display, without
which GLFW makes no window and the test says so and is left out.

Usage: python3 test/run_lwjgl.py
"""

import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, os.environ.get("BUILD", "build"))
OUT = os.path.join(BUILD, "lwjgl")


def build(src, main, exe):
    r = subprocess.run([sys.executable, os.path.join(ROOT, "lwjgl", "build.py"), "--main", main, src,
                        "-o", exe], capture_output=True, text=True)
    if r.returncode != 0:
        print("FAIL[lwjgl] %s: %s" % (src, (r.stdout + r.stderr).strip()[-1500:]))
        return False
    return True


def main():
    if sys.platform != "darwin":
        print("lwjgl: left out (macOS's alone so far)")
        return 0
    if not shutil.which("javac"):
        print("lwjgl: left out (needs a JDK)")
        return 0
    teavm = [d for d in os.listdir(os.path.join(BUILD, "tools")) if d.startswith("teavm-")] \
        if os.path.isdir(os.path.join(BUILD, "tools")) else []
    glfw = [d for d in os.listdir(os.path.join(BUILD, "tools")) if d.startswith("glfw-")] \
        if os.path.isdir(os.path.join(BUILD, "tools")) else []
    if not teavm or not glfw:
        print("lwjgl: left out (needs `make teavm` and `make glfw`)")
        return 0
    os.makedirs(OUT, exist_ok=True)
    bad = 0
    if not build(os.path.join(ROOT, "lwjgl", "demo", "hello"), "HelloWorld", os.path.join(OUT, "HelloWorld")):
        bad += 1
    src = os.path.join(ROOT, "test", "lwjgl", "bluewindow")
    exe = os.path.join(OUT, "bluewindow")
    if not build(src, "Main", exe):
        return 1
    try:
        r = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        print("FAIL[lwjgl] bluewindow: still running after 60 s")
        return 1
    if "glfwInit failed" in r.stdout or "no window" in r.stdout:
        print("lwjgl: bluewindow left out: GLFW has no display here (%s)" % r.stdout.strip().split("\n")[-1])
        return 1 if bad else 0
    with open(os.path.join(src, "expected.txt")) as f:
        want = f.read()
    if r.returncode != 0 or r.stdout != want:
        got_l, want_l = r.stdout.split("\n"), want.split("\n")
        k = next((i for i in range(min(len(got_l), len(want_l))) if got_l[i] != want_l[i]),
                 min(len(got_l), len(want_l)))
        print("FAIL[lwjgl] bluewindow: exit %d, line %d %r, wanted %r; stderr %r"
              % (r.returncode, k + 1, got_l[k] if k < len(got_l) else None,
                 want_l[k] if k < len(want_l) else None, r.stderr.strip()[:300]))
        return 1
    if bad:
        return 1
    print("lwjgl: HelloWorld built, and a blue window's %d checks as expected" % (want.count("\n")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
