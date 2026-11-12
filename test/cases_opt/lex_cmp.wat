;; Two pairs compared by their first members and, where those are equal, by
;; their second -- `a.m == b.m ? a.n < b.n : a.m < b.m`, a select of two
;; compares on a third or three branches -- are one compare of two keys,
;; each the first member above the second: a value whose low bits are the
;; first and the rest the second, rotated; one whose high bits are the first,
;; as it is; two constants, one; anything else put together with a shift and
;; an or, where the other key is one of those (opt.c lx_match,
;; fold_lex_select, lx_branch).  From test/gen_lex_cmp.py, which works out
;; every answer from the test as it is written.
;;
;;   rot        the first members the low halves of two i64s, the second the
;;             high: the test Zig's priority queue makes of two entries, as
;;             one compare of the two rotated
;;   rotne      the same on a ne, its arms the other way round
;;   rotge      greater or equal, both compares: the keys' the other way
;;             round and not strict
;;   strict     the firsts' compare not strict and the seconds' strict: as
;;             strict as the seconds'
;;   loose      the firsts' strict and the seconds' not
;;   swapped    the firsts' compare with its operands the other way round
;;             from the test of whether they are equal
;;   hi32       the second members as i32s, the high halves wrapped
;;   masked     the first members as i64s, the low halves masked
;;   cross      the seconds' compare the other way from the firsts': each key
;;             would be one value's first member and the other's second,
;;             which no rotation is, and the test stays
;;   same       the first members the high halves: the compare of the two
;;             values themselves, no rotation
;;   samesg     the first members the high halves as signed i32s: the values'
;;             signed compare
;;   rotsg      the first members the low halves, signed: the rotated values'
;;             signed compare
;;   nsigned    the second members signed i32s that may be negative: no
;;             unsigned compare of the keys' low bits is theirs, and the test
;;             stays
;;   npos       the second members compared signed as i64s, which a high half
;;             is never negative as
;;   mpos       the first members compared signed, and neither negative, 31
;;             bits each: the unsigned compare
;;   uses       the firsts' compare read once more, by the result: it stays,
;;             and so do the others
;;   other      the firsts' compare of one of them and a third value: no
;;             compare of what is tested for equality
;;   eqarm      an arm that is no less or greater but another test for
;;             equality
;;   half16     an i32's low 16 bits first and its high 16 second: the two
;;             rotated by 16, compared as i32s
;;   part8      the low 8 bits first and the other 24 second: rotated by 8
;;   gap        the low 8 bits first and the high 16 second, the 8 between in
;;             neither: no rotation has just those, and the test stays
;;   overlap    the low 16 bits first and all above the low 8 second, 8 bits
;;             in both: the test stays
;;   notmask    the first members an and with a constant that is no mask of
;;             low bits
;;   shrs       the second members an arithmetic shift, the sign in all the
;;             bits above: not the bits alone
;;   ext16      the two 16-bit parts widened and compared as i64s, which are
;;             the i32s' compares: the i32 rotated
;;   shmod      the high part shifted down by 48, which an i32 takes as 16
;;   upper      the bits from 8 up first and those from 24 up second: the
;;             first do not start at the bottom, nor the second
;;   short      the low 8 bits first and the next 8 second, which do not
;;             reach the top
;;   nshift     the high 16 bits first and 16 from bit 8 up second: the
;;             second do not start at the bottom
;;   gaphi      the high 16 bits first and the low 8 second, the 8 between in
;;             neither
;;   shorthi    the low 8 bits second and the next 8 first, which do not
;;             reach the top
;;   sgle       the firsts signed and not strict, the seconds not strict:
;;             signed and not strict
;;   sgge       the firsts signed, greater or equal, the seconds greater
;;   usescond   the test of whether the firsts are equal read once more
;;   usesn      the seconds' compare read once more
;;   gen        one pair an i64's halves, the other two i32s: their key put
;;             together with a shift and an or
;;   gensg      the same with the first members signed
;;   genwide    the other pair's second member an i64 that may have more than
;;             32 bits: it has no place in the key, and the test stays
;;   genfits    that i64 masked to 32 bits: it has
;;   genmwide   the other pair's first member an i64 that may have more than
;;             32 bits, the firsts compared as i64s: the test stays
;;   genconst   the other pair's second member a constant
;;   both       two constants: the key a constant
;;   bothsg     two constants beside a signed first member, the constant
;;             negative
;;   mixed      the other pair an i32's two 16-bit parts, whose own key would
;;             be 32 bits: put together as the i64's is laid out
;;   mixedrev   the same with the i32's parts the lesser side: laid out as
;;             the i64's all the same, the one layout both fit
;;   w32m       an i32's two parts, the first widened and compared with an
;;             i64: the two are no one value's bits as they are compared, and
;;             the test stays
;;   w32n       or the second widened
;;   same8      an i32's high 24 bits first and its low 8 second, the value
;;             itself, beside two i32s masked to as many: put together with a
;;             shift by 8
;;   sg16       an i32's low 16 bits first, compared signed with an i32 that
;;             may be negative: a signed first member has to be the key's top
;;             bits and as wide as its type, and the test stays
;;   sgmix      the firsts signed, the one known not to be negative and the
;;             other an i32 that may be: the signed compare still
;;   nsgmix     the seconds signed, the one known not to be negative and the
;;             other not: the test stays
;;   sgmixrev   the same, the firsts' compare the other way round
;;   nsgmixrev  the same, the seconds' compare the other way round
;;   mixedw     an i64's low 48 bits first and its high 16 second, beside an
;;             i32's two 16-bit parts widened: put together with a shift by 16
;;   mixedc     two i64s, the one's halves and the other's low 16 bits and
;;             high 48: neither fits the other's layout, and the test stays
;;   genwrap    the other pair two i64s wrapped: each widened from its low
;;             half where the key is put together, whatever the i64 has above
;;   two        two pairs neither of which is one value's bits: two keys put
;;             together are no less work, and the test stays
;;   br         the test as branches, the firsts the low halves: one branch
;;             on the rotated values' compare
;;   brne       on a ne
;;   brswap     the seconds' branch going the other way round to the same two
;;             places: the inverse of its compare
;;   brsame     the firsts the high halves: a branch on the values' own
;;             compare
;;   brvals     the two places handed the same values on either way
;;   brconst    handed equal constants, each way its own: one made where the
;;             branch now is
;;   brdiffer   handed different constants by the two ways: they are not the
;;             same two places with the same values, and the branches stay
;;   brstore    the seconds' block stores before it compares, at a place
;;             worked out there: more than a compare, and the branches stay
;;   brstoren   the firsts' block stores before it compares
;;   brhi32     the seconds' block wraps the high halves before it compares
;;             them
;;   brmask16   an i32's high 16 bits first: the seconds' block masks the low
;;             16
;;   brjoin     the seconds' block entered another way too, where a third
;;             value says so: it stays for that way, and the branch on
;;             whether the firsts are equal goes past it, on the keys' compare
;;   brjoinn    the firsts' block entered another way too: the same
;;   brload     the firsts the high half of one value and the low of the
;;             other, whose second member is loaded only where the firsts are
;;             equal, as C reads a struct's next field: a load is not made
;;             where it was not, and the branches stay
;;   brthird    the seconds' block going on to a third place: not the same two
;;   brcross    the seconds' compare the other way from the firsts', as in
;;             cross
;;   breh       the firsts tested for equality ahead of a call a handler
;;             stands behind and branched on after it: the branch is in a
;;             block of its own behind the call's, and the keys are made there
;;   brjoinp    each of the two blocks entered another way with values of its
;;             own -- the seconds' with another second member -- so that the
;;             blocks have parameters, one handed on to the two places: the
;;             keys are made of what the branch on the firsts hands them, and
;;             the branch hands on the same
;;   brgen      outside any loop, one pair an i64's halves and the other two
;;             i32s: a rotation, a shift and an or are more than the first
;;             compare and its branch were, and the branches stay
;;   brgensame  the same with the i64's first member its high half, a key
;;             that is the value itself: one branch
;;   brloop     in a loop, a pair of two i32s the loop does not change
;;             against each of the memory's i64s -- a new entry sifted up
;;             Zig's queue: its key is put together once ahead of the loop,
;;             and a time round is a rotation, a compare and a branch
;;   brloopvar  the same with the pair changing each time round: put together
;;             in the loop it is more than the branches, which stay
;;   brloopc    the pair in the loop an i32 the loop does not change and a
;;             constant
;;   brloopk    the pair in the loop a constant the test of the firsts reads
;;             there and an i32 the loop does not change
;;   brloopvarn only the pair's second member changing each time round,
;;             widened where the seconds are compared: put together in the
;;             loop all the same, and the branches stay
;;   brlooprot  an i64 the loop does not change against a pair of two i32s
;;             loaded each time round: the i64 is rotated once ahead of the
;;             loop, and the pair's key put together in it is no more than
;;             the first compare and its branch were
;;   brloopp    in a loop, the seconds' block entered another way too, its
;;             second member a parameter: the branch on the firsts hands it
;;             one loaded each time round, and a key put together of that in
;;             the loop is more than the branches, which stay
;;   brback     the two places the top of the loop the branches are in,
;;             handed the next index, and its way out, handed another value:
;;             the branch hands them the same
;;   queue      both in one function, over the memory's entries in a copy:
;;             the least of those left picked by the select and moved down,
;;             then each entry counted against every other by the branches
;;
;; stats -O2: lex_cmps=33 lex_brs=19
;; shape: rot \bror\b=2
;; shape: rot \bcsel\b=0
;; shape: rot \bcmp\b=1
;; shape: same \bror\b=0
;; shape: same \bcmp\b=1
;; shape: samesg \bror\b=0
;; shape: mpos \bror\b=0
;; shape: half16 \bror\s+w=2
;; shape: gen \bror\b=1
;; shape: gen \borr\b=1
;; shape: both \bror\b=1
;; shape: mixedrev \bror\b=1
;; shape: br \bror\b=2
;; shape: br \bb\.(eq|ne)\b=0
;; shape: brsame \bror\b=0
;; shape: brsame \bb\.(eq|ne)\b=0
;; shape: brmask16 \bror\b=0
;; shape: brmask16 \bb\.(eq|ne)\b=0
;; loop: brloop \bror\b=1
;; loop: brloop \borr\b=0
;; loop: brloop \bb\.(eq|ne)\b=0
;; loop: brloopc \bror\b=1
;; loop: brloopc \bb\.(eq|ne)\b=0
;; loop: brloopk \bror\b=1
;; loop: brloopk \bb\.(eq|ne)\b=0
;; loop: brlooprot \bror\b=0
;; loop: brlooprot \bb\.(eq|ne)\b=0
;; loop: brback \bror\b=1
;; loop: brback \bb\.(eq|ne)\b=0
;; loop: queue \bcset\b=0
;; loop: queue \bccmp\b=1
;;
;; run: rot 4294967297 4294967297 => i32:100
;; run: rot 21474836481 12884901890 => i32:101
;; run: rot 12884901890 21474836481 => i32:100
;; run: rot 12884901889 21474836481 => i32:101
;; run: rot 21474836481 12884901889 => i32:100
;; run: rot 2147483648 1 => i32:100
;; run: rot 1 2147483648 => i32:101
;; run: rot 9223372036854775815 4294967303 => i32:100
;; run: rot 4294967303 9223372036854775815 => i32:101
;; run: rot 18446744073709551615 18446744073709551615 => i32:100
;; run: rot 18446744069414584320 1 => i32:101
;; run: rot 1 18446744069414584320 => i32:100
;; run: rot 9223372039002259456 9223372034707292160 => i32:100
;; run: rot 11150031900141442680 11150031900141442680 => i32:100
;; run: rot 11150031900141442680 1311768467463790320 => i32:101
;; run: rot 1311768467463790320 11150031900141442680 => i32:100
;; run: rotne 4294967297 4294967297 => i32:0
;; run: rotne 21474836481 12884901890 => i32:1
;; run: rotne 12884901890 21474836481 => i32:0
;; run: rotne 12884901889 21474836481 => i32:1
;; run: rotne 21474836481 12884901889 => i32:0
;; run: rotne 2147483648 1 => i32:0
;; run: rotne 1 2147483648 => i32:1
;; run: rotne 9223372036854775815 4294967303 => i32:0
;; run: rotne 4294967303 9223372036854775815 => i32:1
;; run: rotne 18446744073709551615 18446744073709551615 => i32:0
;; run: rotne 18446744069414584320 1 => i32:1
;; run: rotne 1 18446744069414584320 => i32:0
;; run: rotne 9223372039002259456 9223372034707292160 => i32:0
;; run: rotne 11150031900141442680 11150031900141442680 => i32:0
;; run: rotne 11150031900141442680 1311768467463790320 => i32:1
;; run: rotne 1311768467463790320 11150031900141442680 => i32:0
;; run: rotge 4294967297 4294967297 => i32:1
;; run: rotge 21474836481 12884901890 => i32:0
;; run: rotge 12884901890 21474836481 => i32:1
;; run: rotge 12884901889 21474836481 => i32:0
;; run: rotge 21474836481 12884901889 => i32:1
;; run: rotge 2147483648 1 => i32:1
;; run: rotge 1 2147483648 => i32:0
;; run: rotge 9223372036854775815 4294967303 => i32:1
;; run: rotge 4294967303 9223372036854775815 => i32:0
;; run: rotge 18446744073709551615 18446744073709551615 => i32:1
;; run: rotge 18446744069414584320 1 => i32:0
;; run: rotge 1 18446744069414584320 => i32:1
;; run: rotge 9223372039002259456 9223372034707292160 => i32:1
;; run: rotge 11150031900141442680 11150031900141442680 => i32:1
;; run: rotge 11150031900141442680 1311768467463790320 => i32:0
;; run: rotge 1311768467463790320 11150031900141442680 => i32:1
;; run: strict 4294967297 4294967297 => i32:0
;; run: strict 21474836481 12884901890 => i32:0
;; run: strict 12884901890 21474836481 => i32:1
;; run: strict 12884901889 21474836481 => i32:0
;; run: strict 21474836481 12884901889 => i32:1
;; run: strict 2147483648 1 => i32:1
;; run: strict 1 2147483648 => i32:0
;; run: strict 9223372036854775815 4294967303 => i32:1
;; run: strict 4294967303 9223372036854775815 => i32:0
;; run: strict 18446744073709551615 18446744073709551615 => i32:0
;; run: strict 18446744069414584320 1 => i32:0
;; run: strict 1 18446744069414584320 => i32:1
;; run: strict 9223372039002259456 9223372034707292160 => i32:1
;; run: strict 11150031900141442680 11150031900141442680 => i32:0
;; run: strict 11150031900141442680 1311768467463790320 => i32:0
;; run: strict 1311768467463790320 11150031900141442680 => i32:1
;; run: loose 4294967297 4294967297 => i32:1
;; run: loose 21474836481 12884901890 => i32:1
;; run: loose 12884901890 21474836481 => i32:0
;; run: loose 12884901889 21474836481 => i32:1
;; run: loose 21474836481 12884901889 => i32:0
;; run: loose 2147483648 1 => i32:0
;; run: loose 1 2147483648 => i32:1
;; run: loose 9223372036854775815 4294967303 => i32:0
;; run: loose 4294967303 9223372036854775815 => i32:1
;; run: loose 18446744073709551615 18446744073709551615 => i32:1
;; run: loose 18446744069414584320 1 => i32:1
;; run: loose 1 18446744069414584320 => i32:0
;; run: loose 9223372039002259456 9223372034707292160 => i32:0
;; run: loose 11150031900141442680 11150031900141442680 => i32:1
;; run: loose 11150031900141442680 1311768467463790320 => i32:1
;; run: loose 1311768467463790320 11150031900141442680 => i32:0
;; run: swapped 4294967297 4294967297 => i32:0
;; run: swapped 21474836481 12884901890 => i32:1
;; run: swapped 12884901890 21474836481 => i32:0
;; run: swapped 12884901889 21474836481 => i32:1
;; run: swapped 21474836481 12884901889 => i32:0
;; run: swapped 2147483648 1 => i32:0
;; run: swapped 1 2147483648 => i32:1
;; run: swapped 9223372036854775815 4294967303 => i32:0
;; run: swapped 4294967303 9223372036854775815 => i32:1
;; run: swapped 18446744073709551615 18446744073709551615 => i32:0
;; run: swapped 18446744069414584320 1 => i32:1
;; run: swapped 1 18446744069414584320 => i32:0
;; run: swapped 9223372039002259456 9223372034707292160 => i32:0
;; run: swapped 11150031900141442680 11150031900141442680 => i32:0
;; run: swapped 11150031900141442680 1311768467463790320 => i32:1
;; run: swapped 1311768467463790320 11150031900141442680 => i32:0
;; run: hi32 4294967297 4294967297 => i32:0
;; run: hi32 21474836481 12884901890 => i32:1
;; run: hi32 12884901890 21474836481 => i32:0
;; run: hi32 12884901889 21474836481 => i32:1
;; run: hi32 21474836481 12884901889 => i32:0
;; run: hi32 2147483648 1 => i32:0
;; run: hi32 1 2147483648 => i32:1
;; run: hi32 9223372036854775815 4294967303 => i32:0
;; run: hi32 4294967303 9223372036854775815 => i32:1
;; run: hi32 18446744073709551615 18446744073709551615 => i32:0
;; run: hi32 18446744069414584320 1 => i32:1
;; run: hi32 1 18446744069414584320 => i32:0
;; run: hi32 9223372039002259456 9223372034707292160 => i32:0
;; run: hi32 11150031900141442680 11150031900141442680 => i32:0
;; run: hi32 11150031900141442680 1311768467463790320 => i32:1
;; run: hi32 1311768467463790320 11150031900141442680 => i32:0
;; run: masked 4294967297 4294967297 => i32:0
;; run: masked 21474836481 12884901890 => i32:1
;; run: masked 12884901890 21474836481 => i32:0
;; run: masked 12884901889 21474836481 => i32:1
;; run: masked 21474836481 12884901889 => i32:0
;; run: masked 2147483648 1 => i32:0
;; run: masked 1 2147483648 => i32:1
;; run: masked 9223372036854775815 4294967303 => i32:0
;; run: masked 4294967303 9223372036854775815 => i32:1
;; run: masked 18446744073709551615 18446744073709551615 => i32:0
;; run: masked 18446744069414584320 1 => i32:1
;; run: masked 1 18446744069414584320 => i32:0
;; run: masked 9223372039002259456 9223372034707292160 => i32:0
;; run: masked 11150031900141442680 11150031900141442680 => i32:0
;; run: masked 11150031900141442680 1311768467463790320 => i32:1
;; run: masked 1311768467463790320 11150031900141442680 => i32:0
;; run: cross 4294967297 4294967297 => i32:0
;; run: cross 21474836481 12884901890 => i32:1
;; run: cross 12884901890 21474836481 => i32:0
;; run: cross 12884901889 21474836481 => i32:0
;; run: cross 21474836481 12884901889 => i32:1
;; run: cross 2147483648 1 => i32:0
;; run: cross 1 2147483648 => i32:1
;; run: cross 9223372036854775815 4294967303 => i32:1
;; run: cross 4294967303 9223372036854775815 => i32:0
;; run: cross 18446744073709551615 18446744073709551615 => i32:0
;; run: cross 18446744069414584320 1 => i32:1
;; run: cross 1 18446744069414584320 => i32:0
;; run: cross 9223372039002259456 9223372034707292160 => i32:1
;; run: cross 11150031900141442680 11150031900141442680 => i32:0
;; run: cross 11150031900141442680 1311768467463790320 => i32:1
;; run: cross 1311768467463790320 11150031900141442680 => i32:0
;; run: same 4294967297 4294967297 => i32:0
;; run: same 4294967301 8589934595 => i32:1
;; run: same 8589934595 4294967301 => i32:0
;; run: same 4294967299 4294967301 => i32:1
;; run: same 4294967301 4294967299 => i32:0
;; run: same 9223372036854775808 4294967296 => i32:0
;; run: same 4294967296 9223372036854775808 => i32:1
;; run: same 32212254720 30064771073 => i32:0
;; run: same 30064771073 32212254720 => i32:1
;; run: same 18446744073709551615 18446744073709551615 => i32:0
;; run: same 4294967295 4294967296 => i32:1
;; run: same 4294967296 4294967295 => i32:0
;; run: same 9223372039002259456 9223372039002259455 => i32:0
;; run: same 1311768467463790320 1311768467463790320 => i32:0
;; run: same 1311768467463790320 11150031900141442680 => i32:1
;; run: same 11150031900141442680 1311768467463790320 => i32:0
;; run: samesg 4294967297 4294967297 => i32:1
;; run: samesg 4294967301 8589934595 => i32:1
;; run: samesg 8589934595 4294967301 => i32:0
;; run: samesg 4294967299 4294967301 => i32:1
;; run: samesg 4294967301 4294967299 => i32:0
;; run: samesg 9223372036854775808 4294967296 => i32:1
;; run: samesg 4294967296 9223372036854775808 => i32:0
;; run: samesg 32212254720 30064771073 => i32:0
;; run: samesg 30064771073 32212254720 => i32:1
;; run: samesg 18446744073709551615 18446744073709551615 => i32:1
;; run: samesg 4294967295 4294967296 => i32:1
;; run: samesg 4294967296 4294967295 => i32:0
;; run: samesg 9223372039002259456 9223372039002259455 => i32:0
;; run: samesg 1311768467463790320 1311768467463790320 => i32:1
;; run: samesg 1311768467463790320 11150031900141442680 => i32:0
;; run: samesg 11150031900141442680 1311768467463790320 => i32:1
;; run: rotsg 4294967297 4294967297 => i32:1
;; run: rotsg 21474836481 12884901890 => i32:0
;; run: rotsg 12884901890 21474836481 => i32:1
;; run: rotsg 12884901889 21474836481 => i32:0
;; run: rotsg 21474836481 12884901889 => i32:1
;; run: rotsg 2147483648 1 => i32:0
;; run: rotsg 1 2147483648 => i32:1
;; run: rotsg 9223372036854775815 4294967303 => i32:1
;; run: rotsg 4294967303 9223372036854775815 => i32:0
;; run: rotsg 18446744073709551615 18446744073709551615 => i32:1
;; run: rotsg 18446744069414584320 1 => i32:0
;; run: rotsg 1 18446744069414584320 => i32:1
;; run: rotsg 9223372039002259456 9223372034707292160 => i32:1
;; run: rotsg 11150031900141442680 11150031900141442680 => i32:1
;; run: rotsg 11150031900141442680 1311768467463790320 => i32:1
;; run: rotsg 1311768467463790320 11150031900141442680 => i32:0
;; run: nsigned 4294967297 4294967297 => i32:0
;; run: nsigned 21474836481 12884901890 => i32:1
;; run: nsigned 12884901890 21474836481 => i32:0
;; run: nsigned 12884901889 21474836481 => i32:1
;; run: nsigned 21474836481 12884901889 => i32:0
;; run: nsigned 2147483648 1 => i32:0
;; run: nsigned 1 2147483648 => i32:1
;; run: nsigned 9223372036854775815 4294967303 => i32:1
;; run: nsigned 4294967303 9223372036854775815 => i32:0
;; run: nsigned 18446744073709551615 18446744073709551615 => i32:0
;; run: nsigned 18446744069414584320 1 => i32:1
;; run: nsigned 1 18446744069414584320 => i32:0
;; run: nsigned 9223372039002259456 9223372034707292160 => i32:1
;; run: nsigned 11150031900141442680 11150031900141442680 => i32:0
;; run: nsigned 11150031900141442680 1311768467463790320 => i32:1
;; run: nsigned 1311768467463790320 11150031900141442680 => i32:0
;; run: npos 4294967297 4294967297 => i32:0
;; run: npos 21474836481 12884901890 => i32:1
;; run: npos 12884901890 21474836481 => i32:0
;; run: npos 12884901889 21474836481 => i32:1
;; run: npos 21474836481 12884901889 => i32:0
;; run: npos 2147483648 1 => i32:0
;; run: npos 1 2147483648 => i32:1
;; run: npos 9223372036854775815 4294967303 => i32:0
;; run: npos 4294967303 9223372036854775815 => i32:1
;; run: npos 18446744073709551615 18446744073709551615 => i32:0
;; run: npos 18446744069414584320 1 => i32:1
;; run: npos 1 18446744069414584320 => i32:0
;; run: npos 9223372039002259456 9223372034707292160 => i32:0
;; run: npos 11150031900141442680 11150031900141442680 => i32:0
;; run: npos 11150031900141442680 1311768467463790320 => i32:1
;; run: npos 1311768467463790320 11150031900141442680 => i32:0
;; run: mpos 4294967297 4294967297 => i32:200
;; run: mpos 4294967301 8589934595 => i32:201
;; run: mpos 8589934595 4294967301 => i32:200
;; run: mpos 4294967299 4294967301 => i32:201
;; run: mpos 4294967301 4294967299 => i32:200
;; run: mpos 9223372036854775808 4294967296 => i32:200
;; run: mpos 4294967296 9223372036854775808 => i32:201
;; run: mpos 32212254720 30064771073 => i32:200
;; run: mpos 30064771073 32212254720 => i32:201
;; run: mpos 18446744073709551615 18446744073709551615 => i32:200
;; run: mpos 4294967295 4294967296 => i32:201
;; run: mpos 4294967296 4294967295 => i32:200
;; run: mpos 9223372039002259456 9223372039002259455 => i32:200
;; run: mpos 1311768467463790320 1311768467463790320 => i32:200
;; run: mpos 1311768467463790320 11150031900141442680 => i32:201
;; run: mpos 11150031900141442680 1311768467463790320 => i32:200
;; run: mpos 9223372036854775808 1 => i32:200
;; run: mpos 1 9223372036854775808 => i32:201
;; run: mpos 8589934592 8589934591 => i32:200
;; run: uses 4294967297 4294967297 => i32:0
;; run: uses 21474836481 12884901890 => i32:2
;; run: uses 12884901890 21474836481 => i32:0
;; run: uses 12884901889 21474836481 => i32:1
;; run: uses 21474836481 12884901889 => i32:0
;; run: uses 2147483648 1 => i32:0
;; run: uses 1 2147483648 => i32:2
;; run: uses 9223372036854775815 4294967303 => i32:0
;; run: uses 4294967303 9223372036854775815 => i32:1
;; run: uses 18446744073709551615 18446744073709551615 => i32:0
;; run: uses 18446744069414584320 1 => i32:2
;; run: uses 1 18446744069414584320 => i32:0
;; run: uses 9223372039002259456 9223372034707292160 => i32:0
;; run: uses 11150031900141442680 11150031900141442680 => i32:0
;; run: uses 11150031900141442680 1311768467463790320 => i32:2
;; run: uses 1311768467463790320 11150031900141442680 => i32:0
;; run: other 4294967297 4294967297 => i32:0
;; run: other 21474836481 12884901890 => i32:1
;; run: other 12884901890 21474836481 => i32:1
;; run: other 12884901889 21474836481 => i32:1
;; run: other 21474836481 12884901889 => i32:0
;; run: other 2147483648 1 => i32:0
;; run: other 1 2147483648 => i32:0
;; run: other 9223372036854775815 4294967303 => i32:0
;; run: other 4294967303 9223372036854775815 => i32:1
;; run: other 18446744073709551615 18446744073709551615 => i32:0
;; run: other 18446744069414584320 1 => i32:0
;; run: other 1 18446744069414584320 => i32:1
;; run: other 9223372039002259456 9223372034707292160 => i32:0
;; run: other 11150031900141442680 11150031900141442680 => i32:0
;; run: other 11150031900141442680 1311768467463790320 => i32:0
;; run: other 1311768467463790320 11150031900141442680 => i32:0
;; run: eqarm 4294967297 4294967297 => i32:1
;; run: eqarm 21474836481 12884901890 => i32:1
;; run: eqarm 12884901890 21474836481 => i32:0
;; run: eqarm 12884901889 21474836481 => i32:0
;; run: eqarm 21474836481 12884901889 => i32:0
;; run: eqarm 2147483648 1 => i32:0
;; run: eqarm 1 2147483648 => i32:1
;; run: eqarm 9223372036854775815 4294967303 => i32:0
;; run: eqarm 4294967303 9223372036854775815 => i32:0
;; run: eqarm 18446744073709551615 18446744073709551615 => i32:1
;; run: eqarm 18446744069414584320 1 => i32:1
;; run: eqarm 1 18446744069414584320 => i32:0
;; run: eqarm 9223372039002259456 9223372034707292160 => i32:0
;; run: eqarm 11150031900141442680 11150031900141442680 => i32:1
;; run: eqarm 11150031900141442680 1311768467463790320 => i32:1
;; run: eqarm 1311768467463790320 11150031900141442680 => i32:0
;; run: half16 65537 65537 => i32:0
;; run: half16 327681 196610 => i32:1
;; run: half16 196610 327681 => i32:0
;; run: half16 196609 327681 => i32:1
;; run: half16 327681 196609 => i32:0
;; run: half16 0 1 => i32:1
;; run: half16 1 0 => i32:0
;; run: half16 7 65543 => i32:1
;; run: half16 65543 7 => i32:0
;; run: half16 4294967295 4294967295 => i32:0
;; run: half16 4294901760 1 => i32:1
;; run: half16 1 4294901760 => i32:0
;; run: half16 0 4294901760 => i32:1
;; run: half16 3740292728 3740292728 => i32:0
;; run: half16 3740292728 1450761968 => i32:1
;; run: half16 1450761968 3740292728 => i32:0
;; run: half16 32768 1 => i32:0
;; run: half16 1 32768 => i32:1
;; run: half16 2147483655 65543 => i32:0
;; run: half16 65543 2147483655 => i32:1
;; run: part8 65537 65537 => i32:1
;; run: part8 327681 196610 => i32:1
;; run: part8 196610 327681 => i32:0
;; run: part8 196609 327681 => i32:1
;; run: part8 327681 196609 => i32:0
;; run: part8 0 1 => i32:1
;; run: part8 1 0 => i32:0
;; run: part8 7 65543 => i32:1
;; run: part8 65543 7 => i32:0
;; run: part8 4294967295 4294967295 => i32:1
;; run: part8 4294901760 1 => i32:1
;; run: part8 1 4294901760 => i32:0
;; run: part8 0 4294901760 => i32:1
;; run: part8 3740292728 3740292728 => i32:1
;; run: part8 3740292728 1450761968 => i32:1
;; run: part8 1450761968 3740292728 => i32:0
;; run: part8 32768 1 => i32:1
;; run: part8 1 32768 => i32:0
;; run: part8 2147483655 65543 => i32:0
;; run: part8 65543 2147483655 => i32:1
;; run: part8 256 511 => i32:1
;; run: part8 511 256 => i32:0
;; run: part8 4278190081 16776961 => i32:0
;; run: part8 16776961 4278190081 => i32:1
;; run: gap 65537 65537 => i32:0
;; run: gap 327681 196610 => i32:1
;; run: gap 196610 327681 => i32:0
;; run: gap 196609 327681 => i32:1
;; run: gap 327681 196609 => i32:0
;; run: gap 0 1 => i32:1
;; run: gap 1 0 => i32:0
;; run: gap 7 65543 => i32:1
;; run: gap 65543 7 => i32:0
;; run: gap 4294967295 4294967295 => i32:0
;; run: gap 4294901760 1 => i32:1
;; run: gap 1 4294901760 => i32:0
;; run: gap 0 4294901760 => i32:1
;; run: gap 3740292728 3740292728 => i32:0
;; run: gap 3740292728 1450761968 => i32:1
;; run: gap 1450761968 3740292728 => i32:0
;; run: gap 32768 1 => i32:1
;; run: gap 1 32768 => i32:0
;; run: gap 2147483655 65543 => i32:0
;; run: gap 65543 2147483655 => i32:1
;; run: gap 65792 130816 => i32:0
;; run: gap 130816 65792 => i32:0
;; run: overlap 65537 65537 => i32:0
;; run: overlap 327681 196610 => i32:1
;; run: overlap 196610 327681 => i32:0
;; run: overlap 196609 327681 => i32:1
;; run: overlap 327681 196609 => i32:0
;; run: overlap 0 1 => i32:1
;; run: overlap 1 0 => i32:0
;; run: overlap 7 65543 => i32:1
;; run: overlap 65543 7 => i32:0
;; run: overlap 4294967295 4294967295 => i32:0
;; run: overlap 4294901760 1 => i32:1
;; run: overlap 1 4294901760 => i32:0
;; run: overlap 0 4294901760 => i32:1
;; run: overlap 3740292728 3740292728 => i32:0
;; run: overlap 3740292728 1450761968 => i32:1
;; run: overlap 1450761968 3740292728 => i32:0
;; run: overlap 32768 1 => i32:0
;; run: overlap 1 32768 => i32:1
;; run: overlap 2147483655 65543 => i32:0
;; run: overlap 65543 2147483655 => i32:1
;; run: notmask 65537 65537 => i32:0
;; run: notmask 327681 196610 => i32:1
;; run: notmask 196610 327681 => i32:0
;; run: notmask 196609 327681 => i32:1
;; run: notmask 327681 196609 => i32:0
;; run: notmask 0 1 => i32:0
;; run: notmask 1 0 => i32:0
;; run: notmask 7 65543 => i32:1
;; run: notmask 65543 7 => i32:0
;; run: notmask 4294967295 4294967295 => i32:0
;; run: notmask 4294901760 1 => i32:0
;; run: notmask 1 4294901760 => i32:1
;; run: notmask 0 4294901760 => i32:1
;; run: notmask 3740292728 3740292728 => i32:0
;; run: notmask 3740292728 1450761968 => i32:1
;; run: notmask 1450761968 3740292728 => i32:0
;; run: notmask 32768 1 => i32:0
;; run: notmask 1 32768 => i32:1
;; run: notmask 2147483655 65543 => i32:0
;; run: notmask 65543 2147483655 => i32:1
;; run: notmask 65537 131072 => i32:1
;; run: notmask 131072 65537 => i32:0
;; run: shrs 65537 65537 => i32:0
;; run: shrs 327681 196610 => i32:1
;; run: shrs 196610 327681 => i32:0
;; run: shrs 196609 327681 => i32:1
;; run: shrs 327681 196609 => i32:0
;; run: shrs 0 1 => i32:1
;; run: shrs 1 0 => i32:0
;; run: shrs 7 65543 => i32:1
;; run: shrs 65543 7 => i32:0
;; run: shrs 4294967295 4294967295 => i32:0
;; run: shrs 4294901760 1 => i32:1
;; run: shrs 1 4294901760 => i32:0
;; run: shrs 0 4294901760 => i32:1
;; run: shrs 3740292728 3740292728 => i32:0
;; run: shrs 3740292728 1450761968 => i32:1
;; run: shrs 1450761968 3740292728 => i32:0
;; run: shrs 32768 1 => i32:0
;; run: shrs 1 32768 => i32:1
;; run: shrs 2147483655 65543 => i32:0
;; run: shrs 65543 2147483655 => i32:1
;; run: ext16 65537 65537 => i32:300
;; run: ext16 327681 196610 => i32:301
;; run: ext16 196610 327681 => i32:300
;; run: ext16 196609 327681 => i32:301
;; run: ext16 327681 196609 => i32:300
;; run: ext16 0 1 => i32:301
;; run: ext16 1 0 => i32:300
;; run: ext16 7 65543 => i32:301
;; run: ext16 65543 7 => i32:300
;; run: ext16 4294967295 4294967295 => i32:300
;; run: ext16 4294901760 1 => i32:301
;; run: ext16 1 4294901760 => i32:300
;; run: ext16 0 4294901760 => i32:301
;; run: ext16 3740292728 3740292728 => i32:300
;; run: ext16 3740292728 1450761968 => i32:301
;; run: ext16 1450761968 3740292728 => i32:300
;; run: ext16 32768 1 => i32:300
;; run: ext16 1 32768 => i32:301
;; run: ext16 2147483655 65543 => i32:300
;; run: ext16 65543 2147483655 => i32:301
;; run: shmod 65537 65537 => i32:400
;; run: shmod 327681 196610 => i32:401
;; run: shmod 196610 327681 => i32:400
;; run: shmod 196609 327681 => i32:401
;; run: shmod 327681 196609 => i32:400
;; run: shmod 0 1 => i32:401
;; run: shmod 1 0 => i32:400
;; run: shmod 7 65543 => i32:401
;; run: shmod 65543 7 => i32:400
;; run: shmod 4294967295 4294967295 => i32:400
;; run: shmod 4294901760 1 => i32:401
;; run: shmod 1 4294901760 => i32:400
;; run: shmod 0 4294901760 => i32:401
;; run: shmod 3740292728 3740292728 => i32:400
;; run: shmod 3740292728 1450761968 => i32:401
;; run: shmod 1450761968 3740292728 => i32:400
;; run: shmod 32768 1 => i32:400
;; run: shmod 1 32768 => i32:401
;; run: shmod 2147483655 65543 => i32:400
;; run: shmod 65543 2147483655 => i32:401
;; run: upper 65537 65537 => i32:0
;; run: upper 327681 196610 => i32:0
;; run: upper 196610 327681 => i32:1
;; run: upper 196609 327681 => i32:1
;; run: upper 327681 196609 => i32:0
;; run: upper 0 1 => i32:0
;; run: upper 1 0 => i32:0
;; run: upper 7 65543 => i32:1
;; run: upper 65543 7 => i32:0
;; run: upper 4294967295 4294967295 => i32:0
;; run: upper 4294901760 1 => i32:0
;; run: upper 1 4294901760 => i32:1
;; run: upper 0 4294901760 => i32:1
;; run: upper 3740292728 3740292728 => i32:0
;; run: upper 3740292728 1450761968 => i32:0
;; run: upper 1450761968 3740292728 => i32:1
;; run: upper 32768 1 => i32:0
;; run: upper 1 32768 => i32:1
;; run: upper 2147483655 65543 => i32:0
;; run: upper 65543 2147483655 => i32:1
;; run: upper 256 511 => i32:0
;; run: upper 511 256 => i32:0
;; run: upper 4278190081 16776961 => i32:0
;; run: upper 16776961 4278190081 => i32:1
;; run: upper 305419896 305419897 => i32:0
;; run: upper 305419896 305485432 => i32:1
;; run: upper 305419896 322197112 => i32:1
;; run: upper 305463160 305398015 => i32:0
;; run: short 65537 65537 => i32:0
;; run: short 327681 196610 => i32:1
;; run: short 196610 327681 => i32:0
;; run: short 196609 327681 => i32:0
;; run: short 327681 196609 => i32:0
;; run: short 0 1 => i32:1
;; run: short 1 0 => i32:0
;; run: short 7 65543 => i32:0
;; run: short 65543 7 => i32:0
;; run: short 4294967295 4294967295 => i32:0
;; run: short 4294901760 1 => i32:1
;; run: short 1 4294901760 => i32:0
;; run: short 0 4294901760 => i32:0
;; run: short 3740292728 3740292728 => i32:0
;; run: short 3740292728 1450761968 => i32:1
;; run: short 1450761968 3740292728 => i32:0
;; run: short 32768 1 => i32:1
;; run: short 1 32768 => i32:0
;; run: short 2147483655 65543 => i32:0
;; run: short 65543 2147483655 => i32:0
;; run: short 256 511 => i32:1
;; run: short 511 256 => i32:0
;; run: short 4278190081 16776961 => i32:1
;; run: short 16776961 4278190081 => i32:0
;; run: short 305419896 305419897 => i32:1
;; run: short 305419896 305485432 => i32:0
;; run: short 305419896 322197112 => i32:0
;; run: short 305463160 305398015 => i32:1
;; run: nshift 65537 65537 => i32:0
;; run: nshift 327681 196610 => i32:0
;; run: nshift 196610 327681 => i32:1
;; run: nshift 196609 327681 => i32:1
;; run: nshift 327681 196609 => i32:0
;; run: nshift 0 1 => i32:0
;; run: nshift 1 0 => i32:0
;; run: nshift 7 65543 => i32:1
;; run: nshift 65543 7 => i32:0
;; run: nshift 4294967295 4294967295 => i32:0
;; run: nshift 4294901760 1 => i32:0
;; run: nshift 1 4294901760 => i32:1
;; run: nshift 0 4294901760 => i32:1
;; run: nshift 3740292728 3740292728 => i32:0
;; run: nshift 3740292728 1450761968 => i32:0
;; run: nshift 1450761968 3740292728 => i32:1
;; run: nshift 32768 1 => i32:0
;; run: nshift 1 32768 => i32:1
;; run: nshift 2147483655 65543 => i32:0
;; run: nshift 65543 2147483655 => i32:1
;; run: nshift 256 511 => i32:0
;; run: nshift 511 256 => i32:0
;; run: nshift 4278190081 16776961 => i32:0
;; run: nshift 16776961 4278190081 => i32:1
;; run: nshift 305419896 305419897 => i32:0
;; run: nshift 305419896 305485432 => i32:1
;; run: nshift 305419896 322197112 => i32:1
;; run: nshift 305463160 305398015 => i32:0
;; run: gaphi 65537 65537 => i32:0
;; run: gaphi 327681 196610 => i32:0
;; run: gaphi 196610 327681 => i32:1
;; run: gaphi 196609 327681 => i32:1
;; run: gaphi 327681 196609 => i32:0
;; run: gaphi 0 1 => i32:1
;; run: gaphi 1 0 => i32:0
;; run: gaphi 7 65543 => i32:1
;; run: gaphi 65543 7 => i32:0
;; run: gaphi 4294967295 4294967295 => i32:0
;; run: gaphi 4294901760 1 => i32:0
;; run: gaphi 1 4294901760 => i32:1
;; run: gaphi 0 4294901760 => i32:1
;; run: gaphi 3740292728 3740292728 => i32:0
;; run: gaphi 3740292728 1450761968 => i32:0
;; run: gaphi 1450761968 3740292728 => i32:1
;; run: gaphi 32768 1 => i32:1
;; run: gaphi 1 32768 => i32:0
;; run: gaphi 2147483655 65543 => i32:0
;; run: gaphi 65543 2147483655 => i32:1
;; run: gaphi 256 511 => i32:1
;; run: gaphi 511 256 => i32:0
;; run: gaphi 4278190081 16776961 => i32:0
;; run: gaphi 16776961 4278190081 => i32:1
;; run: gaphi 305419896 305419897 => i32:1
;; run: gaphi 305419896 305485432 => i32:1
;; run: gaphi 305419896 322197112 => i32:1
;; run: gaphi 305463160 305398015 => i32:1
;; run: shorthi 65537 65537 => i32:0
;; run: shorthi 327681 196610 => i32:1
;; run: shorthi 196610 327681 => i32:0
;; run: shorthi 196609 327681 => i32:0
;; run: shorthi 327681 196609 => i32:0
;; run: shorthi 0 1 => i32:1
;; run: shorthi 1 0 => i32:0
;; run: shorthi 7 65543 => i32:0
;; run: shorthi 65543 7 => i32:0
;; run: shorthi 4294967295 4294967295 => i32:0
;; run: shorthi 4294901760 1 => i32:1
;; run: shorthi 1 4294901760 => i32:0
;; run: shorthi 0 4294901760 => i32:0
;; run: shorthi 3740292728 3740292728 => i32:0
;; run: shorthi 3740292728 1450761968 => i32:1
;; run: shorthi 1450761968 3740292728 => i32:0
;; run: shorthi 32768 1 => i32:0
;; run: shorthi 1 32768 => i32:1
;; run: shorthi 2147483655 65543 => i32:0
;; run: shorthi 65543 2147483655 => i32:0
;; run: shorthi 256 511 => i32:1
;; run: shorthi 511 256 => i32:0
;; run: shorthi 4278190081 16776961 => i32:1
;; run: shorthi 16776961 4278190081 => i32:0
;; run: shorthi 305419896 305419897 => i32:1
;; run: shorthi 305419896 305485432 => i32:0
;; run: shorthi 305419896 322197112 => i32:0
;; run: shorthi 305463160 305398015 => i32:0
;; run: sgle 4294967297 4294967297 => i32:1
;; run: sgle 21474836481 12884901890 => i32:1
;; run: sgle 12884901890 21474836481 => i32:0
;; run: sgle 12884901889 21474836481 => i32:1
;; run: sgle 21474836481 12884901889 => i32:0
;; run: sgle 2147483648 1 => i32:1
;; run: sgle 1 2147483648 => i32:0
;; run: sgle 9223372036854775815 4294967303 => i32:0
;; run: sgle 4294967303 9223372036854775815 => i32:1
;; run: sgle 18446744073709551615 18446744073709551615 => i32:1
;; run: sgle 18446744069414584320 1 => i32:1
;; run: sgle 1 18446744069414584320 => i32:0
;; run: sgle 9223372039002259456 9223372034707292160 => i32:0
;; run: sgle 11150031900141442680 11150031900141442680 => i32:1
;; run: sgle 11150031900141442680 1311768467463790320 => i32:0
;; run: sgle 1311768467463790320 11150031900141442680 => i32:1
;; run: sgge 4294967297 4294967297 => i32:0
;; run: sgge 21474836481 12884901890 => i32:0
;; run: sgge 12884901890 21474836481 => i32:1
;; run: sgge 12884901889 21474836481 => i32:0
;; run: sgge 21474836481 12884901889 => i32:1
;; run: sgge 2147483648 1 => i32:0
;; run: sgge 1 2147483648 => i32:1
;; run: sgge 9223372036854775815 4294967303 => i32:1
;; run: sgge 4294967303 9223372036854775815 => i32:0
;; run: sgge 18446744073709551615 18446744073709551615 => i32:0
;; run: sgge 18446744069414584320 1 => i32:0
;; run: sgge 1 18446744069414584320 => i32:1
;; run: sgge 9223372039002259456 9223372034707292160 => i32:1
;; run: sgge 11150031900141442680 11150031900141442680 => i32:0
;; run: sgge 11150031900141442680 1311768467463790320 => i32:1
;; run: sgge 1311768467463790320 11150031900141442680 => i32:0
;; run: usescond 4294967297 4294967297 => i32:1
;; run: usescond 21474836481 12884901890 => i32:1
;; run: usescond 12884901890 21474836481 => i32:0
;; run: usescond 12884901889 21474836481 => i32:2
;; run: usescond 21474836481 12884901889 => i32:1
;; run: usescond 2147483648 1 => i32:0
;; run: usescond 1 2147483648 => i32:1
;; run: usescond 9223372036854775815 4294967303 => i32:1
;; run: usescond 4294967303 9223372036854775815 => i32:2
;; run: usescond 18446744073709551615 18446744073709551615 => i32:1
;; run: usescond 18446744069414584320 1 => i32:1
;; run: usescond 1 18446744069414584320 => i32:0
;; run: usescond 9223372039002259456 9223372034707292160 => i32:1
;; run: usescond 11150031900141442680 11150031900141442680 => i32:1
;; run: usescond 11150031900141442680 1311768467463790320 => i32:1
;; run: usescond 1311768467463790320 11150031900141442680 => i32:0
;; run: usesn 4294967297 4294967297 => i32:0
;; run: usesn 21474836481 12884901890 => i32:1
;; run: usesn 12884901890 21474836481 => i32:1
;; run: usesn 12884901889 21474836481 => i32:2
;; run: usesn 21474836481 12884901889 => i32:0
;; run: usesn 2147483648 1 => i32:0
;; run: usesn 1 2147483648 => i32:1
;; run: usesn 9223372036854775815 4294967303 => i32:0
;; run: usesn 4294967303 9223372036854775815 => i32:2
;; run: usesn 18446744073709551615 18446744073709551615 => i32:0
;; run: usesn 18446744069414584320 1 => i32:1
;; run: usesn 1 18446744069414584320 => i32:1
;; run: usesn 9223372039002259456 9223372034707292160 => i32:0
;; run: usesn 11150031900141442680 11150031900141442680 => i32:0
;; run: usesn 11150031900141442680 1311768467463790320 => i32:1
;; run: usesn 1311768467463790320 11150031900141442680 => i32:1
;; run: gen 4294967297 1 1 => i32:500
;; run: gen 21474836481 2 3 => i32:501
;; run: gen 12884901890 1 5 => i32:500
;; run: gen 12884901889 1 5 => i32:501
;; run: gen 21474836481 1 3 => i32:500
;; run: gen 2147483648 1 0 => i32:500
;; run: gen 1 2147483648 0 => i32:501
;; run: gen 9223372036854775815 7 1 => i32:500
;; run: gen 4294967303 7 2147483648 => i32:501
;; run: gen 18446744073709551615 4294967295 4294967295 => i32:500
;; run: gen 18446744069414584320 1 0 => i32:501
;; run: gen 1 0 4294967295 => i32:500
;; run: gen 9223372039002259456 2147483648 2147483647 => i32:500
;; run: gen 11150031900141442680 305419896 2596069104 => i32:500
;; run: gen 11150031900141442680 2596069104 305419896 => i32:501
;; run: gen 1311768467463790320 305419896 2596069104 => i32:500
;; run: gensg 4294967297 1 1 => i32:0
;; run: gensg 21474836481 2 3 => i32:0
;; run: gensg 12884901890 1 5 => i32:1
;; run: gensg 12884901889 1 5 => i32:0
;; run: gensg 21474836481 1 3 => i32:1
;; run: gensg 2147483648 1 0 => i32:0
;; run: gensg 1 2147483648 0 => i32:1
;; run: gensg 9223372036854775815 7 1 => i32:1
;; run: gensg 4294967303 7 2147483648 => i32:0
;; run: gensg 18446744073709551615 4294967295 4294967295 => i32:0
;; run: gensg 18446744069414584320 1 0 => i32:0
;; run: gensg 1 0 4294967295 => i32:1
;; run: gensg 9223372039002259456 2147483648 2147483647 => i32:1
;; run: gensg 11150031900141442680 305419896 2596069104 => i32:0
;; run: gensg 11150031900141442680 2596069104 305419896 => i32:1
;; run: gensg 1311768467463790320 305419896 2596069104 => i32:0
;; run: genwide 4294967297 1 1 => i32:0
;; run: genwide 21474836481 2 3 => i32:1
;; run: genwide 12884901890 1 5 => i32:0
;; run: genwide 12884901889 1 5 => i32:1
;; run: genwide 21474836481 1 3 => i32:0
;; run: genwide 2147483648 1 0 => i32:0
;; run: genwide 1 2147483648 0 => i32:1
;; run: genwide 9223372036854775815 7 1 => i32:0
;; run: genwide 4294967303 7 2147483648 => i32:1
;; run: genwide 18446744073709551615 4294967295 4294967295 => i32:0
;; run: genwide 18446744069414584320 1 0 => i32:1
;; run: genwide 1 0 4294967295 => i32:0
;; run: genwide 9223372039002259456 2147483648 2147483647 => i32:0
;; run: genwide 11150031900141442680 305419896 2596069104 => i32:0
;; run: genwide 11150031900141442680 2596069104 305419896 => i32:1
;; run: genwide 1311768467463790320 305419896 2596069104 => i32:0
;; run: genwide 12884901893 5 4294967296 => i32:1
;; run: genwide 12884901893 5 4294967298 => i32:1
;; run: genfits 4294967297 1 1 => i32:0
;; run: genfits 21474836481 2 3 => i32:1
;; run: genfits 12884901890 1 5 => i32:0
;; run: genfits 12884901889 1 5 => i32:1
;; run: genfits 21474836481 1 3 => i32:0
;; run: genfits 2147483648 1 0 => i32:0
;; run: genfits 1 2147483648 0 => i32:1
;; run: genfits 9223372036854775815 7 1 => i32:0
;; run: genfits 4294967303 7 2147483648 => i32:1
;; run: genfits 18446744073709551615 4294967295 4294967295 => i32:0
;; run: genfits 18446744069414584320 1 0 => i32:1
;; run: genfits 1 0 4294967295 => i32:0
;; run: genfits 9223372039002259456 2147483648 2147483647 => i32:0
;; run: genfits 11150031900141442680 305419896 2596069104 => i32:0
;; run: genfits 11150031900141442680 2596069104 305419896 => i32:1
;; run: genfits 1311768467463790320 305419896 2596069104 => i32:0
;; run: genfits 12884901893 5 4294967296 => i32:0
;; run: genfits 12884901893 5 4294967298 => i32:0
;; run: genmwide 4294967297 1 1 => i32:0
;; run: genmwide 21474836481 2 3 => i32:1
;; run: genmwide 12884901890 1 5 => i32:0
;; run: genmwide 12884901889 1 5 => i32:1
;; run: genmwide 21474836481 1 3 => i32:0
;; run: genmwide 2147483648 1 0 => i32:0
;; run: genmwide 1 2147483648 0 => i32:1
;; run: genmwide 9223372036854775815 7 1 => i32:0
;; run: genmwide 4294967303 7 2147483648 => i32:1
;; run: genmwide 18446744073709551615 4294967295 4294967295 => i32:0
;; run: genmwide 18446744069414584320 1 0 => i32:1
;; run: genmwide 1 0 4294967295 => i32:0
;; run: genmwide 9223372039002259456 2147483648 2147483647 => i32:0
;; run: genmwide 11150031900141442680 305419896 2596069104 => i32:0
;; run: genmwide 11150031900141442680 2596069104 305419896 => i32:1
;; run: genmwide 1311768467463790320 305419896 2596069104 => i32:0
;; run: genmwide 12884901893 4294967301 9 => i32:1
;; run: genmwide 12884901893 1099511627776 0 => i32:1
;; run: genconst 4294967297 1 1 => i32:1
;; run: genconst 21474836481 2 3 => i32:1
;; run: genconst 12884901890 1 5 => i32:0
;; run: genconst 12884901889 1 5 => i32:0
;; run: genconst 21474836481 1 3 => i32:0
;; run: genconst 2147483648 1 0 => i32:0
;; run: genconst 1 2147483648 0 => i32:1
;; run: genconst 9223372036854775815 7 1 => i32:0
;; run: genconst 4294967303 7 2147483648 => i32:1
;; run: genconst 18446744073709551615 4294967295 4294967295 => i32:0
;; run: genconst 18446744069414584320 1 0 => i32:1
;; run: genconst 1 0 4294967295 => i32:0
;; run: genconst 9223372039002259456 2147483648 2147483647 => i32:0
;; run: genconst 11150031900141442680 305419896 2596069104 => i32:0
;; run: genconst 11150031900141442680 2596069104 305419896 => i32:1
;; run: genconst 1311768467463790320 305419896 2596069104 => i32:0
;; run: both 4294967297 0 0 => i32:1
;; run: both 21474836481 0 0 => i32:1
;; run: both 12884901890 0 0 => i32:1
;; run: both 12884901889 0 0 => i32:1
;; run: both 21474836481 0 0 => i32:1
;; run: both 2147483648 0 0 => i32:0
;; run: both 1 0 0 => i32:1
;; run: both 9223372036854775815 0 0 => i32:0
;; run: both 4294967303 0 0 => i32:0
;; run: both 18446744073709551615 0 0 => i32:0
;; run: both 18446744069414584320 0 0 => i32:1
;; run: both 1 0 0 => i32:1
;; run: both 9223372039002259456 0 0 => i32:0
;; run: both 11150031900141442680 0 0 => i32:0
;; run: both 11150031900141442680 0 0 => i32:0
;; run: both 1311768467463790320 0 0 => i32:0
;; run: both 9223372049739677701 0 0 => i32:1
;; run: both 9223372054034644997 0 0 => i32:0
;; run: both 12884901893 0 0 => i32:1
;; run: bothsg 4294967297 0 0 => i32:0
;; run: bothsg 21474836481 0 0 => i32:0
;; run: bothsg 12884901890 0 0 => i32:0
;; run: bothsg 12884901889 0 0 => i32:0
;; run: bothsg 21474836481 0 0 => i32:0
;; run: bothsg 2147483648 0 0 => i32:1
;; run: bothsg 1 0 0 => i32:0
;; run: bothsg 9223372036854775815 0 0 => i32:0
;; run: bothsg 4294967303 0 0 => i32:0
;; run: bothsg 18446744073709551615 0 0 => i32:0
;; run: bothsg 18446744069414584320 0 0 => i32:0
;; run: bothsg 1 0 0 => i32:0
;; run: bothsg 9223372039002259456 0 0 => i32:1
;; run: bothsg 11150031900141442680 0 0 => i32:0
;; run: bothsg 11150031900141442680 0 0 => i32:0
;; run: bothsg 1311768467463790320 0 0 => i32:1
;; run: bothsg 38654705659 0 0 => i32:1
;; run: bothsg 42949672955 0 0 => i32:0
;; run: bothsg 429496729594 0 0 => i32:1
;; run: bothsg 4294967292 0 0 => i32:0
;; run: mixed 4294967297 65537 0 => i32:0
;; run: mixed 21474836481 196610 0 => i32:1
;; run: mixed 12884901890 327681 0 => i32:0
;; run: mixed 12884901889 327681 0 => i32:1
;; run: mixed 21474836481 196609 0 => i32:0
;; run: mixed 0 1 0 => i32:1
;; run: mixed 1 0 0 => i32:0
;; run: mixed 7 65543 0 => i32:1
;; run: mixed 4294967303 7 0 => i32:0
;; run: mixed 281470681808895 4294967295 0 => i32:0
;; run: mixed 281470681743360 1 0 => i32:1
;; run: mixed 1 4294901760 0 => i32:0
;; run: mixed 0 4294901760 0 => i32:1
;; run: mixed 245122373539448 3740292728 0 => i32:0
;; run: mixed 245122373539448 1450761968 0 => i32:1
;; run: mixed 95073396121328 3740292728 0 => i32:0
;; run: mixed 32768 1 0 => i32:0
;; run: mixed 1 32768 0 => i32:1
;; run: mixed 140737488355335 65543 0 => i32:0
;; run: mixed 4294967303 2147483655 0 => i32:1
;; run: mixedrev 4294967297 65537 0 => i32:600
;; run: mixedrev 21474836481 196610 0 => i32:600
;; run: mixedrev 12884901890 327681 0 => i32:601
;; run: mixedrev 12884901889 327681 0 => i32:600
;; run: mixedrev 21474836481 196609 0 => i32:601
;; run: mixedrev 0 1 0 => i32:600
;; run: mixedrev 1 0 0 => i32:601
;; run: mixedrev 7 65543 0 => i32:600
;; run: mixedrev 4294967303 7 0 => i32:601
;; run: mixedrev 281470681808895 4294967295 0 => i32:600
;; run: mixedrev 281470681743360 1 0 => i32:600
;; run: mixedrev 1 4294901760 0 => i32:601
;; run: mixedrev 0 4294901760 0 => i32:600
;; run: mixedrev 245122373539448 3740292728 0 => i32:600
;; run: mixedrev 245122373539448 1450761968 0 => i32:600
;; run: mixedrev 95073396121328 3740292728 0 => i32:601
;; run: mixedrev 32768 1 0 => i32:601
;; run: mixedrev 1 32768 0 => i32:600
;; run: mixedrev 140737488355335 65543 0 => i32:601
;; run: mixedrev 4294967303 2147483655 0 => i32:600
;; run: w32m 65537 9895605108737 327681 => i32:0
;; run: w32m 327681 9895605108738 327683 => i32:1
;; run: w32m 196610 9895605108737 327685 => i32:0
;; run: w32m 196609 9895605108737 327685 => i32:1
;; run: w32m 327681 9895605108737 327683 => i32:0
;; run: w32m 0 9895605108737 327680 => i32:1
;; run: w32m 1 9895605108736 327680 => i32:0
;; run: w32m 7 9895605108743 327681 => i32:1
;; run: w32m 65543 9895605108743 327680 => i32:0
;; run: w32m 4294967295 9895605174271 393215 => i32:0
;; run: w32m 4294901760 9895605108737 327680 => i32:1
;; run: w32m 1 9895605108736 393215 => i32:0
;; run: w32m 0 9895605108736 393215 => i32:1
;; run: w32m 3740292728 9895605130872 384752 => i32:0
;; run: w32m 3740292728 9895605165808 349816 => i32:1
;; run: w32m 1450761968 9895605130872 384752 => i32:0
;; run: w32m 32768 9895605108737 327680 => i32:0
;; run: w32m 1 9895605141504 327680 => i32:1
;; run: w32m 2147483655 9895605108743 327681 => i32:0
;; run: w32m 65543 9895605108743 360448 => i32:1
;; run: w32n 65537 458753 9895604977665 => i32:0
;; run: w32n 327681 458754 9895604977667 => i32:1
;; run: w32n 196610 458753 9895604977669 => i32:0
;; run: w32n 196609 458753 9895604977669 => i32:1
;; run: w32n 327681 458753 9895604977667 => i32:0
;; run: w32n 0 458753 9895604977664 => i32:1
;; run: w32n 1 458752 9895604977664 => i32:0
;; run: w32n 7 458759 9895604977665 => i32:1
;; run: w32n 65543 458759 9895604977664 => i32:0
;; run: w32n 4294967295 524287 9895605043199 => i32:0
;; run: w32n 4294901760 458753 9895604977664 => i32:1
;; run: w32n 1 458752 9895605043199 => i32:0
;; run: w32n 0 458752 9895605043199 => i32:1
;; run: w32n 3740292728 480888 9895605034736 => i32:0
;; run: w32n 3740292728 515824 9895604999800 => i32:1
;; run: w32n 1450761968 480888 9895605034736 => i32:0
;; run: w32n 32768 458753 9895604977664 => i32:0
;; run: w32n 1 491520 9895604977664 => i32:1
;; run: w32n 2147483655 458759 9895604977665 => i32:0
;; run: w32n 65543 458759 9895605010432 => i32:1
;; run: same8 257 1426063361 43521 => i32:0
;; run: same8 261 1426063362 43523 => i32:1
;; run: same8 515 1426063361 43525 => i32:0
;; run: same8 259 1426063361 43525 => i32:1
;; run: same8 261 1426063361 43523 => i32:0
;; run: same8 0 1426063361 43520 => i32:1
;; run: same8 256 1426063360 43520 => i32:0
;; run: same8 1792 1426063367 43521 => i32:1
;; run: same8 1793 1426063367 43520 => i32:0
;; run: same8 4294967295 1442840575 43775 => i32:0
;; run: same8 255 1426063361 43520 => i32:1
;; run: same8 256 1426063360 43775 => i32:0
;; run: same8 0 1426063360 43775 => i32:1
;; run: same8 878082288 1429493368 43760 => i32:0
;; run: same8 878082288 1438441200 43640 => i32:1
;; run: same8 3168727160 1429493368 43760 => i32:0
;; run: sg16 65537 1 1 => i32:0
;; run: sg16 327681 2 3 => i32:1
;; run: sg16 196610 1 5 => i32:0
;; run: sg16 196609 1 5 => i32:1
;; run: sg16 327681 1 3 => i32:0
;; run: sg16 0 1 0 => i32:1
;; run: sg16 1 0 0 => i32:0
;; run: sg16 7 7 1 => i32:1
;; run: sg16 65543 7 0 => i32:0
;; run: sg16 4294967295 65535 65535 => i32:0
;; run: sg16 4294901760 1 0 => i32:1
;; run: sg16 1 0 65535 => i32:0
;; run: sg16 0 0 65535 => i32:1
;; run: sg16 3740292728 22136 57072 => i32:0
;; run: sg16 3740292728 57072 22136 => i32:1
;; run: sg16 1450761968 22136 57072 => i32:0
;; run: sg16 32768 1 0 => i32:0
;; run: sg16 1 32768 0 => i32:1
;; run: sg16 2147483655 7 1 => i32:0
;; run: sg16 65543 7 32768 => i32:1
;; run: sg16 196613 4294967289 9 => i32:0
;; run: sg16 196613 2147483653 1 => i32:0
;; run: sg16 65535 4294967295 0 => i32:0
;; run: sgmix 4294967297 1 1 => i32:0
;; run: sgmix 21474836481 2 3 => i32:1
;; run: sgmix 12884901890 1 5 => i32:0
;; run: sgmix 12884901889 1 5 => i32:1
;; run: sgmix 21474836481 1 3 => i32:0
;; run: sgmix 2147483648 1 0 => i32:1
;; run: sgmix 1 2147483648 0 => i32:0
;; run: sgmix 9223372036854775815 7 1 => i32:0
;; run: sgmix 4294967303 7 2147483648 => i32:1
;; run: sgmix 18446744073709551615 4294967295 4294967295 => i32:0
;; run: sgmix 18446744069414584320 1 0 => i32:1
;; run: sgmix 1 0 4294967295 => i32:0
;; run: sgmix 9223372039002259456 2147483648 2147483647 => i32:0
;; run: sgmix 11150031900141442680 305419896 2596069104 => i32:0
;; run: sgmix 11150031900141442680 2596069104 305419896 => i32:0
;; run: sgmix 1311768467463790320 305419896 2596069104 => i32:0
;; run: sgmix 12884901893 4294967291 0 => i32:0
;; run: sgmix 6442450943 2147483648 0 => i32:0
;; run: sgmix 0 4294967295 9 => i32:0
;; run: nsgmix 4294967297 1 1 => i32:0
;; run: nsgmix 21474836481 2 3 => i32:0
;; run: nsgmix 12884901890 1 5 => i32:1
;; run: nsgmix 12884901889 1 5 => i32:0
;; run: nsgmix 21474836481 1 3 => i32:1
;; run: nsgmix 2147483648 1 0 => i32:1
;; run: nsgmix 1 2147483648 0 => i32:0
;; run: nsgmix 9223372036854775815 7 1 => i32:0
;; run: nsgmix 4294967303 7 2147483648 => i32:1
;; run: nsgmix 18446744073709551615 4294967295 4294967295 => i32:0
;; run: nsgmix 18446744069414584320 1 0 => i32:0
;; run: nsgmix 1 0 4294967295 => i32:1
;; run: nsgmix 9223372039002259456 2147483648 2147483647 => i32:0
;; run: nsgmix 11150031900141442680 305419896 2596069104 => i32:0
;; run: nsgmix 11150031900141442680 2596069104 305419896 => i32:0
;; run: nsgmix 1311768467463790320 305419896 2596069104 => i32:1
;; run: nsgmix 9223372036854775813 5 7 => i32:0
;; run: nsgmix 18446744004990074885 5 2147483647 => i32:0
;; run: sgmixrev 4294967297 1 1 => i32:0
;; run: sgmixrev 21474836481 2 3 => i32:0
;; run: sgmixrev 12884901890 1 5 => i32:1
;; run: sgmixrev 12884901889 1 5 => i32:0
;; run: sgmixrev 21474836481 1 3 => i32:1
;; run: sgmixrev 2147483648 1 0 => i32:0
;; run: sgmixrev 1 2147483648 0 => i32:1
;; run: sgmixrev 9223372036854775815 7 1 => i32:1
;; run: sgmixrev 4294967303 7 2147483648 => i32:0
;; run: sgmixrev 18446744073709551615 4294967295 4294967295 => i32:1
;; run: sgmixrev 18446744069414584320 1 0 => i32:0
;; run: sgmixrev 1 0 4294967295 => i32:1
;; run: sgmixrev 9223372039002259456 2147483648 2147483647 => i32:1
;; run: sgmixrev 11150031900141442680 305419896 2596069104 => i32:0
;; run: sgmixrev 11150031900141442680 2596069104 305419896 => i32:1
;; run: sgmixrev 1311768467463790320 305419896 2596069104 => i32:1
;; run: sgmixrev 12884901893 4294967291 0 => i32:1
;; run: sgmixrev 6442450943 2147483648 0 => i32:1
;; run: sgmixrev 0 4294967295 9 => i32:1
;; run: nsgmixrev 4294967297 1 1 => i32:0
;; run: nsgmixrev 21474836481 2 3 => i32:1
;; run: nsgmixrev 12884901890 1 5 => i32:0
;; run: nsgmixrev 12884901889 1 5 => i32:1
;; run: nsgmixrev 21474836481 1 3 => i32:0
;; run: nsgmixrev 2147483648 1 0 => i32:0
;; run: nsgmixrev 1 2147483648 0 => i32:1
;; run: nsgmixrev 9223372036854775815 7 1 => i32:1
;; run: nsgmixrev 4294967303 7 2147483648 => i32:0
;; run: nsgmixrev 18446744073709551615 4294967295 4294967295 => i32:1
;; run: nsgmixrev 18446744069414584320 1 0 => i32:1
;; run: nsgmixrev 1 0 4294967295 => i32:0
;; run: nsgmixrev 9223372039002259456 2147483648 2147483647 => i32:1
;; run: nsgmixrev 11150031900141442680 305419896 2596069104 => i32:1
;; run: nsgmixrev 11150031900141442680 2596069104 305419896 => i32:1
;; run: nsgmixrev 1311768467463790320 305419896 2596069104 => i32:0
;; run: nsgmixrev 9223372036854775813 5 7 => i32:1
;; run: nsgmixrev 18446744004990074885 5 2147483647 => i32:1
;; run: mixedw 281474976710657 65537 0 => i32:0
;; run: mixedw 1407374883553281 196610 0 => i32:1
;; run: mixedw 844424930131970 327681 0 => i32:0
;; run: mixedw 844424930131969 327681 0 => i32:1
;; run: mixedw 1407374883553281 196609 0 => i32:0
;; run: mixedw 0 1 0 => i32:1
;; run: mixedw 1 0 0 => i32:0
;; run: mixedw 7 65543 0 => i32:1
;; run: mixedw 281474976710663 7 0 => i32:0
;; run: mixedw 18446462598732906495 4294967295 0 => i32:0
;; run: mixedw 18446462598732840960 1 0 => i32:1
;; run: mixedw 1 4294901760 0 => i32:0
;; run: mixedw 0 4294901760 0 => i32:1
;; run: mixedw 16064339870830581368 3740292728 0 => i32:0
;; run: mixedw 16064339870830581368 1450761968 0 => i32:1
;; run: mixedw 6230730084467138288 3740292728 0 => i32:0
;; run: mixedw 32768 1 0 => i32:0
;; run: mixedw 1 32768 0 => i32:1
;; run: mixedw 9223372036854775815 65543 0 => i32:0
;; run: mixedw 281474976710663 2147483655 0 => i32:1
;; run: mixedw 1408474395181056 589833 0 => i32:0
;; run: mixedw 1407374883553287 393223 0 => i32:1
;; run: mixedc 4294967297 4294967297 => i32:1
;; run: mixedc 21474836481 12884901890 => i32:1
;; run: mixedc 12884901890 21474836481 => i32:0
;; run: mixedc 12884901889 21474836481 => i32:1
;; run: mixedc 21474836481 12884901889 => i32:1
;; run: mixedc 2147483648 1 => i32:0
;; run: mixedc 1 2147483648 => i32:0
;; run: mixedc 9223372036854775815 4294967303 => i32:0
;; run: mixedc 4294967303 9223372036854775815 => i32:1
;; run: mixedc 18446744073709551615 18446744073709551615 => i32:0
;; run: mixedc 18446744069414584320 1 => i32:1
;; run: mixedc 1 18446744069414584320 => i32:0
;; run: mixedc 9223372039002259456 9223372034707292160 => i32:0
;; run: mixedc 11150031900141442680 11150031900141442680 => i32:0
;; run: mixedc 11150031900141442680 1311768467463790320 => i32:0
;; run: mixedc 1311768467463790320 11150031900141442680 => i32:0
;; run: mixedc 12884901893 196613 => i32:0
;; run: mixedc 12884901893 262149 => i32:1
;; run: mixedc 12884901893 1152921504606846981 => i32:1
;; run: genwrap 4294967297 244834610708481 209933706461185 => i32:0
;; run: genwrap 21474836481 244834610708482 209933706461187 => i32:1
;; run: genwrap 12884901890 244834610708481 209933706461189 => i32:0
;; run: genwrap 12884901889 244834610708481 209933706461189 => i32:1
;; run: genwrap 21474836481 244834610708481 209933706461187 => i32:0
;; run: genwrap 2147483648 244834610708481 209933706461184 => i32:0
;; run: genwrap 1 244836758192128 209933706461184 => i32:1
;; run: genwrap 9223372036854775815 244834610708487 209933706461185 => i32:0
;; run: genwrap 4294967303 244834610708487 209935853944832 => i32:1
;; run: genwrap 18446744073709551615 244838905675775 209938001428479 => i32:0
;; run: genwrap 18446744069414584320 244834610708481 209933706461184 => i32:1
;; run: genwrap 1 244834610708480 209938001428479 => i32:0
;; run: genwrap 9223372039002259456 244836758192128 209935853944831 => i32:0
;; run: genwrap 11150031900141442680 244834916128376 209936302530288 => i32:0
;; run: genwrap 11150031900141442680 244837206777584 209934011881080 => i32:1
;; run: genwrap 1311768467463790320 244834916128376 209936302530288 => i32:0
;; run: two 1 1 1 1 => i32:0
;; run: two 1 5 2 3 => i32:1
;; run: two 2 3 1 5 => i32:0
;; run: two 1 3 1 5 => i32:1
;; run: two 1 5 1 3 => i32:0
;; run: two 2147483648 0 1 0 => i32:0
;; run: two 1 0 2147483648 0 => i32:1
;; run: two 7 2147483648 7 1 => i32:0
;; run: two 7 1 7 2147483648 => i32:1
;; run: two 4294967295 4294967295 4294967295 4294967295 => i32:0
;; run: two 0 4294967295 1 0 => i32:1
;; run: two 1 0 0 4294967295 => i32:0
;; run: two 2147483648 2147483648 2147483648 2147483647 => i32:0
;; run: two 305419896 2596069104 305419896 2596069104 => i32:0
;; run: two 305419896 2596069104 2596069104 305419896 => i32:1
;; run: two 2596069104 305419896 305419896 2596069104 => i32:0
;; run: br 4294967297 4294967297 => i32:11
;; run: br 21474836481 12884901890 => i32:22
;; run: br 12884901890 21474836481 => i32:11
;; run: br 12884901889 21474836481 => i32:22
;; run: br 21474836481 12884901889 => i32:11
;; run: br 2147483648 1 => i32:11
;; run: br 1 2147483648 => i32:22
;; run: br 9223372036854775815 4294967303 => i32:11
;; run: br 4294967303 9223372036854775815 => i32:22
;; run: br 18446744073709551615 18446744073709551615 => i32:11
;; run: br 18446744069414584320 1 => i32:22
;; run: br 1 18446744069414584320 => i32:11
;; run: br 9223372039002259456 9223372034707292160 => i32:11
;; run: br 11150031900141442680 11150031900141442680 => i32:11
;; run: br 11150031900141442680 1311768467463790320 => i32:22
;; run: br 1311768467463790320 11150031900141442680 => i32:11
;; run: brne 4294967297 4294967297 => i32:22
;; run: brne 21474836481 12884901890 => i32:11
;; run: brne 12884901890 21474836481 => i32:22
;; run: brne 12884901889 21474836481 => i32:11
;; run: brne 21474836481 12884901889 => i32:22
;; run: brne 2147483648 1 => i32:22
;; run: brne 1 2147483648 => i32:11
;; run: brne 9223372036854775815 4294967303 => i32:22
;; run: brne 4294967303 9223372036854775815 => i32:11
;; run: brne 18446744073709551615 18446744073709551615 => i32:22
;; run: brne 18446744069414584320 1 => i32:11
;; run: brne 1 18446744069414584320 => i32:22
;; run: brne 9223372039002259456 9223372034707292160 => i32:22
;; run: brne 11150031900141442680 11150031900141442680 => i32:22
;; run: brne 11150031900141442680 1311768467463790320 => i32:11
;; run: brne 1311768467463790320 11150031900141442680 => i32:22
;; run: brswap 4294967297 4294967297 => i32:13
;; run: brswap 21474836481 12884901890 => i32:24
;; run: brswap 12884901890 21474836481 => i32:13
;; run: brswap 12884901889 21474836481 => i32:24
;; run: brswap 21474836481 12884901889 => i32:13
;; run: brswap 2147483648 1 => i32:13
;; run: brswap 1 2147483648 => i32:24
;; run: brswap 9223372036854775815 4294967303 => i32:13
;; run: brswap 4294967303 9223372036854775815 => i32:24
;; run: brswap 18446744073709551615 18446744073709551615 => i32:13
;; run: brswap 18446744069414584320 1 => i32:24
;; run: brswap 1 18446744069414584320 => i32:13
;; run: brswap 9223372039002259456 9223372034707292160 => i32:13
;; run: brswap 11150031900141442680 11150031900141442680 => i32:13
;; run: brswap 11150031900141442680 1311768467463790320 => i32:24
;; run: brswap 1311768467463790320 11150031900141442680 => i32:13
;; run: brsame 4294967297 4294967297 => i32:22
;; run: brsame 4294967301 8589934595 => i32:11
;; run: brsame 8589934595 4294967301 => i32:22
;; run: brsame 4294967299 4294967301 => i32:11
;; run: brsame 4294967301 4294967299 => i32:22
;; run: brsame 9223372036854775808 4294967296 => i32:22
;; run: brsame 4294967296 9223372036854775808 => i32:11
;; run: brsame 32212254720 30064771073 => i32:22
;; run: brsame 30064771073 32212254720 => i32:11
;; run: brsame 18446744073709551615 18446744073709551615 => i32:22
;; run: brsame 4294967295 4294967296 => i32:11
;; run: brsame 4294967296 4294967295 => i32:22
;; run: brsame 9223372039002259456 9223372039002259455 => i32:22
;; run: brsame 1311768467463790320 1311768467463790320 => i32:22
;; run: brsame 1311768467463790320 11150031900141442680 => i32:11
;; run: brsame 11150031900141442680 1311768467463790320 => i32:22
;; run: brvals 4294967297 4294967297 => i32:6
;; run: brvals 21474836481 12884901890 => i32:4
;; run: brvals 12884901890 21474836481 => i32:2
;; run: brvals 12884901889 21474836481 => i32:4
;; run: brvals 21474836481 12884901889 => i32:4
;; run: brvals 2147483648 1 => i32:7
;; run: brvals 1 2147483648 => i32:4
;; run: brvals 9223372036854775815 4294967303 => i32:6
;; run: brvals 4294967303 9223372036854775815 => i32:10
;; run: brvals 18446744073709551615 18446744073709551615 => i32:-8
;; run: brvals 18446744069414584320 1 => i32:3
;; run: brvals 1 18446744069414584320 => i32:-8
;; run: brvals 9223372039002259456 9223372034707292160 => i32:2147483640
;; run: brvals 11150031900141442680 11150031900141442680 => i32:-1698898185
;; run: brvals 11150031900141442680 1311768467463790320 => i32:305419899
;; run: brvals 1311768467463790320 11150031900141442680 => i32:-1698898185
;; run: brconst 4294967297 4294967297 => i32:79
;; run: brconst 21474836481 12884901890 => i32:78
;; run: brconst 12884901890 21474836481 => i32:79
;; run: brconst 12884901889 21474836481 => i32:78
;; run: brconst 21474836481 12884901889 => i32:79
;; run: brconst 2147483648 1 => i32:79
;; run: brconst 1 2147483648 => i32:78
;; run: brconst 9223372036854775815 4294967303 => i32:79
;; run: brconst 4294967303 9223372036854775815 => i32:78
;; run: brconst 18446744073709551615 18446744073709551615 => i32:79
;; run: brconst 18446744069414584320 1 => i32:78
;; run: brconst 1 18446744069414584320 => i32:79
;; run: brconst 9223372039002259456 9223372034707292160 => i32:79
;; run: brconst 11150031900141442680 11150031900141442680 => i32:79
;; run: brconst 11150031900141442680 1311768467463790320 => i32:78
;; run: brconst 1311768467463790320 11150031900141442680 => i32:79
;; run: brdiffer 4294967297 4294967297 => i32:79
;; run: brdiffer 21474836481 12884901890 => i32:56
;; run: brdiffer 12884901890 21474836481 => i32:57
;; run: brdiffer 12884901889 21474836481 => i32:78
;; run: brdiffer 21474836481 12884901889 => i32:79
;; run: brdiffer 2147483648 1 => i32:57
;; run: brdiffer 1 2147483648 => i32:56
;; run: brdiffer 9223372036854775815 4294967303 => i32:79
;; run: brdiffer 4294967303 9223372036854775815 => i32:78
;; run: brdiffer 18446744073709551615 18446744073709551615 => i32:79
;; run: brdiffer 18446744069414584320 1 => i32:56
;; run: brdiffer 1 18446744069414584320 => i32:57
;; run: brdiffer 9223372039002259456 9223372034707292160 => i32:79
;; run: brdiffer 11150031900141442680 11150031900141442680 => i32:79
;; run: brdiffer 11150031900141442680 1311768467463790320 => i32:56
;; run: brdiffer 1311768467463790320 11150031900141442680 => i32:57
;; run: brstore 4294967297 4294967297 => i32:25
;; run: brstore 21474836481 12884901890 => i32:11
;; run: brstore 12884901890 21474836481 => i32:22
;; run: brstore 12884901889 21474836481 => i32:14
;; run: brstore 21474836481 12884901889 => i32:25
;; run: brstore 2147483648 1 => i32:22
;; run: brstore 1 2147483648 => i32:11
;; run: brstore 9223372036854775815 4294967303 => i32:70
;; run: brstore 4294967303 9223372036854775815 => i32:59
;; run: brstore 18446744073709551615 18446744073709551615 => i32:70
;; run: brstore 18446744069414584320 1 => i32:11
;; run: brstore 1 18446744069414584320 => i32:22
;; run: brstore 9223372039002259456 9223372034707292160 => i32:25
;; run: brstore 11150031900141442680 11150031900141442680 => i32:25
;; run: brstore 11150031900141442680 1311768467463790320 => i32:11
;; run: brstore 1311768467463790320 11150031900141442680 => i32:22
;; run: brstoren 4294967297 4294967297 => i32:22
;; run: brstoren 21474836481 12884901890 => i32:14
;; run: brstoren 12884901890 21474836481 => i32:25
;; run: brstoren 12884901889 21474836481 => i32:11
;; run: brstoren 21474836481 12884901889 => i32:22
;; run: brstoren 2147483648 1 => i32:25
;; run: brstoren 1 2147483648 => i32:14
;; run: brstoren 9223372036854775815 4294967303 => i32:22
;; run: brstoren 4294967303 9223372036854775815 => i32:11
;; run: brstoren 18446744073709551615 18446744073709551615 => i32:22
;; run: brstoren 18446744069414584320 1 => i32:14
;; run: brstoren 1 18446744069414584320 => i32:25
;; run: brstoren 9223372039002259456 9223372034707292160 => i32:22
;; run: brstoren 11150031900141442680 11150031900141442680 => i32:22
;; run: brstoren 11150031900141442680 1311768467463790320 => i32:14
;; run: brstoren 1311768467463790320 11150031900141442680 => i32:25
;; run: brhi32 4294967297 4294967297 => i32:26
;; run: brhi32 21474836481 12884901890 => i32:26
;; run: brhi32 12884901890 21474836481 => i32:15
;; run: brhi32 12884901889 21474836481 => i32:26
;; run: brhi32 21474836481 12884901889 => i32:15
;; run: brhi32 2147483648 1 => i32:15
;; run: brhi32 1 2147483648 => i32:26
;; run: brhi32 9223372036854775815 4294967303 => i32:15
;; run: brhi32 4294967303 9223372036854775815 => i32:26
;; run: brhi32 18446744073709551615 18446744073709551615 => i32:26
;; run: brhi32 18446744069414584320 1 => i32:26
;; run: brhi32 1 18446744069414584320 => i32:15
;; run: brhi32 9223372039002259456 9223372034707292160 => i32:15
;; run: brhi32 11150031900141442680 11150031900141442680 => i32:26
;; run: brhi32 11150031900141442680 1311768467463790320 => i32:26
;; run: brhi32 1311768467463790320 11150031900141442680 => i32:15
;; run: brmask16 65537 65537 => i32:17
;; run: brmask16 327681 196610 => i32:28
;; run: brmask16 196610 327681 => i32:17
;; run: brmask16 196609 327681 => i32:17
;; run: brmask16 327681 196609 => i32:28
;; run: brmask16 0 1 => i32:17
;; run: brmask16 1 0 => i32:28
;; run: brmask16 7 65543 => i32:17
;; run: brmask16 65543 7 => i32:28
;; run: brmask16 4294967295 4294967295 => i32:17
;; run: brmask16 4294901760 1 => i32:28
;; run: brmask16 1 4294901760 => i32:17
;; run: brmask16 0 4294901760 => i32:17
;; run: brmask16 3740292728 3740292728 => i32:17
;; run: brmask16 3740292728 1450761968 => i32:28
;; run: brmask16 1450761968 3740292728 => i32:17
;; run: brmask16 32768 1 => i32:28
;; run: brmask16 1 32768 => i32:17
;; run: brmask16 2147483655 65543 => i32:28
;; run: brmask16 65543 2147483655 => i32:17
;; run: brmask16 256 511 => i32:17
;; run: brmask16 511 256 => i32:28
;; run: brmask16 4278190081 16776961 => i32:28
;; run: brmask16 16776961 4278190081 => i32:17
;; run: brmask16 305419896 305419897 => i32:17
;; run: brmask16 305419896 305485432 => i32:17
;; run: brmask16 305419896 322197112 => i32:17
;; run: brmask16 305463160 305398015 => i32:28
;; run: brjoin 4294967297 4294967297 0 => i32:22
;; run: brjoin 4294967297 4294967297 1 => i32:22
;; run: brjoin 21474836481 12884901890 0 => i32:11
;; run: brjoin 21474836481 12884901890 1 => i32:22
;; run: brjoin 12884901890 21474836481 0 => i32:22
;; run: brjoin 12884901890 21474836481 1 => i32:11
;; run: brjoin 12884901889 21474836481 0 => i32:11
;; run: brjoin 12884901889 21474836481 1 => i32:11
;; run: brjoin 21474836481 12884901889 0 => i32:22
;; run: brjoin 21474836481 12884901889 1 => i32:22
;; run: brjoin 2147483648 1 0 => i32:22
;; run: brjoin 2147483648 1 1 => i32:22
;; run: brjoin 1 2147483648 0 => i32:11
;; run: brjoin 1 2147483648 1 => i32:22
;; run: brjoin 9223372036854775815 4294967303 0 => i32:22
;; run: brjoin 9223372036854775815 4294967303 1 => i32:22
;; run: brjoin 4294967303 9223372036854775815 0 => i32:11
;; run: brjoin 4294967303 9223372036854775815 1 => i32:11
;; run: brjoin 18446744073709551615 18446744073709551615 0 => i32:22
;; run: brjoin 18446744073709551615 18446744073709551615 1 => i32:22
;; run: brjoin 18446744069414584320 1 0 => i32:11
;; run: brjoin 18446744069414584320 1 1 => i32:22
;; run: brjoin 1 18446744069414584320 0 => i32:22
;; run: brjoin 1 18446744069414584320 1 => i32:11
;; run: brjoin 9223372039002259456 9223372034707292160 0 => i32:22
;; run: brjoin 9223372039002259456 9223372034707292160 1 => i32:22
;; run: brjoin 11150031900141442680 11150031900141442680 0 => i32:22
;; run: brjoin 11150031900141442680 11150031900141442680 1 => i32:22
;; run: brjoin 11150031900141442680 1311768467463790320 0 => i32:11
;; run: brjoin 11150031900141442680 1311768467463790320 1 => i32:22
;; run: brjoin 1311768467463790320 11150031900141442680 0 => i32:22
;; run: brjoin 1311768467463790320 11150031900141442680 1 => i32:11
;; run: brjoinn 4294967297 4294967297 0 => i32:22
;; run: brjoinn 4294967297 4294967297 1 => i32:22
;; run: brjoinn 21474836481 12884901890 0 => i32:11
;; run: brjoinn 21474836481 12884901890 1 => i32:11
;; run: brjoinn 12884901890 21474836481 0 => i32:22
;; run: brjoinn 12884901890 21474836481 1 => i32:22
;; run: brjoinn 12884901889 21474836481 0 => i32:11
;; run: brjoinn 12884901889 21474836481 1 => i32:22
;; run: brjoinn 21474836481 12884901889 0 => i32:22
;; run: brjoinn 21474836481 12884901889 1 => i32:22
;; run: brjoinn 2147483648 1 0 => i32:22
;; run: brjoinn 2147483648 1 1 => i32:22
;; run: brjoinn 1 2147483648 0 => i32:11
;; run: brjoinn 1 2147483648 1 => i32:11
;; run: brjoinn 9223372036854775815 4294967303 0 => i32:22
;; run: brjoinn 9223372036854775815 4294967303 1 => i32:22
;; run: brjoinn 4294967303 9223372036854775815 0 => i32:11
;; run: brjoinn 4294967303 9223372036854775815 1 => i32:22
;; run: brjoinn 18446744073709551615 18446744073709551615 0 => i32:22
;; run: brjoinn 18446744073709551615 18446744073709551615 1 => i32:22
;; run: brjoinn 18446744069414584320 1 0 => i32:11
;; run: brjoinn 18446744069414584320 1 1 => i32:11
;; run: brjoinn 1 18446744069414584320 0 => i32:22
;; run: brjoinn 1 18446744069414584320 1 => i32:22
;; run: brjoinn 9223372039002259456 9223372034707292160 0 => i32:22
;; run: brjoinn 9223372039002259456 9223372034707292160 1 => i32:22
;; run: brjoinn 11150031900141442680 11150031900141442680 0 => i32:22
;; run: brjoinn 11150031900141442680 11150031900141442680 1 => i32:22
;; run: brjoinn 11150031900141442680 1311768467463790320 0 => i32:11
;; run: brjoinn 11150031900141442680 1311768467463790320 1 => i32:11
;; run: brjoinn 1311768467463790320 11150031900141442680 0 => i32:22
;; run: brjoinn 1311768467463790320 11150031900141442680 1 => i32:22
;; run: brload 4294967297 4294967297 => i32:11
;; run: brload 4294967301 8589934595 => i32:11
;; run: brload 8589934595 4294967301 => i32:11
;; run: brload 4294967299 4294967301 => i32:11
;; run: brload 4294967301 4294967299 => i32:11
;; run: brload 9223372036854775808 4294967296 => i32:22
;; run: brload 4294967296 9223372036854775808 => i32:22
;; run: brload 32212254720 30064771073 => i32:22
;; run: brload 30064771073 32212254720 => i32:11
;; run: brload 18446744073709551615 18446744073709551615 => i32:22
;; run: brload 4294967295 4294967296 => i32:22
;; run: brload 4294967296 4294967295 => i32:11
;; run: brload 9223372039002259456 9223372039002259455 => i32:22
;; run: brload 1311768467463790320 1311768467463790320 => i32:11
;; run: brload 1311768467463790320 11150031900141442680 => i32:22
;; run: brload 11150031900141442680 1311768467463790320 => i32:22
;; run: brload 21474836483 5 => i32:11
;; run: brload 21474836489 5 => i32:22
;; run: brload 21474836490 5 => i32:22
;; run: brthird 4294967297 4294967297 => i32:11
;; run: brthird 21474836481 12884901890 => i32:11
;; run: brthird 12884901890 21474836481 => i32:22
;; run: brthird 12884901889 21474836481 => i32:33
;; run: brthird 21474836481 12884901889 => i32:11
;; run: brthird 2147483648 1 => i32:22
;; run: brthird 1 2147483648 => i32:11
;; run: brthird 9223372036854775815 4294967303 => i32:11
;; run: brthird 4294967303 9223372036854775815 => i32:33
;; run: brthird 18446744073709551615 18446744073709551615 => i32:11
;; run: brthird 18446744069414584320 1 => i32:11
;; run: brthird 1 18446744069414584320 => i32:22
;; run: brthird 9223372039002259456 9223372034707292160 => i32:11
;; run: brthird 11150031900141442680 11150031900141442680 => i32:11
;; run: brthird 11150031900141442680 1311768467463790320 => i32:11
;; run: brthird 1311768467463790320 11150031900141442680 => i32:22
;; run: brcross 4294967297 4294967297 => i32:22
;; run: brcross 21474836481 12884901890 => i32:11
;; run: brcross 12884901890 21474836481 => i32:22
;; run: brcross 12884901889 21474836481 => i32:22
;; run: brcross 21474836481 12884901889 => i32:11
;; run: brcross 2147483648 1 => i32:22
;; run: brcross 1 2147483648 => i32:11
;; run: brcross 9223372036854775815 4294967303 => i32:11
;; run: brcross 4294967303 9223372036854775815 => i32:22
;; run: brcross 18446744073709551615 18446744073709551615 => i32:22
;; run: brcross 18446744069414584320 1 => i32:11
;; run: brcross 1 18446744069414584320 => i32:22
;; run: brcross 9223372039002259456 9223372034707292160 => i32:11
;; run: brcross 11150031900141442680 11150031900141442680 => i32:22
;; run: brcross 11150031900141442680 1311768467463790320 => i32:11
;; run: brcross 1311768467463790320 11150031900141442680 => i32:22
;; run: breh 4294967297 4294967297 0 => i32:22
;; run: breh 4294967297 4294967297 3 => i32:-7
;; run: breh 21474836481 12884901890 0 => i32:11
;; run: breh 21474836481 12884901890 3 => i32:-7
;; run: breh 12884901890 21474836481 0 => i32:22
;; run: breh 12884901890 21474836481 3 => i32:-7
;; run: breh 12884901889 21474836481 0 => i32:11
;; run: breh 12884901889 21474836481 3 => i32:-7
;; run: breh 21474836481 12884901889 0 => i32:22
;; run: breh 21474836481 12884901889 3 => i32:-7
;; run: breh 2147483648 1 0 => i32:22
;; run: breh 2147483648 1 3 => i32:-7
;; run: breh 1 2147483648 0 => i32:11
;; run: breh 1 2147483648 3 => i32:-7
;; run: breh 9223372036854775815 4294967303 0 => i32:22
;; run: breh 9223372036854775815 4294967303 3 => i32:-7
;; run: breh 4294967303 9223372036854775815 1 => i32:11
;; run: breh 18446744073709551615 18446744073709551615 1 => i32:22
;; run: breh 18446744069414584320 1 1 => i32:11
;; run: breh 1 18446744069414584320 1 => i32:22
;; run: breh 9223372039002259456 9223372034707292160 1 => i32:22
;; run: breh 11150031900141442680 11150031900141442680 1 => i32:22
;; run: breh 11150031900141442680 1311768467463790320 1 => i32:11
;; run: breh 1311768467463790320 11150031900141442680 1 => i32:22
;; run: brjoinp 4294967297 4294967297 0 => i32:122
;; run: brjoinp 4294967297 4294967297 1 => i32:211
;; run: brjoinp 4294967297 4294967297 2 => i32:322
;; run: brjoinp 4294967301 12884901890 0 => i32:111
;; run: brjoinp 4294967301 12884901890 1 => i32:222
;; run: brjoinp 4294967301 12884901890 2 => i32:311
;; run: brjoinp 8589934595 21474836481 0 => i32:122
;; run: brjoinp 8589934595 21474836481 1 => i32:211
;; run: brjoinp 8589934595 21474836481 2 => i32:322
;; run: brjoinp 4294967299 21474836481 0 => i32:111
;; run: brjoinp 4294967299 21474836481 1 => i32:211
;; run: brjoinp 4294967299 21474836481 2 => i32:322
;; run: brjoinp 4294967301 12884901889 0 => i32:122
;; run: brjoinp 4294967301 12884901889 1 => i32:222
;; run: brjoinp 4294967301 12884901889 2 => i32:322
;; run: brjoinp 9223372036854775808 1 0 => i32:122
;; run: brjoinp 9223372036854775808 1 1 => i32:211
;; run: brjoinp 9223372036854775808 1 2 => i32:322
;; run: brjoinp 4294967296 2147483648 0 => i32:111
;; run: brjoinp 4294967296 2147483648 1 => i32:211
;; run: brjoinp 4294967296 2147483648 2 => i32:311
;; run: brjoinp 32212254720 4294967303 0 => i32:122
;; run: brjoinp 32212254720 4294967303 1 => i32:222
;; run: brjoinp 32212254720 4294967303 2 => i32:322
;; run: brjoinp 30064771073 9223372036854775815 0 => i32:111
;; run: brjoinp 30064771073 9223372036854775815 1 => i32:211
;; run: brjoinp 30064771073 9223372036854775815 2 => i32:322
;; run: brjoinp 18446744073709551615 18446744073709551615 0 => i32:122
;; run: brjoinp 18446744073709551615 18446744073709551615 1 => i32:222
;; run: brjoinp 18446744073709551615 18446744073709551615 2 => i32:322
;; run: brjoinp 4294967295 1 0 => i32:111
;; run: brjoinp 4294967295 1 1 => i32:222
;; run: brjoinp 4294967295 1 2 => i32:311
;; run: brjoinp 4294967296 18446744069414584320 0 => i32:122
;; run: brjoinp 4294967296 18446744069414584320 1 => i32:211
;; run: brjoinp 4294967296 18446744069414584320 2 => i32:322
;; run: brjoinp 9223372039002259456 9223372034707292160 0 => i32:122
;; run: brjoinp 9223372039002259456 9223372034707292160 1 => i32:222
;; run: brjoinp 9223372039002259456 9223372034707292160 2 => i32:322
;; run: brjoinp 1311768467463790320 11150031900141442680 0 => i32:122
;; run: brjoinp 1311768467463790320 11150031900141442680 1 => i32:222
;; run: brjoinp 1311768467463790320 11150031900141442680 2 => i32:322
;; run: brjoinp 1311768467463790320 1311768467463790320 0 => i32:111
;; run: brjoinp 1311768467463790320 1311768467463790320 1 => i32:222
;; run: brjoinp 1311768467463790320 1311768467463790320 2 => i32:311
;; run: brjoinp 11150031900141442680 11150031900141442680 0 => i32:122
;; run: brjoinp 11150031900141442680 11150031900141442680 1 => i32:222
;; run: brjoinp 11150031900141442680 11150031900141442680 2 => i32:322
;; run: brgen 4294967297 1 1 => i32:22
;; run: brgen 21474836481 2 3 => i32:11
;; run: brgen 12884901890 1 5 => i32:22
;; run: brgen 12884901889 1 5 => i32:11
;; run: brgen 21474836481 1 3 => i32:22
;; run: brgen 2147483648 1 0 => i32:22
;; run: brgen 1 2147483648 0 => i32:11
;; run: brgen 9223372036854775815 7 1 => i32:22
;; run: brgen 4294967303 7 2147483648 => i32:11
;; run: brgen 18446744073709551615 4294967295 4294967295 => i32:22
;; run: brgen 18446744069414584320 1 0 => i32:11
;; run: brgen 1 0 4294967295 => i32:22
;; run: brgen 9223372039002259456 2147483648 2147483647 => i32:22
;; run: brgen 11150031900141442680 305419896 2596069104 => i32:22
;; run: brgen 11150031900141442680 2596069104 305419896 => i32:11
;; run: brgen 1311768467463790320 305419896 2596069104 => i32:22
;; run: brgensame 4294967297 1 1 => i32:22
;; run: brgensame 4294967301 2 3 => i32:11
;; run: brgensame 8589934595 1 5 => i32:22
;; run: brgensame 4294967299 1 5 => i32:11
;; run: brgensame 4294967301 1 3 => i32:22
;; run: brgensame 9223372036854775808 1 0 => i32:22
;; run: brgensame 4294967296 2147483648 0 => i32:11
;; run: brgensame 32212254720 7 1 => i32:22
;; run: brgensame 30064771073 7 2147483648 => i32:11
;; run: brgensame 18446744073709551615 4294967295 4294967295 => i32:22
;; run: brgensame 4294967295 1 0 => i32:11
;; run: brgensame 4294967296 0 4294967295 => i32:22
;; run: brgensame 9223372039002259456 2147483648 2147483647 => i32:22
;; run: brgensame 1311768467463790320 305419896 2596069104 => i32:22
;; run: brgensame 1311768467463790320 2596069104 305419896 => i32:11
;; run: brgensame 11150031900141442680 305419896 2596069104 => i32:22
;; run: brloop 5 9 16 => i32:2135742187
;; run: brloop 5 4 16 => i32:768867187
;; run: brloop 2147483648 1 16 => i32:-700376997
;; run: brloop 0 0 16 => i32:35872267
;; run: brloop 4294967295 4294967295 16 => i32:-1015466016
;; run: brloop 7 2147483648 12 => i32:26367187
;; run: brloop 2 0 3 => i32:13
;; run: brloop 9 9 0 => i32:0
;; run: brloopvar 5 9 16 => i32:1637649891
;; run: brloopvar 5 4 16 => i32:-735396984
;; run: brloopvar 2147483648 1 16 => i32:1696016536
;; run: brloopvar 0 0 16 => i32:2135742187
;; run: brloopvar 4294967295 4294967295 16 => i32:2135742187
;; run: brloopvar 7 2147483648 12 => i32:26367187
;; run: brloopvar 2 0 3 => i32:22
;; run: brloopvar 9 9 0 => i32:0
;; run: brloopc 5 9 16 => i32:768867187
;; run: brloopc 5 4 16 => i32:768867187
;; run: brloopc 2147483648 1 16 => i32:-700376997
;; run: brloopc 0 0 16 => i32:35872267
;; run: brloopc 4294967295 4294967295 16 => i32:-1468273069
;; run: brloopc 7 2147483648 12 => i32:9492187
;; run: brloopc 2 0 3 => i32:13
;; run: brloopc 9 9 0 => i32:0
;; run: brloopk 5 9 16 => i32:768867187
;; run: brloopk 5 4 16 => i32:768867187
;; run: brloopk 2147483648 1 16 => i32:768867187
;; run: brloopk 0 0 16 => i32:768867187
;; run: brloopk 4294967295 4294967295 16 => i32:276792187
;; run: brloopk 7 2147483648 12 => i32:2050312
;; run: brloopk 2 0 3 => i32:13
;; run: brloopk 9 9 0 => i32:0
;; run: brloopk 0 2147483647 16 => i32:768867187
;; run: brloopk 0 2147483649 16 => i32:276792187
;; run: brloopvarn 5 9 16 => i32:2135742187
;; run: brloopvarn 5 4 16 => i32:768867187
;; run: brloopvarn 2147483648 1 16 => i32:-700376997
;; run: brloopvarn 0 0 16 => i32:35872267
;; run: brloopvarn 4294967295 4294967295 16 => i32:-1468273069
;; run: brloopvarn 7 2147483648 12 => i32:26367187
;; run: brloopvarn 2 0 3 => i32:13
;; run: brloopvarn 9 9 0 => i32:0
;; run: brlooprot 38654705669 16 => i32:2135742187
;; run: brlooprot 12884901893 16 => i32:1637649891
;; run: brlooprot 0 16 => i32:-1015466016
;; run: brlooprot 18446744069414584322 16 => i32:-700376997
;; run: brlooprot 18446744073709551615 16 => i32:35872267
;; run: brlooprot 9223372036854775815 12 => i32:2050312
;; run: brlooprot 6 0 => i32:0
;; run: brloopp 5 16 99 => i32:461320312
;; run: brloopp 5 16 3 => i32:768867187
;; run: brloopp 7 16 8 => i32:2135742187
;; run: brloopp 2147483648 16 0 => i32:1297760720
;; run: brloopp 2 5 2 => i32:121
;; run: brloopp 9 0 0 => i32:0
;; run: brback 38654705669 16 => i32:19
;; run: brback 12884901893 16 => i32:35
;; run: brback 0 16 => i32:261
;; run: brback 18446744069414584322 16 => i32:83
;; run: brback 42949672965 16 => i32:3
;; run: brback 18446744073709551615 16 => i32:3
;; run: brback 0 0 => i32:5
;; run: brback 6 2 => i32:3
;; run: queue 0 => i32:0
;; run: queue 1 => i32:69
;; run: queue 2 => i32:879
;; run: queue 5 => i32:2143708130
;; run: queue 9 => i32:-809236449
;; run: queue 16 => i32:-381479792

(module
  (type $t1 (func (param i32)))
  (memory 1)
  (table 1 funcref)
  (elem (i32.const 0) $thrower)
  (tag $e)
  (data (i32.const 0) "\05\00\00\00\09\00\00\00\05\00\00\00\03\00\00\00\02\00\00\00\ff\ff\ff\ff\00\00\00\80\01\00\00\00\05\00\00\00\09\00\00\00\00\00\00\00\00\00\00\00\ff\ff\ff\ff\ff\ff\ff\ff\00\00\00\80\00\00\00\80\07\00\00\00\ff\ff\ff\7f\07\00\00\00\00\00\00\80\01\00\00\00\02\00\00\00\02\00\00\00\01\00\00\00\ff\ff\ff\7f\05\00\00\00\03\00\00\00\03\00\00\00\05\00\00\00\04\00\00\00\fe\ff\ff\ff\00\00\00\00")
  ;; throws at 3; called through the table at an index read from memory, which keeps it out of line
  (func $thrower (type $t1) (if (i32.eq (local.get 0) (i32.const 3)) (then (throw $e))))
  (func $rot (export "rot") (param $a i64) (param $b i64) (result i32)
    (i32.add (select (i64.lt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32))) (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))) (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))) (i32.const 100)))
  (func $rotne (export "rotne") (param $a i64) (param $b i64) (result i32)
    (select (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))) (i64.lt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32))) (i32.ne (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))))
  (func $rotge (export "rotge") (param $a i64) (param $b i64) (result i32)
    (select (i64.ge_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32))) (i32.ge_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))) (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))))
  (func $strict (export "strict") (param $a i64) (param $b i64) (result i32)
    (select (i64.gt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32))) (i32.ge_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))) (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))))
  (func $loose (export "loose") (param $a i64) (param $b i64) (result i32)
    (select (i64.le_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32))) (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))) (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))))
  (func $swapped (export "swapped") (param $a i64) (param $b i64) (result i32)
    (select (i64.lt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32))) (i32.gt_u (i32.wrap_i64 (local.get $b)) (i32.wrap_i64 (local.get $a))) (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))))
  (func $hi32 (export "hi32") (param $a i64) (param $b i64) (result i32)
    (select (i32.lt_u (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))) (i32.wrap_i64 (i64.shr_u (local.get $b) (i64.const 32)))) (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))) (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))))
  (func $masked (export "masked") (param $a i64) (param $b i64) (result i32)
    (select (i64.lt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32))) (i64.lt_u (i64.and (local.get $a) (i64.const 0xffffffff)) (i64.and (local.get $b) (i64.const 0xffffffff))) (i64.eq (i64.and (local.get $a) (i64.const 0xffffffff)) (i64.and (local.get $b) (i64.const 0xffffffff)))))
  (func $cross (export "cross") (param $a i64) (param $b i64) (result i32)
    (select (i64.gt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32))) (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))) (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))))
  (func $same (export "same") (param $a i64) (param $b i64) (result i32)
    (select (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))) (i64.lt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32))) (i64.eq (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32)))))
  (func $samesg (export "samesg") (param $a i64) (param $b i64) (result i32)
    (select (i32.le_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))) (i32.lt_s (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))) (i32.wrap_i64 (i64.shr_u (local.get $b) (i64.const 32)))) (i32.eq (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))) (i32.wrap_i64 (i64.shr_u (local.get $b) (i64.const 32))))))
  (func $rotsg (export "rotsg") (param $a i64) (param $b i64) (result i32)
    (select (i64.ge_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32))) (i32.gt_s (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))) (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))))
  (func $nsigned (export "nsigned") (param $a i64) (param $b i64) (result i32)
    (select (i32.lt_s (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))) (i32.wrap_i64 (i64.shr_u (local.get $b) (i64.const 32)))) (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))) (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))))
  (func $npos (export "npos") (param $a i64) (param $b i64) (result i32)
    (select (i64.lt_s (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32))) (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))) (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))))
  (func $mpos (export "mpos") (param $a i64) (param $b i64) (result i32)
    (i32.add (select (i64.lt_u (i64.and (local.get $a) (i64.const 0x1ffffffff)) (i64.and (local.get $b) (i64.const 0x1ffffffff))) (i64.lt_s (i64.shr_u (local.get $a) (i64.const 33)) (i64.shr_u (local.get $b) (i64.const 33))) (i64.eq (i64.shr_u (local.get $a) (i64.const 33)) (i64.shr_u (local.get $b) (i64.const 33)))) (i32.const 200)))
  (func $uses (export "uses") (param $a i64) (param $b i64) (result i32)
    (local $c i32)
    (local.set $c (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))))
    (i32.add (local.get $c) (select (i64.lt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32))) (local.get $c) (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))))))
  (func $other (export "other") (param $a i64) (param $b i64) (result i32)
    (select (i64.lt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32))) (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (i64.shr_u (local.get $b) (i64.const 32)))) (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))))
  (func $eqarm (export "eqarm") (param $a i64) (param $b i64) (result i32)
    (select (i64.eq (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32))) (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))) (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))))
  (func $half16 (export "half16") (param $a i32) (param $b i32) (result i32)
    (select (i32.lt_u (i32.shr_u (local.get $a) (i32.const 16)) (i32.shr_u (local.get $b) (i32.const 16))) (i32.lt_u (i32.and (local.get $a) (i32.const 0xffff)) (i32.and (local.get $b) (i32.const 0xffff))) (i32.eq (i32.and (local.get $a) (i32.const 0xffff)) (i32.and (local.get $b) (i32.const 0xffff)))))
  (func $part8 (export "part8") (param $a i32) (param $b i32) (result i32)
    (select (i32.le_u (i32.shr_u (local.get $a) (i32.const 8)) (i32.shr_u (local.get $b) (i32.const 8))) (i32.lt_u (i32.and (local.get $a) (i32.const 255)) (i32.and (local.get $b) (i32.const 255))) (i32.eq (i32.and (local.get $a) (i32.const 255)) (i32.and (local.get $b) (i32.const 255)))))
  (func $gap (export "gap") (param $a i32) (param $b i32) (result i32)
    (select (i32.lt_u (i32.shr_u (local.get $a) (i32.const 16)) (i32.shr_u (local.get $b) (i32.const 16))) (i32.lt_u (i32.and (local.get $a) (i32.const 255)) (i32.and (local.get $b) (i32.const 255))) (i32.eq (i32.and (local.get $a) (i32.const 255)) (i32.and (local.get $b) (i32.const 255)))))
  (func $overlap (export "overlap") (param $a i32) (param $b i32) (result i32)
    (select (i32.lt_u (i32.shr_u (local.get $a) (i32.const 8)) (i32.shr_u (local.get $b) (i32.const 8))) (i32.lt_u (i32.and (local.get $a) (i32.const 0xffff)) (i32.and (local.get $b) (i32.const 0xffff))) (i32.eq (i32.and (local.get $a) (i32.const 0xffff)) (i32.and (local.get $b) (i32.const 0xffff)))))
  (func $notmask (export "notmask") (param $a i32) (param $b i32) (result i32)
    (select (i32.lt_u (i32.shr_u (local.get $a) (i32.const 16)) (i32.shr_u (local.get $b) (i32.const 16))) (i32.lt_u (i32.and (local.get $a) (i32.const 0xfffe)) (i32.and (local.get $b) (i32.const 0xfffe))) (i32.eq (i32.and (local.get $a) (i32.const 0xfffe)) (i32.and (local.get $b) (i32.const 0xfffe)))))
  (func $shrs (export "shrs") (param $a i32) (param $b i32) (result i32)
    (select (i32.lt_u (i32.shr_s (local.get $a) (i32.const 16)) (i32.shr_s (local.get $b) (i32.const 16))) (i32.lt_u (i32.and (local.get $a) (i32.const 0xffff)) (i32.and (local.get $b) (i32.const 0xffff))) (i32.eq (i32.and (local.get $a) (i32.const 0xffff)) (i32.and (local.get $b) (i32.const 0xffff)))))
  (func $ext16 (export "ext16") (param $a i32) (param $b i32) (result i32)
    (i32.add (select (i64.lt_u (i64.extend_i32_u (i32.shr_u (local.get $a) (i32.const 16))) (i64.extend_i32_u (i32.shr_u (local.get $b) (i32.const 16)))) (i64.lt_u (i64.extend_i32_u (i32.and (local.get $a) (i32.const 0xffff))) (i64.extend_i32_u (i32.and (local.get $b) (i32.const 0xffff)))) (i64.eq (i64.extend_i32_u (i32.and (local.get $a) (i32.const 0xffff))) (i64.extend_i32_u (i32.and (local.get $b) (i32.const 0xffff))))) (i32.const 300)))
  (func $shmod (export "shmod") (param $a i32) (param $b i32) (result i32)
    (i32.add (select (i32.lt_u (i32.shr_u (local.get $a) (i32.const 48)) (i32.shr_u (local.get $b) (i32.const 48))) (i32.lt_u (i32.and (local.get $a) (i32.const 0xffff)) (i32.and (local.get $b) (i32.const 0xffff))) (i32.eq (i32.and (local.get $a) (i32.const 0xffff)) (i32.and (local.get $b) (i32.const 0xffff)))) (i32.const 400)))
  (func $upper (export "upper") (param $a i32) (param $b i32) (result i32)
    (select (i32.lt_u (i32.shr_u (local.get $a) (i32.const 24)) (i32.shr_u (local.get $b) (i32.const 24))) (i32.lt_u (i32.shr_u (local.get $a) (i32.const 8)) (i32.shr_u (local.get $b) (i32.const 8))) (i32.eq (i32.shr_u (local.get $a) (i32.const 8)) (i32.shr_u (local.get $b) (i32.const 8)))))
  (func $short (export "short") (param $a i32) (param $b i32) (result i32)
    (select (i32.lt_u (i32.and (i32.shr_u (local.get $a) (i32.const 8)) (i32.const 255)) (i32.and (i32.shr_u (local.get $b) (i32.const 8)) (i32.const 255))) (i32.lt_u (i32.and (local.get $a) (i32.const 255)) (i32.and (local.get $b) (i32.const 255))) (i32.eq (i32.and (local.get $a) (i32.const 255)) (i32.and (local.get $b) (i32.const 255)))))
  (func $nshift (export "nshift") (param $a i32) (param $b i32) (result i32)
    (select (i32.lt_u (i32.and (i32.shr_u (local.get $a) (i32.const 8)) (i32.const 65535)) (i32.and (i32.shr_u (local.get $b) (i32.const 8)) (i32.const 65535))) (i32.lt_u (i32.shr_u (local.get $a) (i32.const 16)) (i32.shr_u (local.get $b) (i32.const 16))) (i32.eq (i32.shr_u (local.get $a) (i32.const 16)) (i32.shr_u (local.get $b) (i32.const 16)))))
  (func $gaphi (export "gaphi") (param $a i32) (param $b i32) (result i32)
    (select (i32.lt_u (i32.and (local.get $a) (i32.const 255)) (i32.and (local.get $b) (i32.const 255))) (i32.lt_u (i32.shr_u (local.get $a) (i32.const 16)) (i32.shr_u (local.get $b) (i32.const 16))) (i32.eq (i32.shr_u (local.get $a) (i32.const 16)) (i32.shr_u (local.get $b) (i32.const 16)))))
  (func $shorthi (export "shorthi") (param $a i32) (param $b i32) (result i32)
    (select (i32.lt_u (i32.and (local.get $a) (i32.const 255)) (i32.and (local.get $b) (i32.const 255))) (i32.lt_u (i32.and (i32.shr_u (local.get $a) (i32.const 8)) (i32.const 255)) (i32.and (i32.shr_u (local.get $b) (i32.const 8)) (i32.const 255))) (i32.eq (i32.and (i32.shr_u (local.get $a) (i32.const 8)) (i32.const 255)) (i32.and (i32.shr_u (local.get $b) (i32.const 8)) (i32.const 255)))))
  (func $sgle (export "sgle") (param $a i64) (param $b i64) (result i32)
    (select (i64.le_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32))) (i32.le_s (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))) (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))))
  (func $sgge (export "sgge") (param $a i64) (param $b i64) (result i32)
    (select (i64.gt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32))) (i32.ge_s (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))) (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))))
  (func $usescond (export "usescond") (param $a i64) (param $b i64) (result i32)
    (local $c i32)
    (local.set $c (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))))
    (i32.add (local.get $c) (select (i64.lt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32))) (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))) (local.get $c))))
  (func $usesn (export "usesn") (param $a i64) (param $b i64) (result i32)
    (local $c i32)
    (local.set $c (i64.lt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32))))
    (i32.add (local.get $c) (select (local.get $c) (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))) (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))))))
  (func $gen (export "gen") (param $a i64) (param $m i32) (param $n i32) (result i32)
    (i32.add (select (i32.lt_u (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))) (local.get $n)) (i32.lt_u (i32.wrap_i64 (local.get $a)) (local.get $m)) (i32.eq (i32.wrap_i64 (local.get $a)) (local.get $m))) (i32.const 500)))
  (func $gensg (export "gensg") (param $a i64) (param $m i32) (param $n i32) (result i32)
    (select (i32.gt_u (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))) (local.get $n)) (i32.gt_s (i32.wrap_i64 (local.get $a)) (local.get $m)) (i32.eq (i32.wrap_i64 (local.get $a)) (local.get $m))))
  (func $genwide (export "genwide") (param $a i64) (param $m i32) (param $n i64) (result i32)
    (select (i64.lt_u (i64.shr_u (local.get $a) (i64.const 32)) (local.get $n)) (i32.lt_u (i32.wrap_i64 (local.get $a)) (local.get $m)) (i32.eq (i32.wrap_i64 (local.get $a)) (local.get $m))))
  (func $genfits (export "genfits") (param $a i64) (param $m i32) (param $n i64) (result i32)
    (select (i64.lt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.and (local.get $n) (i64.const 0xffffffff))) (i32.lt_u (i32.wrap_i64 (local.get $a)) (local.get $m)) (i32.eq (i32.wrap_i64 (local.get $a)) (local.get $m))))
  (func $genmwide (export "genmwide") (param $a i64) (param $m i64) (param $n i32) (result i32)
    (select (i32.lt_u (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))) (local.get $n)) (i64.lt_u (i64.and (local.get $a) (i64.const 0xffffffff)) (local.get $m)) (i64.eq (i64.and (local.get $a) (i64.const 0xffffffff)) (local.get $m))))
  (func $genconst (export "genconst") (param $a i64) (param $m i32) (param $n i32) (result i32)
    (select (i32.lt_u (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))) (i32.const 3)) (i32.lt_u (i32.wrap_i64 (local.get $a)) (local.get $m)) (i32.eq (i32.wrap_i64 (local.get $a)) (local.get $m))))
  (func $both (export "both") (param $a i64) (param $m i32) (param $n i32) (result i32)
    (select (i32.le_u (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))) (i32.const 0x80000003)) (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.const 5)) (i32.eq (i32.wrap_i64 (local.get $a)) (i32.const 5))))
  (func $bothsg (export "bothsg") (param $a i64) (param $m i32) (param $n i32) (result i32)
    (select (i32.lt_u (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))) (i32.const 9)) (i32.lt_s (i32.wrap_i64 (local.get $a)) (i32.const -5)) (i32.eq (i32.wrap_i64 (local.get $a)) (i32.const -5))))
  (func $mixed (export "mixed") (param $a i64) (param $m i32) (param $n i32) (result i32)
    (select (i32.lt_u (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))) (i32.shr_u (local.get $m) (i32.const 16))) (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.and (local.get $m) (i32.const 0xffff))) (i32.eq (i32.wrap_i64 (local.get $a)) (i32.and (local.get $m) (i32.const 0xffff)))))
  (func $mixedrev (export "mixedrev") (param $a i64) (param $m i32) (param $n i32) (result i32)
    (i32.add (select (i32.gt_u (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))) (i32.shr_u (local.get $m) (i32.const 16))) (i32.gt_u (i32.wrap_i64 (local.get $a)) (i32.and (local.get $m) (i32.const 0xffff))) (i32.eq (i32.wrap_i64 (local.get $a)) (i32.and (local.get $m) (i32.const 0xffff)))) (i32.const 600)))
  (func $w32m (export "w32m") (param $a i32) (param $m i64) (param $n i32) (result i32)
    (select (i32.lt_u (i32.shr_u (local.get $a) (i32.const 16)) (i32.and (local.get $n) (i32.const 65535))) (i64.lt_u (i64.extend_i32_u (i32.and (local.get $a) (i32.const 0xffff))) (i64.and (local.get $m) (i64.const 0xffff))) (i64.eq (i64.extend_i32_u (i32.and (local.get $a) (i32.const 0xffff))) (i64.and (local.get $m) (i64.const 0xffff)))))
  (func $w32n (export "w32n") (param $a i32) (param $m i32) (param $n i64) (result i32)
    (select (i64.lt_u (i64.extend_i32_u (i32.shr_u (local.get $a) (i32.const 16))) (i64.and (local.get $n) (i64.const 0xffff))) (i32.lt_u (i32.and (local.get $a) (i32.const 0xffff)) (i32.and (local.get $m) (i32.const 65535))) (i32.eq (i32.and (local.get $a) (i32.const 0xffff)) (i32.and (local.get $m) (i32.const 65535)))))
  (func $same8 (export "same8") (param $a i32) (param $m i32) (param $n i32) (result i32)
    (select (i32.lt_u (i32.and (local.get $a) (i32.const 255)) (i32.and (local.get $n) (i32.const 255))) (i32.lt_u (i32.shr_u (local.get $a) (i32.const 8)) (i32.and (local.get $m) (i32.const 16777215))) (i32.eq (i32.shr_u (local.get $a) (i32.const 8)) (i32.and (local.get $m) (i32.const 16777215)))))
  (func $sg16 (export "sg16") (param $a i32) (param $m i32) (param $n i32) (result i32)
    (select (i32.lt_u (i32.shr_u (local.get $a) (i32.const 16)) (i32.and (local.get $n) (i32.const 65535))) (i32.lt_s (i32.and (local.get $a) (i32.const 0xffff)) (local.get $m)) (i32.eq (i32.and (local.get $a) (i32.const 0xffff)) (local.get $m))))
  (func $sgmix (export "sgmix") (param $a i64) (param $m i32) (param $n i32) (result i32)
    (select (i32.lt_u (i32.wrap_i64 (i64.shr_u (i64.and (local.get $a) (i64.const 0xffffffff7fffffff)) (i64.const 32))) (local.get $n)) (i32.lt_s (i32.wrap_i64 (i64.and (local.get $a) (i64.const 0xffffffff7fffffff))) (local.get $m)) (i32.eq (i32.wrap_i64 (i64.and (local.get $a) (i64.const 0xffffffff7fffffff))) (local.get $m))))
  (func $nsgmix (export "nsgmix") (param $a i64) (param $m i32) (param $n i32) (result i32)
    (select (i32.lt_s (i32.and (local.get $n) (i32.const 2147483647)) (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32)))) (i32.lt_u (local.get $m) (i32.wrap_i64 (local.get $a))) (i32.eq (local.get $m) (i32.wrap_i64 (local.get $a)))))
  (func $sgmixrev (export "sgmixrev") (param $a i64) (param $m i32) (param $n i32) (result i32)
    (select (i32.gt_u (i32.wrap_i64 (i64.shr_u (i64.and (local.get $a) (i64.const 0xffffffff7fffffff)) (i64.const 32))) (local.get $n)) (i32.lt_s (local.get $m) (i32.wrap_i64 (i64.and (local.get $a) (i64.const 0xffffffff7fffffff)))) (i32.eq (i32.wrap_i64 (i64.and (local.get $a) (i64.const 0xffffffff7fffffff))) (local.get $m))))
  (func $nsgmixrev (export "nsgmixrev") (param $a i64) (param $m i32) (param $n i32) (result i32)
    (select (i32.lt_s (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))) (i32.and (local.get $n) (i32.const 2147483647))) (i32.gt_u (local.get $m) (i32.wrap_i64 (local.get $a))) (i32.eq (local.get $m) (i32.wrap_i64 (local.get $a)))))
  (func $mixedw (export "mixedw") (param $a i64) (param $m i32) (param $n i32) (result i32)
    (select (i64.lt_u (i64.shr_u (local.get $a) (i64.const 48)) (i64.extend_i32_u (i32.shr_u (local.get $m) (i32.const 16)))) (i64.lt_u (i64.and (local.get $a) (i64.const 0xffffffffffff)) (i64.extend_i32_u (i32.and (local.get $m) (i32.const 65535)))) (i64.eq (i64.and (local.get $a) (i64.const 0xffffffffffff)) (i64.extend_i32_u (i32.and (local.get $m) (i32.const 65535))))))
  (func $mixedc (export "mixedc") (param $a i64) (param $b i64) (result i32)
    (select (i64.lt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 16))) (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.and (i32.wrap_i64 (local.get $b)) (i32.const 65535))) (i32.eq (i32.wrap_i64 (local.get $a)) (i32.and (i32.wrap_i64 (local.get $b)) (i32.const 65535)))))
  (func $genwrap (export "genwrap") (param $a i64) (param $m i64) (param $n i64) (result i32)
    (select (i32.lt_u (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))) (i32.wrap_i64 (local.get $n))) (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $m))) (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $m)))))
  (func $two (export "two") (param $m i32) (param $n i32) (param $p i32) (param $q i32) (result i32)
    (select (i32.lt_u (local.get $n) (local.get $q)) (i32.lt_u (local.get $m) (local.get $p)) (i32.eq (local.get $m) (local.get $p))))
  (func $br (export "br") (param $a i64) (param $b i64) (result i32)
    (block $f
      (block $t
        (if (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))
          (then (br_if $t (i64.ge_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32)))) (br $f))
          (else (br_if $t (i32.ge_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))) (br $f))))
      (i32.store (i32.const 512) (i32.const 1))
      (return (i32.const 11)))
    (i32.store (i32.const 516) (i32.const 2))
    (i32.const 22))
  (func $brne (export "brne") (param $a i64) (param $b i64) (result i32)
    (block $f
      (block $t
        (if (i32.ne (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))
          (then (br_if $t (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))) (br $f))
          (else (br_if $t (i64.lt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32)))) (br $f))))
      (i32.store (i32.const 512) (i32.const 1))
      (return (i32.const 11)))
    (i32.store (i32.const 516) (i32.const 2))
    (i32.const 22))
  (func $brswap (export "brswap") (param $a i64) (param $b i64) (result i32)
    (block $f
      (block $t
        (if (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))
          (then (br_if $f (i64.lt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32)))) (br $t))
          (else (br_if $t (i32.ge_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))) (br $f))))
      (i32.store (i32.const 512) (i32.const 1))
      (return (i32.const 13)))
    (i32.store (i32.const 516) (i32.const 2))
    (i32.const 24))
  (func $brsame (export "brsame") (param $a i64) (param $b i64) (result i32)
    (block $f
      (block $t
        (if (i64.eq (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32)))
          (then (br_if $t (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))) (br $f))
          (else (br_if $t (i64.lt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32)))) (br $f))))
      (i32.store (i32.const 512) (i32.const 1))
      (return (i32.const 11)))
    (i32.store (i32.const 516) (i32.const 2))
    (i32.const 22))
  (func $brvals (export "brvals") (param $a i64) (param $b i64) (result i32)
    (local $x i32) (local $y i32)
    (local.set $x (i32.add (i32.wrap_i64 (local.get $a)) (i32.const 3)))
    (local.set $y (i32.xor (i32.wrap_i64 (i64.shr_u (local.get $b) (i64.const 32))) (i32.const 7)))
    (block $f
      (block $t
        (if (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))
          (then (br_if $t (i64.lt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32)))) (br $f))
          (else (br_if $t (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))) (br $f))))
      (i32.store (i32.const 512) (i32.const 1))
      (return (local.get $x)))
    (i32.store (i32.const 516) (i32.const 2))
    (local.get $y))
  (func $brconst (export "brconst") (param $a i64) (param $b i64) (result i32)
    (local $x i32)
    (block $f
      (block $t
        (if (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))
          (then (local.set $x (i32.const 77)) (br_if $t (i64.lt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32)))) (br $f))
          (else (local.set $x (i32.const 77)) (br_if $t (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))) (br $f))))
      (i32.store (i32.const 512) (i32.const 1))
      (return (i32.add (local.get $x) (i32.const 1))))
    (i32.store (i32.const 516) (i32.const 2))
    (i32.add (local.get $x) (i32.const 2)))
  (func $brdiffer (export "brdiffer") (param $a i64) (param $b i64) (result i32)
    (local $x i32)
    (block $f
      (block $t
        (if (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))
          (then (local.set $x (i32.const 77)) (br_if $t (i64.lt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32)))) (br $f))
          (else (local.set $x (i32.const 55)) (br_if $t (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))) (br $f))))
      (i32.store (i32.const 512) (i32.const 1))
      (return (i32.add (local.get $x) (i32.const 1))))
    (i32.store (i32.const 516) (i32.const 2))
    (i32.add (local.get $x) (i32.const 2)))
  (func $brstore (export "brstore") (param $a i64) (param $b i64) (result i32)
    (i64.store (i32.const 520) (i64.const 0))
    (block $f
      (block $t
        (if (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))
          (then (i32.store (i32.add (i32.const 520) (i32.and (i32.wrap_i64 (local.get $a)) (i32.const 4))) (i32.const 3)) (br_if $t (i64.lt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32)))) (br $f))
          (else (br_if $t (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))) (br $f))))
      (i32.store (i32.const 512) (i32.const 1))
      (return (i32.add (i32.const 11) (i32.add (i32.load (i32.const 520)) (i32.shl (i32.load (i32.const 524)) (i32.const 4))))))
    (i32.store (i32.const 516) (i32.const 2))
    (i32.add (i32.const 22) (i32.add (i32.load (i32.const 520)) (i32.shl (i32.load (i32.const 524)) (i32.const 4)))))
  (func $brstoren (export "brstoren") (param $a i64) (param $b i64) (result i32)
    (i64.store (i32.const 520) (i64.const 0))
    (block $f
      (block $t
        (if (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))
          (then (br_if $t (i64.lt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32)))) (br $f))
          (else (i32.store (i32.add (i32.const 520) (i32.and (i32.wrap_i64 (local.get $a)) (i32.const 4))) (i32.const 3)) (br_if $t (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))) (br $f))))
      (i32.store (i32.const 512) (i32.const 1))
      (return (i32.add (i32.const 11) (i32.add (i32.load (i32.const 520)) (i32.shl (i32.load (i32.const 524)) (i32.const 4))))))
    (i32.store (i32.const 516) (i32.const 2))
    (i32.add (i32.const 22) (i32.add (i32.load (i32.const 520)) (i32.shl (i32.load (i32.const 524)) (i32.const 4)))))
  (func $brhi32 (export "brhi32") (param $a i64) (param $b i64) (result i32)
    (block $f
      (block $t
        (if (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))
          (then (br_if $t (i32.gt_u (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))) (i32.wrap_i64 (i64.shr_u (local.get $b) (i64.const 32))))) (br $f))
          (else (br_if $t (i32.gt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))) (br $f))))
      (i32.store (i32.const 512) (i32.const 1))
      (return (i32.const 15)))
    (i32.store (i32.const 516) (i32.const 2))
    (i32.const 26))
  (func $brmask16 (export "brmask16") (param $a i32) (param $b i32) (result i32)
    (block $f
      (block $t
        (if (i32.eq (i32.shr_u (local.get $a) (i32.const 16)) (i32.shr_u (local.get $b) (i32.const 16)))
          (then (br_if $t (i32.le_u (i32.and (local.get $a) (i32.const 0xffff)) (i32.and (local.get $b) (i32.const 0xffff)))) (br $f))
          (else (br_if $t (i32.lt_u (i32.shr_u (local.get $a) (i32.const 16)) (i32.shr_u (local.get $b) (i32.const 16)))) (br $f))))
      (i32.store (i32.const 512) (i32.const 1))
      (return (i32.const 17)))
    (i32.store (i32.const 516) (i32.const 2))
    (i32.const 28))
  (func $brjoin (export "brjoin") (param $a i64) (param $b i64) (param $g i32) (result i32)
    (local $la i32) (local $lb i32) (local $ha i32) (local $hb i32)
    (local.set $la (i32.wrap_i64 (local.get $a)))
    (local.set $lb (i32.wrap_i64 (local.get $b)))
    (local.set $ha (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))))
    (local.set $hb (i32.wrap_i64 (i64.shr_u (local.get $b) (i64.const 32))))
    (block $f
      (block $t
        (block $e
          (block $n
            (br_if $e (local.get $g))
            (br_if $e (i32.eq (local.get $la) (local.get $lb))))
          (br_if $t (i32.lt_u (local.get $la) (local.get $lb)))
          (br $f))
        (br_if $t (i32.lt_u (local.get $ha) (local.get $hb)))
        (br $f))
      (i32.store (i32.const 512) (local.get $g))
      (return (i32.const 11)))
    (i32.store (i32.const 516) (local.get $g))
    (i32.const 22))
  (func $brjoinn (export "brjoinn") (param $a i64) (param $b i64) (param $g i32) (result i32)
    (local $la i32) (local $lb i32) (local $ha i32) (local $hb i32)
    (local.set $la (i32.wrap_i64 (local.get $a)))
    (local.set $lb (i32.wrap_i64 (local.get $b)))
    (local.set $ha (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))))
    (local.set $hb (i32.wrap_i64 (i64.shr_u (local.get $b) (i64.const 32))))
    (block $f
      (block $t
        (block $e
          (block $n
            (br_if $n (local.get $g))
            (br_if $e (i32.eq (local.get $la) (local.get $lb))))
          (br_if $t (i32.lt_u (local.get $la) (local.get $lb)))
          (br $f))
        (br_if $t (i32.lt_u (local.get $ha) (local.get $hb)))
        (br $f))
      (i32.store (i32.const 512) (local.get $g))
      (return (i32.const 11)))
    (i32.store (i32.const 516) (local.get $g))
    (i32.const 22))
  (func $brload (export "brload") (param $a i64) (param $b i64) (result i32)
    (block $f
      (block $t
        (if (i32.eq (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))) (i32.wrap_i64 (local.get $b)))
          (then (br_if $t (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.load (i32.const 4)))) (br $f))
          (else (br_if $t (i32.lt_u (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))) (i32.wrap_i64 (local.get $b)))) (br $f))))
      (i32.store (i32.const 512) (i32.const 1))
      (return (i32.const 11)))
    (i32.store (i32.const 516) (i32.const 2))
    (i32.const 22))
  (func $brthird (export "brthird") (param $a i64) (param $b i64) (result i32)
    (block $f
      (block $t
        (if (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))
          (then (br_if $t (i64.ge_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32)))) (return (i32.const 33)))
          (else (br_if $t (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))) (br $f))))
      (i32.store (i32.const 512) (i32.const 1))
      (return (i32.const 11)))
    (i32.store (i32.const 516) (i32.const 2))
    (i32.const 22))
  (func $brcross (export "brcross") (param $a i64) (param $b i64) (result i32)
    (block $f
      (block $t
        (if (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))
          (then (br_if $t (i64.gt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32)))) (br $f))
          (else (br_if $t (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b)))) (br $f))))
      (i32.store (i32.const 512) (i32.const 1))
      (return (i32.const 11)))
    (i32.store (i32.const 516) (i32.const 2))
    (i32.const 22))
  (func $breh (export "breh") (param $a i64) (param $b i64) (param $g i32) (result i32)
    (local $c i32)
    (block $h
      (try_table (catch_all $h)
        (block $f
          (block $t
            (block $e
              (local.set $c (i32.eq (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))))
              (call_indirect (type $t1) (local.get $g) (i32.load8_u (i32.const 220)))
              (br_if $e (local.get $c))
              (br_if $t (i32.lt_u (i32.wrap_i64 (local.get $a)) (i32.wrap_i64 (local.get $b))))
              (br $f))
            (br_if $t (i64.lt_u (i64.shr_u (local.get $a) (i64.const 32)) (i64.shr_u (local.get $b) (i64.const 32))))
            (br $f))
          (i32.store (i32.const 512) (local.get $g))
          (return (i32.const 11)))
        (i32.store (i32.const 516) (local.get $g))
        (return (i32.const 22))))
    (i32.const -7))
  (func $brjoinp (export "brjoinp") (param $a i64) (param $b i64) (param $g i32) (result i32)
    (local $y i32) (local $r i32)
    (local $la i32) (local $lb i32) (local $ha i32) (local $hb i32)
    (local.set $la (i32.wrap_i64 (local.get $a)))
    (local.set $lb (i32.wrap_i64 (local.get $b)))
    (local.set $ha (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))))
    (local.set $hb (i32.wrap_i64 (i64.shr_u (local.get $b) (i64.const 32))))
    (local.set $y (local.get $hb))
    (local.set $r (i32.const 100))
    (block $f
      (block $t
        (block $e
          (block $n
            (if (i32.eq (local.get $g) (i32.const 1))
              (then (local.set $y (i32.const 5)) (local.set $r (i32.const 200)) (br $e)))
            (if (i32.eq (local.get $g) (i32.const 2))
              (then (local.set $r (i32.const 300)) (br $n)))
            (br_if $e (i32.eq (local.get $ha) (local.get $lb))))
          (br_if $t (i32.lt_u (local.get $ha) (local.get $lb)))
          (br $f))
        (br_if $t (i32.lt_u (local.get $la) (local.get $y)))
        (br $f))
      (i32.store (i32.const 512) (local.get $g))
      (return (i32.add (local.get $r) (i32.const 11))))
    (i32.store (i32.const 516) (local.get $g))
    (i32.add (local.get $r) (i32.const 22)))
  (func $brgen (export "brgen") (param $a i64) (param $m i32) (param $n i32) (result i32)
    (block $f
      (block $t
        (if (i32.eq (i32.wrap_i64 (local.get $a)) (local.get $m))
          (then (br_if $t (i32.lt_u (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))) (local.get $n))) (br $f))
          (else (br_if $t (i32.lt_u (i32.wrap_i64 (local.get $a)) (local.get $m))) (br $f))))
      (i32.store (i32.const 512) (i32.const 1))
      (return (i32.const 11)))
    (i32.store (i32.const 516) (i32.const 2))
    (i32.const 22))
  (func $brgensame (export "brgensame") (param $a i64) (param $m i32) (param $n i32) (result i32)
    (block $f
      (block $t
        (if (i32.eq (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))) (local.get $m))
          (then (br_if $t (i32.lt_u (i32.wrap_i64 (local.get $a)) (local.get $n))) (br $f))
          (else (br_if $t (i32.lt_u (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))) (local.get $m))) (br $f))))
      (i32.store (i32.const 512) (i32.const 1))
      (return (i32.const 11)))
    (i32.store (i32.const 516) (i32.const 2))
    (i32.const 22))
  (func $brloop (export "brloop") (param $m i32) (param $n i32) (param $cnt i32) (result i32)
    (local $i i32) (local $x i64) (local $s i32)
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $cnt)))
        (local.set $x (i64.load (i32.shl (local.get $i) (i32.const 3))))
        (block $next
          (block $f
            (block $t
              (if (i32.eq (local.get $m) (i32.wrap_i64 (local.get $x)))
                (then (br_if $t (i64.gt_u (i64.shr_u (local.get $x) (i64.const 32)) (i64.extend_i32_u (local.get $n)))) (br $f))
                (else (br_if $t (i32.lt_u (local.get $m) (i32.wrap_i64 (local.get $x)))) (br $f))))
            (i32.store8 (i32.add (i32.const 1024) (local.get $i)) (i32.const 1))
            (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (i32.const 1)))
            (br $next))
          (i32.store8 (i32.add (i32.const 1100) (local.get $i)) (i32.const 1))
          (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 5)) (i32.const 2))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $brloopvar (export "brloopvar") (param $m i32) (param $n i32) (param $cnt i32) (result i32)
    (local $i i32) (local $x i64) (local $s i32)
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $cnt)))
        (local.set $x (i64.load (i32.shl (local.get $i) (i32.const 3))))
        (local.set $m (i32.add (local.get $m) (local.get $i)))
        (local.set $n (i32.xor (local.get $n) (local.get $i)))
        (block $next
          (block $f
            (block $t
              (if (i32.eq (local.get $m) (i32.wrap_i64 (local.get $x)))
                (then (br_if $t (i64.gt_u (i64.shr_u (local.get $x) (i64.const 32)) (i64.extend_i32_u (local.get $n)))) (br $f))
                (else (br_if $t (i32.lt_u (local.get $m) (i32.wrap_i64 (local.get $x)))) (br $f))))
            (i32.store8 (i32.add (i32.const 1024) (local.get $i)) (i32.const 1))
            (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (i32.const 1)))
            (br $next))
          (i32.store8 (i32.add (i32.const 1100) (local.get $i)) (i32.const 1))
          (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 5)) (i32.const 2))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $brloopc (export "brloopc") (param $m i32) (param $n i32) (param $cnt i32) (result i32)
    (local $i i32) (local $x i64) (local $s i32)
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $cnt)))
        (local.set $x (i64.load (i32.shl (local.get $i) (i32.const 3))))
        (block $next
          (block $f
            (block $t
              (if (i32.eq (local.get $m) (i32.wrap_i64 (local.get $x)))
                (then (br_if $t (i64.gt_u (i64.shr_u (local.get $x) (i64.const 32)) (i64.const 4))) (br $f))
                (else (br_if $t (i32.lt_u (local.get $m) (i32.wrap_i64 (local.get $x)))) (br $f))))
            (i32.store8 (i32.add (i32.const 1024) (local.get $i)) (i32.const 1))
            (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (i32.const 1)))
            (br $next))
          (i32.store8 (i32.add (i32.const 1100) (local.get $i)) (i32.const 1))
          (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 5)) (i32.const 2))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $brloopk (export "brloopk") (param $m i32) (param $n i32) (param $cnt i32) (result i32)
    (local $i i32) (local $x i64) (local $s i32)
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $cnt)))
        (local.set $x (i64.load (i32.shl (local.get $i) (i32.const 3))))
        (block $next
          (block $f
            (block $t
              (if (i32.eq (i32.wrap_i64 (local.get $x)) (i32.const 7))
                (then (br_if $t (i64.lt_u (i64.shr_u (local.get $x) (i64.const 32)) (i64.extend_i32_u (local.get $n)))) (br $f))
                (else (br_if $t (i32.lt_u (i32.wrap_i64 (local.get $x)) (i32.const 7))) (br $f))))
            (i32.store8 (i32.add (i32.const 1024) (local.get $i)) (i32.const 1))
            (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (i32.const 1)))
            (br $next))
          (i32.store8 (i32.add (i32.const 1100) (local.get $i)) (i32.const 1))
          (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 5)) (i32.const 2))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $brloopvarn (export "brloopvarn") (param $m i32) (param $n i32) (param $cnt i32) (result i32)
    (local $i i32) (local $x i64) (local $s i32)
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $cnt)))
        (local.set $x (i64.load (i32.shl (local.get $i) (i32.const 3))))
        (local.set $n (i32.xor (local.get $n) (local.get $i)))
        (block $next
          (block $f
            (block $t
              (if (i32.eq (local.get $m) (i32.wrap_i64 (local.get $x)))
                (then (br_if $t (i64.gt_u (i64.shr_u (local.get $x) (i64.const 32)) (i64.extend_i32_u (local.get $n)))) (br $f))
                (else (br_if $t (i32.lt_u (local.get $m) (i32.wrap_i64 (local.get $x)))) (br $f))))
            (i32.store8 (i32.add (i32.const 1024) (local.get $i)) (i32.const 1))
            (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (i32.const 1)))
            (br $next))
          (i32.store8 (i32.add (i32.const 1100) (local.get $i)) (i32.const 1))
          (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 5)) (i32.const 2))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $brlooprot (export "brlooprot") (param $a i64) (param $cnt i32) (result i32)
    (local $i i32) (local $m i32) (local $n i32) (local $s i32)
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $cnt)))
        (local.set $m (i32.load (i32.shl (local.get $i) (i32.const 3))))
        (local.set $n (i32.load offset=4 (i32.shl (local.get $i) (i32.const 3))))
        (block $next
          (block $f
            (block $t
              (if (i32.eq (i32.wrap_i64 (local.get $a)) (local.get $m))
                (then (br_if $t (i32.gt_u (i32.wrap_i64 (i64.shr_u (local.get $a) (i64.const 32))) (local.get $n))) (br $f))
                (else (br_if $t (i32.gt_u (i32.wrap_i64 (local.get $a)) (local.get $m))) (br $f))))
            (i32.store8 (i32.add (i32.const 1024) (local.get $i)) (i32.const 1))
            (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (i32.const 1)))
            (br $next))
          (i32.store8 (i32.add (i32.const 1100) (local.get $i)) (i32.const 1))
          (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 5)) (i32.const 2))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $brloopp (export "brloopp") (param $m i32) (param $cnt i32) (param $g i32) (result i32)
    (local $i i32) (local $x i64) (local $s i32) (local $y i32)
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $cnt)))
        (local.set $x (i64.load (i32.shl (local.get $i) (i32.const 3))))
        (local.set $y (i32.load offset=4 (i32.shl (i32.and (i32.add (local.get $i) (i32.const 1)) (i32.const 15)) (i32.const 3))))
        (block $next
          (block $f
            (block $t
              (block $e
                (if (i32.eq (local.get $g) (local.get $i)) (then (local.set $y (i32.const 5)) (br $e)))
                (br_if $e (i32.eq (local.get $m) (i32.wrap_i64 (local.get $x))))
                (br_if $t (i32.lt_u (local.get $m) (i32.wrap_i64 (local.get $x))))
                (br $f))
              (br_if $t (i32.lt_u (local.get $y) (i32.wrap_i64 (i64.shr_u (local.get $x) (i64.const 32)))))
              (br $f))
            (i32.store8 (i32.add (i32.const 1024) (local.get $i)) (i32.const 1))
            (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 3)) (i32.const 1)))
            (br $next))
          (i32.store8 (i32.add (i32.const 1100) (local.get $i)) (i32.const 1))
          (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 5)) (i32.const 2))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (local.get $s))
  (func $brback (export "brback") (param $a i64) (param $cnt i32) (result i32)
    (local $i i32) (local $x i64) (local $r i32)
    (local.set $r (i32.const 5))
    (block $out
      (loop $l
        (br_if $out (i32.ge_u (local.get $i) (local.get $cnt)))
        (local.set $x (i64.load (i32.shl (local.get $i) (i32.const 3))))
        (local.set $r (i32.const 3))
        (if (i32.eq (i32.wrap_i64 (local.get $x)) (i32.wrap_i64 (local.get $a)))
          (then (br_if $out (i64.lt_u (i64.shr_u (local.get $x) (i64.const 32)) (i64.shr_u (local.get $a) (i64.const 32)))))
          (else (br_if $out (i32.lt_u (i32.wrap_i64 (local.get $x)) (i32.wrap_i64 (local.get $a))))))
        (local.set $r (i32.const 5))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l)))
    (i32.add (i32.shl (local.get $i) (i32.const 4)) (local.get $r)))
  (func $queue (export "queue") (param $cnt i32) (result i32)
    (local $i i32) (local $j i32) (local $b i32) (local $s i32) (local $x i64) (local $y i64)
    (memory.copy (i32.const 2048) (i32.const 0) (i32.shl (local.get $cnt) (i32.const 3)))
    (block $done
      (loop $outer
        (br_if $done (i32.ge_u (local.get $i) (local.get $cnt)))
        (local.set $b (local.get $i))
        (local.set $j (i32.add (local.get $i) (i32.const 1)))
        (block $picked
          (loop $pick
            (br_if $picked (i32.ge_u (local.get $j) (local.get $cnt)))
            (local.set $x (i64.load (i32.add (i32.const 2048) (i32.shl (local.get $j) (i32.const 3)))))
            (local.set $y (i64.load (i32.add (i32.const 2048) (i32.shl (local.get $b) (i32.const 3)))))
            (local.set $b (select (local.get $j) (local.get $b)
              (select (i64.lt_u (i64.shr_u (local.get $x) (i64.const 32)) (i64.shr_u (local.get $y) (i64.const 32)))
                      (i32.lt_u (i32.wrap_i64 (local.get $x)) (i32.wrap_i64 (local.get $y)))
                      (i32.eq (i32.wrap_i64 (local.get $x)) (i32.wrap_i64 (local.get $y))))))
            (local.set $j (i32.add (local.get $j) (i32.const 1)))
            (br $pick)))
        (local.set $x (i64.load (i32.add (i32.const 2048) (i32.shl (local.get $b) (i32.const 3)))))
        (i64.store (i32.add (i32.const 2048) (i32.shl (local.get $b) (i32.const 3)))
                   (i64.load (i32.add (i32.const 2048) (i32.shl (local.get $i) (i32.const 3)))))
        (i64.store (i32.add (i32.const 2048) (i32.shl (local.get $i) (i32.const 3))) (local.get $x))
        (local.set $s (i32.add (i32.mul (local.get $s) (i32.const 31))
          (i32.add (i32.wrap_i64 (local.get $x)) (i32.mul (i32.wrap_i64 (i64.shr_u (local.get $x) (i64.const 32))) (i32.const 7)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $outer)))
    (local.set $i (i32.const 0))
    (block $done2
      (loop $outer2
        (br_if $done2 (i32.ge_u (local.get $i) (local.get $cnt)))
        (local.set $x (i64.load (i32.add (i32.const 2048) (i32.shl (local.get $i) (i32.const 3)))))
        (local.set $j (i32.const 0))
        (block $counted
          (loop $count
            (br_if $counted (i32.ge_u (local.get $j) (local.get $cnt)))
            (local.set $y (i64.load (i32.add (i32.const 2048) (i32.shl (local.get $j) (i32.const 3)))))
            (block $no
              (block $yes
                (if (i32.eq (i32.wrap_i64 (local.get $x)) (i32.wrap_i64 (local.get $y)))
                  (then (br_if $yes (i64.ge_u (i64.shr_u (local.get $x) (i64.const 32)) (i64.shr_u (local.get $y) (i64.const 32))))
                        (br $no))
                  (else (br_if $yes (i32.ge_u (i32.wrap_i64 (local.get $x)) (i32.wrap_i64 (local.get $y))))
                        (br $no))))
              (i32.store8 (i32.add (i32.const 3072) (local.get $j)) (i32.const 1))
              (local.set $s (i32.add (local.get $s) (i32.add (local.get $i) (i32.const 1)))))
            (local.set $j (i32.add (local.get $j) (i32.const 1)))
            (br $count)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $outer2)))
    (local.get $s))
)
