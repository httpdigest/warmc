"""Switches that must leave the code as it is.

Some of what the compiler does has a slower way that finds the same thing,
kept behind a switch; the object a module compiles to is the same bytes
with each:

  WARMC_NO_ORDER_LIGHT   the orderings between jump threadings made whole
                         (ir.c ir_order_light): a light one leaves out the
                         loop depths, the divergence and the layout's order,
                         which nothing between two threadings may read
  WARMC_POOL_ALL_TAILS   every tail of a root list looked up in its pool
                         (module_data.c root_run_intern), where the search
                         stops at the first one found
  WARMC_VERIFY_GATES     each gate's skipped work done and compared -- among
                         them a whole ordering after every light one, which
                         checks what the light one did find

over every module of the cases, at -O2 and -O3.

Usage: python3 test/run_same.py
"""
import concurrent.futures
import glob
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_gosp

ROOT = os.path.dirname(HERE)
BUILD = os.environ.get("BUILD", "build")
WARMC = os.path.join(ROOT, BUILD, "warmc")
SWITCHES = ("WARMC_NO_ORDER_LIGHT", "WARMC_POOL_ALL_TAILS", "WARMC_VERIFY_GATES")
DIRS = ("cases", "cases_opt", "cases_gc", "cases_traps")
LEVELS = ("-O2", "-O3")


def main():
    os.chdir(ROOT)
    # (an object needs no runtime: --rt and its path are left out)
    flags, skip = [], False
    for a in gen_gosp.target_flags(BUILD):
        if skip:
            skip = False
        elif a == "--rt":
            skip = True
        else:
            flags.append(a)
    mods = sorted(m for d in DIRS for m in glob.glob(os.path.join("test", d, "*.wat")))
    with tempfile.TemporaryDirectory() as tmp:
        def one(job):
            mod, level = job
            base = os.path.join(tmp, "%s%s" % (mod.replace(os.sep, "_"), level))
            objs = []
            for sw in (None,) + SWITCHES:
                obj = "%s.%s.o" % (base, sw or "plain")
                env = dict(os.environ, **{sw: "1"}) if sw else None
                r = subprocess.run([WARMC, level] + flags + ["-c", "-o", obj, mod],
                                   capture_output=True, text=True, env=env)
                if r.returncode:
                    if sw is None:
                        return []      # a module this target does not compile: no object to compare
                    return ["%s %s with %s: warmc: %s" % (mod, level, sw, r.stderr.strip()[:300])]
                objs.append(open(obj, "rb").read())
                os.remove(obj)
            return ["%s %s: the object differs with %s" % (mod, level, sw)
                    for sw, o in zip(SWITCHES, objs[1:]) if o != objs[0]]
        jobs = [(m, lv) for m in mods for lv in LEVELS]
        with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as ex:
            fails = [f for fs in ex.map(one, jobs) for f in fs]
    for f in fails[:20]:
        print("FAIL[same] " + f)
    if fails:
        return 1
    print("same: %d modules compile to the same objects at %s with each of %s"
          % (len(mods), " and ".join(LEVELS), ", ".join(SWITCHES)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
