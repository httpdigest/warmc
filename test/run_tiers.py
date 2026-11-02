"""Tiered compiling (src/jit.c): programs of every producer run through the
JIT, once compiled at -O2 as it is by default and once tiered, with the
tier-up held back until the program's first write and made then
(WARMC_TIERED=host, WARMC_TIER_DEFER): the frames below that call -- Go's
scheduler loop, GHC's trampoline, a JVM language's main -- are -O0 code
that goes on to call the optimised code, with the signatures Go's and GHC's
rewrites give their functions on both sides, the collector walking frames
of both tiers, exceptions thrown through both.  The two runs must print the
same and exit alike, and the tiered one must say that its tier-up came
(WARMC_TIER_DEBUG).

The programs are the corpora's own, built as test/gobench.py builds them; a
corpus whose producer is not installed is skipped and said so.

    python3 test/run_tiers.py
"""
import concurrent.futures
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import gobench  # noqa: E402
import hostbuild  # noqa: E402

BUILD = os.path.abspath(os.environ.get("BUILD", os.path.join(ROOT, "build")))
WARMRUN = hostbuild.exe(os.path.join(BUILD, "warmrun"))
TIMEOUT = float(os.environ.get("WARMC_RUN_TIMEOUT", "120"))

# the largest programs of each corpus, which make the most functions and
# call WASI only after starting up
SEL = {
    "asbench": ["mathf", "mathfn"], "cbench": ["qsort", "libm"],
    "cppbench": ["regex", "sstream"], "gobench": ["textproc", "parsenum"],
    "hsbench": ["printf", "sequence"], "javabench": ["stream", "strapi"],
    "ktbench": ["maphash", "seq"], "rsbench": ["mapint", "fmtfloat"],
    "stdgo": ["regex", "json"], "vecbench": ["popcnt"],
    "zigbench": ["wordfreq", "sortrec"], "gotests": ["hashing"],
    "stdgotests": ["goparse"], "hstests": ["containers"],
    "javatests": ["collections"], "kttests": ["collections"],
}


def modules():
    """(corpus, kernel, wasm) for each that builds; the corpora skipped"""
    out, skipped = [], []
    for corpus, want in SEL.items():
        gobench.set_corpus(corpus)
        names, lang = gobench.kernels()
        tool, probe = gobench.TOOLCHAIN[lang]
        if not gobench.have([tool] + probe) or (lang == "rs" and not gobench.rust_target_installed()):
            skipped.append(corpus)
            continue
        for k in want:
            if k not in names:
                sys.exit("tiers: %s has no kernel %s" % (corpus, k))
            w = gobench.build_wasm(k, lang)
            if w is None:
                sys.exit("tiers: %s/%s does not build" % (corpus, k))
            out.append((corpus, k, w))
    return out, skipped


def run(w, env):
    r = subprocess.run([WARMRUN, w], capture_output=True, timeout=TIMEOUT,
                       env=dict(os.environ, **env))
    return r.returncode, r.stdout, r.stderr.decode(errors="replace")


def one(m):
    corpus, k, w = m
    a = run(w, {})
    b = run(w, {"WARMC_TIERED": "host", "WARMC_TIER_DEFER": "1", "WARMC_TIER_DEBUG": "1"})
    name = "%s/%s" % (corpus, k)
    if a[0] != b[0] or a[1] != b[1]:
        return "FAIL[tiers] %s: untiered exits %d, tiered %d%s: %s" % (
            name, a[0], b[0], "" if a[1] == b[1] else ", and prints otherwise",
            b[2].strip().splitlines()[-1:] if b[2].strip() else "")
    if "tier-up:" not in b[2]:
        return "FAIL[tiers] %s: no tier-up came" % name
    return None


def main():
    if not os.path.exists(WARMRUN):
        print("tiers: %s not built (make jit)" % WARMRUN)
        return 1
    mods, skipped = modules()
    with concurrent.futures.ThreadPoolExecutor(max(1, (os.cpu_count() or 4) // 4)) as ex:
        fails = [f for f in ex.map(one, mods) if f]
    for f in fails:
        print(f)
    print("tiers: %d of %d programs run tiered as untiered, the tier-up under their "
          "frames%s" % (len(mods) - len(fails), len(mods),
                        "; skipped (no producer): " + " ".join(skipped) if skipped else ""))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
