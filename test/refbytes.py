#!/usr/bin/env python3
"""Byte-identical object regression harness.

Codegen-preserving refactors (moving Buf out of a64.h, target-descriptor
hooks, RelKind growth) must not change a single output byte for the existing
targets.  This records a SHA-256 manifest of `warmc -c` output over a fixed
corpus x {linux, macos} x {-O0, -O2} and checks later builds against it, so
the refactor window is verifiable from a host that cannot execute AArch64.

  refbytes.py --record          write test/refbytes.json from the current warmc
  refbytes.py --check           compare the current warmc against the manifest
  refbytes.py --jobs-test       prove the worker pool does not perturb output

A module that fails to compile records as "error:<exit code>"; those compare
like any other value, so a corpus quirk cannot mask a regression elsewhere.
"""
import argparse
import concurrent.futures
import glob
import hashlib
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORPUS_DIRS = ["test/cases", "test/cases_traps", "test/cases_gc",
               "test/cases_relaxed", "test/bench"]
TARGETS = ["linux", "macos"]
LEVELS = ["-O0", "-O2"]
MANIFEST = os.path.join(ROOT, "test", "refbytes.json")


def warmc_path(build):
    for name in ("warmc.exe", "warmc"):
        p = os.path.join(ROOT, build, name)
        if os.path.exists(p):
            return p
    sys.exit(f"refbytes: no warmc in {build}/ (run make first)")


def corpus():
    files = []
    for d in CORPUS_DIRS:
        files += sorted(glob.glob(os.path.join(ROOT, d, "*.wat")))
    if not files:
        sys.exit("refbytes: empty corpus")
    return files


def scrubbed_env(jobs):
    env = {k: v for k, v in os.environ.items() if not k.startswith("WARMC_")}
    env["WARMC_JOBS"] = str(jobs)
    return env


def one_hash(warmc, wat, target, level, env, tmpdir):
    out = tempfile.NamedTemporaryFile(dir=tmpdir, suffix=".o", delete=False)
    out.close()
    r = subprocess.run([warmc, level, "-c", f"--target={target}", "-o", out.name, wat],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
    if r.returncode != 0:
        os.unlink(out.name)
        return f"error:{r.returncode}"
    with open(out.name, "rb") as f:
        h = hashlib.sha256(f.read()).hexdigest()
    os.unlink(out.name)
    return h


def build_manifest(warmc, jobs, workers):
    env = scrubbed_env(jobs)
    result = {}
    with tempfile.TemporaryDirectory() as tmpdir, \
         concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        futs = {}
        for wat in corpus():
            rel = os.path.relpath(wat, ROOT).replace("\\", "/")
            for target in TARGETS:
                for level in LEVELS:
                    key = f"{rel}|{target}|{level}"
                    futs[key] = pool.submit(one_hash, warmc, wat, target, level, env, tmpdir)
        for key, fut in futs.items():
            result[key] = fut.result()
    return dict(sorted(result.items()))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--record", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--jobs-test", action="store_true")
    ap.add_argument("--build", default="build")
    ap.add_argument("--workers", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()
    warmc = warmc_path(args.build)

    if args.jobs_test:
        a = build_manifest(warmc, 1, args.workers)
        b = build_manifest(warmc, 8, args.workers)
        diff = [k for k in a if a[k] != b.get(k)]
        if diff:
            for k in diff[:20]:
                print(f"  jobs-dependent output: {k}")
            sys.exit(f"refbytes: {len(diff)} manifest entries depend on WARMC_JOBS")
        print(f"refbytes: {len(a)} entries identical at WARMC_JOBS=1 and 8")
        return

    if args.record:
        m = build_manifest(warmc, 8, args.workers)
        errs = sum(1 for v in m.values() if v.startswith("error:"))
        with open(MANIFEST, "w") as f:
            json.dump(m, f, indent=0, sort_keys=True)
            f.write("\n")
        print(f"refbytes: recorded {len(m)} entries ({errs} compile errors) to {MANIFEST}")
        return

    if args.check:
        if not os.path.exists(MANIFEST):
            sys.exit("refbytes: no manifest; run --record at the baseline commit first")
        with open(MANIFEST) as f:
            want = json.load(f)
        got = build_manifest(warmc, 8, args.workers)
        bad = [k for k in want if want[k] != got.get(k)]
        gone = [k for k in want if k not in got]
        new = [k for k in got if k not in want]
        for k in bad[:40]:
            print(f"  CHANGED {k}: {want[k][:16]} -> {got.get(k, 'missing')[:16]}")
        if new:
            print(f"  ({len(new)} new corpus entries not in manifest; re-record to include)")
        if bad:
            sys.exit(f"refbytes: {len(bad)} of {len(want)} outputs changed "
                     f"({len(gone)} missing)")
        print(f"refbytes: {len(want)} outputs byte-identical")
        return

    ap.print_help()


if __name__ == "__main__":
    main()
