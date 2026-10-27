"""Results the test suites keep, each under exactly what it depends on.

A key is a digest of what goes into a result: strings (a tool's version, a
flag, an export's name) and files' contents (warmc, a runtime archive, a
module).  Three kinds of result live under build/testcache:

  * a reference's answers -- what wasmtime or wasm-interp says a module
    does -- under the tool's version and the module's bytes, which changes
    far less often than warmc does;
  * a run's output, under the program's own bytes, its arguments and every
    WARMC_ setting: a change to warmc that leaves a program as it was costs
    none of its runs.

(Compiled programs are warmc's own WARMC_CACHE's.)  What the cache holds
past CACHE_MB, the least recently used first, goes when run_all.sh trims it
before the rows.  WARMC_NO_TESTCACHE turns it off, for a run that must do
everything.
"""
import hashlib
import json
import os
import shutil
import threading

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIR = os.path.join(ROOT, os.environ.get("BUILD", "build"), "testcache")
CACHE_MB = int(os.environ.get("WARMC_TESTCACHE_MB", "2000"))
OFF = bool(os.environ.get("WARMC_NO_TESTCACHE"))
# what reaches no program and no compile: a limit, how to run a program
IGNORED_ENV = {"WARMC_TIMEOUT", "WARMC_RUN_TIMEOUT", "WARMC_NO_TESTCACHE", "WARMC_TESTCACHE_MB",
               "WARMC_CACHE"}

_lock = threading.Lock()
_digests = {}


class File(str):
    """a path whose contents, not its name, go into a key"""


def file_digest(path):
    st = os.stat(path)
    memo = (path, st.st_size, st.st_mtime_ns)
    with _lock:
        if memo in _digests:
            return _digests[memo]
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    d = h.hexdigest()
    with _lock:
        _digests[memo] = d
    return d


def warmc_env():
    """every WARMC_ setting, which may reach a compile or a run"""
    return sorted((k, v) for k, v in os.environ.items()
                  if k.startswith("WARMC_") and k not in IGNORED_ENV)


def key(*parts):
    h = hashlib.sha256()
    for p in parts:
        if isinstance(p, File):
            h.update(b"F" + file_digest(p).encode())
        elif isinstance(p, (list, tuple)):
            h.update(b"L" + key(*p).encode())
        else:
            h.update(b"S" + str(p).encode() + b"\0")
    return h.hexdigest()


def _path(kind, k):
    return os.path.join(DIR, kind, k[:2], k)


def get(kind, k):
    """the JSON value kept under k, or None"""
    if OFF:
        return None
    p = _path(kind, k)
    try:
        with open(p) as f:
            v = json.load(f)
        os.utime(p)
        return v
    except (OSError, ValueError):
        return None


def put(kind, k, value):
    if OFF:
        return
    p = _path(kind, k)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    tmp = "%s.%d.%d" % (p, os.getpid(), threading.get_ident())
    with open(tmp, "w") as f:
        json.dump(value, f)
    os.replace(tmp, p)


def run(exe, argv, runner):
    """runner() -> (rc, stdout, stderr) of exe given argv, kept under the
    program's bytes, the arguments and the WARMC_ settings; an rc of None,
    a run that did not end in time, is not kept"""
    k = key(File(exe), list(argv), warmc_env())
    v = get("run", k)
    if v is not None:
        return tuple(v)
    v = runner()
    if v[0] is not None:
        put("run", k, list(v))
    return v


def trim():
    """past CACHE_MB, the least recently used first: run_all.sh, before the
    rows -- never while one may be reading what it would take away"""
    files = []
    for dp, _dn, fns in os.walk(DIR):
        for fn in fns:
            p = os.path.join(dp, fn)
            try:
                st = os.stat(p)
            except OSError:
                continue
            files.append((st.st_mtime, st.st_size, p))
    total = sum(f[1] for f in files)
    for _t, size, p in sorted(files):
        if total <= CACHE_MB << 20:
            break
        try:
            os.remove(p)
            total -= size
        except OSError:
            pass


def clear():
    shutil.rmtree(DIR, ignore_errors=True)


def main(argv):
    """For the bash suites:
      testcache.py key [--file F | --str S | --env PREFIX]...   print a key
      testcache.py get KIND KEY      print what is kept; exit 1 where nothing
      testcache.py put KIND KEY      keep stdin
      testcache.py trim              down to WARMC_TESTCACHE_MB
    --env adds every setting whose name starts with PREFIX."""
    import sys
    if argv[:1] == ["key"]:
        parts, i = [], 1
        while i + 1 < len(argv):
            o, v = argv[i], argv[i + 1]
            if o == "--file":
                parts.append(File(v) if os.path.exists(v) else "missing " + v)
            elif o == "--str":
                parts.append(v)
            elif o == "--env":
                parts.append(sorted((k, x) for k, x in os.environ.items()
                                    if k.startswith(v) and k not in IGNORED_ENV))
            i += 2
        print(key(*parts))
        return 0
    if argv[:1] == ["get"] and len(argv) == 3:
        v = get(argv[1], argv[2])
        if v is None:
            return 1
        sys.stdout.write(v)
        return 0
    if argv[:1] == ["trim"]:
        trim()
        return 0
    if argv[:1] == ["put"] and len(argv) == 3:
        put(argv[1], argv[2], sys.stdin.read())
        return 0
    print(main.__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    import sys
    sys.exit(main(sys.argv[1:]))
