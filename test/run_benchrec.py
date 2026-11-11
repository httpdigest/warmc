"""The other engines' figures in the baseline (test/gobench.py).

A timing run with --engines records each engine's time beside warmc's, with
warmc's time over it; --record-engines writes the engines' figures beside
what is recorded and moves nothing else; a later record of warmc alone
keeps them and takes its ratios over them again.  Here on figures made up
for it, no engine run:

  rows     an engine's time is its best less its own empty program's, where
           it runs the kernel; a kernel it does not run has no figure of it,
           and a time under 5 ms no ratio
  keep     a record that timed no engine keeps each recorded time and takes
           warmc's new time over it
  engines  --record-engines on a machine half as fast: the engine's time is
           what its ratio to warmc in that run says of the recorded warmc
           time; the figures of an engine that no longer runs the kernel
           go; wasmtime's and native's stay as they were
  reuse    --reuse-baseline's rows have the recorded engines as they have
           wasmtime
  version  an engine's version is recorded without the path it was run by

Usage: python3 test/run_benchrec.py
"""
import json
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gobench

fails = []


def want(what, got, exp):
    if got != exp:
        fails.append("%s: %r, not %r" % (what, got, exp))


def main():
    names = ["a", "b"]
    best = {"-O2": {"empty": 0.002, "a": 0.102, "b": 0.052},
            "wasmtime": {"empty": 0.005, "a": 0.205, "b": 0.105},
            "wamr": {"empty": 0.005, "a": 0.055},
            "wasmer": {"empty": 0.015, "a": 0.215, "b": 0.015},
            "wasmedge": {"empty": 0.01, "a": 0.0149, "b": 0.02}}
    second = {"-O2": {"empty": 0.002, "a": 0.103, "b": 0.052}}
    rss = {c: {k: 3 << 20 for k in ["empty"] + names} for c in best}
    rss["wamr"]["a"] = 18 << 20
    rows = gobench.baseline_rows(names, ["-O2"], best, second, True, rss, (), ["wamr", "wasmer", "wasmedge"])
    want("rows: an engine's time, less its floor", rows["a"]["wamr"], 0.05)
    want("rows: warmc's time over it", rows["a"]["ratio_wamr"], 2.0)
    want("rows: its memory", rows["a"]["rss_kb_wamr"], 18 << 10)
    want("rows: the second engine's ratio", rows["a"]["ratio_wasmer"], 0.5)
    want("rows: a kernel the engine does not run", [k for k in rows["b"] if "wamr" in k], [])
    want("rows: a time of nothing is kept", rows["b"]["wasmer"], 0.0)
    want("rows: and has no ratio", "ratio_wasmer" in rows["b"], False)
    want("rows: nor has a time just under 5 ms", (rows["a"]["wasmedge"], "ratio_wasmedge" in rows["a"]), (0.0049, False))
    want("rows: a time over it has", rows["b"]["ratio_wasmedge"], 5.0)
    want("rows: wasmtime's ratio as it was", rows["a"]["ratio"], 0.5)
    plain = gobench.baseline_rows(names, ["-O2"], best, second, True, rss)
    want("rows: no engine asked for, none recorded", gobench.engine_keys(plain["a"]), [])

    old = {"a": {"warmc": 0.1, "wamr": 0.05, "ratio_wamr": 2.0, "rss_kb_wamr": 7, "wasmer": 0.004},
           "b": {"warmc": 0.05}}
    new = gobench.baseline_keep_engines({"a": {"warmc": 0.2}, "b": {"warmc": 0.1},
                                         "c": {"warmc": 0.3, "wamr": 0.6}}, old)
    want("keep: the recorded time", new["a"]["wamr"], 0.05)
    want("keep: warmc's new time over it", new["a"]["ratio_wamr"], 4.0)
    want("keep: the memory", new["a"]["rss_kb_wamr"], 7)
    want("keep: a time under 5 ms has no ratio", (new["a"]["wasmer"], "ratio_wasmer" in new["a"]), (0.004, False))
    want("keep: a kernel that had none gets none", gobench.engine_keys(new["b"]), [])
    want("keep: a row timed in this run is left", new["c"], {"warmc": 0.3, "wamr": 0.6})

    with tempfile.TemporaryDirectory() as tmp:
        gobench.BASELINE = os.path.join(tmp, "baseline.json")
        was = {"corp": {"host": "h", "wasmtime": "wasmtime 1", "kernels": {
            "a": {"warmc": 0.1, "wasmtime": 0.2, "ratio": 0.5, "native": 0.08, "native_ratio": 1.25,
                  "wamr": 0.9, "ratio_wamr": 0.11, "rss_kb_wamr": 1, "wasmer": 0.7, "ratio_wasmer": 0.14},
            "b": {"warmc": 0.05, "wasmtime": 0.1, "ratio": 0.5}}},
            "other": {"kernels": {"x": {"warmc": 1.0}}}}
        json.dump(was, open(gobench.BASELINE, "w"))
        # the run: everything takes twice as long as when warmc was recorded
        run = {"-O2": {"empty": 0.004, "a": 0.204, "b": 0.104},
               "wamr": {"empty": 0.01, "a": 0.11, "b": 0.06},
               "wasmedge": {"empty": 0.02, "a": 0.028, "b": 0.02}}
        rc = gobench.baseline_record_engines("corp", names, "-O2", {"wamr": "wamrc 9", "wasmedge": "w 1"}, run,
                                             {c: {k: 5 << 20 for k in ["empty"] + names} for c in run})
        got = json.load(open(gobench.BASELINE))
        a = got["corp"]["kernels"]["a"]
        want("engines: it says so", rc, 0)
        want("engines: the engine's time beside the recorded warmc's", a["wamr"], 0.05)
        want("engines: this run's ratio", a["ratio_wamr"], 2.0)
        want("engines: its memory in this run", a["rss_kb_wamr"], 5 << 10)
        want("engines: an engine that no longer runs the kernel", [k for k in a if "wasmer" in k], [])
        want("engines: the rest of the row", {k: a[k] for k in ("warmc", "wasmtime", "ratio", "native", "native_ratio")},
             {"warmc": 0.1, "wasmtime": 0.2, "ratio": 0.5, "native": 0.08, "native_ratio": 1.25})
        want("engines: the other kernel", got["corp"]["kernels"]["b"]["ratio_wamr"], 2.0)
        want("engines: a time that comes to under 5 ms beside the recorded warmc's is kept, with no ratio",
             (a["wasmedge"], "ratio_wasmedge" in a), (0.004, False))
        want("engines: and the empty program's time", (got["corp"]["kernels"]["b"]["wasmedge"],
                                                       "ratio_wasmedge" in got["corp"]["kernels"]["b"]), (0.0, False))
        want("engines: the versions", got["corp"]["engines"], {"wamr": "wamrc 9", "wasmedge": "w 1"})
        want("engines: the corpus's other fields", (got["corp"]["host"], got["corp"]["wasmtime"]), ("h", "wasmtime 1"))
        want("engines: another corpus", got["other"], was["other"])
        want("engines: a corpus with no baseline", gobench.baseline_record_engines(
            "none", names, "-O2", {"wamr": "wamrc 9"}, run, {}), 1)

        reused = gobench.baseline_reuse("corp", {"a": {"warmc": 0.2}, "b": {"warmc": 0.1}})
        want("reuse: the engine's recorded time", reused["a"]["wamr"], 0.05)
        want("reuse: warmc's new time over it", reused["a"]["ratio_wamr"], 4.0)
        want("reuse: and over wasmtime's", reused["a"]["ratio"], 1.0)

    first = gobench.first_line
    gobench.first_line = lambda cmd: "/some/where/bin/wasmedge version 0.18.1"
    want("version: without its path", gobench.engine_versions(["wasmedge"]), {"wasmedge": "wasmedge version 0.18.1"})
    gobench.first_line = lambda cmd: None
    want("version: of an engine that says none", gobench.engine_versions(["wamr"]), {"wamr": None})
    gobench.first_line = first

    for f in fails:
        print("FAIL[benchrec]: " + f)
    print("benchrec: the other engines' figures in the baseline: %s"
          % ("as recorded" if not fails else "%d failure(s)" % len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
