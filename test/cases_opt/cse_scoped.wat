;; Common subexpressions, found by the walk down the dominator tree
;; (opt.c pass_cse): an instruction takes the twin that a block dominating
;; its own holds, and the walk forgets what a subtree put in the table once
;; it is past the subtree.
;;   * dom: the entry's x*y+7 serves both arms and the join -- one multiply;
;;   * join: each arm of an if makes x-y, and the join makes its own, since
;;     on its path one of the arms never ran;
;;   * nest: an outer arm's twins serve the code after the inner arms
;;     inside it, the inner arms' do not serve it, and the other outer arm
;;     makes its own;
;;   * switch: every arm of a br_table makes x*77+y, and so does the code
;;     after it -- seven multiplies, none taken from a sibling;
;;   * loop: the header's (x+y)*i serves the body below it;
;;   * wide: the table grows inside an arm, and what the arm put there
;;     still leaves it with the arm.
;; Answers worked out in Python.
;;
;; shape: dom \s(mul|madd)\s=1
;; shape: switch \s(mul|madd)\s=7
;; run: dom 1234567 891 0 => i32:469822086
;; run: dom 1234567 891 1 => i32:1843278648
;; run: join 1234567 891 0 => i32:38130709
;; run: join 1234567 891 1 => i32:34390596
;; run: join 891 1234567 0 => i32:-498740206
;; run: join 891 1234567 1 => i32:34390620
;; run: nest 1234567 891 0 => i32:7408140
;; run: nest 1234567 891 1 => i32:9876874
;; run: nest 1234567 891 3 => i32:11388619
;; run: nest 1234567 891 5 => i32:9876874
;; run: nest 1234567 891 7 => i32:11112979
;; run: nest 891 1234567 2 => i32:2473980
;; run: switch 1234567 891 0 => i32:190125100
;; run: switch 1234567 891 1 => i32:190125099
;; run: switch 1234567 891 2 => i32:190125098
;; run: switch 1234567 891 3 => i32:855562950
;; run: switch 1234567 891 4 => i32:101003959
;; run: switch 1234567 891 5 => i32:570375300
;; run: switch 1234567 891 6 => i32:95062556
;; run: switch 1234567 891 7 => i32:95062556
;; run: loop 1234567 891 9 => i32:67185218
;; run: loop 1234567 891 1 => i32:1235458
;; run: loop 891 1234567 4 => i32:11678740
;; run: wide 1234567 1 => i32:4940267
;; run: wide 1234567 0 => i32:7410402
;; run: wide -7 1 => i32:1971
;; run: wide 891 0 => i32:8346
(module
  (func $dom (export "dom") (param $x i32) (param $y i32) (param $c i32) (result i32)
    (local $r i32)
    (local.set $r (i32.add (i32.mul (local.get $x) (local.get $y)) (i32.const 7)))
    (if (local.get $c)
      (then (local.set $r (i32.xor (local.get $r)
              (i32.shr_u (i32.add (i32.mul (local.get $x) (local.get $y)) (i32.const 7))
                         (i32.const 1)))))
      (else (local.set $r (i32.sub (local.get $r)
              (i32.shl (i32.add (i32.mul (local.get $x) (local.get $y)) (i32.const 7))
                       (i32.const 5))))))
    (i32.add (local.get $r)
      (i32.rotl (i32.add (i32.mul (local.get $x) (local.get $y)) (i32.const 7)) (i32.const 3))))

  (func $join (export "join") (param $x i32) (param $y i32) (param $c i32) (result i32)
    (local $r i32)
    (if (local.get $c)
      (then (local.set $r (i32.shl (i32.sub (local.get $x) (local.get $y)) (i32.const 2))))
      (else (local.set $r (i32.shr_u (i32.sub (local.get $x) (local.get $y)) (i32.const 3)))))
    (i32.xor (local.get $r) (i32.mul (i32.sub (local.get $x) (local.get $y)) (i32.const 31))))

  (func $nest (export "nest") (param $x i32) (param $y i32) (param $c i32) (result i32)
    (local $r i32) (local $o i32)
    (if (i32.and (local.get $c) (i32.const 1))
      (then
        (local.set $o (i32.or (i32.mul (local.get $x) (i32.const 3)) (local.get $y)))
        (if (i32.and (local.get $c) (i32.const 2))
          (then
            (local.set $r (i32.add (i32.and (local.get $x) (local.get $y)) (i32.const 11)))
            (if (i32.and (local.get $c) (i32.const 4))
              (then (local.set $r (i32.add (local.get $r)
                      (i32.add (i32.and (local.get $x) (local.get $y)) (i32.const 11)))))
              (else (local.set $r (i32.mul (local.get $r)
                      (i32.add (i32.and (local.get $x) (local.get $y)) (i32.const 11)))))))
          (else
            (local.set $r (i32.sub (i32.add (i32.and (local.get $x) (local.get $y)) (i32.const 11))
                                   (local.get $x)))))
        (local.set $r (i32.add (local.get $r)
          (i32.add (local.get $o) (i32.or (i32.mul (local.get $x) (i32.const 3)) (local.get $y)))))
        (local.set $r (i32.xor (local.get $r)
          (i32.add (i32.and (local.get $x) (local.get $y)) (i32.const 11)))))
      (else
        (local.set $r (i32.or (i32.mul (local.get $x) (i32.const 3)) (local.get $y)))
        (local.set $r (i32.add (local.get $r)
          (i32.add (i32.and (local.get $x) (local.get $y)) (i32.const 11))))))
    (i32.add (local.get $r) (i32.or (i32.mul (local.get $x) (i32.const 3)) (local.get $y))))

  (func $switch (export "switch") (param $x i32) (param $y i32) (param $c i32) (result i32)
    (local $r i32)
    (block $out
      (block $d (block $a5 (block $a4 (block $a3 (block $a2 (block $a1 (block $a0
        (br_table $a0 $a1 $a2 $a3 $a4 $a5 $d (local.get $c)))
        (local.set $r (i32.add (i32.mul (local.get $x) (i32.const 77)) (local.get $y)))
        (br $out))
        (local.set $r (i32.sub (i32.add (i32.mul (local.get $x) (i32.const 77)) (local.get $y)) (i32.const 1)))
        (br $out))
        (local.set $r (i32.xor (i32.add (i32.mul (local.get $x) (i32.const 77)) (local.get $y)) (i32.const 2)))
        (br $out))
        (local.set $r (i32.rotl (i32.add (i32.mul (local.get $x) (i32.const 77)) (local.get $y)) (i32.const 3)))
        (br $out))
        (local.set $r (i32.shr_u (i32.add (i32.mul (local.get $x) (i32.const 77)) (local.get $y)) (i32.const 4)))
        (br $out))
        (local.set $r (i32.mul (i32.add (i32.mul (local.get $x) (i32.const 77)) (local.get $y)) (i32.const 5)))
        (br $out))
      (local.set $r (i32.const 6)))
    (i32.add (local.get $r) (i32.add (i32.mul (local.get $x) (i32.const 77)) (local.get $y))))

  (func $loop (export "loop") (param $x i32) (param $y i32) (param $n i32) (result i32)
    (local $i i32) (local $r i32)
    (loop $l
      (local.set $r (i32.add (local.get $r) (i32.mul (i32.add (local.get $x) (local.get $y)) (local.get $i))))
      (if (i32.and (local.get $i) (i32.const 1))
        (then (local.set $r (i32.xor (local.get $r) (i32.mul (i32.add (local.get $x) (local.get $y)) (local.get $i))))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br_if $l (i32.lt_u (local.get $i) (local.get $n))))
    (i32.add (local.get $r) (i32.mul (i32.add (local.get $x) (local.get $y)) (local.get $i))))

  ;; a table grown in an arm: the walk forgets the arm's entries once past
  ;; it, wherever the growth moved them, so the other arm and the join make
  ;; their own x+500
  (func $wide (export "wide") (param $x i32) (param $c i32) (result i32)
    (local $s i32)
    (if (local.get $c)
      (then
      (local.set $s (i32.mul (i32.add (local.get $x) (i32.const 500)) (i32.const 3)))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 1))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 2))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 3))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 4))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 5))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 6))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 7))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 8))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 9))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 10))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 11))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 12))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 13))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 14))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 15))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 16))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 17))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 18))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 19))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 20))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 21))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 22))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 23))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 24))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 25))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 26))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 27))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 28))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 29))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 30))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 31))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 32))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 33))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 34))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 35))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 36))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 37))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 38))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 39))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 40))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 41))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 42))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 43))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 44))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 45))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 46))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 47))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 48))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 49))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 50))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 51))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 52))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 53))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 54))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 55))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 56))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 57))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 58))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 59))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 60))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 61))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 62))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 63))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 64))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 65))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 66))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 67))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 68))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 69))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 70))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 71))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 72))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 73))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 74))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 75))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 76))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 77))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 78))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 79))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 80))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 81))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 82))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 83))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 84))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 85))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 86))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 87))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 88))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 89))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 90))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 91))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 92))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 93))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 94))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 95))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 96))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 97))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 98))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 99))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 100))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 101))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 102))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 103))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 104))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 105))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 106))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 107))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 108))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 109))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 110))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 111))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 112))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 113))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 114))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 115))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 116))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 117))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 118))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 119))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 120))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 121))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 122))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 123))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 124))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 125))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 126))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 127))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 128))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 129))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 130))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 131))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 132))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 133))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 134))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 135))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 136))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 137))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 138))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 139))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 140))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 141))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 142))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 143))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 144))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 145))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 146))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 147))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 148))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 149))))
      (local.set $s (i32.xor (local.get $s) (i32.add (local.get $x) (i32.const 150)))))
      (else
        (local.set $s (i32.mul (i32.add (local.get $x) (i32.const 500)) (i32.const 5)))))
    (i32.add (local.get $s) (i32.add (local.get $x) (i32.const 500))))
)
