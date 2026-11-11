"""test/cases_opt/go_tables.wat: resume dispatches of 2 to 25 cases in
functions that store no return address, so that only 0 and 1, a call's way
in, can arrive and opt.c pass_go_resume sends every other case and the
default to a trap -- through a block it makes, in functions of 4 to 27
blocks.  One of those has exactly as many blocks as it has room for when
the pass runs (8, and 16), and the new block moves them all.

  python3 test/gen_go_tables.py test/cases_opt/go_tables.wat
"""
import sys

K = 24


def func(k):
    """f_k: a dispatch of 2 + k cases; 0 and 1 the entry, each other case c a
    block that stores 1000 (k + 1) + c at 128 and returns, the default
    unreachable.  No call, so no return address names a case."""
    n = 2 + k
    L = ['  (func $f%d (type $go) (local i32)' % k,
         '    global.get 0', '    local.set 1']
    L += ['    block'] * n           # the entry, each other case, the default
    L += ['    local.get 0',
          '    br_table %s' % ' '.join(['0', '0'] + [str(c - 1) for c in range(2, n)] + [str(n - 1)]),
          '    end',
          '    (i32.store (i32.const 136) (i32.add (i32.load (i32.const 136)) (i32.const %d)))' % (k + 1),
          '    i32.const 0', '    return']
    for c in range(2, n):
        L += ['    end',
              '    (i32.store (i32.const 128) (i32.const %d))' % (1000 * (k + 1) + c),
              '    i32.const 0', '    return']
    L += ['    end', '    unreachable)']
    return '\n'.join(L) + '\n'


HEAD = ''';; Go's resume dispatch pruned (opt.c pass_go_resume) in functions of every
;; number of blocks from 4 to 27: f0 to f23, fk with a br_table of 2 + k
;; cases on its first parameter -- 0 and 1 the entry, which adds k + 1 to the
;; word at 136; each other case c a block that stores 1000 (k + 1) + c at
;; 128; the default unreachable.  None stores a return address, so nothing
;; resumes it and only 0 and 1 can arrive: every other case traps, and the
;; pass makes the block they go to.  Making it moves the function's blocks
;; when the function has as many as it has room for -- f4's 8 and f12's 16
;; -- and the dispatch is then the one in the blocks where they are now:
;; taken where they were, it kept its own edges with the pruned table's
;; after them, every case as it had been and the two past its default the
;; entry's.
;;
;; at(k, i) enters fk at i as a resume would and answers 100000 times the
;; word at 136 plus the word at 128.  At 0 and 1, the entry; at 2, a case
;; that is pruned where the pass runs and the wasm's own block at -O0; at
;; k + 3, past the table: the default, a trap at every level.
;;
;; stats -O2: go_resume_pruned=%d
;;
'''


def main():
    runs = []
    for k in range(K):
        for i in (0, 1):
            runs.append(';; run: at %d %d => i32:%d' % (k, i, (k + 1) * 100000))
        if k:
            runs.append(';; run: at %d 2 => trap unreachable' % k)
            runs.append(';; run -O0: at %d 2 => i32:%d' % (k, 1000 * (k + 1) + 2))
        runs.append(';; run: at %d %d => trap unreachable' % (k, k + 3))
    mod = ['(module', '  (@custom "go:buildid" "warmc-test")',
           '  (type $go (func (param i32) (result i32)))',
           '  (type $ex (func (param i32 i32) (result i32)))',
           '  (global $sp (mut i32) (i32.const 4096))',
           '  (global $ctxt (mut i64) (i64.const 0))',
           '  (global $g (mut i64) (i64.const 64))',
           '  (memory 1)',
           '  (table %d funcref)' % K,
           '  (elem (i32.const 0) %s)' % ' '.join('$f%d' % k for k in range(K))]
    mod += [func(k) for k in range(K)]
    mod += ['  (func (export "at") (type $ex)',
            '    (i32.store (i32.const 128) (i32.const 0))',
            '    (i32.store (i32.const 136) (i32.const 0))',
            '    (drop (call_indirect (type $go) (local.get 1) (local.get 0)))',
            '    (i32.add (i32.mul (i32.load (i32.const 136)) (i32.const 100000))',
            '             (i32.load (i32.const 128))))', ')']
    # the cases pruned: every one but 0 and 1, and the default
    pruned = sum(k + 1 for k in range(K) if k)
    open(sys.argv[1], 'w').write(HEAD % pruned + '\n'.join(runs) + '\n' + '\n'.join(mod) + '\n')


if __name__ == '__main__':
    main()
