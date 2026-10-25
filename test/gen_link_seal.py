"""The modules of the link test for sealing, one per way a reference can cross
a module's boundary.

warmc decides a cast inline from the object's type slot only in a module no
other module's object can reach (compile.c module_gc_sealed), and the runtime
hands each module a range of type ids of its own -- so another module's
object fails every slot test, one of a structurally equal type too, which
ref.cast must let through.  Each module mN below lets references cross its
boundary one way only, and casts a $box of another module's that comes in
that way, beside one of its own: if warmc took it for sealed, that cast
would trap.

P is the other module for m1 to m5, which import from it; R is the other
module for m6 to m11, and imports from them.  m11 is reached through P too:
an exported tag brings an object in only as an exception thrown into the
module, so it calls P, which calls the function R put in P's table, which
throws m11's tag with a $box of R's.

Each mN's `run` returns 100, its own box's, plus the other module's box's N.
m0 lets nothing cross, and is sealed: its object asks the runtime nothing.

Usage: gen_link_seal.py OUT_DIR
"""
import os
import sys

BOX = '(type $box (struct (field $v i32)))'
FT = '(type $ft (func (param anyref) (result i32)))'

# A box of the module's own, so that its type has a slot, and the cast of it
OWN = '''  (global $own (mut anyref) (ref.null any))
  (func $own (result i32)
    (global.set $own (struct.new $box (i32.const 100)))
    (struct.get $box $v (ref.cast (ref $box) (global.get $own))))
  (func $cast (param $x anyref) (result i32)
    (struct.get $box $v (ref.cast (ref $box) (local.get $x))))'''

# A function of the module's that casts what it is called with
TAKE = '''  (func $take (type $ft) (param $x anyref) (result i32)
    (i32.add (call $own) (call $cast (local.get $x))))
  (elem declare func $take)'''

MODULES = {
    # nothing crosses: sealed, its cast of its own box needs no runtime
    "m0": '''
  %s
%s
  (func (export "run") (result i32) (call $own))''' % (BOX, OWN),

    "P": '''
  %s
  %s
  (type $vf (func))
  (func (export "make") (result anyref) (struct.new $box (i32.const 1)))
  (func (export "call_with") (param $f (ref null $ft)) (result i32)
    (call_ref $ft (struct.new $box (i32.const 2)) (local.get $f)))
  (global (export "g") anyref (struct.new $box (i32.const 3)))
  (table (export "tab") 1 anyref (struct.new $box (i32.const 4)))
  (tag $t (export "tag") (param anyref))
  (func (export "throw_box") (throw $t (struct.new $box (i32.const 5))))
  (table $ftab (export "ftab") 1 funcref)
  (func (export "poke") (call_indirect $ftab (type $vf) (i32.const 0)))''' % (BOX, FT),

    # an imported function's result
    "m1": '''
  %s
  (import "P" "make" (func $make (result anyref)))
%s
  (func (export "run") (result i32)
    (i32.add (call $own) (call $cast (call $make))))''' % (BOX, OWN),

    # an imported function's argument: a function of the module's, which
    # the other calls with its box
    "m2": '''
  %s
  %s
  (import "P" "call_with" (func $call_with (param (ref null $ft)) (result i32)))
%s
%s
  (func (export "run") (result i32)
    (call $call_with (ref.func $take)))''' % (BOX, FT, OWN, TAKE),

    # an imported global
    "m3": '''
  %s
  (import "P" "g" (global $g anyref))
%s
  (func (export "run") (result i32)
    (i32.add (call $own) (call $cast (global.get $g))))''' % (BOX, OWN),

    # an imported table
    "m4": '''
  %s
  (import "P" "tab" (table $t 1 anyref))
%s
  (func (export "run") (result i32)
    (i32.add (call $own) (call $cast (table.get $t (i32.const 0)))))''' % (BOX, OWN),

    # an imported tag
    "m5": '''
  %s
  (import "P" "tag" (tag $t (param anyref)))
  (import "P" "throw_box" (func $throw))
%s
  (func (export "run") (result i32)
    (i32.add (call $own)
      (call $cast
        (block $h (result anyref)
          (try_table (catch $t $h) (call $throw))
          (ref.null any)))))''' % (BOX, OWN),

    # an exported function's argument
    "m6": '''
  %s
  %s
%s
%s
  (func (export "take") (param $x anyref) (result i32)
    (call $take (local.get $x)))''' % (BOX, FT, OWN, TAKE),

    # an exported function's result: a function of the module's
    "m7": '''
  %s
  %s
%s
%s
  (func (export "get") (result (ref null $ft)) (ref.func $take))''' % (BOX, FT, OWN, TAKE),

    # an exported global the other module sets
    "m8": '''
  %s
  (global $g (export "g") (mut anyref) (ref.null any))
%s
  (func (export "peek") (result i32)
    (i32.add (call $own) (call $cast (global.get $g))))''' % (BOX, OWN),

    # an exported global that is not mutable, holding a function of the module's
    "m9": '''
  %s
  %s
%s
%s
  (global (export "f") (ref null $ft) (ref.func $take))''' % (BOX, FT, OWN, TAKE),

    # an exported table the other module writes
    "m10": '''
  %s
  (table $t (export "t") 1 anyref)
%s
  (func (export "peek") (result i32)
    (i32.add (call $own) (call $cast (table.get $t (i32.const 0)))))''' % (BOX, OWN),

    # an exported tag, thrown into the module with the other's box
    "m11": '''
  %s
  (import "P" "poke" (func $poke))
  (tag $e (export "e") (param anyref))
%s
  (func (export "run") (result i32)
    (i32.add (call $own)
      (call $cast
        (block $h (result anyref)
          (try_table (catch $e $h) (call $poke))
          (ref.null any)))))''' % (BOX, OWN),

    "R": '''
  %s
  %s
  (type $vf (func))
  (import "m6" "take" (func $take6 (param anyref) (result i32)))
  (import "m7" "get" (func $get7 (result (ref null $ft))))
  (import "m8" "g" (global $g8 (mut anyref)))
  (import "m8" "peek" (func $peek8 (result i32)))
  (import "m9" "f" (global $f9 (ref null $ft)))
  (import "m10" "t" (table $t10 1 anyref))
  (import "m10" "peek" (func $peek10 (result i32)))
  (import "m11" "e" (tag $e11 (param anyref)))
  (import "P" "ftab" (table $ftab 1 funcref))
  (func (export "run6") (result i32) (call $take6 (struct.new $box (i32.const 6))))
  (func (export "run7") (result i32)
    (call_ref $ft (struct.new $box (i32.const 7)) (call $get7)))
  (func (export "run8") (result i32)
    (global.set $g8 (struct.new $box (i32.const 8)))
    (call $peek8))
  (func (export "run9") (result i32)
    (call_ref $ft (struct.new $box (i32.const 9)) (global.get $f9)))
  (func (export "run10") (result i32)
    (table.set $t10 (i32.const 0) (struct.new $box (i32.const 10)))
    (call $peek10))
  (func $throw11 (type $vf) (throw $e11 (struct.new $box (i32.const 11))))
  (elem declare func $throw11)
  (func (export "arm11") (table.set $ftab (i32.const 0) (ref.func $throw11)))''' % (BOX, FT),
}


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    for name, body in MODULES.items():
        with open(os.path.join(out, "seal_%s.wat" % name), "w") as f:
            f.write(";; Generated by test/gen_link_seal.py -- do not edit.\n(module%s\n)\n" % body)


if __name__ == "__main__":
    main()
