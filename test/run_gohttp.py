"""An HTTP server in Go's net/http under warmc's WASI host (test/gohttp).

Built GOOS=wasip1 by Go's own compiler, compiled by warmc at -O0, -O1 and
-O2, and run with --tcplisten before --dir on the command line, so that the
listener comes after the directory, as the host numbers it.  The server's
stdin, stdout and stderr are pipes this script holds the other ends of; at
each level, through http.client:

  * a request and its answer, many on one connection that stays the one
    connection, a header, a file from the preopened directory;
  * a 3 MB body echoed back, and 20 MB sent to a reader that waits first,
    while another connection is answered;
  * 40 connections that each wait half a second, at once: past the 32
    subscriptions poll_oneoff makes room for on the stack;
  * clients that go away: before a request, halfway through one, and
    halfway through an answer of 50 MB;
  * 2 MB to stdout that nobody reads yet, while another connection is
    answered: Go waits for stdout in poll_oneoff, as for a socket;
  * all of it with stdin open and nothing on it, a goroutine waiting there
    (a stdin that blocked would stop the whole program), and the three
    pipes' own flags not made non-blocking -- they are this script's as
    much as the server's, as a terminal is the shell's;
  * "quit" on stdin, after which the server exits 0 and says "bye".

Left out without go 1.22 or later on PATH (net.FileListener for wasip1),
and on Windows, whose WASI host has no sockets.

Usage: python3 test/run_gohttp.py
       python3 test/run_gohttp.py --exe EXE...   (servers built already, for
                                                 test/run_wasi_linux.sh)
"""

import fcntl
import hashlib
import http.client
import os
import re
import socket
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.environ.get("BUILD", "build")
WARMC = os.path.join(ROOT, BUILD, "warmc")
WORK = os.path.join(ROOT, BUILD, "gohttprun")
LEVELS = os.environ.get("GOHTTP_LEVELS", "-O0 -O1 -O2").split()
TIMEOUT = 20

fails = []


def fail(level, msg):
    fails.append(msg)
    print("FAIL[gohttp%s] %s" % (level, msg))


def host_flags():
    r = subprocess.run('. test/hostenv.sh; echo "$WARMC_TARGET_FLAGS"', shell=True,
                       capture_output=True, text=True, cwd=ROOT,
                       env=dict(os.environ, BUILD=BUILD), executable="/bin/bash")
    return r.stdout.split()


def go_version():
    try:
        r = subprocess.run(["go", "env", "GOVERSION"], capture_output=True, text=True)
    except OSError:
        return None
    m = re.match(r"go(\d+)\.(\d+)", r.stdout.strip())
    return (int(m.group(1)), int(m.group(2))) if m else None


def pattern(n):
    """What /big?n=N sends: byte i is i % 251."""
    one = bytes(i % 251 for i in range(251))
    return (one * (n // 251 + 1))[:n]


def get(port, path, headers=None, body=None, method=None):
    c = http.client.HTTPConnection("127.0.0.1", port, timeout=TIMEOUT)
    try:
        c.request(method or ("POST" if body is not None else "GET"), path,
                  body=body, headers=headers or {})
        r = c.getresponse()
        return r.status, r.read()
    finally:
        c.close()


def in_thread(fn):
    """fn run on a thread of its own; join() gives what it returned or raised."""
    out = {}

    def run():
        try:
            out["v"] = fn()
        except Exception as e:  # noqa: BLE001 -- reported by the check that wanted it
            out["e"] = e
    t = threading.Thread(target=run, daemon=True)
    t.start()

    def join(timeout=TIMEOUT):
        t.join(timeout)
        if t.is_alive():
            raise TimeoutError("no answer in %ss" % timeout)
        if "e" in out:
            raise out["e"]
        return out["v"]
    return join


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def start(exe, ddir):
    """The server, listening, and the three pipes' ends: ours and its own."""
    for _ in range(5):
        port = free_port()
        in_r, in_w = os.pipe()
        out_r, out_w = os.pipe()
        err_r, err_w = os.pipe()
        p = subprocess.Popen([exe, "--tcplisten=127.0.0.1:%d" % port,
                              "--dir=%s::/d" % ddir],
                             stdin=in_r, stdout=out_w, stderr=err_w, env={})
        deadline = time.time() + 15
        while time.time() < deadline and p.poll() is None:
            try:
                socket.create_connection(("127.0.0.1", port), timeout=1).close()
                return p, port, (in_r, in_w), (out_r, out_w), (err_r, err_w)
            except OSError:
                time.sleep(0.05)
        p.kill()
        p.wait()
        for fd in (in_r, in_w, out_r, out_w, err_r, err_w):
            os.close(fd)
    return None


def drain(fd):
    """Everything read from fd until its end, on a thread of its own."""
    def read():
        chunks = []
        while True:
            b = os.read(fd, 1 << 16)
            if not b:
                return b"".join(chunks)
            chunks.append(b)
    return in_thread(read)


def check(level, exe, ddir):
    started = start(exe, ddir)
    if not started:
        fail(level, "the server never came up on a port")
        return
    p, port, (in_r, in_w), (out_r, out_w), (err_r, err_w) = started
    err_text = drain(err_r)
    out_text = None

    def expect(what, got, want):
        if got != want:
            g, w = repr(got), repr(want)
            fail(level, "%s: got %s, want %s" % (what, g[:120], w[:120]))
            return False
        return True

    # what the server was being asked when an exception came out of it
    at = ["GET /hello"]
    try:
        expect("GET /hello", get(port, "/hello"), (200, b"hello, world\n"))

        # the pipes are the server's, and ours: their flags stay as they were
        for name, fd in (("stdin", in_r), ("stdout", out_w), ("stderr", err_w)):
            if fcntl.fcntl(fd, fcntl.F_GETFL) & os.O_NONBLOCK:
                fail(level, "the server made the %s pipe non-blocking for everyone "
                            "who has it" % name)
        os.close(in_r)
        os.close(err_w)

        at[0] = "requests on one connection"
        c = http.client.HTTPConnection("127.0.0.1", port, timeout=TIMEOUT)
        c.request("GET", "/conns")
        before = c.getresponse().read()
        for i in range(40):
            c.request("GET", "/header", headers={"X-Probe": "probe-%d" % i})
            r = c.getresponse()
            if not expect("X-Probe on a kept connection", (r.status, r.read()),
                          (200, b"probe-%d\n" % i)):
                break
        c.request("GET", "/conns")
        expect("connections after 40 requests on one", c.getresponse().read(), before)
        c.close()

        at[0] = "GET /file"
        with open(os.path.join(ddir, "seed.txt"), "rb") as f:
            expect("GET /file", get(port, "/file"), (200, f.read()))

        at[0] = "POST /echo"
        body = os.urandom(3 << 20)
        st, got = get(port, "/echo", body=body)
        if st != 200 or got != body:
            fail(level, "POST /echo of 3 MB: status %d, %d bytes back, %s" %
                 (st, len(got), "the same" if got == body else "not the same"))

        # 20 MB to a reader that waits: the server's writes fill the socket
        # and wait in poll_oneoff, and another connection is answered
        at[0] = "GET /big"
        n = 20 << 20
        s = socket.create_connection(("127.0.0.1", port), timeout=TIMEOUT)
        s.sendall(b"GET /big?n=%d HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n" % n)
        time.sleep(0.5)
        at[0] = "GET /hello while an answer waits for its reader"
        t0 = time.time()
        expect("GET /hello while an answer waits for its reader", get(port, "/hello"),
               (200, b"hello, world\n"))
        took = time.time() - t0
        if took > 5:
            fail(level, "GET /hello took %.1fs while an answer waited for its reader" % took)
        at[0] = "reading GET /big"
        got = b""
        while True:
            b = s.recv(1 << 20)
            if not b:
                break
            got += b
        s.close()
        head, _, data = got.partition(b"\r\n\r\n")
        if not head.startswith(b"HTTP/1.1 200") or \
           hashlib.sha256(data).digest() != hashlib.sha256(pattern(n)).digest():
            fail(level, "GET /big of 20 MB: [%s], %d bytes, want %d of the pattern" %
                 (head.split(b"\r\n")[0].decode(errors="replace"), len(data), n))

        t0 = time.time()
        joins = [in_thread(lambda: get(port, "/slow?ms=500")) for _ in range(40)]
        for j in joins:
            try:
                expect("GET /slow", j(), (200, b"slept\n"))
            except Exception as e:  # noqa: BLE001
                fail(level, "GET /slow: %s" % e)
        took = time.time() - t0
        if took > 5:
            fail(level, "40 connections that each wait 0.5s took %.1fs: "
                        "they were not served together" % took)

        at[0] = "clients that go away"
        socket.create_connection(("127.0.0.1", port), timeout=TIMEOUT).close()
        s = socket.create_connection(("127.0.0.1", port), timeout=TIMEOUT)
        s.sendall(b"GET /hello HTTP/1.1\r\nHo")
        s.close()
        s = socket.create_connection(("127.0.0.1", port), timeout=TIMEOUT)
        s.sendall(b"GET /big?n=%d HTTP/1.1\r\nHost: x\r\n\r\n" % (50 << 20))
        s.recv(1024)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, b"\1\0\0\0\0\0\0\0")
        s.close()
        at[0] = "GET /hello after clients went away"
        expect("GET /hello after clients went away", get(port, "/hello"),
               (200, b"hello, world\n"))

        # 2 MB to stdout, which nobody reads yet: that goroutine waits, and
        # another connection is answered
        at[0] = "GET /hello while stdout waits for its reader"
        printing = in_thread(lambda: get(port, "/print?n=20000"))
        time.sleep(0.5)
        t0 = time.time()
        expect("GET /hello while stdout waits for its reader", get(port, "/hello"),
               (200, b"hello, world\n"))
        took = time.time() - t0
        if took > 5:
            fail(level, "GET /hello took %.1fs while stdout waited for its reader" % took)
        os.close(out_w)
        out_text = drain(out_r)
        try:
            expect("GET /print", printing(), (200, b"printed\n"))
        except Exception as e:  # noqa: BLE001
            fail(level, "GET /print: %s" % e)

        at[0] = "quit"
        os.write(in_w, b"quit\n")
        try:
            rc = p.wait(TIMEOUT)
        except subprocess.TimeoutExpired:
            fail(level, "the server did not exit after quit")
            rc = None
        if rc is not None:
            expect("exit status", rc, 0)
        line = b"x" * 99 + b"\n"
        expect("stdout", out_text(), line * 20000 + b"bye\n")
        expect("stderr", err_text(), b"gohttp: serving\n")
    except Exception as e:  # noqa: BLE001 -- the server is still to be stopped
        fail(level, "%s: %s: %s" % (at[0], type(e).__name__, e))
    finally:
        if p.poll() is None:
            p.kill()
            p.wait()
        for fd in (in_r, in_w, out_w, err_w):
            try:
                os.close(fd)
            except OSError:
                pass


def seed(ddir):
    os.makedirs(ddir, exist_ok=True)
    with open(os.path.join(ddir, "seed.txt"), "w") as f:
        f.write("seed\n")


def main():
    if sys.argv[1:2] == ["--exe"]:
        for exe in sys.argv[2:]:
            ddir = os.path.join(os.path.dirname(os.path.abspath(exe)), "gohttp.d")
            seed(ddir)
            check(" " + os.path.basename(exe), exe, ddir)
        return 1 if fails else 0
    flags = host_flags()
    if "--target=windows" in flags:
        print("gohttp: SKIPPED (the Windows WASI host has no sockets)")
        return 0
    v = go_version()
    if not v or v < (1, 22):
        print("gohttp: SKIPPED (no go 1.22 or later on PATH)")
        return 0
    os.makedirs(WORK, exist_ok=True)
    wasm = os.path.join(WORK, "gohttp.wasm")
    r = subprocess.run(["go", "build", "-o", wasm, os.path.join(ROOT, "test", "gohttp", "main.go")],
                       capture_output=True, text=True, cwd=ROOT,
                       env=dict(os.environ, GOOS="wasip1", GOARCH="wasm"))
    if r.returncode:
        print("FAIL[gohttp] go build: " + r.stderr.strip()[:2000])
        return 1
    ddir = os.path.join(WORK, "d")
    seed(ddir)
    for level in LEVELS:
        exe = os.path.join(WORK, "gohttp" + level)
        r = subprocess.run([WARMC, "--wasi"] + flags + [level, "-o", exe, wasm],
                           capture_output=True, text=True)
        if r.returncode:
            fail(level, "warmc: " + r.stderr.strip()[:2000])
            continue
        check(level, exe, ddir)
    if fails:
        return 1
    print("gohttp: %d level(s) pass" % len(LEVELS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
