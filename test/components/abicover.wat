;; The shapes the emitter decides between, which random WIT does not reach.
;;
;; test/run_adapter.py executes the emitted load/store against the reference,
;; but only over whatever types the corpus happens to contain -- and wit-smith
;; produced no variant that widens a 32-bit case into a 64-bit slot, so
;; `i64.extend_i32_u` could be written as `_s` and every test still passed.
;; Each type here exists to make one of the emitter's choices observable.
(component
  ;; Sign extension on the way out of memory: s8 and u8 share a width and
  ;; differ only in the load, and the same for s16/u16.
  (type $narrow (record (field "a" s8) (field "b" u8)
                        (field "c" s16) (field "d" u16)))

  ;; Padding.  A u8 followed by a u64 puts seven bytes between them, and a
  ;; walk that forgets to realign writes the second field over the first.
  (type $padded (record (field "a" u8) (field "b" u64) (field "c" u8)
                        (field "d" u16) (field "e" u32)))

  ;; join(i32, i64) = i64, so the u32 case is widened -- unsigned, which only
  ;; a value with the top bit set can tell from signed.
  (type $widen (variant (case "small" u32) (case "big" u64)))

  ;; join(f32, i32) = i32: the float is carried as its bits, so this is a
  ;; reinterpret and not a conversion.  Getting that wrong turns 1.5 into 1.
  (type $bits (variant (case "f" f32) (case "i" s32)))

  ;; join(f32, f64) = i64: reinterpret to i32, then widen.  Two steps, and
  ;; the order of them is observable.
  (type $bits64 (variant (case "a" f32) (case "b" f64)))

  ;; A variant inside a variant's case: the inner discriminant occupies a slot
  ;; that the outer join may have widened, so it is coerced like any payload.
  (type $inner (variant (case "x" u32) (case "y" f64)))
  (type $nested (variant (case "p" $inner) (case "q" u8) (case "r")))

  ;; Every discriminant width the rule can pick, and the boundaries: 1 case
  ;; is still a byte, 256 is the last byte, 257 the first two-byte one.  The
  ;; large ones are enums so the file stays readable.
  (type $one (variant (case "only" u16)))
  (type $e2 (enum "a" "b"))

  ;; Flags at each storage width and at the boundaries between them.
  (type $f1 (flags "a"))
  (type $f8 (flags "a" "b" "c" "d" "e" "f" "g" "h"))
  (type $f9 (flags "a" "b" "c" "d" "e" "f" "g" "h" "i"))
  (type $f16 (flags "a" "b" "c" "d" "e" "f" "g" "h"
                    "i" "j" "k" "l" "m" "n" "o" "p"))
  (type $f17 (flags "a" "b" "c" "d" "e" "f" "g" "h"
                    "i" "j" "k" "l" "m" "n" "o" "p" "q"))

  ;; option and result are variants after despecialisation, and the payload
  ;; offset of an option<u64> is 8 while its discriminant is one byte.
  (type $ou (option u64))
  (type $of (option f32))
  (type $rr (result u32 (error f64)))
  (type $rn (result))

  ;; A tuple of the above, so the offsets compose rather than each being
  ;; checked only at zero.
  (type $mix (tuple u8 $widen u16 $bits f64 $ou))

  ;; 257 cases, which is the first count whose discriminant is two bytes wide.
  ;; The rule picks u8 up to 256 and u16 up to 65536, so nothing below this
  ;; can tell a one-byte load from a two-byte one -- and the corpus had
  ;; nothing above 20.
  (type $e257 (enum "c0" "c1" "c2" "c3" "c4" "c5" "c6" "c7" "c8" "c9" "c10"
                    "c11" "c12" "c13" "c14" "c15" "c16" "c17" "c18" "c19"
                    "c20" "c21" "c22" "c23" "c24" "c25" "c26" "c27" "c28"
                    "c29" "c30" "c31" "c32" "c33" "c34" "c35" "c36" "c37"
                    "c38" "c39" "c40" "c41" "c42" "c43" "c44" "c45" "c46"
                    "c47" "c48" "c49" "c50" "c51" "c52" "c53" "c54" "c55"
                    "c56" "c57" "c58" "c59" "c60" "c61" "c62" "c63" "c64"
                    "c65" "c66" "c67" "c68" "c69" "c70" "c71" "c72" "c73"
                    "c74" "c75" "c76" "c77" "c78" "c79" "c80" "c81" "c82"
                    "c83" "c84" "c85" "c86" "c87" "c88" "c89" "c90" "c91"
                    "c92" "c93" "c94" "c95" "c96" "c97" "c98" "c99" "c100"
                    "c101" "c102" "c103" "c104" "c105" "c106" "c107" "c108"
                    "c109" "c110" "c111" "c112" "c113" "c114" "c115" "c116"
                    "c117" "c118" "c119" "c120" "c121" "c122" "c123" "c124"
                    "c125" "c126" "c127" "c128" "c129" "c130" "c131" "c132"
                    "c133" "c134" "c135" "c136" "c137" "c138" "c139" "c140"
                    "c141" "c142" "c143" "c144" "c145" "c146" "c147" "c148"
                    "c149" "c150" "c151" "c152" "c153" "c154" "c155" "c156"
                    "c157" "c158" "c159" "c160" "c161" "c162" "c163" "c164"
                    "c165" "c166" "c167" "c168" "c169" "c170" "c171" "c172"
                    "c173" "c174" "c175" "c176" "c177" "c178" "c179" "c180"
                    "c181" "c182" "c183" "c184" "c185" "c186" "c187" "c188"
                    "c189" "c190" "c191" "c192" "c193" "c194" "c195" "c196"
                    "c197" "c198" "c199" "c200" "c201" "c202" "c203" "c204"
                    "c205" "c206" "c207" "c208" "c209" "c210" "c211" "c212"
                    "c213" "c214" "c215" "c216" "c217" "c218" "c219" "c220"
                    "c221" "c222" "c223" "c224" "c225" "c226" "c227" "c228"
                    "c229" "c230" "c231" "c232" "c233" "c234" "c235" "c236"
                    "c237" "c238" "c239" "c240" "c241" "c242" "c243" "c244"
                    "c245" "c246" "c247" "c248" "c249" "c250" "c251" "c252"
                    "c253" "c254" "c255" "c256"))

  ;; Lists, and specifically a pair whose allocations must be realigned
  ;; between them.  A list<u8> of odd length leaves the allocator's cursor on
  ;; an odd address, so the list<u64> after it is only correctly placed if the
  ;; allocation asks for the element's alignment -- with every list allocated
  ;; at 1 the corpus still passes, because nothing else ever leaves the cursor
  ;; misaligned.
  (type $lu8 (list u8))
  (type $lu64 (list u64))
  (type $realign (record (field "a" (list u8)) (field "b" (list u64))))
  (type $lnest (list (list u32)))
  (type $lrec (list (record (field "x" u8) (field "y" u64))))
  (type $lvar (list (variant (case "a" u32) (case "b" f64))))
  (type $ovl (option (list s16)))
  (type $lfix (list u16 4))

  ;; Strings, including one on its own -- which is what lets a probe hand the
  ;; conversion bytes that are not utf8 and see whether it notices.
  (type $str string)
  (type $lstr (list string))
  (type $ostr (option string))
  (type $rstr (record (field "a" string) (field "b" u32) (field "c" string)))
)
