"""WARMC_LISTING's offsets (isel.c).  Killed by test/run_listing.py: each
listed mul against the object's disassembly."""
I = 'src/isel.c'
AT = '  uint32_t code_at = (uint32_t)(lead.len + pro.len);'
M = {
 'no_lead': (I, [(AT, '  uint32_t code_at = (uint32_t)pro.len;')]),
 'no_prologue': (I, [(AT, '  uint32_t code_at = (uint32_t)lead.len;')]),
 'body': (I, [('            fout->code_len, code_at, s->slots_off, s->nslots);', '            fout->code_len, 0u, s->slots_off, s->nslots);')]),
}
EQUIVALENT = {}
SUITES = (('listing', ['python3', 'test/run_listing.py'], {}),)
