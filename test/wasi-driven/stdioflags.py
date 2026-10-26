"""Drives test/wasi-driven/stdioflags.wat, built by run_wasi.sh: its stdin
and stdout are pipes whose other ends, and whose own flags, are this
script's.  The module's header says what each run does.

Usage: python3 test/wasi-driven/stdioflags.py EXE
"""

import fcntl
import os
import select
import subprocess
import sys
import time

TIMEOUT = 10
fails = []


def nonblocking(fd):
    return bool(fcntl.fcntl(fd, fcntl.F_GETFL) & os.O_NONBLOCK)


def read_exactly(fd, n):
    got = b""
    deadline = time.time() + TIMEOUT
    while len(got) < n:
        left = deadline - time.time()
        if left <= 0 or not select.select([fd], [], [], left)[0]:
            break
        b = os.read(fd, n - len(got))
        if not b:
            break
        got += b
    return got


def read_to_end(fd):
    got = b""
    deadline = time.time() + TIMEOUT
    while True:
        left = deadline - time.time()
        if left <= 0 or not select.select([fd], [], [], left)[0]:
            return got
        b = os.read(fd, 1 << 16)
        if not b:
            return got
        got += b


def finish(p, what):
    try:
        rc = p.wait(TIMEOUT)
    except subprocess.TimeoutExpired:
        p.kill()
        p.wait()
        rc = "none, killed after %ds" % TIMEOUT
    if rc != 0:
        fails.append("%s: exit status %s (stdioflags.wat names the step)" % (what, rc))


def blocking_pipes(exe):
    in_r, in_w = os.pipe()
    out_r, out_w = os.pipe()
    p = subprocess.Popen([exe], stdin=in_r, stdout=out_w, stderr=subprocess.DEVNULL, env={})
    got = read_exactly(out_r, 2)
    if got != b"a\n":
        fails.append("non-blocking: got %r before stdin was written, want b'a\\n'" % got)
    for name, fd in (("stdin", in_r), ("stdout", out_w)):
        if nonblocking(fd):
            fails.append("non-blocking: the guest's flags made the %s pipe itself "
                         "non-blocking" % name)
    os.close(in_r)
    os.close(out_w)
    os.write(in_w, b"x\n")
    # nothing read for a while: the guest fills stdout up to EAGAIN
    time.sleep(0.5)
    got = read_to_end(out_r)
    filler = got[:-3].replace(b"f" * 512, b"")
    if not got.endswith(b"\nc\n") or filler or len(got) < 512 + 3:
        fails.append("non-blocking: stdout after 'a' was %d bytes ending %r, want "
                     "512 'f' at least once, then b'\\nc\\n'" % (len(got), got[-8:]))
    finish(p, "non-blocking")
    os.close(in_w)
    os.close(out_r)


def nonblocking_stdin(exe):
    in_r, in_w = os.pipe()
    out_r, out_w = os.pipe()
    fcntl.fcntl(in_r, fcntl.F_SETFL, fcntl.fcntl(in_r, fcntl.F_GETFL) | os.O_NONBLOCK)
    p = subprocess.Popen([exe, "blocking"], stdin=in_r, stdout=out_w,
                         stderr=subprocess.DEVNULL, env={})
    os.close(out_w)
    got = read_exactly(out_r, 2)
    if got != b"b\n":
        fails.append("blocking: got %r, want b'b\\n'" % got)
    time.sleep(0.3)
    os.write(in_w, b"y\n")
    finish(p, "blocking over a non-blocking pipe")
    if not nonblocking(in_r):
        fails.append("blocking: the stdin pipe lost the O_NONBLOCK it was given")
    for fd in (in_r, in_w, out_r):
        os.close(fd)


def append_file(exe, work):
    path = os.path.join(work, "stdioflags.append")
    with open(path, "wb") as f:
        f.write(b"0123456789")
    fd = os.open(path, os.O_WRONLY)
    p = subprocess.Popen([exe, "append", "x"], stdin=subprocess.DEVNULL, stdout=fd,
                         stderr=subprocess.DEVNULL, env={})
    finish(p, "append")
    if fcntl.fcntl(fd, fcntl.F_GETFL) & os.O_APPEND:
        fails.append("append: the guest's flags made the stdout file itself O_APPEND")
    os.close(fd)
    with open(path, "rb") as f:
        got = f.read()
    if got != b"0123456789x":
        fails.append("append: the file is %r, want b'0123456789x'" % got)


def main():
    exe = sys.argv[1]
    blocking_pipes(exe)
    nonblocking_stdin(exe)
    append_file(exe, os.path.dirname(exe))
    for f in fails:
        print("FAIL[wasi/stdioflags]: " + f)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
