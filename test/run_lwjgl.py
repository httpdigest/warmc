"""warmc's LWJGL (lwjgl/): LWJGL 3 programs, on LWJGL's own jars, through
TeaVM and warmc, natively.

  * lwjgl/demo/hello, LWJGL's HelloWorld, builds, and so does
    test/lwjgl/plain, which calls no C function, and runs;
  * test/lwjgl/memory runs as the JVM runs it with LWJGL's own natives (its
    expected.txt is that run's output): LWJGL's off-heap memory through
    warmc's backend and allocator, in the program's memory and in C's;
  * test/lwjgl/jni runs, its output expected.txt's: calls through
    org.lwjgl.system.JNI, which lwjgl/build.py generates, of C of its own
    (--c), at addresses dlsym finds, and through LWJGL's libffi;
  * test/lwjgl/leak runs with LWJGL's debug allocator, which reports the
    block it did not free when main returns, from a shutdown hook;
  * test/lwjgl/objnative, which calls a native taking an object, is refused
    by TeaVM, which names the native;
  * test/lwjgl/bluewindow runs: GLFW's version, an error GLFW reports through
    the error callback and one a callback throws coming out of the GLFW call
    that ran it, a window and what GLFW and OpenGL say about it, the addresses
    LWJGL's capabilities hold, and the pixels of a few frames cleared to blue
    read back with glReadPixels -- its output expected.txt's, line for line;
  * and its module calls each GLFW and OpenGL function it does by the
    function's own import, which warmc makes a direct call: a call through an
    address is LWJGL's own start-up's or shutdown's, or an optional
    function's, whose address LWJGL checks, alone, and nothing else has
    GLFW's functions looked up by name.

It needs macOS, a JDK, `make teavm`, `make glfw` and `make lwjgl`, and a
display, without which GLFW makes no window and bluewindow says so and is left
out.

Usage: python3 test/run_lwjgl.py
"""

import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, os.environ.get("BUILD", "build"))
OUT = os.path.join(BUILD, "lwjgl")
sys.path.insert(0, os.path.join(ROOT, "test", "teavm"))
import wasi  # noqa: E402

# The GLFW and OpenGL functions bluewindow calls, each through LWJGL's API:
# a native (glClear), a call through GLFW's Functions (glfwPollEvents), and
# one through the capabilities with a Java array (glGetIntegerv).
DIRECT = """glfwSetErrorCallback glfwInit glfwGetVersion glfwWindowHint glfwDefaultWindowHints
glfwCreateWindow glfwSetKeyCallback glfwGetWindowSize glfwGetVideoMode glfwGetPrimaryMonitor
glfwSetWindowPos glfwMakeContextCurrent glfwSwapInterval glfwShowWindow glfwGetFramebufferSize
glfwWindowShouldClose glfwSwapBuffers glfwPollEvents glfwDestroyWindow glfwTerminate glGetString
glViewport glGetIntegerv glClearColor glClear glReadPixels glGetError""".split()
# Where LWJGL calls through an address it holds, which bluewindow reaches:
# its start-up's queries of the OpenGL version and extensions, a function
# provider's fallback, its shutdown's loop over the callback setters, and an
# optional function, whose address it checks; and bluewindow's own call.
THROUGH = {"org.lwjgl.opengl.GL::createCapabilities", "org.lwjgl.opengl.GL$1::getFunctionAddress",
           "org.lwjgl.glfw.Callbacks::glfwFreeCallbacks", "org.lwjgl.glfw.GLFW::glfwResetPreeditText",
           "Main::main"}
# what the debug allocator says of a block not freed, with no stack to walk
LEAK = re.compile(r"\[LWJGL\] 24 bytes leaked, thread \d+ \(main\), address: 0x[0-9A-F]+\n")


def build(src, main, exe, *extra):
    r = subprocess.run([sys.executable, os.path.join(ROOT, "lwjgl", "build.py"), "--main", main]
                       + list(extra) + [src, "-o", exe], capture_output=True, text=True)
    if r.returncode != 0:
        print("FAIL[lwjgl] %s: %s" % (src, (r.stdout + r.stderr).strip()[-1500:]))
        return False
    return True


def expect(name, r, want):
    """Whether run r printed want, saying where it did not."""
    if r.returncode == 0 and r.stdout == want:
        return True
    got_l, want_l = r.stdout.split("\n"), want.split("\n")
    k = next((i for i in range(min(len(got_l), len(want_l))) if got_l[i] != want_l[i]),
             min(len(got_l), len(want_l)))
    print("FAIL[lwjgl] %s: exit %d, line %d %r, wanted %r; stderr %r"
          % (name, r.returncode, k + 1, got_l[k] if k < len(got_l) else None,
             want_l[k] if k < len(want_l) else None, r.stderr.strip()[:300]))
    return False


def run_expected(name, *extra):
    """test/lwjgl/NAME built and run, its output expected.txt's."""
    src = os.path.join(ROOT, "test", "lwjgl", name)
    exe = os.path.join(OUT, name)
    if not build(src, "Main", exe, *extra):
        return False
    try:
        r = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        print("FAIL[lwjgl] %s: still running after 60 s" % name)
        return False
    with open(os.path.join(src, "expected.txt")) as f:
        return expect(name, r, f.read())


def check_direct(module):
    """The module lwjgl/build.py made of bluewindow calls GLFW and OpenGL
    directly: each of DIRECT by an import of its own name, a call through an
    address only in THROUGH, and GLFW's Functions initialized only there."""
    text = subprocess.run([wasi.wasm_tools(), "print", module], capture_output=True, text=True).stdout
    imports, through = {}, set()
    for name, sym in re.findall(r'^  \(import "lwjgl" "([^"]*)" \(func (\$\S+)', text, re.M):
        imports[sym] = name
    names = set(imports.values())
    bad = [f for f in DIRECT if f not in names]
    if bad:
        print("FAIL[lwjgl] bluewindow: no import of its own for %s" % " ".join(bad))
    func = None
    for line in text.split("\n"):
        m = re.match(r"  \(func (\$\S+)", line)
        if m:
            func = m.group(1)[1:]
            continue
        # a call of the address itself, or of JNI's method for it where TeaVM
        # kept that a function
        m = re.match(r"\s+call \$(\S+)", line)
        if m and (imports.get("$" + m.group(1), "").startswith("warmc_call")
                  or m.group(1).startswith("org.lwjgl.system.JNI::")) \
                and func not in THROUGH and not func.startswith("org.lwjgl.system.JNI::"):
            through.add(func)
        if "GLFW$Functions@initializer" in line and func is not None and func not in THROUGH \
                and func != "org.lwjgl.glfw.GLFW$Functions::<clinit>":
            through.add(func + " (GLFW's Functions initialized)")
    if through:
        print("FAIL[lwjgl] bluewindow: a call through an address in %s" % ", ".join(sorted(through)))
    return not bad and not through


def main():
    if sys.platform != "darwin":
        print("lwjgl: left out (macOS's alone so far)")
        return 0
    if not shutil.which("javac"):
        print("lwjgl: left out (needs a JDK)")
        return 0
    tools = os.path.join(BUILD, "tools")
    have = os.listdir(tools) if os.path.isdir(tools) else []
    if not all(any(d.startswith(p) for d in have) for p in ("teavm-", "glfw-", "lwjgl-")):
        print("lwjgl: left out (needs `make teavm`, `make glfw` and `make lwjgl`)")
        return 0
    os.makedirs(OUT, exist_ok=True)
    bad = 0
    if not build(os.path.join(ROOT, "lwjgl", "demo", "hello"), "HelloWorld", os.path.join(OUT, "HelloWorld")):
        bad += 1
    # a program that calls none of GLFW or OpenGL builds and runs as well
    plain = os.path.join(OUT, "plain")
    if build(os.path.join(ROOT, "test", "lwjgl", "plain"), "Main", plain):
        r = subprocess.run([plain], capture_output=True, text=True)
        if r.returncode != 0 or r.stdout != "no C called\n":
            print("FAIL[lwjgl] plain: exit %d, stdout %r" % (r.returncode, r.stdout))
            bad += 1
    else:
        bad += 1
    bad += not run_expected("memory")
    bad += not run_expected("jni", "--c", os.path.join(ROOT, "test", "lwjgl", "jni", "native.c"))
    leak = os.path.join(OUT, "leak")
    if build(os.path.join(ROOT, "test", "lwjgl", "leak"), "Main", leak):
        r = subprocess.run([leak], capture_output=True, text=True)
        if (r.returncode != 0 or r.stdout != "main returns with 24 bytes not freed\n"
                or not LEAK.fullmatch(r.stderr)):
            print("FAIL[lwjgl] leak: exit %d, stdout %r, stderr %r" % (r.returncode, r.stdout, r.stderr))
            bad += 1
    else:
        bad += 1
    r = subprocess.run([sys.executable, os.path.join(ROOT, "lwjgl", "build.py"),
                        os.path.join(ROOT, "test", "lwjgl", "objnative"), "-o", os.path.join(OUT, "objnative")],
                       capture_output=True, text=True)
    if r.returncode == 0 or "TeaVM failed" not in r.stderr \
            or "not annotated with org.teavm.interop.Import\n    at org.lwjgl.system.jni.JNINativeInterface.NewGlobalRef" \
            not in r.stderr:
        print("FAIL[lwjgl] objnative: exit %d, wanted TeaVM to refuse NewGlobalRef: %s"
              % (r.returncode, r.stderr.strip()[-600:]))
        bad += 1

    src = os.path.join(ROOT, "test", "lwjgl", "bluewindow")
    exe = os.path.join(OUT, "bluewindow")
    if not build(src, "Main", exe):
        return 1
    bad += not check_direct(os.path.join(OUT, "app.wasm"))
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
    if not expect("bluewindow", r, want):
        return 1
    if bad:
        return 1
    print("lwjgl: HelloWorld built, LWJGL's memory as the JVM has it, calls through JNI and libffi as "
          "expected, a leak reported, a native taking an object refused, and a blue window's %d checks, "
          "its GLFW and OpenGL calls direct" % want.count("\n"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
