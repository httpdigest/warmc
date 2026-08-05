;; The exporting side: everything a module can hand to another one.
(module
  (memory (export "mem") 1)
  (table (export "tab") 4 funcref)
  (global (export "counter") (mut i32) (i32.const 100))
  (global (export "answer") i32 (i32.const 42))

  ;; Not exported, and read only by our own code below: a function that reads
  ;; it gives the right answer *only* when entered with this instance.
  (global $secret (mut i32) (i32.const 4242))

  (func $twice (export "twice") (param i32) (result i32)
    local.get 0 local.get 0 i32.add)

  (func $bump (export "bump") (result i32)
    global.get 0
    i32.const 1
    i32.add
    global.set 0
    global.get 0)

  ;; Fills a slot of our own table with one of our own functions, so the
  ;; importer can call it indirectly and land in *this* instance.
  (func (export "install")
    i32.const 0
    ref.func $twice
    table.set)

  ;; B re-exports this one, so it is reached through an export of a module
  ;; that did not define it.
  (func (export "read_secret") (result i32) global.get $secret)

  ;; And B names this as its start function, so it runs when B is
  ;; instantiated -- an imported start, which has no code in B at all.
  (func (export "tick") global.get $secret i32.const 1 i32.add global.set $secret)

  ;; An exception tag, and something that raises it.  A tag is an *instance*
  ;; level object: what crosses a module boundary is its type, and what a
  ;; `catch` compares is the object -- so B's clause naming its own imported
  ;; tag index can only take this exception because both names reach one
  ;; object, and a second instantiation of this module would raise a different
  ;; one that neither of them could catch.
  (tag $oops (export "oops") (param i32))
  (func $raise (export "raise") (param i32) (local.get 0) (throw $oops))
  ;; Raised from two frames down, so the walk crosses a module boundary in the
  ;; middle rather than at its first step.
  (func (export "raise_deep") (param i32)
    (call $raise (i32.add (local.get 0) (i32.const 1))))

  ;; A tag this module keeps to itself, and a frame that catches it.  An
  ;; exception on its way out to the importer steps over a handler list
  ;; belonging to a module that is not the one searching, so the clause it
  ;; finds there must be compared against *this* instance's tag object.
  (tag $mine)
  (func (export "raise_shielded") (param i32)
    (block $h
      (try_table (catch $mine $h) (call $raise (local.get 0)))
      (return))
    (unreachable))

  ;; The same raiser under a signature the importer can tail-call: leaving its
  ;; own frame to get here is what makes a region it left stop applying.
  (func (export "raise_r") (param i32) (result i32)
    (local.get 0) (throw $oops))

  ;; A slot of our own table, entered with our own instance.  We import
  ;; nothing, so this is the only way a call can go from here to the importer
  ;; -- it fills the slot with one of its own functions -- and it is what lets
  ;; a chain alternate between the two instances all the way down.
  (type $unary (func (param i32) (result i32)))
  (func (export "call_slot1") (param i32) (result i32)
    (local.get 0) (i32.const 1) (call_indirect (type $unary)))

  ;; And somewhere to raise an exception object that was caught elsewhere.
  ;; The importer catches one of ours by reference and hands it back, so the
  ;; object crosses the boundary in the other direction and is raised from a
  ;; frame of ours long after the one that made it has gone.
  (func (export "rethrow") (param exnref) (throw_ref (local.get 0)))

  (data (i32.const 0) "hello")
)
