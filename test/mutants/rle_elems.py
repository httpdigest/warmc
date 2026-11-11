"""An array's element read again (opt.c pass_gc_rle's elements).  Killed by
test/cases_opt/rle_elems.wat's answers and its count of the reads taken.
Not mutated: the type an entry gives against the type a read wants -- one
root at one index and size has one element type."""
O = 'src/opt.c'
def m(old, new, path=O): return (path, [(old, new)])
GET = ('        Ref root = gc_ref_root(f, ir_args(f, v)[0]);\n'
       '        uint64_t off = RLE_ELEM | ir_resolve(f, ir_args(f, v)[1]);\n'
       '        uint32_t size = (uint32_t)(in->imm2 & (7u | GCF_SIGNED));\n')
M = {
 'off': m('  bool elems = !warmc_getenv("WARMC_NO_RLE_ELEM");', '  bool elems = false;'),
 # what ends an element
 'set_kill': m('        if (!(in->imm2 & GCF_INIT)) rle_kill_elems(&cur, rle_elem_kind(size, vt), false);',
               '        if (0) rle_kill_elems(&cur, rle_elem_kind(size, vt), false);'),
 'init_kill': m('        if (!(in->imm2 & GCF_INIT)) rle_kill_elems(&cur, rle_elem_kind(size, vt), false);',
                '        rle_kill_elems(&cur, rle_elem_kind(size, vt), false);'),
 'kind_ref': m('  return (size & 7u) | (vt_is_ref(ty) ? 8u : 0u);', '  return (size & 7u);'),
 'kind_size': m('  return (size & 7u) | (vt_is_ref(ty) ? 8u : 0u);', '  return (vt_is_ref(ty) ? 8u : 0u);'),
 'array_op': m('        rle_kill_elems(&cur, 0, true);', '        ;'),
 'kill_all': m('        (!all && rle_elem_kind(st->e[j].size, st->e[j].ty) != kind))',
               '        (rle_elem_kind(st->e[j].size, st->e[j].ty) != kind))'),
 'kill_fields': m('    if (!RLE_IS_ELEM(st->e[j].off) ||\n', '    if (0 ||\n'),
 # a loop's header
 'loop_mask': m('                  ? !(me >> rle_elem_kind(a->e[j].size, a->e[j].ty) & 1)',
                '                  ? true'),
 'loop_set': m('        wel[bi] |= (uint16_t)(1u << rle_elem_kind((uint32_t)in->imm2,',
               '        wel[bi] |= 0 * (uint16_t)(1u << rle_elem_kind((uint32_t)in->imm2,'),
 'loop_op': m('      else if (in->op == I_ARRAY_OP) wel[bi] = 0xffff;', '      else if (in->op == I_ARRAY_OP) wel[bi] = 0;'),
 'loop_me': m('        if (inloop[x]) { m |= wmask[x]; me |= wel[x]; killed',
              '        if (inloop[x]) { m |= wmask[x]; killed'),
 # what a write gives, and a read's key
 'packed_put': m('        if (vt_width(vt) == (1u << size))\n          rle_put(&cur, (RleEnt){ root, val, off, size, vt });',
                 '        rle_put(&cur, (RleEnt){ root, val, off, size, vt });'),
 'set_put': m('        if (vt_width(vt) == (1u << size))\n          rle_put(&cur, (RleEnt){ root, val, off, size, vt });',
              '        ;'),
 'get_put': m('        rle_put(&cur, (RleEnt){ root, v, off, size, (ValType)in->type });\n      } else if (elems && in->op == I_ARRAY_SET',
              '      } else if (elems && in->op == I_ARRAY_SET'),
 'signed_key': m(GET, GET.replace('(7u | GCF_SIGNED)', '7u')),
 'idx_key': m(GET, GET.replace('RLE_ELEM | ir_resolve(f, ir_args(f, v)[1])', 'RLE_ELEM')),
 'set_idx': m('        uint64_t off = RLE_ELEM | ir_resolve(f, ir_args(f, v)[1]);\n        Ref val',
              '        uint64_t off = RLE_ELEM;\n        Ref val'),
 'root_key': m('          if (e->root != root || e->off != off || e->size != size) continue;',
               '          if (e->off != off || e->size != size) continue;'),
 'size_key': m('          if (e->root != root || e->off != off || e->size != size) continue;',
               '          if (e->root != root || e->off != off) continue;'),
 # no read of an element is made on a rare way to be merged, as a field's is
 'pend_elem': m('          if (RLE_IS_PEND(e.value) || RLE_IS_ELEM(e.off)) continue;',
                '          if (RLE_IS_PEND(e.value)) continue;'),
}
EQUIVALENT = {}
SUITES = (('optcases', ['python3', 'test/run_optcases.py'], {}),
          ('cases', ['./test/run_tests.sh'], {'WARMC_FLAGS': '-O2'}))
