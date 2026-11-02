/* A store, as the core spec has one: the runtime's state for a set of
   instances that run together -- their slots, the heap their GC objects
   are in and its collector's thread, the trap a call into them is
   catching, their WASI state -- which rt/rt.c and rt/wasi.c reach
   through ST.

   A program, or a shared library warmc made, has one store, warmc_store0,
   and ST is its address: the code reads its state where it would read a
   global.  A JIT's host (src/capi.c) has one for each wasm_store_t, and ST
   is the one the calling thread runs now (warmc_store, thread-local),
   which the host sets as a thread enters a store and puts back as it
   leaves.  Two stores run on two threads at once, as nothing of one is
   the other's; one store runs on one thread at a time, as the host's lock
   for it has it.  What every store shares -- the modules' descriptors, the
   guard pages, the options -- stays where it is, under locks of its own.

   Compiled code finds its store through the instance: WarmInstance.store,
   which the barrier's stub and the allocation trampolines (rt/start.S,
   rt/start_x64.S) read gc_fast and gc_ctx through, at the offsets
   STORE_GC_FAST and STORE_GC_CTX. */
#ifndef WARM_STORE_H
#define WARM_STORE_H

#include "warm_rt.h"

struct Instance;
struct GcRoots;
struct GcPage;

/* A JIT's host makes and frees instances as it goes: as many as this may
   live at once in a store, their slots reused. */
#if defined(WARMC_JIT)
#define MAX_INSTANCES (1u << 16)
#else
#define MAX_INSTANCES 4096
#endif
#define MAX_MODULE_DESCS 4096

/* What the program writes on its allocation, barrier and mark-queue paths,
   and what the collector thread writes for every object it marks, are each
   in cache lines of their own.  A line is 128 bytes on Apple silicon (and
   x86 fetches lines in adjacent pairs), so the 64-byte alignment these
   counters had left a hot counter of one side beside the other side's;
   which ones met moved with the size of the code linked in front of them,
   and ktbench/list ran in 70 ms or 130 ms depending on it. */
#define GC_LINE 128u

/* a word a thread waits on until it changes, and whether one sleeps on it */
typedef struct { u32 v; u32 sleepers; } GcthGate;

/* What the barrier's fast path reads, at the offsets start.S names: the
   heap's range, the map, each page's mark epoch and the mark bits, the
   colour that is good, whether a mark is on and the epoch its pages' bits
   are valid in, and the mark queue with the program's end of it (gc_mqp),
   where it puts an object it shades.  The collector writes it where it
   writes what it mirrors, at the rounds the program answers, and the
   program reads it after them.  healed and shaded count the loads the fast
   path healed and the objects it shaded. */
typedef struct {
  u64 base, heap_bytes;
  u8 *map;
  u32 *page_epoch;
  u64 *markbits;
  u64 good;
  u32 marking, ready;
  u64 healed;
  u8 **mq;
  u64 *mqp;
  u64 shaded;
} GcFast;

/* The callee-saved registers and the stack pointer and return address of
   the wasm frame that called into an allocation, which a collection there
   walks from and rewrites (start.S's GC_ENTER and GC_LEAVE).  One that
   called a stack-switching operation (warmc_cont_op) keeps its float
   callee-saved registers and frame pointer here too, and is taken up again
   from here with the status the operation returns (warmc_cont_jump). */
typedef struct { u64 gpr[10]; u64 sp; u64 pc; u64 fpr[8]; u64 fp; u64 status; } GcCtx;
#define GCCTX_FPR    96
#define GCCTX_FP     160
#define GCCTX_STATUS 168
_Static_assert(__builtin_offsetof(GcCtx, fpr) == GCCTX_FPR &&
               __builtin_offsetof(GcCtx, fp) == GCCTX_FP &&
               __builtin_offsetof(GcCtx, status) == GCCTX_STATUS,
               "start.S's GCCTX offsets");

/* A stack of the stack-switching proposal's continuations, rt/cont.inc's.
   The store's root is the stack every call into its wasm starts on. */
typedef struct Fiber Fiber;
struct Fiber {
  /* compiled code's frame where it waits -- at a suspension, or at the
     resume that runs its child -- which it is taken up from, and the area
     of that frame its operation's values come back to; nothing of the
     runtime's is on its stack while it waits.  A fiber yet to start has
     warmc_fiber_entry for its frame. */
  GcCtx ctx;
  u64 *area;
  /* its stack's mapping, and the limit compiled code checks frames on it
     against */
  u8 *map;
  u64 map_len;
  u64 limit;
  /* on Windows, the thread's TEB's StackBase, StackLimit and
     DeallocationStack while it runs, which host code reads (the root's as
     they were when it last waited) */
  u64 teb_base, teb_limit, teb_dealloc;
  /* while it runs, the fiber whose resume runs it; while it is suspended,
     the one below it in its chain (child), the chain's top first */
  Fiber *parent, *child;
  /* the resume that runs it, whose handlers a suspension looks for, the
     instance that resume is in, and how deep in calls into wasm it is */
  const struct ContSite *rsite;
  void *rvm;
  u64 depth;
  u32 state;
  /* who has it: 0 no one, 1 the collector walking it, 2 the program, which
     runs it or is about to (cont_claim) */
  u32 own;
  /* the collector's epoch its references were last healed in */
  u32 seen;
  /* the cycle its chain's handle was made in, at the chain's top */
  u32 born;
  /* the function a fiber that has not started calls, and the values bound
     ahead of the ones a resume gives (cont.bind), sixteen bytes each */
  void *fn_code, *fn_instance;
  u32 nbound, capbound;
  u64 *bound;
  u8 *bound_kinds;
  /* at a chain's top while it is suspended: its handle, which the end of
     a mark frees the chain with when no mark reached it */
  u64 handle;
  /* every fiber of the store */
  Fiber *next_all, *prev_all;
};

/* the most values a stack-switching operation passes on at once: wasm's
   thousand parameters and a continuation */
#define CONT_XFER_MAX 1024u
#define CONT_POOL 64u

typedef struct {
  Fiber root;
  /* the fiber that runs, 0 for the root */
  Fiber *cur;
  Fiber *all, *free_fibers, *zombies;
  void *chunks;
  /* how many of the transfer area's values are the collector's roots for
     now, their kinds in xfer_kind; the collector's epoch (cont_claim) */
  u32 xfer_n, epoch;
  Fiber *transit;
  /* what WARMC_GC_STATS reports: fibers made, freed by a mark's end and
     live, stacks mapped, and switches */
  u64 nmade, nswept, nlive, nmapped, nswitch;
  /* a fiber about to start: the kinds of its arguments */
  const u8 *start_kinds;
  u32 start_n, nstacks;
  struct { u8 *map; u64 len; } stacks[CONT_POOL];
  u8 xfer_kind[CONT_XFER_MAX];
  u8 kinds[CONT_XFER_MAX];
  u64 xfer[2 * CONT_XFER_MAX];
} ContState;

/* the points gc_jitter perturbs (WARMC_GC_JITTER) */
enum { GCJ_ROUND, GCJ_IDLE, GCJ_MARK, GCJ_RELOC, GCJ_SLOT, GCJ_N };

/* A component's resources, by handle */
typedef struct {
  u32 rt;
  u32 own;
  u32 lends;
  u32 pad;
  u64 rep;
} ResSlot;

typedef struct {
  ResSlot *slots;
  u32 *freelist;
  u32 len, cap, nfree;
} ResTable;

typedef struct Store Store;
struct Store {
  /* ---- read by compiled code's stubs (start.S), at fixed offsets ---- */
  GcFast gc_fast;
  GcCtx gc_ctx;

  /* ---- a call into wasm: the trap it catches, how deep it is ---- */
#if defined(__x86_64__)
  /* the Win64 callee-saved set plus full-width xmm6-15 needs 240 bytes */
  u64 g_catch_buf[32];
#else
  u64 g_catch_buf[24];
#endif
  int g_catching;
  u32 g_trap_code;
  /* calls into wasm under way, which a collection from the innermost may
     not walk past when there is more than one (gc_may_collect) */
  u64 gc_invoke_depth;
  /* the limit compiled code checks its frames against, which every
     instance of the store has (warmc_rt_thread_enter) */
  u64 g_stack_limit;
  /* the instance a program's harness runs */
  WarmInstance *g_vm;
  /* The instances being made, innermost first: a start function may make
     another.  An instance is registered before it is linked, so one whose
     making a trap or a link error ends stays registered, linked as far as
     it got -- and must, as what it wrote into another instance's table
     before the trap still calls its functions. */
  struct Instance *g_making;

  /* ---- the collector's heap ---- */
  u64 gc_heap_bytes;
  u8 *gc_base;
  /* the heap's reservation, which gc_base is aligned within */
  u8 *gc_map;
  u64 gc_map_len;
  const GcTypeInfo **gc_typetab;
  u32 gc_ntypes;
  u32 gc_ndesc_types;
  u64 gc_live, gc_marked, gc_trigger;
  u64 gc_full_req, gc_full_done, gc_cycle_serves;
  u64 gc_ncycles, gc_npages_opened, gc_npages_freed;
  u64 gc_mark_end_took, gc_ncycles_stw;
  u64 gc_released, gc_nreleases;
  u64 gc_t_total, gc_pause_count, gc_pause_max, gc_pause_hist[5];
  struct GcPage *gc_pages;
  u64 gc_ngran;
  u8 *gc_gran_map;
  u64 *gc_granfree;
  u64 gc_granfree_words;
  u64 gc_gran_used;
  u64 gc_gran_peak;
  u64 gc_gran_hwm;
  /* The pages allocation bumps into: small and medium objects, and the
     program's allocation buffers (gc_lab_refill), small pages of their own
     -- in a medium one, the small objects the program makes there would
     take eight granules together to compact into.  None is relocated or
     freed while allocation may still bump into it. */
  u32 gc_small_page, gc_medium_page, gc_lab_page;
  /* Granules the program's allocations leave free for relocation to
     compact into: a heap the program fills has something live on every
     page, so a cycle frees none of them, and without room relocation
     cannot empty any either (gc_select_reloc).  A thirty-second of the
     heap, at least two medium pages and at most an eighth of it;
     gc_evacuating while relocation takes its pages, and gc_reserve_open for
     an allocation that two cycles have not made room for (gc_alloc), which
     takes it as a last resort: a heap whose live data fills it should hold
     as much as it did without. */
  u64 gc_reserve_gran;
  u32 gc_evacuating, gc_reserve_open;
  u64 *gc_markbits;
  u32 *gc_page_epoch;
  u8 **gc_grey;
  u64 gc_capgrey;
  u64 gc_cycle_no;
  u8 **gc_mq;
  u32 gc_conc_on, gc_conc_tried;
  u32 gc_cycle_running;
  u64 gc_good;
  u64 gc_badmask;
  u32 gc_mark_epoch;
  u32 gc_page_lock;
  u8 *gc_lab_start;
  u64 gc_nlabs;
  u64 gc_born_skipped;
  u64 gc_reloc_pages, gc_reloc_bytes, gc_reloc_objs, gc_reloc_stalls;
  u64 gc_reloc_lost, gc_reloc_mut, gc_reloc_pages_all, gc_reloc_objs_all;
  u64 gc_reloc_tried;
  u32 gc_evac_small, gc_evac_medium;
  u64 *gc_fwd;
  u64 gc_fwd_slots;
  u64 *gc_fwd_dead;
  u64 gc_fwd_dead_slots;
  struct GcRoots *gc_rootlist;
  u32 gc_from_wasm;
  u32 gc_no_collect;
  u64 gc_jitter_state[2];
  u64 gc_jitter_hits[2][GCJ_N];
  u64 gc_poll;
  GcthGate gc_round_done;
  u32 gc_round_ok;
  u32 gcth_stopping, gcth_started;
  GcthGate gcth_stopped;
  u64 gc_nrounds_asked, gc_nrounds, gc_nrounds_refused, gc_nrounds_aborted;
  u64 gc_t_rounds, gc_round_max;
  GcthGate gcth_idle;
  u64 gc_nrests, gc_nrests_late;
  u32 gc_round_active;
  u32 gc_rounds_seen;
  u64 gc_claim_slot, gc_claim_from;
  u64 gc_nrounds_held, gc_nrounds_collided, gc_nmark_rests;
  u64 gc_nassists, gc_nassists_stw;

  /* ---- the collector's thread ---- */
  u64 gcth_nwaits;
  void *gcth_thread;
  /* what the collector's thread runs (gcth_spawn, in a JIT's library) */
  void (*gcth_entry)(void);
  u32 gcth_tid;
  u64 gcth_stack_lo, gcth_stack_hi;
  GcthGate gcth_req;
  GcthGate gcth_ack;
  u64 gcth_send_ns;
  u64 gcth_lat_min, gcth_lat_sum, gcth_lat_n;
  u64 gcth_count;
  u32 gcth_nreq;
  u64 gcth_stop_late;

  /* ---- what the program writes on its allocation, barrier and mark
     queue paths, and what the collector's thread writes for every object
     it marks, each in lines of their own (GC_LINE) ---- */
  _Alignas(GC_LINE) struct {
    u64 alloc_since, bytes_alloc, fast_bytes, barrier_slow, barrier_shaded;
  } gc_mut;
  /* The program's end of the mark queue: head moves with every object a
     barrier shades, and the collector polls pub while it waits for work,
     so the pair is kept off the counters' line as well. */
  _Alignas(GC_LINE) struct {
    u64 head, pub;
  } gc_mqp;
  _Alignas(GC_LINE) struct {
    u64 ngrey, mark_conc_objs, mark_wait_took, mq_tail;
  } gc_col;
  _Alignas(GC_LINE) u32 gc_phase;
  _Alignas(GC_LINE) GcthGate gc_mut_wait;
  /* [0] the cursor, [1] the end; compiled code bumps [0] on every inline
     allocation through vmctx, so the cell has a line to itself. */
  _Alignas(GC_LINE) u64 gc_lab_cell[GC_LINE / 8];

  /* ---- instances ---- */
  _Alignas(GC_LINE) u32 ninstances;
  /* slots below ninstances that a freed instance left empty, and where the
     first of them may be */
  u32 nfree_slots, free_hint;
  u32 nregistry;
  /* instances freed while a collection scanned them, given back after it
     (warmc_instance_free) */
  struct Instance *inst_graveyard;
  ResTable g_restab;
  struct Instance *instances[MAX_INSTANCES];
  struct { const char *name; u32 name_len; WarmInstance *inst; } registry[MAX_INSTANCES];
  WarmInstance *g_cinsts[MAX_INSTANCES];
  struct { const ModuleDesc *md; u32 base; } gc_desc_types[MAX_MODULE_DESCS];

  /* ---- WASI's, which rt/wasi.c keeps (WasiStore there) ---- */
  void *wasi;

  /* ---- stack switching's (rt/cont.inc) ---- */
  ContState cont;
};

#define STORE_GC_FAST 0
#define STORE_GC_CTX  88
_Static_assert(__builtin_offsetof(Store, gc_fast) == STORE_GC_FAST &&
               __builtin_offsetof(Store, gc_ctx) == STORE_GC_CTX,
               "start.S's STORE_GC_FAST and STORE_GC_CTX");

#if defined(WARMC_JIT)
extern __attribute__((visibility("hidden"))) _Thread_local Store *warmc_store;
#define ST warmc_store
#else
extern __attribute__((visibility("hidden"))) Store warmc_store0;
#define ST (&warmc_store0)
#endif

#endif
