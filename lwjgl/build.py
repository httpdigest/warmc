"""An LWJGL 3 program to a native macOS executable, through TeaVM and warmc.

  python3 lwjgl/build.py [--main CLASS] [-O0|-O1|-O2|-O3] [--c FILE.c]...
                         [--every-gl-function] SRC_DIR -o OUT

SRC_DIR holds the program's .java files, written against LWJGL 3's API, and
CLASS (default Main) is its main class.  --c's C files are the program's own,
compiled and linked into it, their functions there for it to find by name as
LWJGL finds a library's.  The program is compiled against LWJGL's own jars
(`make lwjgl`), and TeaVM makes a wasm-gc module of the program, LWJGL's
classes and warmc's platform layer for them (lwjgl/platform), which takes the
place of the few of LWJGL's classes that load JNI libraries and reach the
JVM's memory:

  * LWJGL's natives are imports of module "lwjgl" (lwjgl/teavm/
    LwjglTransformer.java), which warmc's --native-module makes direct calls:
    an OpenGL function a call of macOS's; org.lwjgl.system.JNI's methods,
    generated here from LWJGL's own, calls of the function at an address
    (warmc_call); and a call through JNI of a function LWJGL looked up by
    name, as each GLFW function is called, a direct call of it;
  * MemoryUtil's backend and allocator are warmc's, over linear memory at its
    native addresses (lwjgl/platform/org/lwjgl/system/warmc);
  * a callback is a closure of the system's libffi that calls the program
    back (lwjgl/platform/org/lwjgl/system/Upcalls.java,
    lwjgl/native/lwjgl.c);
  * a library is the program itself, linked with GLFW (`make glfw`) and
    against macOS's OpenGL framework, its functions found by dlsym;
  * GLCapabilities looks up only the OpenGL functions the framework exports,
    as the SDK's stub of it lists them, which are the only ones dlsym can
    find: a check of a version or extension it has none of is false at
    once.  --every-gl-function looks each up, as LWJGL does, which makes a
    larger program with the same capabilities.

The TeaVM adapter (test/teavm/wasi.py) makes the module a WASI command and
warmc a native executable.

It needs `make all`, `make teavm`, `make glfw`, `make lwjgl` and a JDK, 21 or
later.
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD = os.path.join(ROOT, os.environ.get("BUILD", "build"))
TEAVM_SRC = os.path.join(ROOT, "test", "teavm")
sys.path.insert(0, TEAVM_SRC)
import wasi  # noqa: E402

WORK = os.path.join(BUILD, "lwjgl")
# The frameworks GLFW's static library needs on macOS, and OpenGL's.
FRAMEWORKS = ["Cocoa", "IOKit", "CoreFoundation", "QuartzCore", "OpenGL"]
# LWJGL's jars a program is compiled against and TeaVM reads.
ARTIFACTS = ["lwjgl", "lwjgl-glfw", "lwjgl-opengl"]
ENTRY = "WarmcMain"


def pinned(name):
    with open(os.path.join(ROOT, "Makefile")) as f:
        for line in f:
            if line.startswith(name + " "):
                return line.split(":=", 1)[1].strip()
    raise SystemExit("lwjgl: no %s in the Makefile" % name)


TEAVM = os.path.join(BUILD, "tools", "teavm-" + pinned("TEAVM_VERSION"))
GLFW = os.path.join(BUILD, "tools", "glfw-" + pinned("GLFW_VERSION"))
LWJGL_VERSION = pinned("LWJGL_VERSION")
LWJGL = os.path.join(BUILD, "tools", "lwjgl-" + LWJGL_VERSION)
JARS = [os.path.join(LWJGL, "%s-%s.jar" % (a, LWJGL_VERSION)) for a in ARTIFACTS]


def run(cmd, what):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit("lwjgl: %s failed:\n%s%s" % (what, r.stdout, r.stderr))
    return r


def newest(paths):
    return max((os.path.getmtime(p) for p in paths), default=0)


def stale(target, sources):
    return not os.path.exists(target) or os.path.getmtime(target) < newest(sources)


# ------------------------------------------------------------- generated

JAVA = {"I": "int", "J": "long", "F": "float", "D": "double", "S": "short", "B": "byte",
        "C": "char", "Z": "boolean", "V": "void"}


def descriptor_types(desc):
    """(params, result) of a method descriptor, each a type letter, or '[' and
    the element's for an array."""
    m = re.match(r"\((.*)\)(.*)$", desc)
    params, i, s = [], 0, m.group(1)
    while i < len(s):
        if s[i] == "[":
            params.append("[" + s[i + 1])
            i += 2
        else:
            params.append(s[i])
            i += 1
    return params, m.group(2)


def jni_methods(jar):
    """(name, params, result) of each native of LWJGL's org.lwjgl.system.JNI."""
    out = run(["javap", "-cp", jar, "-s", "org.lwjgl.system.JNI"], "javap of JNI").stdout.split("\n")
    methods = []
    for i, line in enumerate(out):
        m = re.match(r"\s+public static native \S+ (\w+)\(", line)
        if m:
            desc = re.match(r"\s+descriptor: (\S+)", out[i + 1]).group(1)
            params, result = descriptor_types(desc)
            methods.append((m.group(1), params, result))
    return methods


def jni_source(jar):
    """org.lwjgl.system.JNI for warmc: each of LWJGL's a call of the function
    at the address its last argument holds (warmc_call), through a native of
    its own, which lwjgl/teavm/LwjglTransformer.java has a call through a
    known address make a direct call of the function in its place.  An array
    is copied into linear memory for C and back after; a narrow result is
    taken from a whole register, which C leaves undefined above it; and then
    the exception a callback the function ran kept is thrown (Upcalls), as
    JNI throws it when the native returns."""
    L = ["// Generated by lwjgl/build.py from LWJGL %s's org.lwjgl.system.JNI -- do not edit." % LWJGL_VERSION,
         "package org.lwjgl.system;",
         "",
         "import org.lwjgl.system.warmc.WarmcArrays;",
         "import org.teavm.interop.Import;",
         "",
         "public final class JNI {",
         "    private JNI() {",
         "    }"]
    for k, (name, params, result) in enumerate(jni_methods(jar)):
        decl = ", ".join("%s p%d" % (JAVA[t[1]] + "[]" if t[0] == "[" else JAVA[t], i)
                         for i, t in enumerate(params))
        cparams = ", ".join("%s p%d" % ("long" if t[0] == "[" else JAVA[t], i) for i, t in enumerate(params))
        cresult = "int" if result in "BSCZ" else JAVA[result]
        arrays = [i for i, t in enumerate(params) if t[0] == "["]
        L += ["", '    @Import(module = "lwjgl", name = "warmc_call.%s.%d")' % (name, k),
              "    private static native %s %s$%d(%s);" % (cresult, name, k, cparams),
              "", "    public static %s %s(%s) {" % (JAVA[result], name, decl)]
        call = "%s$%d(%s)" % (name, k, ", ".join(("a%d" if i in arrays else "p%d") % i
                                                  for i in range(len(params))))
        body = []
        if result == "V":
            call += ";"
        elif arrays:
            body.append("%s r;" % cresult)
            call = "r = %s;" % call
        else:
            call = "%s r = %s;" % (cresult, call)
        if arrays:
            body += ["MemoryStack stack = MemoryStack.stackGet();",
                     "int sp = stack.getPointer();",
                     "try {"]
            body += ["    long a%d = WarmcArrays.in(stack, p%d);" % (i, i) for i in arrays]
            body += ["    " + call]
            body += ["    WarmcArrays.out(p%d, a%d);" % (i, i) for i in arrays]
            body += ["} finally {", "    stack.setPointer(sp);", "}"]
        else:
            body.append(call)
        body += ["if (Upcalls.pending != null) {", "    Upcalls.rethrow();", "}"]
        if result != "V":
            body.append("return %s;" % {"B": "(byte) r", "S": "(short) r", "C": "(char) r",
                                        "Z": "(r & 0xFF) != 0"}.get(result, "r"))
        L += ["        " + x for x in body]
        L.append("    }")
    L += ["}", ""]
    return "\n".join(L)


def version_source(jar):
    """org.lwjgl.VersionImpl for warmc, with what LWJGL's reads from its jar's
    manifest at run time read from it here."""
    with zipfile.ZipFile(jar) as z:
        manifest = z.read("META-INF/MANIFEST.MF").decode()
    field = dict(re.findall(r"^([\w-]+): (.*?)\s*$", manifest, re.M))
    return "\n".join([
        "// Generated by lwjgl/build.py from LWJGL %s's manifest -- do not edit." % LWJGL_VERSION,
        "package org.lwjgl;",
        "",
        "final class VersionImpl {",
        "    private VersionImpl() {",
        "    }",
        "",
        "    static String find() {",
        '        return Version.createImplementation("%s", "%s");'
        % (field["Specification-Version"], field["Implementation-Version"]),
        "    }",
        "}",
        ""])


def entry_source(main_class):
    """The program's entry point: warmc's setup, its main, then the shutdown
    hooks."""
    return "\n".join([
        "// Generated by lwjgl/build.py -- do not edit.",
        "public final class %s {" % ENTRY,
        "    private %s() {" % ENTRY,
        "    }",
        "",
        "    public static void main(String[] args) throws Throwable {",
        "        org.lwjgl.system.warmc.Setup.run();",
        "        %s.main(args);" % main_class,
        "        org.lwjgl.system.warmc.Setup.exit();",
        "    }",
        "}",
        ""])


# ---------------------------------------------------------------- built

def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if os.path.exists(path):
        with open(path) as f:
            if f.read() == text:
                return
    with open(path, "w") as f:
        f.write(text)


def gl_exports():
    """The OpenGL functions macOS's framework exports for arm64, one a line,
    from the SDK's text stub of it (and the libraries it re-exports)."""
    sdk = run(["xcrun", "--show-sdk-path"], "xcrun").stdout.strip()
    tbd = os.path.join(sdk, "System", "Library", "Frameworks", "OpenGL.framework", "Versions", "A", "OpenGL.tbd")
    out = os.path.join(WORK, "gl-exports.txt")
    if stale(out, [tbd, __file__]):
        with open(tbd) as f:
            text = f.read()
        names = set()
        for targets, symbols in re.findall(r"targets:\s*\[(.*?)\]\s*symbols:\s*\[(.*?)\]", text, re.S):
            if "arm64" in targets:
                names.update(s.strip().strip("'")[1:] for s in symbols.split(",") if s.strip().strip("'").startswith("_gl"))
        if not names:
            raise SystemExit("lwjgl: no OpenGL function in %s" % tbd)
        write(out, "".join(n + "\n" for n in sorted(names)))
    return out


def transformer():
    """lwjgl/teavm, compiled against TeaVM, for the driver's class path."""
    out = os.path.join(WORK, "transformer")
    srcs = glob.glob(os.path.join(HERE, "teavm", "*.java"))
    stamp = os.path.join(out, ".complete")
    if stale(stamp, srcs):
        shutil.rmtree(out, ignore_errors=True)
        run(["javac", "--release", "17", "-nowarn", "-cp", os.path.join(TEAVM, "*"), "-d", out] + srcs,
            "javac of lwjgl/teavm")
        open(stamp, "w").close()
    return out


def platform():
    """lwjgl/platform and the generated classes, compiled against LWJGL's jars
    and TeaVM's class library, once."""
    gen = os.path.join(WORK, "gen")
    write(os.path.join(gen, "org", "lwjgl", "system", "JNI.java"), jni_source(JARS[0]))
    write(os.path.join(gen, "org", "lwjgl", "VersionImpl.java"), version_source(JARS[0]))
    out = os.path.join(WORK, "platform")
    stamp = os.path.join(out, ".complete")
    sources = sorted(glob.glob(os.path.join(HERE, "platform", "**", "*.java"), recursive=True) +
                     glob.glob(os.path.join(gen, "**", "*.java"), recursive=True))
    stubs = glob.glob(os.path.join(HERE, "stub", "**", "*.java"), recursive=True)
    if stale(stamp, sources + stubs + JARS):
        shutil.rmtree(out, ignore_errors=True)
        # java.nio.LwjglBuffers is TeaVM's to provide (lwjgl/stub)
        cp = os.pathsep.join(JARS + [os.path.join(TEAVM, "*")])
        run(["javac", "--release", "21", "-nowarn", "-encoding", "UTF-8", "-proc:none", "-implicit:none",
             "--patch-module", "java.base=" + os.path.join(HERE, "stub"),
             "-cp", cp, "-d", out] + sources, "javac of lwjgl/platform")
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


def own(src):
    """One of the program's own C files, compiled."""
    obj = os.path.join(WORK, "c", os.path.splitext(os.path.basename(src))[0] + ".o")
    os.makedirs(os.path.dirname(obj), exist_ok=True)
    run(["clang", "-O2", "-mmacosx-version-min=11.0", "-c", "-o", obj, src], "clang of " + src)
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
    ap.add_argument("--c", action="append", default=[], metavar="FILE.c", help="C of the program's own")
    ap.add_argument("--every-gl-function", action="store_true",
                    help="look up every OpenGL function LWJGL knows, not only those macOS exports")
    ap.add_argument("-v", action="store_true", help="print the link command")
    a = ap.parse_args()

    for need, how in ((os.path.join(TEAVM, "patched", ".complete"), "make teavm"),
                      (os.path.join(GLFW, ".complete"), "make glfw"),
                      (os.path.join(LWJGL, ".complete"), "make lwjgl"),
                      (os.path.join(BUILD, "warmc"), "make all")):
        if not os.path.exists(need):
            raise SystemExit("lwjgl: no %s: run `%s`" % (need, how))
    os.makedirs(WORK, exist_ok=True)
    classes = platform()
    obj = glue()

    # the program's sources and the entry point, in one directory, as the
    # driver compiles a directory
    src = os.path.join(WORK, "src")
    shutil.rmtree(src, ignore_errors=True)
    os.makedirs(src)
    for f in glob.glob(os.path.join(a.src, "*.java")):
        shutil.copy(f, src)
    write(os.path.join(src, ENTRY + ".java"), entry_source(a.main))

    job = os.path.join(WORK, "job")
    shutil.rmtree(job, ignore_errors=True)
    cp = os.pathsep.join([os.path.join(TEAVM, "patched"), os.path.join(TEAVM, "*"), driver(), transformer()])
    props = [] if a.every_gl_function else ["-Dwarmc.lwjgl.gl-exports=" + gl_exports()]
    run(["java"] + props + ["-cp", cp, "warmc.teavm.TeaVMBuild", "--teavm", TEAVM, "--main", ENTRY,
         "--classpath", os.pathsep.join([classes] + JARS),
         "--transformer", "warmc.lwjgl.LwjglTransformer",
         "--support", os.path.join(TEAVM_SRC, "FdLibm.java"),
         "%s=%s" % (src, job)], "TeaVM")
    with open(os.path.join(job, "main.wasm"), "rb") as f:
        module = f.read()
    adapted = os.path.join(WORK, "app.wasm")
    err = wasi.write(module, adapted, ENTRY, native={"lwjgl"})
    if err:
        raise SystemExit("lwjgl: the TeaVM adapter: %s" % err)

    # GLFW whole: LWJGL finds its functions by name (dlsym), which nothing
    # else references
    link = [obj] + [own(c) for c in a.c]
    link += ["-force_load", os.path.join(GLFW, "lib-arm64", "libglfw3.a"), "-lffi"]
    for fw in FRAMEWORKS:
        link += ["-framework", fw]
    cmd = [os.path.join(BUILD, "warmc"), "--target", "macos",
           "--rt", os.path.join(BUILD, "warmrt-macos.a"), "--wasi", "-O" + a.level,
           # the system linker, for --link, even for a program that calls no C
           "--system-ld", "--native-module", "lwjgl", "--link", " ".join(link), "-o", a.out, adapted]
    if a.v:
        cmd.append("-v")
    r = run(cmd, "warmc")
    sys.stderr.write("".join(l + "\n" for l in r.stderr.splitlines()
                             if "was built for newer" not in l))


if __name__ == "__main__":
    main()
