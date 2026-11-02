/* ---- stack switching ------------------------------------------------------
   The stack-switching proposal's continuations, each on a stack of its own
   (included by rt/rt.c).

   A continuation is a chain of fibers: the one a cont.new makes, run by
   the resume that first takes it, and the ones it resumed in turn, down to
   the one that suspended -- top to bottom, each fiber's parent the one
   above it while it runs, each one's child the one below it while the
   chain is suspended.  What wasm holds is a handle, an object of the
   collector's heap (GCK_CONT) naming the chain's top and bottom, which a
   resume, resume_throw, switch or cont.bind consumes: it empties the
   handle, so that a second use traps (continuation already consumed); a
   suspension makes a new one.

   Compiled code calls warmc_cont_op (start.S) with its operands in an area
   of its frame, sixteen bytes a value, and finds there what it is given
   back.  The trampoline keeps the frame's callee-saved registers, frame
   pointer, stack pointer and return address -- a safepoint of the call's --
   in the store's gc_ctx, as an allocation keeps them, and its float
   callee-saved registers beside them.  An operation that switches leaves
   the running fiber waiting with that context as its own and the area's
   address, writes what the fiber it switches to is given into that one's
   area, and gives the trampoline that one's context to take up
   (warmc_cont_jump): its registers -- healed, if the collector moved what
   they held meanwhile -- its stack, and the status its warmc_cont_op
   returns.  No frame of the runtime's stays on a stack that waits.  A fiber
   that has not run has warmc_fiber_entry for its context, which calls its
   function through warmc_fiber_invoke: that call's return address,
   FIBER_RET, is where a fiber's frames end, and the collector's frame walk
   and the unwinder go on from there to the frame of the resume in its
   parent.

   The collector walks a suspended chain's frames when it marks its handle;
   a chain the program takes is claimed first (cont_claim), the collector
   done with it, and its references healed for the collector's epoch -- a
   mark's or a relocation's -- unless they were already, as a chain's are
   that ran in it.  A chain whose handle no mark reached is freed at the
   mark's end.  An exception a fiber does not catch goes on from its
   parent's resume; one thrown into a suspended continuation is unwound
   from where it waits; a trap ends every fiber between it and the call
   that catches it.  The instances' stack limit is the running fiber's. */

enum { F_FRESH = 0, F_RUNNING, F_SUSPENDED, F_DONE };
enum { OWN_FREE = 0, OWN_GC = 1, OWN_PROG = 2 };
#define CONT_TYPE_INDEX 1u
#define CONT_HANDLE_BYTES 32u

typedef struct { GcHeader h; Fiber *top, *bottom; } ContHandle;

void warmc_fiber_entry(void);
__attribute__((noreturn)) void warmc_cont_jump(const GcCtx *ctx);

#if defined(__x86_64__)
#define CONT_NREG_INT 3
#define CONT_NREG_FLT 4
#else
#define CONT_NREG_INT 7
#define CONT_NREG_FLT 8
#endif

/* WARMC_CONT_STACK_KB: each fiber's stack, a mebibyte by default */
static u64 cont_stack_bytes = 1u << 20;
#define CONT_GUARD   (64u << 10)
/* What is left below the limit compiled code checks, for the runtime's own
   frames and a host function's: an eighth of the stack, from 64 KiB to
   256 KiB, as a thread's is (warmc_start_c). */
static u64 cont_margin(u64 len) {
  u64 m = len / 8;
  if (m < (64u << 10)) m = 64u << 10;
  if (m > (256u << 10)) m = 256u << 10;
  return m;
}
/* What a fiber's stack counts for towards the collector's next cycle, as
   much as it touches at least: a program that makes fibers and drops them
   allocates little on the heap but their handles, and would not collect
   them otherwise. */
#define CONT_STACK_COST (32u << 10)

static const GcTypeInfo gc_type_cont = {
  0, 0, 0, (u32)(sizeof(Fiber *) * 2), 0, 0, GCK_CONT, 0, GC_HDR_BYTES, 0
};

static Fiber *cont_root(void) { return &ST->cont.root; }
static Fiber *cont_cur(void) { return ST->cont.cur ? ST->cont.cur : cont_root(); }
static u64 *cont_xfer(u32 i) { return &ST->cont.xfer[2 * i]; }

/* every instance of the store checks its frames against lim */
static void cont_set_limit(u64 lim) {
  if (lim == ST->g_stack_limit) return;
  ST->g_stack_limit = lim;
  for (u32 i = 0; i < ST->ninstances; i++)
    if (ST->instances[i]) ST->instances[i]->vm.stack_limit = lim;
}

#if defined(WARMC_WINDOWS)
static void teb_get(Fiber *f) {
  __asm__ volatile("movq %%gs:0x08, %0" : "=r"(f->teb_base));
  __asm__ volatile("movq %%gs:0x10, %0" : "=r"(f->teb_limit));
  __asm__ volatile("movq %%gs:0x1478, %0" : "=r"(f->teb_dealloc));
}
static void teb_set(const Fiber *f) {
  if (!f->teb_base) return;
  __asm__ volatile("movq %0, %%gs:0x08" : : "r"(f->teb_base) : "memory");
  __asm__ volatile("movq %0, %%gs:0x10" : : "r"(f->teb_limit) : "memory");
  __asm__ volatile("movq %0, %%gs:0x1478" : : "r"(f->teb_dealloc) : "memory");
}
#else
static void teb_get(Fiber *f) { (void)f; }
static void teb_set(const Fiber *f) { (void)f; }
#endif

/* f runs from here on */
static void cont_run(Fiber *f) {
  ST->cont.cur = f == cont_root() ? 0 : f;
  cont_set_limit(f->limit);
  teb_set(f);
  ST->cont.nswitch++;
}

static int ck_float(u8 k) { return k == CK_F32 || k == CK_F64 || k == CK_V128; }

/* ---- fibers and their stacks ---- */

/* Fibers come from chunks of FIBER_CHUNK, kept until the store goes: a
   fiber the collector may still look at is never unmapped under it. */
#define FIBER_CHUNK 128u
typedef struct FiberChunk { struct FiberChunk *next; u64 used; Fiber f[FIBER_CHUNK]; } FiberChunk;

static Fiber *fiber_new(void) {
  Fiber *f = ST->cont.free_fibers;
  if (f) ST->cont.free_fibers = f->next_all;
  else {
    FiberChunk *c = (FiberChunk *)ST->cont.chunks;
    if (!c || c->used == FIBER_CHUNK) {
      c = (FiberChunk *)sys_mmap(0, sizeof(FiberChunk), PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS);
      if (!c || c == (FiberChunk *)-1) warmc_trap(TRAP_HEAP_OOM);
      c->next = (FiberChunk *)ST->cont.chunks;
      c->used = 0;
      ST->cont.chunks = c;
    }
    f = &c->f[c->used++];
  }
  for (usz i = 0; i < sizeof *f; i++) ((u8 *)f)[i] = 0;
  f->own = OWN_PROG;
  f->seen = ST->cont.epoch;
  ST->cont.nmade++;
  ST->cont.nlive++;
  f->next_all = ST->cont.all;
  if (f->next_all) f->next_all->prev_all = f;
  ST->cont.all = f;
  return f;
}

/* f and its stack given back: the fiber to the store's free ones, the
   stack to its pool */
static void fiber_free(Fiber *f) {
  if (f->prev_all) f->prev_all->next_all = f->next_all;
  else ST->cont.all = f->next_all;
  if (f->next_all) f->next_all->prev_all = f->prev_all;
  if (f->map) {
    if (ST->cont.nstacks < CONT_POOL) {
      ST->cont.stacks[ST->cont.nstacks].map = f->map;
      ST->cont.stacks[ST->cont.nstacks].len = f->map_len;
      ST->cont.nstacks++;
    } else {
      sys_munmap(f->map, (usz)f->map_len);
      ST->cont.nmapped--;
    }
  }
  ST->cont.nlive--;
  if (f->bound) inst_free(f->bound);
  if (f->bound_kinds) inst_free(f->bound_kinds);
  f->map = 0;
  f->bound = 0;
  f->bound_kinds = 0;
  f->nbound = f->capbound = 0;
  f->state = F_DONE;
  f->handle = 0;
  f->prev_all = 0;
  f->next_all = ST->cont.free_fibers;
  ST->cont.free_fibers = f;
}

/* the fibers that ended, freed once nothing runs on their stacks */
static void cont_reap(void) {
  while (ST->cont.zombies) {
    Fiber *f = ST->cont.zombies;
    ST->cont.zombies = f->child;
    f->child = 0;
    fiber_free(f);
  }
}

static void cont_zombie(Fiber *f) {
  f->state = F_DONE;
  f->parent = 0;
  f->child = ST->cont.zombies;
  ST->cont.zombies = f;
}

/* f, about to start: a stack, with warmc_fiber_entry at its top to take up
   (the fiber where the first callee-saved register goes) */
static void fiber_stack(Fiber *f) {
  usz len = (usz)cont_stack_bytes + CONT_GUARD;
  u8 *map;
  if (ST->cont.nstacks) {
    ST->cont.nstacks--;
    map = ST->cont.stacks[ST->cont.nstacks].map;
    len = (usz)ST->cont.stacks[ST->cont.nstacks].len;
  } else {
    map = (u8 *)sys_mmap(0, len, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS);
    if (!map || map == (u8 *)-1) warmc_trap(TRAP_HEAP_OOM);
    if (sys_mprotect(map + CONT_GUARD, len - CONT_GUARD, PROT_READ | PROT_WRITE) != 0) {
      sys_munmap(map, len);
      warmc_trap(TRAP_HEAP_OOM);
    }
    ST->cont.nmapped++;
  }
  ST->gc_mut.alloc_since += CONT_STACK_COST;
  f->map = map;
  f->map_len = len;
  f->limit = (u64)(uptr)map + CONT_GUARD + cont_margin(len - CONT_GUARD);
  for (u32 i = 0; i < 10; i++) f->ctx.gpr[i] = 0;
  f->ctx.gpr[0] = (u64)(uptr)f;
  f->ctx.sp = ((u64)(uptr)map + len) & ~(u64)15;
  f->ctx.pc = (u64)(uptr)warmc_fiber_entry;
  f->ctx.fp = 0;
  f->teb_base = f->ctx.sp;
  f->teb_limit = (u64)(uptr)map + CONT_GUARD;
  f->teb_dealloc = (u64)(uptr)map;
}

/* ---- the collector ---- */

static void cont_scan_frames(Fiber *f, GcRootFn fn) {
  u64 *gprloc[10];
  for (u32 i = 0; i < 10; i++) gprloc[i] = &f->ctx.gpr[i];
  (void)gc_walk_frames(f->ctx.pc, f->ctx.sp, gprloc, fn);
}

static void cont_scan_bound(Fiber *f, GcRootFn fn) {
  for (u32 i = 0; i < f->nbound; i++)
    if (f->bound_kinds[i] == CK_REF) fn(&f->bound[2 * i]);
}

static void cont_scan_fiber(Fiber *f, GcRootFn fn) {
  if (f->state == F_SUSPENDED) cont_scan_frames(f, fn);
  cont_scan_bound(f, fn);
}

/* the program's heal of a slot of a fiber it takes: the good colour, and
   its object shaded while a mark is on */
static void cont_heal_slot(u64 *slot) {
  u64 v = *slot;
  if (!gc_in_heap(v)) return;
  u64 h = gc_heal_reloc(v);
  *slot = h;
  if (__atomic_load_n(&ST->gc_phase, __ATOMIC_ACQUIRE) == GCPH_MARK) gc_mark_ref(h);
}

/* The program's claim of f, before it runs it or changes it: once the
   collector is done with it, and with its references healed for this
   epoch if they are not. */
static void cont_claim(Fiber *f) {
  /* The collector walks fibers only while a mark is on, which only this
     thread starts, at a round: with none on, no walk is under way, nor
     begins before the claim is done. */
  if (__atomic_load_n(&ST->gc_phase, __ATOMIC_RELAXED) != GCPH_MARK) {
    f->own = OWN_PROG;
    if (f->seen != ST->cont.epoch && ST->gc_base) cont_scan_fiber(f, cont_heal_slot);
    f->seen = ST->cont.epoch;
    return;
  }
  for (;;) {
    u32 z = OWN_FREE;
    if (__atomic_compare_exchange_n(&f->own, &z, (u32)OWN_PROG, 0, __ATOMIC_ACQ_REL,
                                    __ATOMIC_ACQUIRE))
      break;
    warmc_cpu_relax();
  }
  if (f->seen != ST->cont.epoch && ST->gc_base) cont_scan_fiber(f, cont_heal_slot);
  f->seen = ST->cont.epoch;
}

/* f the collector's to walk again: what it holds is as this epoch has it,
   whether a claim healed it or it ran in the epoch */
static void cont_release(Fiber *f) {
  f->seen = ST->cont.epoch;
  __atomic_store_n(&f->own, (u32)OWN_FREE, __ATOMIC_RELEASE);
}

static void gc_mark_slot_fn(u64 *slot) { gc_mark_slot(slot); }

/* The collector's mark of a handle: the chain it names, fiber by fiber, each
   held while the next is taken, unless the program has its top -- which it
   then heals and shades itself (cont_claim). */
static void gc_scan_cont(u8 *p) {
  ContHandle *c = (ContHandle *)p;
  Fiber *f = __atomic_load_n(&c->top, __ATOMIC_ACQUIRE);
  if (!f) return;
  u32 z = OWN_FREE;
  if (!__atomic_compare_exchange_n(&f->own, &z, (u32)OWN_GC, 0, __ATOMIC_ACQ_REL,
                                   __ATOMIC_ACQUIRE))
    return;
  if (__atomic_load_n(&c->top, __ATOMIC_ACQUIRE) != f) {
    __atomic_store_n(&f->own, (u32)OWN_FREE, __ATOMIC_RELEASE);
    return;
  }
  u32 epoch = __atomic_load_n(&ST->cont.epoch, __ATOMIC_ACQUIRE);
  for (;;) {
    if (f->seen != epoch) {
      cont_scan_fiber(f, gc_mark_slot_fn);
      f->seen = epoch;
    }
    Fiber *next = f->child;
    if (next) {
      for (;;) {
        z = OWN_FREE;
        if (__atomic_compare_exchange_n(&next->own, &z, (u32)OWN_GC, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
          break;
        if (z == OWN_PROG) { next = 0; break; }
        warmc_cpu_relax();
      }
    }
    __atomic_store_n(&f->own, (u32)OWN_FREE, __ATOMIC_RELEASE);
    if (!next) return;
    f = next;
  }
}

static int gc_is_marked(u64 v) {
  if (!gc_in_heap(v)) return 1;
  u64 off = GC_ADDR(v) - (u64)(uptr)ST->gc_base;
  u32 h = gc_head_of(off >> GC_GRAN_SHIFT);
  if (__atomic_load_n(&ST->gc_page_epoch[h], __ATOMIC_ACQUIRE) !=
      GC_EPOCH_READY(ST->gc_cycle_no))
    return 0;
  u64 i = off / GC_UNIT;
  return (ST->gc_markbits[i >> 6] >> (i & 63)) & 1;
}

static void gc_verify_fail(const char *what, u64 a);

/* WARMC_GC_VERIFY at a mark's end: a reference a kept chain holds is to an
   object the mark reached, or one this cycle made */
static void cont_verify_slot(u64 *slot) {
  u64 v = *slot;
  if (!gc_in_heap(v)) return;
  u64 h = gc_heal_ptr(v);
  if (gc_is_marked(h)) return;
  u32 pg = gc_page_head(gc_page_of((void *)(uptr)GC_ADDR(h)));
  if (ST->gc_pages[pg].alloc_cycle == (u32)ST->gc_cycle_no) return;
  gc_verify_fail("a suspended continuation holds a reference no mark reached", v);
}

/* At a mark's end, before the forwarding table goes: each suspended chain
   whose handle no mark reached and this cycle did not make is freed, and
   every other's handle healed. */
static void cont_sweep(void) {
  u32 cyc = (u32)ST->gc_cycle_no;
  for (Fiber *f = ST->cont.all; f;) {
    Fiber *next = f->next_all;
    if (!f->handle || f->state == F_DONE) { f = next; continue; }
    u64 h = gc_heal_ptr(f->handle);
    if (f->born == cyc || gc_is_marked(h)) {
      f->handle = h;
      if (gc_opt_verify)
        for (Fiber *g = f; g; g = g->child) cont_scan_fiber(g, cont_verify_slot);
      f = next;
      continue;
    }
    for (Fiber *g = f; g;) {
      Fiber *c = g->child;
      if (next == g) next = g->next_all;
      fiber_free(g);
      ST->cont.nswept++;
      g = c;
    }
    f = next;
  }
}

/* the roots stack switching holds: values in transit and a chain between
   two handles -- the running chain's frames are the frame walk's
   (gc_visit_frames) */
static void cont_visit_roots(GcRootFn fn) {
  for (u32 i = 0; i < ST->cont.xfer_n; i++)
    if (ST->cont.xfer_kind[i] == CK_REF) fn(cont_xfer(i));
  if (ST->cont.transit)
    for (Fiber *f = ST->cont.transit; f; f = f->child) cont_scan_fiber(f, fn);
}

/* ---- handles ---- */

static ContHandle *handle_of(u64 v) {
  return (ContHandle *)(uptr)GC_CANON(gc_load(&v));
}

/* A handle for the chain from top to bottom: bumped from the allocation
   buffer compiled code bumps, when it has room -- no collection, so the
   values the operation holds need no rooting -- or else from gc_alloc,
   with the n values of area the collector's roots meanwhile, of the kinds
   given. */
static u64 handle_new(Fiber *top, Fiber *bottom, const u64 *area, u32 n, const u8 *kinds) {
  u64 h, cur = ST->gc_lab_cell[0];
  if (ST->gc_lab_cell[1] - cur >= CONT_HANDLE_BYTES && !gc_opt_stress) {
    ST->gc_lab_cell[0] = cur + CONT_HANDLE_BYTES;
    GcHeader *hd = (GcHeader *)(uptr)GC_ADDR(cur);
    hd->len = 0;
    __atomic_store_n(&hd->w, CONT_TYPE_INDEX, __ATOMIC_RELEASE);
    h = cur;
  } else {
    for (u32 i = 0; i < n; i++) {
      cont_xfer(i)[0] = area[2 * i];
      cont_xfer(i)[1] = area[2 * i + 1];
      ST->cont.xfer_kind[i] = kinds[i];
    }
    ST->cont.xfer_n = n;
    h = (u64)(uptr)gc_alloc(CONT_TYPE_INDEX, &gc_type_cont, sizeof(Fiber *) * 2, 0);
    ST->cont.xfer_n = 0;
    for (u32 i = 0; i < n; i++) {
      ((u64 *)area)[2 * i] = cont_xfer(i)[0];
      ((u64 *)area)[2 * i + 1] = cont_xfer(i)[1];
    }
  }
  ContHandle *c = (ContHandle *)(uptr)GC_CANON(h);
  c->top = top;
  c->bottom = bottom;
  top->handle = h;
  top->born = (u32)ST->gc_cycle_no;
  return h;
}

/* the chain handle v names, taken and claimed: it traps for a null or a
   consumed one */
static void handle_take(u64 v, Fiber **top, Fiber **bottom) {
  if (!v) warmc_trap(TRAP_NULL_CONT);
  ContHandle *c = handle_of(v);
  Fiber *t = c->top;
  if (!t) warmc_trap(TRAP_CONT_CONSUMED);
  *top = t;
  *bottom = c->bottom;
  __atomic_store_n(&c->top, (Fiber *)0, __ATOMIC_RELEASE);
  c->bottom = 0;
  t->handle = 0;
  for (Fiber *f = t; f; f = f->child) cont_claim(f);
}

/* ---- switching ---- */

/* me waits where its operation was called from, its values to come back to
   area */
static void cont_wait(Fiber *me, u64 *area) {
  me->ctx = ST->gc_ctx;
  me->area = area;
  if (me == cont_root()) {
    me->limit = ST->g_stack_limit;
    teb_get(me);
  }
}

/* The running chain from top down to bottom, suspended at bottom's
   operation: its links made the child ones, and every fiber of it the
   collector's again. */
static void cont_suspend(Fiber *top, Fiber *bottom, u64 *area) {
  for (Fiber *g = bottom; g != top; g = g->parent) g->parent->child = g;
  bottom->child = 0;
  top->parent = 0;
  for (Fiber *g = top; g; g = g->child) g->state = F_SUSPENDED;
  cont_wait(bottom, area);
  for (Fiber *g = top; g; g = g->child) cont_release(g);
}

/* A taken chain linked under the resume at site in me, in this call into
   wasm; its bound values, if it has any, put ahead of the n values from
   (of the kinds given) -- into the bottom's area, or, for a bottom yet to
   start, into the transfer area, whose kinds and count it starts with.
   Bottom's context to take up. */
static GcCtx *cont_enter(Fiber *top, Fiber *bottom, Fiber *me, const ContSite *site,
                         void *vm, const u64 *from, u32 n, const u8 *kinds, u64 status) {
  top->parent = me;
  for (Fiber *f = top; f; f = f->child) {
    if (f->state == F_SUSPENDED) f->state = F_RUNNING;
    f->depth = ST->gc_invoke_depth;
  }
  top->rsite = site;
  top->rvm = vm;
  u32 nb = bottom->nbound;
  if (nb + n > CONT_XFER_MAX) warmc_trap(TRAP_HEAP_OOM);
  int fresh = bottom->state == F_FRESH;
  u64 *dst = fresh ? ST->cont.xfer : bottom->area;
  for (u32 i = 0; i < nb; i++) {
    dst[2 * i] = bottom->bound[2 * i];
    dst[2 * i + 1] = bottom->bound[2 * i + 1];
  }
  for (u32 i = 0; i < n; i++) {
    dst[2 * (nb + i)] = from[2 * i];
    dst[2 * (nb + i) + 1] = from[2 * i + 1];
  }
  if (fresh) {
    for (u32 i = 0; i < nb; i++) ST->cont.kinds[i] = bottom->bound_kinds[i];
    for (u32 i = 0; i < n; i++) ST->cont.kinds[nb + i] = kinds[i];
    ST->cont.start_n = nb + n;
    fiber_stack(bottom);
    bottom->state = F_RUNNING;
  }
  bottom->nbound = 0;
  bottom->ctx.status = status;
  cont_run(bottom);
  return &bottom->ctx;
}

/* the handler for tag (an identity, Instance.tags[]) of kind (0 suspend,
   1 switch) on the resumes above the running fiber in this call into wasm:
   the fiber that resume runs, and the handler's index there in *k; 0 for
   none */
static Fiber *cont_handler(const void *tag, u32 kind, u32 *k) {
  for (Fiber *f = ST->cont.cur; f && f != cont_root(); f = f->parent) {
    if (f->depth != ST->gc_invoke_depth) return 0;
    const ContSite *s = f->rsite;
    Instance *in = INST_OF((WarmInstance *)f->rvm);
    const ContHandler *hs = cs_handlers(s);
    for (u32 i = 0; i < s->nhandlers; i++) {
      if (hs[i].kind == kind && hs[i].tag < in->md->ntags && in->tags[hs[i].tag] == tag) {
        *k = i;
        return f;
      }
    }
  }
  return 0;
}

/* f's first run, on its own stack: its function called with the values
   cont_enter left in the transfer area, and its results given to the
   resume that runs it when it returns */
__attribute__((noreturn, used)) void warmc_fiber_run(Fiber *f) {
  cont_reap();
  u32 np = ST->cont.start_n;
  const u8 *kinds = ST->cont.kinds;
  u32 nr = f->rsite->nout;
  u64 iargs[8] = { 0 }, fargs[WARMC_FARGS_WORDS] = { 0 };
  u64 stk[2 * np + 2];
  u64 ret[WARMC_RETBUF_WORDS + 2 * nr];
  u64 nstk = 0;
  u32 ni = 0, nf = 0;
  for (u32 i = 0; i < np; i++) {
    u8 k = kinds[i];
    const u64 *x = cont_xfer(i);
    int fl = ck_float(k);
    if (fl ? nf < CONT_NREG_FLT : ni < CONT_NREG_INT) {
      if (fl) { fargs[2 * nf] = x[0]; fargs[2 * nf + 1] = x[1]; }
      else iargs[ni] = x[0];
    } else {
#if defined(__x86_64__)
      if (k == CK_V128) {
        nstk = (nstk + 1) & ~(u64)1;
        stk[nstk++] = x[0];
        stk[nstk++] = x[1];
      } else {
        stk[nstk++] = x[0];
      }
#else
      stk[nstk++] = x[0];
      stk[nstk++] = k == CK_V128 ? x[1] : 0;
#endif
    }
    if (fl) nf++;
    else ni++;
  }
  ST->gc_from_wasm = 0;
  warmc_fiber_invoke(f->fn_code, (WarmInstance *)f->fn_instance, iargs, fargs, ret,
                     stk, nstk * 8);
  /* returned: its results for the resume that runs it now */
  const ContSite *rs = f->rsite;
  const u8 *rk = cs_out_kinds(rs);
  Fiber *p = f->parent;
  u64 *dst = p->area;
  nr = rs->nout;
  if (nr == 1) {
    const u64 *at = ck_float(rk[0]) ? &ret[WARMC_RET_V0] : &ret[WARMC_RET_X0];
    dst[0] = at[0];
    dst[1] = rk[0] == CK_V128 ? at[1] : 0;
  } else {
    for (u32 i = 0; i < nr; i++) {
      dst[2 * i] = ret[WARMC_RET_MULTI(i)];
      dst[2 * i + 1] = rk[i] == CK_V128 ? ret[WARMC_RET_MULTI(i) + 1] : 0;
    }
  }
  cont_zombie(f);
  p->ctx.status = 0;
  cont_run(p);
  warmc_cont_jump(&p->ctx);
}

/* ---- the operations ---- */

/* Compiled code's stack-switching operation at site of vm's module, its
   operands in area: the context of the fiber to take up, or 0 for the
   operation's own caller, its values in area. */
static GcCtx *cont_op(WarmInstance *vm, const ContSite *s, u64 *area) {
  Instance *in = INST_OF(vm);
  Fiber *me = cont_cur();
  switch (s->op) {
    case CONT_NEW: {
      const FuncRef *fr = (const FuncRef *)(uptr)area[0];
      if (!fr) warmc_trap(TRAP_NULL_REF);
      Fiber *f = fiber_new();
      f->fn_code = fr->code;
      f->fn_instance = fr->instance;
      f->state = F_FRESH;
      u64 h = handle_new(f, f, area, 0, 0);
      cont_release(f);
      area[0] = h;
      area[1] = 0;
      return 0;
    }
    case CONT_BIND: {
      u32 nb = s->nin - 1;
      Fiber *top, *bottom;
      handle_take(area[2 * nb], &top, &bottom);
      const u8 *kinds = cs_in_kinds(s);
      u32 need = bottom->nbound + nb;
      if (need > bottom->capbound) {
        u32 cap = need < 8 ? 8 : need * 2;
        u64 *v = (u64 *)inst_alloc((u64)cap * 16);
        u8 *k = (u8 *)inst_alloc(cap);
        for (u32 i = 0; i < bottom->nbound; i++) {
          v[2 * i] = bottom->bound[2 * i];
          v[2 * i + 1] = bottom->bound[2 * i + 1];
          k[i] = bottom->bound_kinds[i];
        }
        if (bottom->bound) inst_free(bottom->bound);
        if (bottom->bound_kinds) inst_free(bottom->bound_kinds);
        bottom->bound = v;
        bottom->bound_kinds = k;
        bottom->capbound = cap;
      }
      for (u32 i = 0; i < nb; i++) {
        u32 j = bottom->nbound + i;
        bottom->bound[2 * j] = area[2 * i];
        bottom->bound[2 * j + 1] = area[2 * i + 1];
        bottom->bound_kinds[j] = kinds[i];
      }
      bottom->nbound = need;
      ST->cont.transit = top;
      u64 h = handle_new(top, bottom, area, 0, 0);
      ST->cont.transit = 0;
      for (Fiber *g = top; g; g = g->child) cont_release(g);
      area[0] = h;
      area[1] = 0;
      return 0;
    }
    case CONT_RESUME: {
      u32 n = s->nin - 1;
      Fiber *top, *bottom;
      handle_take(area[2 * n], &top, &bottom);
      cont_wait(me, area);
      return cont_enter(top, bottom, me, s, vm, area, n, cs_in_kinds(s), 0);
    }
    case CONT_RESUME_THROW: case CONT_RESUME_THROW_REF: {
      u32 n = s->nin - 1;
      Fiber *top, *bottom;
      handle_take(area[2 * n], &top, &bottom);
      WarmExn *e;
      if (s->op == CONT_RESUME_THROW_REF) {
        e = (WarmExn *)(uptr)area[0];
        if (!e) warmc_trap(TRAP_NULL_REF);
      } else {
        if (s->tag >= in->md->ntags) die("resume_throw names a tag the module does not have");
        /* no collection while the payload is only in the area */
        e = (WarmExn *)exn_alloc(in, s->tag);
        WarmExn *ew = (WarmExn *)(uptr)GC_CANON((u64)(uptr)e);
        ew->tag = in->tags[s->tag];
        ew->nvals = n;
        for (u32 i = 0; i < n; i++) {
          ew->vals[2 * i] = area[2 * i];
          ew->vals[2 * i + 1] = area[2 * i + 1];
        }
      }
      ST->gc_from_wasm = 0;
      if (bottom->state == F_FRESH) {
        /* nothing of it ran: the exception is the resume's own */
        for (Fiber *f = top; f;) { Fiber *c = f->child; fiber_free(f); f = c; }
        unwind(e, ST->gc_ctx.pc, ST->gc_ctx.sp, regs_of_ctx(&ST->gc_ctx));
      }
      cont_wait(me, area);
      top->parent = me;
      for (Fiber *f = top; f; f = f->child) {
        f->state = F_RUNNING;
        f->depth = ST->gc_invoke_depth;
      }
      top->rsite = s;
      top->rvm = vm;
      bottom->nbound = 0;
      cont_run(bottom);
      /* unwound from where it waits, on this stack -- whose frames below
         the one waiting at area are left behind */
      unwind(e, bottom->ctx.pc, bottom->ctx.sp, regs_of_ctx(&bottom->ctx));
    }
    case CONT_SUSPEND: {
      u32 k;
      if (s->tag >= in->md->ntags) die("suspend names a tag the module does not have");
      Fiber *f = cont_handler(in->tags[s->tag], 0, &k);
      if (!f) warmc_trap(TRAP_UNHANDLED_TAG);
      Fiber *bottom = ST->cont.cur;
      Fiber *p = f->parent;
      u64 h = handle_new(f, bottom, area, s->nin, cs_in_kinds(s));
      u64 *dst = p->area;
      for (u32 i = 0; i < 2 * s->nin; i++) dst[i] = area[i];
      dst[2 * s->nin] = h;
      dst[2 * s->nin + 1] = 0;
      cont_suspend(f, bottom, area);
      p->ctx.status = 1 + k;
      cont_run(p);
      return &p->ctx;
    }
    case CONT_SWITCH: {
      u32 n = s->nin - 1;
      u32 k;
      if (s->tag >= in->md->ntags) die("switch names a tag the module does not have");
      Fiber *f = cont_handler(in->tags[s->tag], 1, &k);
      if (!f) warmc_trap(TRAP_UNHANDLED_TAG);
      Fiber *ttop, *tbottom;
      handle_take(area[2 * n], &ttop, &tbottom);
      Fiber *bottom = ST->cont.cur;
      Fiber *p = f->parent;
      const ContSite *hs = f->rsite;
      void *hvm = f->rvm;
      ST->cont.transit = ttop;
      u64 h = handle_new(f, bottom, area, n, cs_in_kinds(s));
      ST->cont.transit = 0;
      area[2 * n] = h;
      area[2 * n + 1] = 0;
      cont_suspend(f, bottom, area);
      return cont_enter(ttop, tbottom, p, hs, hvm, area, n + 1, cs_in_kinds(s), 0);
    }
  }
  die("an unknown stack-switching operation");
  return 0;
}

GcCtx *warmc_cont_op_w(WarmInstance *vm, u32 site, u64 *area) {
  Instance *in = INST_OF(vm);
  if (site >= in->md->ncont_sites) die("a stack-switching site the module does not have");
  u32 saved = ST->gc_from_wasm;
  ST->gc_from_wasm = 1;
  if (ST->cont.zombies) cont_reap();
  if (!ST->gc_base) gc_init();
  GcCtx *to = cont_op(vm, &in->md->cont_sites[site], area);
  ST->gc_from_wasm = to ? 0 : saved;
  return to;
}

/* ---- the unwinder's, traps', the store's end ---- */

/* an exception leaving the running fiber's function: the fiber ended, its
   parent running again at its resume, whose frame the unwinder goes on in */
static Fiber *cont_unwind_out(void) {
  Fiber *f = ST->cont.cur;
  if (!f) die("an exception left a fiber the store does not run");
  Fiber *p = f->parent;
  cont_zombie(f);
  cont_run(p);
  return p;
}

/* The fibers between the running one and keep, which the call that caught
   a trap began in, ended by it: freed, and keep running again. */
static void cont_after_trap(Fiber *keep) {
  if (!keep) keep = cont_root();
  Fiber *f = ST->cont.cur;
  while (f && f != keep && f != cont_root()) {
    Fiber *p = f->parent;
    cont_zombie(f);
    f = p;
  }
  ST->cont.cur = keep == cont_root() ? 0 : keep;
  if (keep->limit) cont_set_limit(keep->limit);
  teb_set(keep);
  ST->cont.xfer_n = 0;
  ST->cont.transit = 0;
  cont_reap();
}

#if defined(WARMC_JIT)
/* a store's end: every fiber and stack of it given back */
static void cont_free_all(void) {
  cont_reap();
  while (ST->cont.all) fiber_free(ST->cont.all);
  for (u32 i = 0; i < ST->cont.nstacks; i++) {
    sys_munmap(ST->cont.stacks[i].map, (usz)ST->cont.stacks[i].len);
    ST->cont.nmapped--;
  }
  ST->cont.nstacks = 0;
  ST->cont.free_fibers = 0;
  while (ST->cont.chunks) {
    FiberChunk *c = (FiberChunk *)ST->cont.chunks;
    ST->cont.chunks = c->next;
    sys_munmap(c, sizeof(FiberChunk));
  }
}
#endif
