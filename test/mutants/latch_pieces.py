"""A latch's moves made ahead of its branch write no register lent to a
value another loop holds in it (isel_ops.inc, the loop pieces of
regalloc.inc place_loop_pieces).  Killed by tinygo's FNV loop in a loop,
test/gowasi/binenc.go at -O1 under run_gowasip2.sh, whose multiplier's
register the outer loop's way round wrote before the inner latch's branch."""
M = {
 'pieces': ('src/isel_ops.inc', [('        if (ok && s->pc_bn && !warmc_getenv("WARMC_NO_LATCH_PIECES")) {',
                                   '        if (0 && ok && s->pc_bn && !warmc_getenv("WARMC_NO_LATCH_PIECES")) {')]),
 'loop_piece': ('src/isel_ops.inc', [('              if (p->kind != (w ? PC_FILL : PC_LOOP)) continue;',
                                       '              if (p->kind != PC_FILL) continue;')]),
}
EQUIVALENT = {}
SUITES = (('gowasip2', ['./test/run_gowasip2.sh'], {'GOWASIP2_LEVELS': '-O1'}),)
