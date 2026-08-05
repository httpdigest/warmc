(module
  (memory 1)
  (data (i32.const 0) "\00\00\00\00\01\00\00\00\02\00\00\00\03\00\00\00\11\00\00\00\ff\00\00\00\00\01\00\00\ff\ff\00\00\00\00\01\00\40\42\0f\00\ff\ff\ff\7f\00\00\00\80\01\00\00\80\ef\be\ad\de\fe\ff\ff\ff\ff\ff\ff\ff")
  (func (export "u32_div_u_3") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 3))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.sub (i32.mul (local.get $i) (i32.const 3)) (i32.const 1))
        (i32.const 3))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.mul (local.get $i) (i32.const 3)) (i32.const 3))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.add (i32.mul (local.get $i) (i32.const 3)) (i32.const 1))
        (i32.const 3))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_rem_u_3") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 3))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.sub (i32.mul (local.get $i) (i32.const 3)) (i32.const 1))
        (i32.const 3))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.mul (local.get $i) (i32.const 3)) (i32.const 3))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.add (i32.mul (local.get $i) (i32.const 3)) (i32.const 1))
        (i32.const 3))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_div_u_5") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 5))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.sub (i32.mul (local.get $i) (i32.const 5)) (i32.const 1))
        (i32.const 5))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.mul (local.get $i) (i32.const 5)) (i32.const 5))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.add (i32.mul (local.get $i) (i32.const 5)) (i32.const 1))
        (i32.const 5))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_rem_u_5") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 5))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.sub (i32.mul (local.get $i) (i32.const 5)) (i32.const 1))
        (i32.const 5))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.mul (local.get $i) (i32.const 5)) (i32.const 5))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.add (i32.mul (local.get $i) (i32.const 5)) (i32.const 1))
        (i32.const 5))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_div_u_6") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 6))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.sub (i32.mul (local.get $i) (i32.const 6)) (i32.const 1))
        (i32.const 6))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.mul (local.get $i) (i32.const 6)) (i32.const 6))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.add (i32.mul (local.get $i) (i32.const 6)) (i32.const 1))
        (i32.const 6))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_rem_u_6") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 6))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.sub (i32.mul (local.get $i) (i32.const 6)) (i32.const 1))
        (i32.const 6))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.mul (local.get $i) (i32.const 6)) (i32.const 6))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.add (i32.mul (local.get $i) (i32.const 6)) (i32.const 1))
        (i32.const 6))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_div_u_7") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 7))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.sub (i32.mul (local.get $i) (i32.const 7)) (i32.const 1))
        (i32.const 7))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.mul (local.get $i) (i32.const 7)) (i32.const 7))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.add (i32.mul (local.get $i) (i32.const 7)) (i32.const 1))
        (i32.const 7))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_rem_u_7") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 7))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.sub (i32.mul (local.get $i) (i32.const 7)) (i32.const 1))
        (i32.const 7))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.mul (local.get $i) (i32.const 7)) (i32.const 7))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.add (i32.mul (local.get $i) (i32.const 7)) (i32.const 1))
        (i32.const 7))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_div_u_9") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 9))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.sub (i32.mul (local.get $i) (i32.const 9)) (i32.const 1))
        (i32.const 9))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.mul (local.get $i) (i32.const 9)) (i32.const 9))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.add (i32.mul (local.get $i) (i32.const 9)) (i32.const 1))
        (i32.const 9))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_rem_u_9") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 9))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.sub (i32.mul (local.get $i) (i32.const 9)) (i32.const 1))
        (i32.const 9))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.mul (local.get $i) (i32.const 9)) (i32.const 9))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.add (i32.mul (local.get $i) (i32.const 9)) (i32.const 1))
        (i32.const 9))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_div_u_10") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 10))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.sub (i32.mul (local.get $i) (i32.const 10)) (i32.const 1))
        (i32.const 10))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.mul (local.get $i) (i32.const 10)) (i32.const 10))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.add (i32.mul (local.get $i) (i32.const 10)) (i32.const 1))
        (i32.const 10))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_rem_u_10") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 10))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.sub (i32.mul (local.get $i) (i32.const 10)) (i32.const 1))
        (i32.const 10))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.mul (local.get $i) (i32.const 10)) (i32.const 10))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.add (i32.mul (local.get $i) (i32.const 10)) (i32.const 1))
        (i32.const 10))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_div_u_11") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 11))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.sub (i32.mul (local.get $i) (i32.const 11)) (i32.const 1))
        (i32.const 11))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.mul (local.get $i) (i32.const 11)) (i32.const 11))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.add (i32.mul (local.get $i) (i32.const 11)) (i32.const 1))
        (i32.const 11))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_rem_u_11") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 11))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.sub (i32.mul (local.get $i) (i32.const 11)) (i32.const 1))
        (i32.const 11))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.mul (local.get $i) (i32.const 11)) (i32.const 11))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.add (i32.mul (local.get $i) (i32.const 11)) (i32.const 1))
        (i32.const 11))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_div_u_14") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 14))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.sub (i32.mul (local.get $i) (i32.const 14)) (i32.const 1))
        (i32.const 14))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.mul (local.get $i) (i32.const 14)) (i32.const 14))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.add (i32.mul (local.get $i) (i32.const 14)) (i32.const 1))
        (i32.const 14))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_rem_u_14") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 14))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.sub (i32.mul (local.get $i) (i32.const 14)) (i32.const 1))
        (i32.const 14))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.mul (local.get $i) (i32.const 14)) (i32.const 14))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.add (i32.mul (local.get $i) (i32.const 14)) (i32.const 1))
        (i32.const 14))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_div_u_100") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 100))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.sub (i32.mul (local.get $i) (i32.const 100)) (i32.const 1))
        (i32.const 100))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.mul (local.get $i) (i32.const 100)) (i32.const 100))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.add (i32.mul (local.get $i) (i32.const 100)) (i32.const 1))
        (i32.const 100))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_rem_u_100") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 100))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.sub (i32.mul (local.get $i) (i32.const 100)) (i32.const 1))
        (i32.const 100))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.mul (local.get $i) (i32.const 100)) (i32.const 100))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.add (i32.mul (local.get $i) (i32.const 100)) (i32.const 1))
        (i32.const 100))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_div_u_641") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 641))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.sub (i32.mul (local.get $i) (i32.const 641)) (i32.const 1))
        (i32.const 641))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.mul (local.get $i) (i32.const 641)) (i32.const 641))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.add (i32.mul (local.get $i) (i32.const 641)) (i32.const 1))
        (i32.const 641))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_rem_u_641") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 641))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.sub (i32.mul (local.get $i) (i32.const 641)) (i32.const 1))
        (i32.const 641))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.mul (local.get $i) (i32.const 641)) (i32.const 641))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.add (i32.mul (local.get $i) (i32.const 641)) (i32.const 1))
        (i32.const 641))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_div_u_1000") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 1000))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.sub (i32.mul (local.get $i) (i32.const 1000)) (i32.const 1))
        (i32.const 1000))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.mul (local.get $i) (i32.const 1000)) (i32.const 1000))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.add (i32.mul (local.get $i) (i32.const 1000)) (i32.const 1))
        (i32.const 1000))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_rem_u_1000") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 1000))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.sub (i32.mul (local.get $i) (i32.const 1000)) (i32.const 1))
        (i32.const 1000))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.mul (local.get $i) (i32.const 1000)) (i32.const 1000))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.add (i32.mul (local.get $i) (i32.const 1000)) (i32.const 1))
        (i32.const 1000))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_div_u_65535") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 65535))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.sub (i32.mul (local.get $i) (i32.const 65535)) (i32.const 1))
        (i32.const 65535))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.mul (local.get $i) (i32.const 65535)) (i32.const 65535))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.add (i32.mul (local.get $i) (i32.const 65535)) (i32.const 1))
        (i32.const 65535))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_rem_u_65535") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 65535))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.sub (i32.mul (local.get $i) (i32.const 65535)) (i32.const 1))
        (i32.const 65535))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.mul (local.get $i) (i32.const 65535)) (i32.const 65535))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.add (i32.mul (local.get $i) (i32.const 65535)) (i32.const 1))
        (i32.const 65535))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_div_u_2147483645") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 2147483645))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.sub (i32.mul (local.get $i) (i32.const 2147483645)) (i32.const 1))
        (i32.const 2147483645))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.mul (local.get $i) (i32.const 2147483645)) (i32.const 2147483645))))
      (local.set $s (i32.add (local.get $s) (i32.div_u
        (i32.add (i32.mul (local.get $i) (i32.const 2147483645)) (i32.const 1))
        (i32.const 2147483645))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u32_rem_u_2147483645") (result i32)
    (local $i i32) (local $s i32)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.load (i32.mul (local.get $i) (i32.const 4))) (i32.const 2147483645))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.sub (i32.mul (local.get $i) (i32.const 2147483645)) (i32.const 1))
        (i32.const 2147483645))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.mul (local.get $i) (i32.const 2147483645)) (i32.const 2147483645))))
      (local.set $s (i32.add (local.get $s) (i32.rem_u
        (i32.add (i32.mul (local.get $i) (i32.const 2147483645)) (i32.const 1))
        (i32.const 2147483645))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.get $s))
  (func (export "u64_div_u_3") (result i64)
    (local $i i32) (local $s i64)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i64.add (local.get $s) (i64.div_u
        (i64.load32_u (i32.mul (local.get $i) (i32.const 4))) (i64.const 3))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i64.add (local.get $s) (i64.div_u
        (i64.sub (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 3))
                 (i64.const 1)) (i64.const 3))))
      (local.set $s (i64.add (local.get $s) (i64.div_u
        (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 3))
        (i64.const 3))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.set $s (i64.add (local.get $s) (i64.div_u (i64.const -1) (i64.const 3))))
    (local.set $s (i64.add (local.get $s) (i64.div_u
      (i64.const 0x8000000000000000) (i64.const 3))))
    (local.get $s))
  (func (export "u64_rem_u_3") (result i64)
    (local $i i32) (local $s i64)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i64.add (local.get $s) (i64.rem_u
        (i64.load32_u (i32.mul (local.get $i) (i32.const 4))) (i64.const 3))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i64.add (local.get $s) (i64.rem_u
        (i64.sub (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 3))
                 (i64.const 1)) (i64.const 3))))
      (local.set $s (i64.add (local.get $s) (i64.rem_u
        (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 3))
        (i64.const 3))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.set $s (i64.add (local.get $s) (i64.rem_u (i64.const -1) (i64.const 3))))
    (local.set $s (i64.add (local.get $s) (i64.rem_u
      (i64.const 0x8000000000000000) (i64.const 3))))
    (local.get $s))
  (func (export "u64_div_u_7") (result i64)
    (local $i i32) (local $s i64)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i64.add (local.get $s) (i64.div_u
        (i64.load32_u (i32.mul (local.get $i) (i32.const 4))) (i64.const 7))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i64.add (local.get $s) (i64.div_u
        (i64.sub (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 7))
                 (i64.const 1)) (i64.const 7))))
      (local.set $s (i64.add (local.get $s) (i64.div_u
        (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 7))
        (i64.const 7))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.set $s (i64.add (local.get $s) (i64.div_u (i64.const -1) (i64.const 7))))
    (local.set $s (i64.add (local.get $s) (i64.div_u
      (i64.const 0x8000000000000000) (i64.const 7))))
    (local.get $s))
  (func (export "u64_rem_u_7") (result i64)
    (local $i i32) (local $s i64)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i64.add (local.get $s) (i64.rem_u
        (i64.load32_u (i32.mul (local.get $i) (i32.const 4))) (i64.const 7))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i64.add (local.get $s) (i64.rem_u
        (i64.sub (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 7))
                 (i64.const 1)) (i64.const 7))))
      (local.set $s (i64.add (local.get $s) (i64.rem_u
        (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 7))
        (i64.const 7))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.set $s (i64.add (local.get $s) (i64.rem_u (i64.const -1) (i64.const 7))))
    (local.set $s (i64.add (local.get $s) (i64.rem_u
      (i64.const 0x8000000000000000) (i64.const 7))))
    (local.get $s))
  (func (export "u64_div_u_10") (result i64)
    (local $i i32) (local $s i64)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i64.add (local.get $s) (i64.div_u
        (i64.load32_u (i32.mul (local.get $i) (i32.const 4))) (i64.const 10))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i64.add (local.get $s) (i64.div_u
        (i64.sub (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 10))
                 (i64.const 1)) (i64.const 10))))
      (local.set $s (i64.add (local.get $s) (i64.div_u
        (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 10))
        (i64.const 10))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.set $s (i64.add (local.get $s) (i64.div_u (i64.const -1) (i64.const 10))))
    (local.set $s (i64.add (local.get $s) (i64.div_u
      (i64.const 0x8000000000000000) (i64.const 10))))
    (local.get $s))
  (func (export "u64_rem_u_10") (result i64)
    (local $i i32) (local $s i64)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i64.add (local.get $s) (i64.rem_u
        (i64.load32_u (i32.mul (local.get $i) (i32.const 4))) (i64.const 10))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i64.add (local.get $s) (i64.rem_u
        (i64.sub (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 10))
                 (i64.const 1)) (i64.const 10))))
      (local.set $s (i64.add (local.get $s) (i64.rem_u
        (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 10))
        (i64.const 10))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.set $s (i64.add (local.get $s) (i64.rem_u (i64.const -1) (i64.const 10))))
    (local.set $s (i64.add (local.get $s) (i64.rem_u
      (i64.const 0x8000000000000000) (i64.const 10))))
    (local.get $s))
  (func (export "u64_div_u_1000") (result i64)
    (local $i i32) (local $s i64)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i64.add (local.get $s) (i64.div_u
        (i64.load32_u (i32.mul (local.get $i) (i32.const 4))) (i64.const 1000))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i64.add (local.get $s) (i64.div_u
        (i64.sub (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 1000))
                 (i64.const 1)) (i64.const 1000))))
      (local.set $s (i64.add (local.get $s) (i64.div_u
        (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 1000))
        (i64.const 1000))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.set $s (i64.add (local.get $s) (i64.div_u (i64.const -1) (i64.const 1000))))
    (local.set $s (i64.add (local.get $s) (i64.div_u
      (i64.const 0x8000000000000000) (i64.const 1000))))
    (local.get $s))
  (func (export "u64_rem_u_1000") (result i64)
    (local $i i32) (local $s i64)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i64.add (local.get $s) (i64.rem_u
        (i64.load32_u (i32.mul (local.get $i) (i32.const 4))) (i64.const 1000))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i64.add (local.get $s) (i64.rem_u
        (i64.sub (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 1000))
                 (i64.const 1)) (i64.const 1000))))
      (local.set $s (i64.add (local.get $s) (i64.rem_u
        (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 1000))
        (i64.const 1000))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.set $s (i64.add (local.get $s) (i64.rem_u (i64.const -1) (i64.const 1000))))
    (local.set $s (i64.add (local.get $s) (i64.rem_u
      (i64.const 0x8000000000000000) (i64.const 1000))))
    (local.get $s))
  (func (export "u64_div_u_4294967295") (result i64)
    (local $i i32) (local $s i64)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i64.add (local.get $s) (i64.div_u
        (i64.load32_u (i32.mul (local.get $i) (i32.const 4))) (i64.const 4294967295))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i64.add (local.get $s) (i64.div_u
        (i64.sub (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 4294967295))
                 (i64.const 1)) (i64.const 4294967295))))
      (local.set $s (i64.add (local.get $s) (i64.div_u
        (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 4294967295))
        (i64.const 4294967295))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.set $s (i64.add (local.get $s) (i64.div_u (i64.const -1) (i64.const 4294967295))))
    (local.set $s (i64.add (local.get $s) (i64.div_u
      (i64.const 0x8000000000000000) (i64.const 4294967295))))
    (local.get $s))
  (func (export "u64_rem_u_4294967295") (result i64)
    (local $i i32) (local $s i64)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i64.add (local.get $s) (i64.rem_u
        (i64.load32_u (i32.mul (local.get $i) (i32.const 4))) (i64.const 4294967295))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i64.add (local.get $s) (i64.rem_u
        (i64.sub (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 4294967295))
                 (i64.const 1)) (i64.const 4294967295))))
      (local.set $s (i64.add (local.get $s) (i64.rem_u
        (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 4294967295))
        (i64.const 4294967295))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.set $s (i64.add (local.get $s) (i64.rem_u (i64.const -1) (i64.const 4294967295))))
    (local.set $s (i64.add (local.get $s) (i64.rem_u
      (i64.const 0x8000000000000000) (i64.const 4294967295))))
    (local.get $s))
  (func (export "u64_div_u_4294967303") (result i64)
    (local $i i32) (local $s i64)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i64.add (local.get $s) (i64.div_u
        (i64.load32_u (i32.mul (local.get $i) (i32.const 4))) (i64.const 4294967303))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i64.add (local.get $s) (i64.div_u
        (i64.sub (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 4294967303))
                 (i64.const 1)) (i64.const 4294967303))))
      (local.set $s (i64.add (local.get $s) (i64.div_u
        (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 4294967303))
        (i64.const 4294967303))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.set $s (i64.add (local.get $s) (i64.div_u (i64.const -1) (i64.const 4294967303))))
    (local.set $s (i64.add (local.get $s) (i64.div_u
      (i64.const 0x8000000000000000) (i64.const 4294967303))))
    (local.get $s))
  (func (export "u64_rem_u_4294967303") (result i64)
    (local $i i32) (local $s i64)
    (block $a (loop $la
      (br_if $a (i32.ge_u (local.get $i) (i32.const 16)))
      (local.set $s (i64.add (local.get $s) (i64.rem_u
        (i64.load32_u (i32.mul (local.get $i) (i32.const 4))) (i64.const 4294967303))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $la)))
    (local.set $i (i32.const 1))
    (block $b (loop $lb
      (br_if $b (i32.gt_u (local.get $i) (i32.const 20)))
      (local.set $s (i64.add (local.get $s) (i64.rem_u
        (i64.sub (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 4294967303))
                 (i64.const 1)) (i64.const 4294967303))))
      (local.set $s (i64.add (local.get $s) (i64.rem_u
        (i64.mul (i64.extend_i32_u (local.get $i)) (i64.const 4294967303))
        (i64.const 4294967303))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $lb)))
    (local.set $s (i64.add (local.get $s) (i64.rem_u (i64.const -1) (i64.const 4294967303))))
    (local.set $s (i64.add (local.get $s) (i64.rem_u
      (i64.const 0x8000000000000000) (i64.const 4294967303))))
    (local.get $s))
)
