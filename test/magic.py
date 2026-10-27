"""Check the magic numbers warmc divides by, against exact arithmetic.

`pass_fold` replaces `n / d` for a constant `d` with the top half of `n * M`
shifted right, which is only correct if M and the shift were chosen right.  A
wrong choice is wrong for a *handful* of dividends out of four billion -- always
at the top of a quotient bucket -- so it is not something a differential test on
a few values finds.  test/cases/magicdiv.wat covers the *lowering*; this covers
the *choice*.

`warmc --magic=D[:64]` prints what the compiler picked, so what is checked here is
the compiler's own answer and not a second implementation of the same algorithm
agreeing with itself.

Two instruments, and the cheap one is validated by the expensive one:

  * **The Granlund-Montgomery bound.**  With `Mfull = M + 2^W` when the `add`
    fixup is used and `M` otherwise, the sequence computes
    `floor(n * Mfull / 2^(W+s))`, and that equals `floor(n / d)` for every
    `n < 2^W` exactly when

        2^(W+s)  <=  Mfull * d  <=  2^(W+s) + 2^s

    which is one line of exact integer arithmetic per divisor, so it can be
    asked of every divisor.

  * **Exhaustive enumeration of the critical dividends.**  Both `floor(n/d)` and
    the computed `q(n)` are non-decreasing, and `floor(n/d)` is constant on each
    bucket `[k*d, (k+1)*d)`, so if the two agree at `k*d` and at `k*d - 1` for
    every k then they agree everywhere.  That is `2^W / d` points, which is cheap
    only for large `d` -- and where it is cheap it is run, and its verdict is
    compared against the bound's.  If the two ever disagreed the bound would be
    the thing in doubt.

Usage: python3 test/magic.py
"""

import subprocess
import sys
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.environ.get("BUILD", "build")
WARMC = os.path.join(ROOT, BUILD, "warmc") + (".exe" if os.name == "nt" else "")

# The limits below are generous for a workstation; a machine slow enough to
# have been given a longer WARMC_TIMEOUT -- CI's runners -- gets that instead.
def limit(seconds):
    return max(seconds, float(os.environ.get("WARMC_TIMEOUT", "0")))

def magics(ds, wide):
    """What warmc chooses for each of these divisors: one process for all
    of them (--magic= takes a list), where a process each was thousands"""
    arg = "--magic=" + ",".join("%d%s" % (d, ":64" if wide else "") for d in ds)
    r = subprocess.run([WARMC, arg], capture_output=True, text=True)
    lines = r.stdout.split("\n")
    if r.returncode != 0 or len([l for l in lines if l]) != len(ds):
        raise SystemExit("warmc --magic failed: %s" % r.stderr.strip()[:200])
    return {d: tuple(int(x) for x in l.split()) for d, l in zip(ds, lines)}

def bound_holds(d, m, add, sh, W):
    """The Granlund-Montgomery condition, in exact integers."""
    full = m + (1 << W) if add else m
    lo = 1 << (W + sh)
    return lo <= full * d <= lo + (1 << sh)

def computed(n, m, add, sh, W):
    """What the emitted sequence computes, in exact integers.

    The `add` form is written the way the code emits it -- subtract, shift, add,
    shift by one less -- rather than as the equivalent wide multiply, so that a
    mistake in *that* rewriting is caught here too.
    """
    mask = (1 << W) - 1
    q = (n * m) >> W
    if add:
        q = (((n - q) & mask) >> 1) + q
        q &= mask
        return q >> (sh - 1) if sh >= 1 else q
    return q >> sh if sh > 0 else q

def enumerate_critical(d, m, add, sh, W):
    """Every dividend where the two step, when there are few enough of them."""
    top = 1 << W
    for k in range(1, top // d + 2):
        for n in (k * d - 1, k * d):
            if n < 0 or n >= top:
                continue
            if computed(n, m, add, sh, W) != n // d:
                return n
    for n in (0, 1, top - 1):
        if computed(n, m, add, sh, W) != n // d:
            return n
    return None

def divisors(W):
    """Divisors worth asking about: everything small, the awkward shapes, and
    enough large ones that the exhaustive arm has something to chew on."""
    ds = set(range(3, 4096))
    for k in range(2, W):
        for off in (-1, 1, 3):
            v = (1 << k) + off
            if 3 <= v < (1 << W):
                ds.add(v)
    ds |= {641, 6700417, 999983, 1000003, 0x7fffffff, 0x80000001, 0xfffffffb,
           0xffffffff}
    if W == 64:
        ds |= {(1 << 40) - 3, (1 << 52) + 7, (1 << 63) - 1, (1 << 63) + 1,
               (1 << 64) - 3, 0xffffffffffffffff}

    return sorted(d for d in ds
                  if d > 2 and (d & (d - 1)) != 0 and d < (1 << (W - 1)))

def main():
    if not os.path.exists(WARMC):
        print("magic: %s not built" % WARMC, file=sys.stderr)
        return 1
    bad = 0
    checked = enumerated = 0

    # A divisor of zero has no reciprocal and the guard that says so used to
    # run *after* the reciprocal was computed, so `--magic=0` evaluated `% 0`
    # on the way to a number it was going to throw away.  Here that is a
    # crash on any host whose divide traps; the answer is what this asks for.
    for w in ("", ":64"):
        r = subprocess.run([WARMC, "--magic=0" + w],
                           capture_output=True, text=True, timeout=limit(30))
        if r.returncode != 0:
            print("FAIL[magic]: --magic=0%s exited %d: %s"
                  % (w, r.returncode, r.stderr.strip()[:120]))
            bad += 1
    for W in (32, 64):

        cheap = (1 << W) // 20000
        ds = divisors(W)
        chosen = magics(ds, W == 64)
        for d in ds:
            m, add, sh = chosen[d]
            checked += 1
            ok = bound_holds(d, m, add, sh, W)
            if not ok:
                print("FAIL[magic]: d=%d W=%d M=%d add=%d s=%d: bound violated"
                      % (d, W, m, add, sh))
                bad += 1
                continue

            if m >= (1 << W):
                print("FAIL[magic]: d=%d W=%d M=%d does not fit %d bits"
                      % (d, W, m, W))
                bad += 1
            if d >= cheap:
                n = enumerate_critical(d, m, add, sh, W)
                enumerated += 1
                if n is not None:
                    print("FAIL[magic]: d=%d W=%d M=%d add=%d s=%d: n=%d gives "
                          "%d, want %d" % (d, W, m, add, sh, n,
                                           computed(n, m, add, sh, W), n // d))
                    bad += 1
    if bad:
        return 1
    print("magic: %d divisors satisfy the bound, %d of them also checked "
          "exhaustively at every critical dividend" % (checked, enumerated))
    return 0

if __name__ == "__main__":
    sys.exit(main())
