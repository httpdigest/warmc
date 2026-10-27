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
after the rows.  WARMC_NO_TESTCACHE turns it off, for a run that must do
everything.
"""
import hashlib
import json
import os
import shutil
import stat
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


def tree(path):
    """an installed tool's directory by its files' names and sizes: it
    changes only by being installed anew, and its contents are hundreds of
    megabytes"""
    out = []
    for dp, dns, fns in os.walk(path):
        dns.sort()
        for fn in sorted(fns):
            p = os.path.join(dp, fn)
            try:
                out.append("%s %d" % (os.path.relpath(p, path), os.path.getsize(p)))
            except OSError:
                pass
    return "\n".join(out)


_versions = {}


def version(cmd):
    """what cmd prints of its version (java -version: on stderr), once a process"""
    k = tuple(cmd)
    if k not in _versions:
        import subprocess
        try:
            r = subprocess.run(cmd, capture_output=True, text=True)
            _versions[k] = r.stdout + r.stderr
        except OSError:
            _versions[k] = "missing"
    return _versions[k]


def put_file(kind, k, src):
    """a file, kept under k"""
    if OFF:
        return
    p = _path(kind, k)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    tmp = "%s.%d.%d" % (p, os.getpid(), threading.get_ident())
    shutil.copyfile(src, tmp)
    os.replace(tmp, p)


def put_bytes(kind, k, data):
    if OFF:
        return
    p = _path(kind, k)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    tmp = "%s.%d.%d" % (p, os.getpid(), threading.get_ident())
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, p)


def get_bytes(kind, k):
    """the bytes kept under k, or None"""
    if OFF:
        return None
    p = _path(kind, k)
    try:
        with open(p, "rb") as f:
            data = f.read()
        os.utime(p)
        return data
    except OSError:
        return None


def get_file(kind, k, dest):
    """the file kept under k, copied to dest: whether there was one"""
    if OFF:
        return False
    p = _path(kind, k)
    tmp = "%s.%d.%d" % (dest, os.getpid(), threading.get_ident())
    try:
        with open(p, "rb") as f:
            # open, it stays whole while a trim takes the name away
            with open(tmp, "wb") as out:
                shutil.copyfileobj(f, out, 1 << 20)
    except OSError:
        if os.path.exists(tmp):
            os.remove(tmp)
        return False
    os.replace(tmp, dest)
    try:
        os.utime(p)
    except OSError:
        pass
    return True


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


def _weigh(p):
    """an item's bytes: a file's, or a directory's files'"""
    try:
        st = os.lstat(p)
        if not stat.S_ISDIR(st.st_mode):
            return st.st_size
        return sum(os.lstat(os.path.join(p, f)).st_size for f in os.listdir(p))
    except OSError:
        return 0


def trim(root=DIR, cap=CACHE_MB << 20, depth=3):
    """root down to 90% of cap bytes where it holds more, the least recently
    used items first.  An item is what lies depth levels down (here a
    result, kind/xx/KEY; in warmc's cache a compile's directory, xx/KEY):
    made once, put in place whole by a rename, never changed after -- a use
    touches its time alone.  So what each holds is kept in root/.sizes by
    the directory it is in, and a directory is listed anew only where its
    own time says an item came or went: hundreds of thousands of items are
    weighed in a fraction of a second, and only a cache past its cap has
    each item's time read.  run_all.sh trims after the rows, on its own
    once make check has ended; a later run that reads an item meanwhile
    holds it open first, warmc's compiles as get_file here, so it reads
    the whole of it or finds none."""
    import pickle
    ledger = os.path.join(root, ".sizes")
    try:
        with open(ledger, "rb") as f:
            old = pickle.load(f)
    except Exception:
        old = {}

    def dirs(rel, level):
        if level == 0:
            yield rel
            return
        try:
            names = sorted(os.listdir(os.path.join(root, rel)))
        except OSError:
            return
        for n in names:
            r = os.path.join(rel, n) if rel else n
            if not n.startswith(".") and os.path.isdir(os.path.join(root, r)):
                yield from dirs(r, level - 1)

    new, changed = {}, False
    for d in dirs("", depth - 1):
        try:
            t = os.stat(os.path.join(root, d)).st_mtime_ns
        except OSError:
            continue
        prev = old.get(d)
        if prev and prev[0] == t:
            new[d] = prev
            continue
        known = prev[1] if prev else {}
        new[d] = (t, {n: known[n] if n in known else _weigh(os.path.join(root, d, n))
                      for n in os.listdir(os.path.join(root, d))})
        changed = True
    changed = changed or len(new) != len(old)
    total = sum(sum(sizes.values()) for _t, sizes in new.values())
    if total > cap:
        items = []
        for d, (_t, sizes) in new.items():
            for n, size in sizes.items():
                try:
                    items.append((os.lstat(os.path.join(root, d, n)).st_mtime_ns, size, d, n))
                except OSError:
                    pass
        items.sort()
        for _t, size, d, n in items:
            if total <= cap // 10 * 9:
                break
            p = os.path.join(root, d, n)
            if os.path.isdir(p) and not os.path.islink(p):
                shutil.rmtree(p, ignore_errors=True)
            else:
                try:
                    os.remove(p)
                except OSError:
                    pass
            total -= size
            del new[d][1][n]
            # its directory's time changed: listed anew next time
            new[d] = (None, new[d][1])
        changed = True
    if changed:
        tmp = "%s.%d" % (ledger, os.getpid())
        with open(tmp, "wb") as f:
            pickle.dump(new, f, pickle.HIGHEST_PROTOCOL)
        os.replace(tmp, ledger)


def clear():
    shutil.rmtree(DIR, ignore_errors=True)


def main(argv):
    """For the bash suites:
      testcache.py key [--file F | --str S | --env PREFIX]...   print a key
      testcache.py get KIND KEY      print what is kept; exit 1 where nothing
      testcache.py put KIND KEY      keep stdin
      testcache.py trim              down to WARMC_TESTCACHE_MB
      testcache.py trim DIR MB DEPTH another such cache: warmc's, depth 2
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
    if argv[:1] == ["trim"] and len(argv) in (1, 4):
        if len(argv) == 1:
            trim()
        else:
            trim(argv[1], int(argv[2]) << 20, int(argv[3]))
        return 0
    if argv[:1] == ["put"] and len(argv) == 3:
        put(argv[1], argv[2], sys.stdin.read())
        return 0
    print(main.__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    import sys
    sys.exit(main(sys.argv[1:]))
