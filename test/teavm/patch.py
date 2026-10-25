"""TeaVM with test/teavm/patches applied: fixes to the release the Makefile
pins, for bugs of its own that the Java corpora would otherwise have to
step around.

  python3 test/teavm/patch.py TEAVM_DIR

TEAVM_DIR is where `make teavm` put TeaVM's jars, and its sources jars in
TEAVM_DIR/sources.  Each patch is a unified diff whose paths start with the
artifact the file is from (a/teavm-core/org/teavm/..., a/teavm-classlib/...),
read from that artifact's sources jar.  The patched files are compiled
against the jars into TEAVM_DIR/patched, which goes ahead of the jars on
both class paths: the JVM's, for the compiler's own classes, and the one
TeaVM reads a program's class library and runtime from.

Applied strictly: a hunk applies where it says it does, with every line of
its context as written, or the patch is refused, so a patch made for one
release cannot land somewhere else in another.  Patches apply in the order
of their names, and one that touches a file another already did sees that
file as the earlier one left it.
"""
import glob
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PATCHES = os.path.join(HERE, "patches")
# The language level of TeaVM's own classes (class file version 61).
RELEASE = "17"

HUNK = re.compile(r"@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")


class PatchError(Exception):
    pass


def parse(text, name):
    """[(path, [(old_start, old_lines, new_lines)])] of a unified diff.  Any
    text before the first file is the patch's description."""
    files = []
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    i = 0
    while i < len(lines):
        if not lines[i].startswith("--- "):
            # git's headers of the file that follows
            if files and not lines[i].startswith(("diff ", "index ")):
                raise PatchError("%s:%d: unexpected %r" % (name, i + 1, lines[i][:60]))
            i += 1
            continue
        if i + 1 >= len(lines) or not lines[i + 1].startswith("+++ "):
            raise PatchError("%s:%d: --- without +++" % (name, i + 1))
        old, new = lines[i][4:].split("\t")[0], lines[i + 1][4:].split("\t")[0]
        if not old.startswith("a/") or not new.startswith("b/") or old[2:] != new[2:]:
            raise PatchError("%s:%d: a patch changes a file, as a/PATH and b/PATH" % (name, i + 1))
        hunks = []
        i += 2
        while i < len(lines) and lines[i].startswith("@@"):
            m = HUNK.match(lines[i])
            if not m:
                raise PatchError("%s:%d: bad hunk header" % (name, i + 1))
            old_n = int(m.group(2)) if m.group(2) is not None else 1
            new_n = int(m.group(4)) if m.group(4) is not None else 1
            i += 1
            a, b = [], []
            while len(a) < old_n or len(b) < new_n:
                if i >= len(lines):
                    raise PatchError("%s: hunk at line %s ends early" % (name, m.group(1)))
                ln = lines[i]
                tag, body = ln[:1], ln[1:]
                if tag == " " or ln == "":
                    a.append(body)
                    b.append(body)
                elif tag == "-":
                    a.append(body)
                elif tag == "+":
                    b.append(body)
                elif tag == "\\":
                    pass
                else:
                    raise PatchError("%s:%d: bad hunk line %r" % (name, i + 1, ln[:60]))
                i += 1
            while i < len(lines) and lines[i].startswith("\\"):
                i += 1
            if len(a) != old_n or len(b) != new_n:
                raise PatchError("%s: hunk at line %s does not count right" % (name, m.group(1)))
            hunks.append((int(m.group(1)), a, b))
        if not hunks:
            raise PatchError("%s: %s has no hunks" % (name, old[2:]))
        files.append((old[2:], hunks))
    if not files:
        raise PatchError("%s: no files" % name)
    return files


def apply(lines, hunks, where):
    """lines with every hunk applied exactly where it says."""
    out, at = [], 0
    for start, a, b in hunks:
        pos = start - 1 if a else start
        if pos < at or lines[pos:pos + len(a)] != a:
            raise PatchError("%s: hunk at line %d does not match" % (where, start))
        out += lines[at:pos] + b
        at = pos + len(a)
    return out + lines[at:]


def source(teavm, version, path):
    """The text of one of TeaVM's source files, from its artifact's sources jar."""
    import zipfile
    artifact, _, inner = path.partition("/")
    jar = os.path.join(teavm, "sources", "%s-%s-sources.jar" % (artifact, version))
    if not artifact.startswith("teavm-") or not os.path.exists(jar):
        raise PatchError("%s: no sources jar for %s (make teavm)" % (path, artifact))
    with zipfile.ZipFile(jar) as z:
        try:
            return z.read(inner).decode("utf-8")
        except KeyError:
            raise PatchError("%s: not in %s" % (path, os.path.basename(jar)))


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch.py TEAVM_DIR")
    teavm = sys.argv[1]
    version = os.path.basename(os.path.normpath(teavm)).rpartition("teavm-")[2]
    stamp = os.path.join(teavm, "patched", ".complete")
    if os.path.exists(stamp):
        os.remove(stamp)
    files = {}
    try:
        for p in sorted(glob.glob(os.path.join(PATCHES, "*.patch"))):
            name = os.path.basename(p)
            with open(p, encoding="utf-8") as f:
                for path, hunks in parse(f.read(), name):
                    if path not in files:
                        files[path] = source(teavm, version, path).replace("\r\n", "\n").split("\n")
                    files[path] = apply(files[path], hunks, "%s: %s" % (name, path))
    except PatchError as e:
        raise SystemExit("teavm patch: %s" % e)
    src = os.path.join(teavm, "patched-src")
    out = os.path.join(teavm, "patched")
    for d in (src, out):
        shutil.rmtree(d, ignore_errors=True)
    written = []
    for path, lines in sorted(files.items()):
        dst = os.path.join(src, *path.split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines))
        written.append(dst)
    os.makedirs(out)
    if written:
        r = subprocess.run(["javac", "--release", RELEASE, "-nowarn", "-encoding", "UTF-8",
                            "-proc:none", "-implicit:none", "-cp", os.path.join(teavm, "*"),
                            "-d", out] + written, capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit("teavm patch: javac failed:\n" + r.stdout + r.stderr)
    with open(stamp, "w"):
        pass
    print("teavm: %d file(s) patched from %d patch(es)"
          % (len(files), len(glob.glob(os.path.join(PATCHES, "*.patch")))))


if __name__ == "__main__":
    main()
