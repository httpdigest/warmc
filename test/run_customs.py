"""Custom sections, as a shared library hands them out by name.

test/cases/customsecs.wat, built with warmc --shared at each of
--custom-sections=default, all and none, and a C host that asks
PREFIX_custom_section for every name the module has and one it has not:
how many there are, each one's payload, and NULL and 0 one past the last.
That is compared with the module's own custom sections as warmc assembles
them, the name section and DWARF's .debug_* left out by default.  At each
mode a program on this host, linked by warmc's own linker, goes through
the format's checker as well (test/custom_check.py); the cross-built
formats get theirs in test/run_macho.sh, run_elf.sh and run_pe.sh.
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import custom_check
import hostbuild

HOST = r'''
#include "cs.h"
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
  for (int a = 1; a < argc; a++) {
    const uint8_t *d = (const uint8_t *)1;
    size_t n = 1;
    size_t count = cs_custom_section(argv[a], 0, NULL, NULL);
    printf("%s %zu", argv[a], count);
    for (size_t i = 0; i < count; i++) {
      cs_custom_section(argv[a], i, &d, &n);
      printf(" ");
      for (size_t k = 0; k < n; k++) printf("%02x", d[k]);
      if (!n) printf("-");
    }
    cs_custom_section(argv[a], count, &d, &n);
    printf(" %s\n", d == NULL && n == 0 ? "end" : "NOT-CLEARED");
  }
  return 0;
}
'''


def main():
    root = os.path.dirname(HERE)
    os.chdir(root)
    build = os.environ.get("BUILD", "build")
    warmc = os.path.join(build, "warmc")
    wat = os.path.join("test", "cases", "customsecs.wat")
    if hostbuild.DARWIN:
        target, rt, ext = "macos", os.path.join(build, "warmrt-macos.a"), "dylib"
    elif not hostbuild.WINDOWS:
        target, rt, ext = "linux", os.path.join(build, "warmrt.a"), "so"
    else:
        print("customs: SKIPPED (the library half is test/run_shared.sh's on Windows)")
        return 0
    customs = custom_check.module_customs(warmc, wat)
    # the names the module has, a name it has not, and names with a prefix
    # or a suffix of one it has
    names = sorted(set(n.decode() for n, _ in customs if n)) + ["missing", "twic", "twicee"]
    sh = lambda c, **kw: subprocess.run(c, capture_output=True, text=True, **kw)
    fails = []
    with tempfile.TemporaryDirectory() as tmp:
        host_c = os.path.join(tmp, "host.c")
        with open(host_c, "w") as f:
            f.write(HOST)
        for mode in ("default", "all", "none"):
            lib = os.path.join(tmp, "libcs." + ext)
            r = sh([warmc, "--target=" + target, "--rt", rt, "--shared",
                    "--custom-sections=" + mode, "--symbol-prefix=cs", "-o", lib, wat])
            if r.returncode:
                fails.append("%s: warmc --shared: %s" % (mode, r.stderr.strip()[:300]))
                continue
            host = os.path.join(tmp, "host")
            r = sh(["cc", "-O2", "-Wall", "-Werror", "-I", tmp, "-o", host, host_c,
                    "-L", tmp, "-lcs", "-Wl,-rpath," + tmp])
            if r.returncode:
                fails.append("%s: the host does not build: %s" % (mode, r.stderr.strip()[:300]))
                continue
            r = sh([host] + names)
            want = custom_check.kept(customs, mode)
            for line, name in zip(r.stdout.splitlines(), names):
                got = line.split(" ")
                pay = [d.hex() or "-" for n, d in want if n == name.encode()]
                if got != [name, str(len(pay))] + pay + ["end"]:
                    fails.append("%s: %s gives %s, not %s"
                                 % (mode, name, got[1:], [str(len(pay))] + pay + ["end"]))
            if len(r.stdout.splitlines()) != len(names):
                fails.append("%s: the host answered %d of %d names: %s"
                             % (mode, len(r.stdout.splitlines()), len(names), r.stderr[:200]))
            if hostbuild.DARWIN:
                prog = os.path.join(tmp, "prog")
                r = sh([warmc, "--target=macos", "--rt", rt, "--custom-sections=" + mode,
                        "-o", prog, wat])
                wasm = os.path.join(tmp, "cs.wasm")
                sh([warmc, "--emit-wasm", "-o", wasm, wat])
                r = sh(["python3", os.path.join(HERE, "check_macho.py"), "--wasm=" + wasm, prog],
                       env=dict(os.environ, CUSTOM_SECTIONS=mode))
                if r.returncode:
                    fails.append("%s: %s" % (mode, (r.stdout + r.stderr).strip()[:300]))
    for f in fails:
        print("FAIL[customs] " + f)
    if not fails:
        print("customs: a library hands out its custom sections by name at each of "
              "--custom-sections=default, all and none, and a program carries them")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
