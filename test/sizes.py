"""What the compiler's output weighs, per module, across the corpora.

Two modes, and the second is the one that matters:

    test/sizes.py record BASE      compile every corpus module, write sizes
                                   and a hash of each output to BASE.json
    test/sizes.py compare BASE     do it again and print the difference

The hash is why this is not just `ls -l`.  A change meant to remove redundant
information has to leave every program running the same way, and the cheapest
statement of that is *the bytes did not move* -- so a kernel whose output
changes at all is listed separately from one that only got smaller.  A step
that shrinks the tables changes the hash of everything, of course; what the
column is for is the steps that should not, and for the corpora a step does
not claim to touch.

The modules are whatever the suites have already built under $BUILD, so this
reads the same inputs `make check` does and builds none of them itself.
"""
import hashlib
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.environ.get("BUILD", os.path.join(ROOT, "build"))
WARMC = os.path.join(BUILD, "warmc") + (".exe" if os.name == "nt" else "")

CORPORA = ["ktbench", "asbench", "rsbench", "gobench", "stdgo", "vecbench", "zigbench",
           "cbench", "cppbench", "hsbench", "javabench"]

def flags_for(path):
    """`--wasi` for a module that imports the preview1 host, nothing otherwise.

    This used to be recorded per corpus, and the record had drifted: the table
    said asbench and vecbench needed no host, while every vecbench module and
    all but one of asbench's import `fd_write`.  Nothing noticed, because warmc
    would build such a program and only fail when it was run -- and this script
    never runs one, it weighs it.  warmc refuses that build now, so the stale
    rows turned into 56 modules recorded as FAIL.  Asking the module removes
    the chance for the answer to be wrong again.
    """
    with open(path, "rb") as fh:
        return ["--wasi"] if b"wasi_snapshot_preview1" in fh.read() else []

def modules():
    for sub in CORPORA:
        d = os.path.join(BUILD, sub)
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if f.endswith(".wasm"):
                p = os.path.join(d, f)
                yield sub, p, flags_for(p)

    hello = os.path.join(ROOT, "example", "kotlin", "out", "hello.wasm")
    if os.path.exists(hello):
        yield "example", hello, flags_for(hello)

def measure(out_dir, extra):
    """{corpus/name: [bytes, sha256]} for every module, compiled fresh."""
    os.makedirs(out_dir, exist_ok=True)
    res = {}
    for corpus, path, flags in modules():
        name = "%s/%s" % (corpus, os.path.basename(path)[:-5])
        out = os.path.join(out_dir, name.replace("/", "_"))
        cmd = [WARMC, "--target", "macos", "--rt",
               os.path.join(BUILD, "warmrt-macos.a")] + flags + extra + \
              ["-o", out, path]
        p = subprocess.run(cmd, capture_output=True)
        if p.returncode != 0:
            res[name] = ["FAIL", p.stderr.decode()[-200:]]
            continue
        with open(out, "rb") as fh:
            blob = fh.read()
        res[name] = [len(blob), hashlib.sha256(blob).hexdigest()[:16]]
        os.unlink(out)
    return res

def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    mode, base = sys.argv[1], sys.argv[2]
    extra = sys.argv[3:]
    tmp = os.path.join(BUILD, "sizes")
    now = measure(tmp, extra)
    if mode == "record":
        with open(base, "w") as fh:
            json.dump(now, fh, indent=1, sort_keys=True)
        total = sum(v[0] for v in now.values() if isinstance(v[0], int))
        print("recorded %d modules, %d bytes -> %s" % (len(now), total, base))
        return 0
    if mode != "compare":
        print(__doc__)
        return 2
    with open(base) as fh:
        was = json.load(fh)

    same_bytes, changed, wins, losses = 0, 0, 0, 0
    tot_a = tot_b = 0
    rows = []
    for name in sorted(set(was) | set(now)):
        a, b = was.get(name), now.get(name)
        if a is None or b is None:
            rows.append((name, "only in %s" % ("new" if a is None else "old")))
            continue
        if not isinstance(a[0], int) or not isinstance(b[0], int):
            rows.append((name, "FAIL"))
            continue
        tot_a += a[0]
        tot_b += b[0]
        if a[1] == b[1]:
            same_bytes += 1
            continue
        changed += 1
        d = b[0] - a[0]
        if d < 0:
            wins += 1
        elif d > 0:
            losses += 1
        rows.append((name, "%9d -> %9d  %+8d  %+6.2f%%" %
                     (a[0], b[0], d, 100.0 * d / a[0])))
    for name, text in rows:
        print("%-28s %s" % (name, text))
    print("---")
    print("byte-identical: %d   changed: %d (%d smaller, %d larger)" %
          (same_bytes, changed, wins, losses))
    if tot_a:
        print("total: %d -> %d  %+d  %+.2f%%" %
              (tot_a, tot_b, tot_b - tot_a, 100.0 * (tot_b - tot_a) / tot_a))
    return 0

if __name__ == "__main__":
    sys.exit(main())
