;; i8x16.shuffle of every shape a64 has one instruction for -- zip, uzp and
;; trn at 8-, 16-, 32- and 64-bit lanes, of x and y in either order and of
;; one with itself, ext at every offset, rev in 16-, 32- and 64-bit
;; containers, dup of a lane -- each with no table lookup; x and y as they
;; are; lanes put in, at each size, two of them, and into a base made by
;; zip; shapes of two steps; one vector shuffled with itself; and random
;; shuffles of one vector or two, a table lookup each, and two lookups of
;; one index vector, made once (isel.c sh_plan).
;; Each function shuffles two vectors of distinct bytes and returns a hash
;; of the result.  At -O2 -j1 too, its functions compiled in order.  Made by
;; test/gen_shuffle.py.
;;
;; shape: same_first \stbl\.16b\s=0
;; shape: zip1_0_xy \stbl\.16b\s=0
;; shape: zip1_0_xy \smovk\s=0
;; shape: zip1_0_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip1_0_xy \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: zip1_0_yx \stbl\.16b\s=0
;; shape: zip1_0_yx \smovk\s=0
;; shape: zip1_0_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip1_0_yx \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: zip1_0_xx \stbl\.16b\s=0
;; shape: zip1_0_xx \smovk\s=0
;; shape: zip1_0_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip1_0_xx \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: zip1_0_yy \stbl\.16b\s=0
;; shape: zip1_0_yy \smovk\s=0
;; shape: zip1_0_yy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip1_0_yy \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: zip1_1_xy \stbl\.16b\s=0
;; shape: zip1_1_xy \smovk\s=0
;; shape: zip1_1_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip1_1_xy \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: zip1_1_yx \stbl\.16b\s=0
;; shape: zip1_1_yx \smovk\s=0
;; shape: zip1_1_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip1_1_yx \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: zip1_1_xx \stbl\.16b\s=0
;; shape: zip1_1_xx \smovk\s=0
;; shape: zip1_1_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip1_1_xx \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: zip1_1_yy \stbl\.16b\s=0
;; shape: zip1_1_yy \smovk\s=0
;; shape: zip1_1_yy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip1_1_yy \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: zip1_2_xy \stbl\.16b\s=0
;; shape: zip1_2_xy \smovk\s=0
;; shape: zip1_2_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip1_2_xy \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: zip1_2_yx \stbl\.16b\s=0
;; shape: zip1_2_yx \smovk\s=0
;; shape: zip1_2_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip1_2_yx \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: zip1_2_xx \stbl\.16b\s=0
;; shape: zip1_2_xx \smovk\s=0
;; shape: zip1_2_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip1_2_xx \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: zip1_2_yy \stbl\.16b\s=0
;; shape: zip1_2_yy \smovk\s=0
;; shape: zip1_2_yy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip1_2_yy \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: zip1_3_xy \stbl\.16b\s=0
;; shape: zip1_3_xy \smovk\s=0
;; shape: zip1_3_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip1_3_xy \s(zip|uzp|trn)[12]\.2d\s|\sdup\.2d\s=1
;; shape: zip1_3_yx \stbl\.16b\s=0
;; shape: zip1_3_yx \smovk\s=0
;; shape: zip1_3_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip1_3_yx \s(zip|uzp|trn)[12]\.2d\s|\sdup\.2d\s=1
;; shape: zip1_3_xx \stbl\.16b\s=0
;; shape: zip1_3_xx \smovk\s=0
;; shape: zip1_3_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip1_3_xx \s(zip|uzp|trn)[12]\.2d\s|\sdup\.2d\s=1
;; shape: zip1_3_yy \stbl\.16b\s=0
;; shape: zip1_3_yy \smovk\s=0
;; shape: zip1_3_yy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip1_3_yy \s(zip|uzp|trn)[12]\.2d\s|\sdup\.2d\s=1
;; shape: zip2_0_xy \stbl\.16b\s=0
;; shape: zip2_0_xy \smovk\s=0
;; shape: zip2_0_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip2_0_xy \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: zip2_0_yx \stbl\.16b\s=0
;; shape: zip2_0_yx \smovk\s=0
;; shape: zip2_0_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip2_0_yx \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: zip2_0_xx \stbl\.16b\s=0
;; shape: zip2_0_xx \smovk\s=0
;; shape: zip2_0_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip2_0_xx \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: zip2_0_yy \stbl\.16b\s=0
;; shape: zip2_0_yy \smovk\s=0
;; shape: zip2_0_yy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip2_0_yy \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: zip2_1_xy \stbl\.16b\s=0
;; shape: zip2_1_xy \smovk\s=0
;; shape: zip2_1_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip2_1_xy \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: zip2_1_yx \stbl\.16b\s=0
;; shape: zip2_1_yx \smovk\s=0
;; shape: zip2_1_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip2_1_yx \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: zip2_1_xx \stbl\.16b\s=0
;; shape: zip2_1_xx \smovk\s=0
;; shape: zip2_1_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip2_1_xx \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: zip2_1_yy \stbl\.16b\s=0
;; shape: zip2_1_yy \smovk\s=0
;; shape: zip2_1_yy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip2_1_yy \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: zip2_2_xy \stbl\.16b\s=0
;; shape: zip2_2_xy \smovk\s=0
;; shape: zip2_2_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip2_2_xy \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: zip2_2_yx \stbl\.16b\s=0
;; shape: zip2_2_yx \smovk\s=0
;; shape: zip2_2_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip2_2_yx \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: zip2_2_xx \stbl\.16b\s=0
;; shape: zip2_2_xx \smovk\s=0
;; shape: zip2_2_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip2_2_xx \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: zip2_2_yy \stbl\.16b\s=0
;; shape: zip2_2_yy \smovk\s=0
;; shape: zip2_2_yy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip2_2_yy \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: zip2_3_xy \stbl\.16b\s=0
;; shape: zip2_3_xy \smovk\s=0
;; shape: zip2_3_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip2_3_xy \s(zip|uzp|trn)[12]\.2d\s|\sdup\.2d\s=1
;; shape: zip2_3_yx \stbl\.16b\s=0
;; shape: zip2_3_yx \smovk\s=0
;; shape: zip2_3_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip2_3_yx \s(zip|uzp|trn)[12]\.2d\s|\sdup\.2d\s=1
;; shape: zip2_3_xx \stbl\.16b\s=0
;; shape: zip2_3_xx \smovk\s=0
;; shape: zip2_3_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip2_3_xx \s(zip|uzp|trn)[12]\.2d\s|\sdup\.2d\s=1
;; shape: zip2_3_yy \stbl\.16b\s=0
;; shape: zip2_3_yy \smovk\s=0
;; shape: zip2_3_yy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: zip2_3_yy \s(zip|uzp|trn)[12]\.2d\s|\sdup\.2d\s=1
;; shape: uzp1_0_xy \stbl\.16b\s=0
;; shape: uzp1_0_xy \smovk\s=0
;; shape: uzp1_0_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp1_0_xy \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: uzp1_0_yx \stbl\.16b\s=0
;; shape: uzp1_0_yx \smovk\s=0
;; shape: uzp1_0_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp1_0_yx \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: uzp1_0_xx \stbl\.16b\s=0
;; shape: uzp1_0_xx \smovk\s=0
;; shape: uzp1_0_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp1_0_xx \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: uzp1_0_yy \stbl\.16b\s=0
;; shape: uzp1_0_yy \smovk\s=0
;; shape: uzp1_0_yy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp1_0_yy \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: uzp1_1_xy \stbl\.16b\s=0
;; shape: uzp1_1_xy \smovk\s=0
;; shape: uzp1_1_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp1_1_xy \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: uzp1_1_yx \stbl\.16b\s=0
;; shape: uzp1_1_yx \smovk\s=0
;; shape: uzp1_1_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp1_1_yx \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: uzp1_1_xx \stbl\.16b\s=0
;; shape: uzp1_1_xx \smovk\s=0
;; shape: uzp1_1_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp1_1_xx \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: uzp1_1_yy \stbl\.16b\s=0
;; shape: uzp1_1_yy \smovk\s=0
;; shape: uzp1_1_yy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp1_1_yy \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: uzp1_2_xy \stbl\.16b\s=0
;; shape: uzp1_2_xy \smovk\s=0
;; shape: uzp1_2_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp1_2_xy \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: uzp1_2_yx \stbl\.16b\s=0
;; shape: uzp1_2_yx \smovk\s=0
;; shape: uzp1_2_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp1_2_yx \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: uzp1_2_xx \stbl\.16b\s=0
;; shape: uzp1_2_xx \smovk\s=0
;; shape: uzp1_2_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp1_2_xx \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: uzp1_2_yy \stbl\.16b\s=0
;; shape: uzp1_2_yy \smovk\s=0
;; shape: uzp1_2_yy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp1_2_yy \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: uzp2_0_xy \stbl\.16b\s=0
;; shape: uzp2_0_xy \smovk\s=0
;; shape: uzp2_0_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp2_0_xy \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: uzp2_0_yx \stbl\.16b\s=0
;; shape: uzp2_0_yx \smovk\s=0
;; shape: uzp2_0_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp2_0_yx \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: uzp2_0_xx \stbl\.16b\s=0
;; shape: uzp2_0_xx \smovk\s=0
;; shape: uzp2_0_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp2_0_xx \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: uzp2_0_yy \stbl\.16b\s=0
;; shape: uzp2_0_yy \smovk\s=0
;; shape: uzp2_0_yy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp2_0_yy \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: uzp2_1_xy \stbl\.16b\s=0
;; shape: uzp2_1_xy \smovk\s=0
;; shape: uzp2_1_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp2_1_xy \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: uzp2_1_yx \stbl\.16b\s=0
;; shape: uzp2_1_yx \smovk\s=0
;; shape: uzp2_1_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp2_1_yx \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: uzp2_1_xx \stbl\.16b\s=0
;; shape: uzp2_1_xx \smovk\s=0
;; shape: uzp2_1_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp2_1_xx \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: uzp2_1_yy \stbl\.16b\s=0
;; shape: uzp2_1_yy \smovk\s=0
;; shape: uzp2_1_yy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp2_1_yy \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: uzp2_2_xy \stbl\.16b\s=0
;; shape: uzp2_2_xy \smovk\s=0
;; shape: uzp2_2_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp2_2_xy \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: uzp2_2_yx \stbl\.16b\s=0
;; shape: uzp2_2_yx \smovk\s=0
;; shape: uzp2_2_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp2_2_yx \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: uzp2_2_xx \stbl\.16b\s=0
;; shape: uzp2_2_xx \smovk\s=0
;; shape: uzp2_2_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp2_2_xx \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: uzp2_2_yy \stbl\.16b\s=0
;; shape: uzp2_2_yy \smovk\s=0
;; shape: uzp2_2_yy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: uzp2_2_yy \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: trn1_0_xy \stbl\.16b\s=0
;; shape: trn1_0_xy \smovk\s=0
;; shape: trn1_0_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn1_0_xy \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: trn1_0_yx \stbl\.16b\s=0
;; shape: trn1_0_yx \smovk\s=0
;; shape: trn1_0_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn1_0_yx \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: trn1_0_xx \stbl\.16b\s=0
;; shape: trn1_0_xx \smovk\s=0
;; shape: trn1_0_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn1_0_xx \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: trn1_0_yy \stbl\.16b\s=0
;; shape: trn1_0_yy \smovk\s=0
;; shape: trn1_0_yy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn1_0_yy \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: trn1_1_xy \stbl\.16b\s=0
;; shape: trn1_1_xy \smovk\s=0
;; shape: trn1_1_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn1_1_xy \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: trn1_1_yx \stbl\.16b\s=0
;; shape: trn1_1_yx \smovk\s=0
;; shape: trn1_1_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn1_1_yx \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: trn1_1_xx \stbl\.16b\s=0
;; shape: trn1_1_xx \smovk\s=0
;; shape: trn1_1_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn1_1_xx \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: trn1_1_yy \stbl\.16b\s=0
;; shape: trn1_1_yy \smovk\s=0
;; shape: trn1_1_yy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn1_1_yy \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: trn1_2_xy \stbl\.16b\s=0
;; shape: trn1_2_xy \smovk\s=0
;; shape: trn1_2_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn1_2_xy \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: trn1_2_yx \stbl\.16b\s=0
;; shape: trn1_2_yx \smovk\s=0
;; shape: trn1_2_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn1_2_yx \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: trn1_2_xx \stbl\.16b\s=0
;; shape: trn1_2_xx \smovk\s=0
;; shape: trn1_2_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn1_2_xx \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: trn1_2_yy \stbl\.16b\s=0
;; shape: trn1_2_yy \smovk\s=0
;; shape: trn1_2_yy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn1_2_yy \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: trn2_0_xy \stbl\.16b\s=0
;; shape: trn2_0_xy \smovk\s=0
;; shape: trn2_0_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn2_0_xy \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: trn2_0_yx \stbl\.16b\s=0
;; shape: trn2_0_yx \smovk\s=0
;; shape: trn2_0_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn2_0_yx \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: trn2_0_xx \stbl\.16b\s=0
;; shape: trn2_0_xx \smovk\s=0
;; shape: trn2_0_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn2_0_xx \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: trn2_0_yy \stbl\.16b\s=0
;; shape: trn2_0_yy \smovk\s=0
;; shape: trn2_0_yy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn2_0_yy \s(zip|uzp|trn)[12]\.16b\s|\sdup\.16b\s=1
;; shape: trn2_1_xy \stbl\.16b\s=0
;; shape: trn2_1_xy \smovk\s=0
;; shape: trn2_1_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn2_1_xy \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: trn2_1_yx \stbl\.16b\s=0
;; shape: trn2_1_yx \smovk\s=0
;; shape: trn2_1_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn2_1_yx \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: trn2_1_xx \stbl\.16b\s=0
;; shape: trn2_1_xx \smovk\s=0
;; shape: trn2_1_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn2_1_xx \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: trn2_1_yy \stbl\.16b\s=0
;; shape: trn2_1_yy \smovk\s=0
;; shape: trn2_1_yy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn2_1_yy \s(zip|uzp|trn)[12]\.8h\s|\sdup\.8h\s=1
;; shape: trn2_2_xy \stbl\.16b\s=0
;; shape: trn2_2_xy \smovk\s=0
;; shape: trn2_2_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn2_2_xy \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: trn2_2_yx \stbl\.16b\s=0
;; shape: trn2_2_yx \smovk\s=0
;; shape: trn2_2_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn2_2_yx \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: trn2_2_xx \stbl\.16b\s=0
;; shape: trn2_2_xx \smovk\s=0
;; shape: trn2_2_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn2_2_xx \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: trn2_2_yy \stbl\.16b\s=0
;; shape: trn2_2_yy \smovk\s=0
;; shape: trn2_2_yy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: trn2_2_yy \s(zip|uzp|trn)[12]\.4s\s|\sdup\.4s\s=1
;; shape: ext_1_xy \stbl\.16b\s=0
;; shape: ext_1_xy \smovk\s=0
;; shape: ext_1_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_1_xy \sext\.16b\s=1
;; shape: ext_1_yx \stbl\.16b\s=0
;; shape: ext_1_yx \smovk\s=0
;; shape: ext_1_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_1_yx \sext\.16b\s=1
;; shape: ext_1_xx \stbl\.16b\s=0
;; shape: ext_1_xx \smovk\s=0
;; shape: ext_1_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_1_xx \sext\.16b\s=1
;; shape: ext_2_xy \stbl\.16b\s=0
;; shape: ext_2_xy \smovk\s=0
;; shape: ext_2_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_2_xy \sext\.16b\s=1
;; shape: ext_2_yx \stbl\.16b\s=0
;; shape: ext_2_yx \smovk\s=0
;; shape: ext_2_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_2_yx \sext\.16b\s=1
;; shape: ext_2_xx \stbl\.16b\s=0
;; shape: ext_2_xx \smovk\s=0
;; shape: ext_2_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_2_xx \sext\.16b\s=1
;; shape: ext_3_xy \stbl\.16b\s=0
;; shape: ext_3_xy \smovk\s=0
;; shape: ext_3_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_3_xy \sext\.16b\s=1
;; shape: ext_3_yx \stbl\.16b\s=0
;; shape: ext_3_yx \smovk\s=0
;; shape: ext_3_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_3_yx \sext\.16b\s=1
;; shape: ext_3_xx \stbl\.16b\s=0
;; shape: ext_3_xx \smovk\s=0
;; shape: ext_3_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_3_xx \sext\.16b\s=1
;; shape: ext_4_xy \stbl\.16b\s=0
;; shape: ext_4_xy \smovk\s=0
;; shape: ext_4_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_4_xy \sext\.16b\s=1
;; shape: ext_4_yx \stbl\.16b\s=0
;; shape: ext_4_yx \smovk\s=0
;; shape: ext_4_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_4_yx \sext\.16b\s=1
;; shape: ext_4_xx \stbl\.16b\s=0
;; shape: ext_4_xx \smovk\s=0
;; shape: ext_4_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_4_xx \sext\.16b\s=1
;; shape: ext_5_xy \stbl\.16b\s=0
;; shape: ext_5_xy \smovk\s=0
;; shape: ext_5_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_5_xy \sext\.16b\s=1
;; shape: ext_5_yx \stbl\.16b\s=0
;; shape: ext_5_yx \smovk\s=0
;; shape: ext_5_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_5_yx \sext\.16b\s=1
;; shape: ext_5_xx \stbl\.16b\s=0
;; shape: ext_5_xx \smovk\s=0
;; shape: ext_5_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_5_xx \sext\.16b\s=1
;; shape: ext_6_xy \stbl\.16b\s=0
;; shape: ext_6_xy \smovk\s=0
;; shape: ext_6_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_6_xy \sext\.16b\s=1
;; shape: ext_6_yx \stbl\.16b\s=0
;; shape: ext_6_yx \smovk\s=0
;; shape: ext_6_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_6_yx \sext\.16b\s=1
;; shape: ext_6_xx \stbl\.16b\s=0
;; shape: ext_6_xx \smovk\s=0
;; shape: ext_6_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_6_xx \sext\.16b\s=1
;; shape: ext_7_xy \stbl\.16b\s=0
;; shape: ext_7_xy \smovk\s=0
;; shape: ext_7_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_7_xy \sext\.16b\s=1
;; shape: ext_7_yx \stbl\.16b\s=0
;; shape: ext_7_yx \smovk\s=0
;; shape: ext_7_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_7_yx \sext\.16b\s=1
;; shape: ext_7_xx \stbl\.16b\s=0
;; shape: ext_7_xx \smovk\s=0
;; shape: ext_7_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_7_xx \sext\.16b\s=1
;; shape: ext_8_xy \stbl\.16b\s=0
;; shape: ext_8_xy \smovk\s=0
;; shape: ext_8_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_8_xy \sext\.16b\s=1
;; shape: ext_8_yx \stbl\.16b\s=0
;; shape: ext_8_yx \smovk\s=0
;; shape: ext_8_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_8_yx \sext\.16b\s=1
;; shape: ext_8_xx \stbl\.16b\s=0
;; shape: ext_8_xx \smovk\s=0
;; shape: ext_8_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_8_xx \sext\.16b\s=1
;; shape: ext_9_xy \stbl\.16b\s=0
;; shape: ext_9_xy \smovk\s=0
;; shape: ext_9_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_9_xy \sext\.16b\s=1
;; shape: ext_9_yx \stbl\.16b\s=0
;; shape: ext_9_yx \smovk\s=0
;; shape: ext_9_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_9_yx \sext\.16b\s=1
;; shape: ext_9_xx \stbl\.16b\s=0
;; shape: ext_9_xx \smovk\s=0
;; shape: ext_9_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_9_xx \sext\.16b\s=1
;; shape: ext_10_xy \stbl\.16b\s=0
;; shape: ext_10_xy \smovk\s=0
;; shape: ext_10_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_10_xy \sext\.16b\s=1
;; shape: ext_10_yx \stbl\.16b\s=0
;; shape: ext_10_yx \smovk\s=0
;; shape: ext_10_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_10_yx \sext\.16b\s=1
;; shape: ext_10_xx \stbl\.16b\s=0
;; shape: ext_10_xx \smovk\s=0
;; shape: ext_10_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_10_xx \sext\.16b\s=1
;; shape: ext_11_xy \stbl\.16b\s=0
;; shape: ext_11_xy \smovk\s=0
;; shape: ext_11_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_11_xy \sext\.16b\s=1
;; shape: ext_11_yx \stbl\.16b\s=0
;; shape: ext_11_yx \smovk\s=0
;; shape: ext_11_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_11_yx \sext\.16b\s=1
;; shape: ext_11_xx \stbl\.16b\s=0
;; shape: ext_11_xx \smovk\s=0
;; shape: ext_11_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_11_xx \sext\.16b\s=1
;; shape: ext_12_xy \stbl\.16b\s=0
;; shape: ext_12_xy \smovk\s=0
;; shape: ext_12_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_12_xy \sext\.16b\s=1
;; shape: ext_12_yx \stbl\.16b\s=0
;; shape: ext_12_yx \smovk\s=0
;; shape: ext_12_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_12_yx \sext\.16b\s=1
;; shape: ext_12_xx \stbl\.16b\s=0
;; shape: ext_12_xx \smovk\s=0
;; shape: ext_12_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_12_xx \sext\.16b\s=1
;; shape: ext_13_xy \stbl\.16b\s=0
;; shape: ext_13_xy \smovk\s=0
;; shape: ext_13_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_13_xy \sext\.16b\s=1
;; shape: ext_13_yx \stbl\.16b\s=0
;; shape: ext_13_yx \smovk\s=0
;; shape: ext_13_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_13_yx \sext\.16b\s=1
;; shape: ext_13_xx \stbl\.16b\s=0
;; shape: ext_13_xx \smovk\s=0
;; shape: ext_13_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_13_xx \sext\.16b\s=1
;; shape: ext_14_xy \stbl\.16b\s=0
;; shape: ext_14_xy \smovk\s=0
;; shape: ext_14_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_14_xy \sext\.16b\s=1
;; shape: ext_14_yx \stbl\.16b\s=0
;; shape: ext_14_yx \smovk\s=0
;; shape: ext_14_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_14_yx \sext\.16b\s=1
;; shape: ext_14_xx \stbl\.16b\s=0
;; shape: ext_14_xx \smovk\s=0
;; shape: ext_14_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_14_xx \sext\.16b\s=1
;; shape: ext_15_xy \stbl\.16b\s=0
;; shape: ext_15_xy \smovk\s=0
;; shape: ext_15_xy \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_15_xy \sext\.16b\s=1
;; shape: ext_15_yx \stbl\.16b\s=0
;; shape: ext_15_yx \smovk\s=0
;; shape: ext_15_yx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_15_yx \sext\.16b\s=1
;; shape: ext_15_xx \stbl\.16b\s=0
;; shape: ext_15_xx \smovk\s=0
;; shape: ext_15_xx \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: ext_15_xx \sext\.16b\s=1
;; shape: rev16_0_0 \stbl\.16b\s=0
;; shape: rev16_0_0 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: rev16_0_0 \srev16\.=1
;; shape: rev16_0_1 \stbl\.16b\s=0
;; shape: rev16_0_1 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: rev16_0_1 \srev16\.=1
;; shape: rev32_0_0 \stbl\.16b\s=0
;; shape: rev32_0_0 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: rev32_0_0 \srev32\.=1
;; shape: rev32_0_1 \stbl\.16b\s=0
;; shape: rev32_0_1 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: rev32_0_1 \srev32\.=1
;; shape: rev32_1_0 \stbl\.16b\s=0
;; shape: rev32_1_0 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: rev32_1_0 \srev32\.=1
;; shape: rev32_1_1 \stbl\.16b\s=0
;; shape: rev32_1_1 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: rev32_1_1 \srev32\.=1
;; shape: rev64_0_0 \stbl\.16b\s=0
;; shape: rev64_0_0 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: rev64_0_0 \srev64\.=1
;; shape: rev64_0_1 \stbl\.16b\s=0
;; shape: rev64_0_1 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: rev64_0_1 \srev64\.=1
;; shape: rev64_1_0 \stbl\.16b\s=0
;; shape: rev64_1_0 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: rev64_1_0 \srev64\.=1
;; shape: rev64_1_1 \stbl\.16b\s=0
;; shape: rev64_1_1 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: rev64_1_1 \srev64\.=1
;; shape: rev64_2_0 \stbl\.16b\s=0
;; shape: rev64_2_0 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: rev64_2_0 \srev64\.=1
;; shape: rev64_2_1 \stbl\.16b\s=0
;; shape: rev64_2_1 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: rev64_2_1 \srev64\.=1
;; shape: dup_0_0_0 \stbl\.16b\s=0
;; shape: dup_0_0_0 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: dup_0_0_0 \sdup\.16b\s=1
;; shape: dup_0_0_1 \stbl\.16b\s=0
;; shape: dup_0_0_1 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: dup_0_0_1 \sdup\.16b\s=1
;; shape: dup_0_8_0 \stbl\.16b\s=0
;; shape: dup_0_8_0 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: dup_0_8_0 \sdup\.16b\s=1
;; shape: dup_0_8_1 \stbl\.16b\s=0
;; shape: dup_0_8_1 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: dup_0_8_1 \sdup\.16b\s=1
;; shape: dup_0_15_0 \stbl\.16b\s=0
;; shape: dup_0_15_0 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: dup_0_15_0 \sdup\.16b\s=1
;; shape: dup_0_15_1 \stbl\.16b\s=0
;; shape: dup_0_15_1 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: dup_0_15_1 \sdup\.16b\s=1
;; shape: dup_1_0_0 \stbl\.16b\s=0
;; shape: dup_1_0_0 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: dup_1_0_0 \sdup\.8h\s=1
;; shape: dup_1_0_1 \stbl\.16b\s=0
;; shape: dup_1_0_1 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: dup_1_0_1 \sdup\.8h\s=1
;; shape: dup_1_4_0 \stbl\.16b\s=0
;; shape: dup_1_4_0 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: dup_1_4_0 \sdup\.8h\s=1
;; shape: dup_1_4_1 \stbl\.16b\s=0
;; shape: dup_1_4_1 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: dup_1_4_1 \sdup\.8h\s=1
;; shape: dup_1_7_0 \stbl\.16b\s=0
;; shape: dup_1_7_0 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: dup_1_7_0 \sdup\.8h\s=1
;; shape: dup_1_7_1 \stbl\.16b\s=0
;; shape: dup_1_7_1 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: dup_1_7_1 \sdup\.8h\s=1
;; shape: dup_2_0_0 \stbl\.16b\s=0
;; shape: dup_2_0_0 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: dup_2_0_0 \sdup\.4s\s=1
;; shape: dup_2_0_1 \stbl\.16b\s=0
;; shape: dup_2_0_1 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: dup_2_0_1 \sdup\.4s\s=1
;; shape: dup_2_2_0 \stbl\.16b\s=0
;; shape: dup_2_2_0 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: dup_2_2_0 \sdup\.4s\s=1
;; shape: dup_2_2_1 \stbl\.16b\s=0
;; shape: dup_2_2_1 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: dup_2_2_1 \sdup\.4s\s=1
;; shape: dup_2_3_0 \stbl\.16b\s=0
;; shape: dup_2_3_0 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: dup_2_3_0 \sdup\.4s\s=1
;; shape: dup_2_3_1 \stbl\.16b\s=0
;; shape: dup_2_3_1 \s(zip|uzp|trn)[12]\.|\sext\.|\srev(16|32|64)\.|\sdup\.|\smov\.(16b|[bhsd])\s=1
;; shape: dup_2_3_1 \sdup\.4s\s=1
;; shape: idx \stbl\.16b\s=0
;; shape: idy \stbl\.16b\s=0
;; shape: ins1_0 \stbl\.16b\s=0
;; shape: ins1_0 \smov\.b\s|\sins\s=1
;; shape: ins1_1 \stbl\.16b\s=0
;; shape: ins1_1 \smov\.h\s|\sins\s=1
;; shape: ins1_2 \stbl\.16b\s=0
;; shape: ins1_2 \smov\.s\s|\sins\s=1
;; shape: ins2 \stbl\.16b\s=0
;; shape: zipins \stbl\.16b\s=0
;; shape: rev4s \stbl\.16b\s=0
;; shape: rev16b \stbl\.16b\s=0
;; shape: swaphalves_rev \stbl\.16b\s=0
;; shape: rnd1_0 \stbl\.16b\s=1
;; shape: rnd1_1 \stbl\.16b\s=1
;; shape: rnd1_2 \stbl\.16b\s=1
;; shape: rnd1_3 \stbl\.16b\s=1
;; shape: rnd1_4 \stbl\.16b\s=1
;; shape: rnd1_5 \stbl\.16b\s=1
;; shape: rnd2_0 \stbl\.16b\s=1
;; shape: rnd2_0 \smov\.16b\s=0
;; shape: rnd2_1 \stbl\.16b\s=1
;; shape: rnd2_1 \smov\.16b\s=0
;; shape: rnd2_2 \stbl\.16b\s=1
;; shape: rnd2_2 \smov\.16b\s=0
;; shape: rnd2_3 \stbl\.16b\s=1
;; shape: rnd2_3 \smov\.16b\s=0
;; shape: rnd2_4 \stbl\.16b\s=1
;; shape: rnd2_4 \smov\.16b\s=0
;; shape: rnd2_5 \stbl\.16b\s=1
;; shape: rnd2_5 \smov\.16b\s=0
;; shape: rndy_0 \stbl\.16b\s=1
;; shape: rndy_1 \stbl\.16b\s=1
;; shape: rndy_2 \stbl\.16b\s=1
;; shape: rndy_3 \stbl\.16b\s=1
;; shape: tbl_twice \stbl\.16b\s=2
;; shape: tbl_twice \smovk\s=6
;;
;; flags: -O2 -j1
;;
;; run: same_first 0 => i32:1157307484
;; run: zip1_0_xy 0 => i32:2039946332
;; run: zip1_0_yx 0 => i32:-516966308
;; run: zip1_0_xx 0 => i32:1157307484
;; run: zip1_0_yy 0 => i32:365672540
;; run: zip1_1_xy 0 => i32:-1702568868
;; run: zip1_1_yx 0 => i32:861239388
;; run: zip1_1_xx 0 => i32:-24847268
;; run: zip1_1_yy 0 => i32:-816482212
;; run: zip1_2_xy 0 => i32:-944074592
;; run: zip1_2_yx 0 => i32:-1954667808
;; run: zip1_2_xx 0 => i32:1093929920
;; run: zip1_2_yy 0 => i32:302294976
;; run: zip1_3_xy 0 => i32:1835022200
;; run: zip1_3_yx 0 => i32:571783032
;; run: zip1_3_xx 0 => i32:1599220088
;; run: zip1_3_yy 0 => i32:807585144
;; run: zip2_0_xy 0 => i32:-505451940
;; run: zip2_0_yx 0 => i32:697820764
;; run: zip2_0_xx 0 => i32:-1385993636
;; run: zip2_0_yy 0 => i32:1578362460
;; run: zip2_1_xy 0 => i32:-487773604
;; run: zip2_1_yx 0 => i32:-1684167076
;; run: zip2_1_xx 0 => i32:1726818908
;; run: zip2_1_yy 0 => i32:396207708
;; run: zip2_2_xy 0 => i32:790748576
;; run: zip2_2_yx 0 => i32:-725134880
;; run: zip2_2_xx 0 => i32:-1449371200
;; run: zip2_2_yy 0 => i32:1514984896
;; run: zip2_3_xy 0 => i32:-725121928
;; run: zip2_3_yx 0 => i32:-977779592
;; run: zip2_3_xx 0 => i32:-944081032
;; run: zip2_3_yy 0 => i32:-758820488
;; run: uzp1_0_xy 0 => i32:-456419152
;; run: uzp1_0_yx 0 => i32:-645916496
;; run: uzp1_0_xx 0 => i32:-675444048
;; run: uzp1_0_yy 0 => i32:-426891600
;; run: uzp1_1_xy 0 => i32:562157744
;; run: uzp1_1_yx 0 => i32:372660400
;; run: uzp1_1_xx 0 => i32:343132848
;; run: uzp1_1_yy 0 => i32:591685296
;; run: uzp1_2_xy 0 => i32:251779248
;; run: uzp1_2_yx 0 => i32:-1011459920
;; run: uzp1_2_xx 0 => i32:15977136
;; run: uzp1_2_yy 0 => i32:-775657808
;; run: uzp2_0_xy 0 => i32:-237460880
;; run: uzp2_0_yx 0 => i32:-426958224
;; run: uzp2_0_xx 0 => i32:-456485776
;; run: uzp2_0_yy 0 => i32:-207933328
;; run: uzp2_1_xy 0 => i32:1000074288
;; run: uzp2_1_yx 0 => i32:810576944
;; run: uzp2_1_xx 0 => i32:781049392
;; run: uzp2_1_yy 0 => i32:1029601840
;; run: uzp2_2_xy 0 => i32:1110769328
;; run: uzp2_2_yx 0 => i32:858111664
;; run: uzp2_2_xx 0 => i32:891810224
;; run: uzp2_2_yy 0 => i32:1077070768
;; run: trn1_0_xy 0 => i32:-676695944
;; run: trn1_0_yx 0 => i32:1044646776
;; run: trn1_0_xx 0 => i32:-1559269256
;; run: trn1_0_yy 0 => i32:1927220088
;; run: trn1_1_xy 0 => i32:-304533384
;; run: trn1_1_yx 0 => i32:-2018981000
;; run: trn1_1_xx 0 => i32:1389965432
;; run: trn1_1_yy 0 => i32:581487480
;; run: trn1_2_xy 0 => i32:1262359584
;; run: trn1_2_yx 0 => i32:251766368
;; run: trn1_2_xx 0 => i32:-994603200
;; run: trn1_2_yy 0 => i32:-1786238144
;; run: trn2_0_xy 0 => i32:-457737672
;; run: trn2_0_yx 0 => i32:1263605048
;; run: trn2_0_xx 0 => i32:-1340310984
;; run: trn2_0_yy 0 => i32:2146178360
;; run: trn2_1_xy 0 => i32:133383160
;; run: trn2_1_yx 0 => i32:-1581064456
;; run: trn2_1_xx 0 => i32:1827881976
;; run: trn2_1_yy 0 => i32:1019404024
;; run: trn2_2_xy 0 => i32:2121349664
;; run: trn2_2_yx 0 => i32:605466208
;; run: trn2_2_xx 0 => i32:-118770112
;; run: trn2_2_yy 0 => i32:-1449381312
;; run: ext_1_xy 0 => i32:1936079416
;; run: ext_1_yx 0 => i32:1664472376
;; run: ext_1_xx 0 => i32:57031224
;; run: ext_2_xy 0 => i32:-2139929608
;; run: ext_2_yx 0 => i32:1365434104
;; run: ext_2_xx 0 => i32:268649464
;; run: ext_3_xy 0 => i32:-1920971336
;; run: ext_3_yx 0 => i32:1582368952
;; run: ext_3_xx 0 => i32:487579064
;; run: ext_4_xy 0 => i32:-1702013064
;; run: ext_4_yx 0 => i32:1801319320
;; run: ext_4_xx 0 => i32:706537224
;; run: ext_5_xy 0 => i32:-1483054792
;; run: ext_5_yx 0 => i32:1483406680
;; run: ext_5_xx 0 => i32:-1490423608
;; run: ext_6_xy 0 => i32:-1264096520
;; run: ext_6_yx 0 => i32:-1470626024
;; run: ext_6_xx 0 => i32:-1499006328
;; run: ext_7_xy 0 => i32:-1045138248
;; run: ext_7_yx 0 => i32:-1314393896
;; run: ext_7_xx 0 => i32:-1280936888
;; run: ext_8_xy 0 => i32:-826179976
;; run: ext_8_yx 0 => i32:-1095680648
;; run: ext_8_xx 0 => i32:-1061982088
;; run: ext_9_xy 0 => i32:-607221704
;; run: ext_9_yx 0 => i32:-339851464
;; run: ext_9_xx 0 => i32:1572895288
;; run: ext_10_xy 0 => i32:-388263432
;; run: ext_10_yx 0 => i32:300634360
;; run: ext_10_xx 0 => i32:-966949896
;; run: ext_11_xy 0 => i32:-169305160
;; run: ext_11_yx 0 => i32:-1424917832
;; run: ext_11_xx 0 => i32:-775545416
;; run: ext_12_xy 0 => i32:49653112
;; run: ext_12_yx 0 => i32:-1213555304
;; run: ext_12_xx 0 => i32:-556694776
;; run: ext_13_xy 0 => i32:268611384
;; run: ext_13_yx 0 => i32:-1531467944
;; run: ext_13_xx 0 => i32:1541311688
;; run: ext_14_xy 0 => i32:470792440
;; run: ext_14_yx 0 => i32:1034203416
;; run: ext_14_xx 0 => i32:2136708744
;; run: ext_15_xy 0 => i32:689685176
;; run: ext_15_yx 0 => i32:1497209560
;; run: ext_15_xx 0 => i32:1501499464
;; run: rev16_0_0 0 => i32:-321734088
;; run: rev16_0_1 0 => i32:-1130212040
;; run: rev32_0_0 0 => i32:-538128968
;; run: rev32_0_1 0 => i32:-1346606920
;; run: rev32_1_0 0 => i32:1500726264
;; run: rev32_1_1 0 => i32:692248312
;; run: rev64_0_0 0 => i32:-790777272
;; run: rev64_0_1 0 => i32:-2104545464
;; run: rev64_1_0 0 => i32:1248077960
;; run: rev64_1_1 0 => i32:-65690232
;; run: rev64_2_0 0 => i32:1464472840
;; run: rev64_2_1 0 => i32:150704648
;; run: dup_0_0_0 0 => i32:-421083072
;; run: dup_0_0_1 0 => i32:-1212718016
;; run: dup_0_8_0 0 => i32:1330583104
;; run: dup_0_8_1 0 => i32:538948160
;; run: dup_0_15_0 0 => i32:-1431676288
;; run: dup_0_15_1 0 => i32:-1027426688
;; run: dup_1_0_0 0 => i32:-1439659968
;; run: dup_1_0_1 0 => i32:2063672384
;; run: dup_1_4_0 0 => i32:312006208
;; run: dup_1_4_1 0 => i32:-479628736
;; run: dup_1_7_0 0 => i32:1625755840
;; run: dup_1_7_1 0 => i32:2030005440
;; run: dup_2_0_0 0 => i32:-1112504256
;; run: dup_2_0_1 0 => i32:-1904139200
;; run: dup_2_2_0 0 => i32:639161920
;; run: dup_2_2_1 0 => i32:-152473024
;; run: dup_2_3_0 0 => i32:1514995008
;; run: dup_2_3_1 0 => i32:1919244608
;; run: idx 0 => i32:1717121144
;; run: idy 0 => i32:908643192
;; run: ins1_0 0 => i32:1720666273
;; run: ins1_1 0 => i32:-1613153990
;; run: ins1_2 0 => i32:1043384428
;; run: ins2 0 => i32:-506169248
;; run: ins_unaligned 0 => i32:-573541983
;; run: ins_perm 0 => i32:314990956
;; run: zipins 0 => i32:-17709096
;; run: rev4s 0 => i32:-1314630392
;; run: rev16b 0 => i32:725086792
;; run: swaphalves_rev 0 => i32:-1078828280
;; run: same_mixed 0 => i32:980046743
;; run: rnd1_0 0 => i32:477806689
;; run: rnd1_1 0 => i32:-2080796426
;; run: rnd1_2 0 => i32:-42328544
;; run: rnd1_3 0 => i32:-980307523
;; run: rnd1_4 0 => i32:-1831007766
;; run: rnd1_5 0 => i32:-1032354333
;; run: rnd2_0 0 => i32:-820325721
;; run: rnd2_1 0 => i32:-438970392
;; run: rnd2_2 0 => i32:926284461
;; run: rnd2_3 0 => i32:-425751237
;; run: rnd2_4 0 => i32:1143027040
;; run: rnd2_5 0 => i32:-1414518266
;; run: rndy_0 0 => i32:-579837834
;; run: rndy_1 0 => i32:740711036
;; run: rndy_2 0 => i32:87105664
;; run: rndy_3 0 => i32:-247249959
;; run: tbl_twice 0 => i32:1483115080

(module
  (memory 1)
  (data (i32.const 0) "\41\48\4f\56\5d\64\6b\72\79\80\87\8e\95\9c\a3\aa\b1\b8\bf\c6\cd\d4\db\e2\e9\f0\f7\fe\05\0c\13\1a")
  (func $h (param $a i32) (result i32)
    (local $r i32) (local $n i32)
    (local.set $n (i32.const 4))
    (block $done
      (loop $l
        (br_if $done (i32.eqz (local.get $n)))
        (local.set $r (i32.add (i32.mul (local.get $r) (i32.const 31)) (i32.load (local.get $a))))
        (local.set $a (i32.add (local.get $a) (i32.const 4)))
        (local.set $n (i32.sub (local.get $n) (i32.const 1)))
        (br $l)))
    (local.get $r))
  (func $same_first (export "same_first") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 16 1 17 2 18 3 19 4 20 5 21 6 22 7 23 (local.get $x) (local.get $x)))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip1_0_xy (export "zip1_0_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 16 1 17 2 18 3 19 4 20 5 21 6 22 7 23 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip1_0_yx (export "zip1_0_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 0 17 1 18 2 19 3 20 4 21 5 22 6 23 7 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip1_0_xx (export "zip1_0_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 0 1 1 2 2 3 3 4 4 5 5 6 6 7 7 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip1_0_yy (export "zip1_0_yy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 16 17 17 18 18 19 19 20 20 21 21 22 22 23 23 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip1_1_xy (export "zip1_1_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 1 16 17 2 3 18 19 4 5 20 21 6 7 22 23 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip1_1_yx (export "zip1_1_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 17 0 1 18 19 2 3 20 21 4 5 22 23 6 7 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip1_1_xx (export "zip1_1_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 1 0 1 2 3 2 3 4 5 4 5 6 7 6 7 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip1_1_yy (export "zip1_1_yy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 17 16 17 18 19 18 19 20 21 20 21 22 23 22 23 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip1_2_xy (export "zip1_2_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 1 2 3 16 17 18 19 4 5 6 7 20 21 22 23 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip1_2_yx (export "zip1_2_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 17 18 19 0 1 2 3 20 21 22 23 4 5 6 7 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip1_2_xx (export "zip1_2_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 1 2 3 0 1 2 3 4 5 6 7 4 5 6 7 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip1_2_yy (export "zip1_2_yy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 17 18 19 16 17 18 19 20 21 22 23 20 21 22 23 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip1_3_xy (export "zip1_3_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 1 2 3 4 5 6 7 16 17 18 19 20 21 22 23 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip1_3_yx (export "zip1_3_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 17 18 19 20 21 22 23 0 1 2 3 4 5 6 7 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip1_3_xx (export "zip1_3_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip1_3_yy (export "zip1_3_yy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 17 18 19 20 21 22 23 16 17 18 19 20 21 22 23 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip2_0_xy (export "zip2_0_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 8 24 9 25 10 26 11 27 12 28 13 29 14 30 15 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip2_0_yx (export "zip2_0_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 24 8 25 9 26 10 27 11 28 12 29 13 30 14 31 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip2_0_xx (export "zip2_0_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 8 8 9 9 10 10 11 11 12 12 13 13 14 14 15 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip2_0_yy (export "zip2_0_yy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 24 24 25 25 26 26 27 27 28 28 29 29 30 30 31 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip2_1_xy (export "zip2_1_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 8 9 24 25 10 11 26 27 12 13 28 29 14 15 30 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip2_1_yx (export "zip2_1_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 24 25 8 9 26 27 10 11 28 29 12 13 30 31 14 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip2_1_xx (export "zip2_1_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 8 9 8 9 10 11 10 11 12 13 12 13 14 15 14 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip2_1_yy (export "zip2_1_yy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 24 25 24 25 26 27 26 27 28 29 28 29 30 31 30 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip2_2_xy (export "zip2_2_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 8 9 10 11 24 25 26 27 12 13 14 15 28 29 30 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip2_2_yx (export "zip2_2_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 24 25 26 27 8 9 10 11 28 29 30 31 12 13 14 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip2_2_xx (export "zip2_2_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 8 9 10 11 8 9 10 11 12 13 14 15 12 13 14 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip2_2_yy (export "zip2_2_yy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 24 25 26 27 24 25 26 27 28 29 30 31 28 29 30 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip2_3_xy (export "zip2_3_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 8 9 10 11 12 13 14 15 24 25 26 27 28 29 30 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip2_3_yx (export "zip2_3_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 24 25 26 27 28 29 30 31 8 9 10 11 12 13 14 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip2_3_xx (export "zip2_3_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 8 9 10 11 12 13 14 15 8 9 10 11 12 13 14 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zip2_3_yy (export "zip2_3_yy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 24 25 26 27 28 29 30 31 24 25 26 27 28 29 30 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp1_0_xy (export "uzp1_0_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 2 4 6 8 10 12 14 16 18 20 22 24 26 28 30 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp1_0_yx (export "uzp1_0_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 18 20 22 24 26 28 30 0 2 4 6 8 10 12 14 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp1_0_xx (export "uzp1_0_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 2 4 6 8 10 12 14 0 2 4 6 8 10 12 14 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp1_0_yy (export "uzp1_0_yy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 18 20 22 24 26 28 30 16 18 20 22 24 26 28 30 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp1_1_xy (export "uzp1_1_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 1 4 5 8 9 12 13 16 17 20 21 24 25 28 29 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp1_1_yx (export "uzp1_1_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 17 20 21 24 25 28 29 0 1 4 5 8 9 12 13 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp1_1_xx (export "uzp1_1_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 1 4 5 8 9 12 13 0 1 4 5 8 9 12 13 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp1_1_yy (export "uzp1_1_yy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 17 20 21 24 25 28 29 16 17 20 21 24 25 28 29 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp1_2_xy (export "uzp1_2_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 1 2 3 8 9 10 11 16 17 18 19 24 25 26 27 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp1_2_yx (export "uzp1_2_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 17 18 19 24 25 26 27 0 1 2 3 8 9 10 11 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp1_2_xx (export "uzp1_2_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 1 2 3 8 9 10 11 0 1 2 3 8 9 10 11 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp1_2_yy (export "uzp1_2_yy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 17 18 19 24 25 26 27 16 17 18 19 24 25 26 27 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp2_0_xy (export "uzp2_0_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 1 3 5 7 9 11 13 15 17 19 21 23 25 27 29 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp2_0_yx (export "uzp2_0_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 17 19 21 23 25 27 29 31 1 3 5 7 9 11 13 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp2_0_xx (export "uzp2_0_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 1 3 5 7 9 11 13 15 1 3 5 7 9 11 13 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp2_0_yy (export "uzp2_0_yy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 17 19 21 23 25 27 29 31 17 19 21 23 25 27 29 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp2_1_xy (export "uzp2_1_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 2 3 6 7 10 11 14 15 18 19 22 23 26 27 30 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp2_1_yx (export "uzp2_1_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 18 19 22 23 26 27 30 31 2 3 6 7 10 11 14 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp2_1_xx (export "uzp2_1_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 2 3 6 7 10 11 14 15 2 3 6 7 10 11 14 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp2_1_yy (export "uzp2_1_yy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 18 19 22 23 26 27 30 31 18 19 22 23 26 27 30 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp2_2_xy (export "uzp2_2_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 4 5 6 7 12 13 14 15 20 21 22 23 28 29 30 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp2_2_yx (export "uzp2_2_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 20 21 22 23 28 29 30 31 4 5 6 7 12 13 14 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp2_2_xx (export "uzp2_2_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 4 5 6 7 12 13 14 15 4 5 6 7 12 13 14 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $uzp2_2_yy (export "uzp2_2_yy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 20 21 22 23 28 29 30 31 20 21 22 23 28 29 30 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn1_0_xy (export "trn1_0_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 16 2 18 4 20 6 22 8 24 10 26 12 28 14 30 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn1_0_yx (export "trn1_0_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 0 18 2 20 4 22 6 24 8 26 10 28 12 30 14 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn1_0_xx (export "trn1_0_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 0 2 2 4 4 6 6 8 8 10 10 12 12 14 14 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn1_0_yy (export "trn1_0_yy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 16 18 18 20 20 22 22 24 24 26 26 28 28 30 30 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn1_1_xy (export "trn1_1_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 1 16 17 4 5 20 21 8 9 24 25 12 13 28 29 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn1_1_yx (export "trn1_1_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 17 0 1 20 21 4 5 24 25 8 9 28 29 12 13 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn1_1_xx (export "trn1_1_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 1 0 1 4 5 4 5 8 9 8 9 12 13 12 13 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn1_1_yy (export "trn1_1_yy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 17 16 17 20 21 20 21 24 25 24 25 28 29 28 29 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn1_2_xy (export "trn1_2_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 1 2 3 16 17 18 19 8 9 10 11 24 25 26 27 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn1_2_yx (export "trn1_2_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 17 18 19 0 1 2 3 24 25 26 27 8 9 10 11 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn1_2_xx (export "trn1_2_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 1 2 3 0 1 2 3 8 9 10 11 8 9 10 11 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn1_2_yy (export "trn1_2_yy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 17 18 19 16 17 18 19 24 25 26 27 24 25 26 27 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn2_0_xy (export "trn2_0_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 1 17 3 19 5 21 7 23 9 25 11 27 13 29 15 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn2_0_yx (export "trn2_0_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 17 1 19 3 21 5 23 7 25 9 27 11 29 13 31 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn2_0_xx (export "trn2_0_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 1 1 3 3 5 5 7 7 9 9 11 11 13 13 15 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn2_0_yy (export "trn2_0_yy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 17 17 19 19 21 21 23 23 25 25 27 27 29 29 31 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn2_1_xy (export "trn2_1_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 2 3 18 19 6 7 22 23 10 11 26 27 14 15 30 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn2_1_yx (export "trn2_1_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 18 19 2 3 22 23 6 7 26 27 10 11 30 31 14 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn2_1_xx (export "trn2_1_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 2 3 2 3 6 7 6 7 10 11 10 11 14 15 14 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn2_1_yy (export "trn2_1_yy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 18 19 18 19 22 23 22 23 26 27 26 27 30 31 30 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn2_2_xy (export "trn2_2_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 4 5 6 7 20 21 22 23 12 13 14 15 28 29 30 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn2_2_yx (export "trn2_2_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 20 21 22 23 4 5 6 7 28 29 30 31 12 13 14 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn2_2_xx (export "trn2_2_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 4 5 6 7 4 5 6 7 12 13 14 15 12 13 14 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $trn2_2_yy (export "trn2_2_yy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 20 21 22 23 20 21 22 23 28 29 30 31 28 29 30 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_1_xy (export "ext_1_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_1_yx (export "ext_1_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 0 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_1_xx (export "ext_1_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 0 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_2_xy (export "ext_2_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_2_yx (export "ext_2_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 18 19 20 21 22 23 24 25 26 27 28 29 30 31 0 1 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_2_xx (export "ext_2_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 2 3 4 5 6 7 8 9 10 11 12 13 14 15 0 1 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_3_xy (export "ext_3_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_3_yx (export "ext_3_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 19 20 21 22 23 24 25 26 27 28 29 30 31 0 1 2 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_3_xx (export "ext_3_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 3 4 5 6 7 8 9 10 11 12 13 14 15 0 1 2 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_4_xy (export "ext_4_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_4_yx (export "ext_4_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 20 21 22 23 24 25 26 27 28 29 30 31 0 1 2 3 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_4_xx (export "ext_4_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 4 5 6 7 8 9 10 11 12 13 14 15 0 1 2 3 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_5_xy (export "ext_5_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_5_yx (export "ext_5_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 21 22 23 24 25 26 27 28 29 30 31 0 1 2 3 4 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_5_xx (export "ext_5_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 5 6 7 8 9 10 11 12 13 14 15 0 1 2 3 4 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_6_xy (export "ext_6_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_6_yx (export "ext_6_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 22 23 24 25 26 27 28 29 30 31 0 1 2 3 4 5 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_6_xx (export "ext_6_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 6 7 8 9 10 11 12 13 14 15 0 1 2 3 4 5 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_7_xy (export "ext_7_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_7_yx (export "ext_7_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 23 24 25 26 27 28 29 30 31 0 1 2 3 4 5 6 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_7_xx (export "ext_7_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 7 8 9 10 11 12 13 14 15 0 1 2 3 4 5 6 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_8_xy (export "ext_8_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_8_yx (export "ext_8_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 24 25 26 27 28 29 30 31 0 1 2 3 4 5 6 7 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_8_xx (export "ext_8_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 8 9 10 11 12 13 14 15 0 1 2 3 4 5 6 7 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_9_xy (export "ext_9_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_9_yx (export "ext_9_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 25 26 27 28 29 30 31 0 1 2 3 4 5 6 7 8 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_9_xx (export "ext_9_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 9 10 11 12 13 14 15 0 1 2 3 4 5 6 7 8 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_10_xy (export "ext_10_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_10_yx (export "ext_10_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 26 27 28 29 30 31 0 1 2 3 4 5 6 7 8 9 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_10_xx (export "ext_10_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 10 11 12 13 14 15 0 1 2 3 4 5 6 7 8 9 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_11_xy (export "ext_11_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_11_yx (export "ext_11_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 27 28 29 30 31 0 1 2 3 4 5 6 7 8 9 10 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_11_xx (export "ext_11_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 11 12 13 14 15 0 1 2 3 4 5 6 7 8 9 10 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_12_xy (export "ext_12_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_12_yx (export "ext_12_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 28 29 30 31 0 1 2 3 4 5 6 7 8 9 10 11 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_12_xx (export "ext_12_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 12 13 14 15 0 1 2 3 4 5 6 7 8 9 10 11 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_13_xy (export "ext_13_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_13_yx (export "ext_13_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 29 30 31 0 1 2 3 4 5 6 7 8 9 10 11 12 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_13_xx (export "ext_13_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 13 14 15 0 1 2 3 4 5 6 7 8 9 10 11 12 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_14_xy (export "ext_14_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_14_yx (export "ext_14_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 30 31 0 1 2 3 4 5 6 7 8 9 10 11 12 13 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_14_xx (export "ext_14_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 14 15 0 1 2 3 4 5 6 7 8 9 10 11 12 13 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_15_xy (export "ext_15_xy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_15_yx (export "ext_15_yx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 31 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ext_15_xx (export "ext_15_xx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 15 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rev16_0_0 (export "rev16_0_0") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 1 0 3 2 5 4 7 6 9 8 11 10 13 12 15 14 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rev16_0_1 (export "rev16_0_1") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 17 16 19 18 21 20 23 22 25 24 27 26 29 28 31 30 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rev32_0_0 (export "rev32_0_0") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 3 2 1 0 7 6 5 4 11 10 9 8 15 14 13 12 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rev32_0_1 (export "rev32_0_1") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 19 18 17 16 23 22 21 20 27 26 25 24 31 30 29 28 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rev32_1_0 (export "rev32_1_0") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 2 3 0 1 6 7 4 5 10 11 8 9 14 15 12 13 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rev32_1_1 (export "rev32_1_1") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 18 19 16 17 22 23 20 21 26 27 24 25 30 31 28 29 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rev64_0_0 (export "rev64_0_0") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 7 6 5 4 3 2 1 0 15 14 13 12 11 10 9 8 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rev64_0_1 (export "rev64_0_1") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 23 22 21 20 19 18 17 16 31 30 29 28 27 26 25 24 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rev64_1_0 (export "rev64_1_0") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 6 7 4 5 2 3 0 1 14 15 12 13 10 11 8 9 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rev64_1_1 (export "rev64_1_1") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 22 23 20 21 18 19 16 17 30 31 28 29 26 27 24 25 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rev64_2_0 (export "rev64_2_0") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 4 5 6 7 0 1 2 3 12 13 14 15 8 9 10 11 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rev64_2_1 (export "rev64_2_1") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 20 21 22 23 16 17 18 19 28 29 30 31 24 25 26 27 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $dup_0_0_0 (export "dup_0_0_0") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $dup_0_0_1 (export "dup_0_0_1") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 16 16 16 16 16 16 16 16 16 16 16 16 16 16 16 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $dup_0_8_0 (export "dup_0_8_0") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 8 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $dup_0_8_1 (export "dup_0_8_1") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 24 24 24 24 24 24 24 24 24 24 24 24 24 24 24 24 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $dup_0_15_0 (export "dup_0_15_0") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 15 15 15 15 15 15 15 15 15 15 15 15 15 15 15 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $dup_0_15_1 (export "dup_0_15_1") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 31 31 31 31 31 31 31 31 31 31 31 31 31 31 31 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $dup_1_0_0 (export "dup_1_0_0") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 1 0 1 0 1 0 1 0 1 0 1 0 1 0 1 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $dup_1_0_1 (export "dup_1_0_1") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 17 16 17 16 17 16 17 16 17 16 17 16 17 16 17 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $dup_1_4_0 (export "dup_1_4_0") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 8 9 8 9 8 9 8 9 8 9 8 9 8 9 8 9 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $dup_1_4_1 (export "dup_1_4_1") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 24 25 24 25 24 25 24 25 24 25 24 25 24 25 24 25 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $dup_1_7_0 (export "dup_1_7_0") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 14 15 14 15 14 15 14 15 14 15 14 15 14 15 14 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $dup_1_7_1 (export "dup_1_7_1") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 30 31 30 31 30 31 30 31 30 31 30 31 30 31 30 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $dup_2_0_0 (export "dup_2_0_0") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 1 2 3 0 1 2 3 0 1 2 3 0 1 2 3 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $dup_2_0_1 (export "dup_2_0_1") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 17 18 19 16 17 18 19 16 17 18 19 16 17 18 19 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $dup_2_2_0 (export "dup_2_2_0") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 8 9 10 11 8 9 10 11 8 9 10 11 8 9 10 11 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $dup_2_2_1 (export "dup_2_2_1") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 24 25 26 27 24 25 26 27 24 25 26 27 24 25 26 27 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $dup_2_3_0 (export "dup_2_3_0") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 12 13 14 15 12 13 14 15 12 13 14 15 12 13 14 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $dup_2_3_1 (export "dup_2_3_1") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 28 29 30 31 28 29 30 31 28 29 30 31 28 29 30 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $idx (export "idx") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $idy (export "idy") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ins1_0 (export "ins1_0") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 17 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ins1_1 (export "ins1_1") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 18 19 2 3 4 5 6 7 8 9 10 11 12 13 14 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ins1_2 (export "ins1_2") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 20 21 22 23 4 5 6 7 8 9 10 11 12 13 14 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ins2 (export "ins2") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 17 18 19 8 9 10 11 24 25 26 27 0 1 2 3 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ins_unaligned (export "ins_unaligned") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 17 18 19 20 4 5 6 7 8 9 10 11 12 13 14 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $ins_perm (export "ins_perm") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 20 22 21 23 4 5 6 7 8 9 10 11 12 13 14 15 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $zipins (export "zipins") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 0 1 2 3 16 17 18 19 4 5 6 7 28 29 30 31 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rev4s (export "rev4s") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 12 13 14 15 8 9 10 11 4 5 6 7 0 1 2 3 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rev16b (export "rev16b") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 15 14 13 12 11 10 9 8 7 6 5 4 3 2 1 0 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $swaphalves_rev (export "swaphalves_rev") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 12 13 14 15 8 9 10 11 20 21 22 23 16 17 18 19 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $same_mixed (export "same_mixed") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 3 19 0 30 9 1 17 14 2 2 21 8 13 11 27 5 (local.get $x) (local.get $x)))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rnd1_0 (export "rnd1_0") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 8 11 0 14 7 1 5 3 11 15 7 12 3 7 0 6 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rnd1_1 (export "rnd1_1") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 13 8 5 12 5 2 4 14 4 4 0 0 6 6 5 5 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rnd1_2 (export "rnd1_2") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 9 10 6 6 5 6 12 9 0 11 13 5 4 8 2 10 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rnd1_3 (export "rnd1_3") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 9 0 10 2 9 11 9 15 10 5 15 15 5 1 8 0 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rnd1_4 (export "rnd1_4") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 11 12 0 13 11 12 0 14 1 5 6 3 7 14 11 11 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rnd1_5 (export "rnd1_5") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 8 14 3 11 9 1 13 2 6 10 11 4 10 8 2 9 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rnd2_0 (export "rnd2_0") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 20 19 11 5 9 19 30 10 3 5 25 2 15 22 16 29 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rnd2_1 (export "rnd2_1") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 26 9 3 2 31 21 13 8 8 26 6 10 27 23 9 3 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rnd2_2 (export "rnd2_2") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 26 18 9 29 10 29 31 20 30 17 18 30 25 9 7 24 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rnd2_3 (export "rnd2_3") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 11 31 21 11 5 31 17 23 4 22 2 19 23 17 31 16 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rnd2_4 (export "rnd2_4") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 18 21 11 0 30 16 20 17 29 18 22 22 17 22 26 22 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rnd2_5 (export "rnd2_5") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 11 28 23 21 9 10 12 23 30 18 5 26 10 26 19 17 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rndy_0 (export "rndy_0") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 16 22 21 30 21 23 21 17 31 23 21 17 20 19 26 21 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rndy_1 (export "rndy_1") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 31 22 17 29 30 27 28 18 22 23 27 16 27 28 24 29 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rndy_2 (export "rndy_2") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 19 27 17 25 19 25 26 25 27 20 29 29 27 30 20 21 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $rndy_3 (export "rndy_3") (param $p i32) (result i32)
    (local $x v128)
    (local.set $x (v128.load (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 28 31 22 20 18 27 16 28 19 26 20 26 28 29 29 23 (local.get $x) (v128.load offset=16 (local.get $p))))
    (call $h (i32.add (local.get $p) (i32.const 64))))
  (func $tbl_twice (export "tbl_twice") (param $p i32) (result i32)
    (local $x v128) (local $y v128)
    (local.set $x (v128.load (local.get $p)))
    (local.set $y (v128.load offset=16 (local.get $p)))
    (v128.store offset=64 (local.get $p) (i8x16.shuffle 3 29 7 0 18 11 25 4 9 30 2 16 13 6 21 1 (local.get $x) (local.get $y)))
    (v128.store offset=80 (local.get $p) (i8x16.shuffle 3 29 7 0 18 11 25 4 9 30 2 16 13 6 21 1 (local.get $y) (local.get $x)))
    (i32.add (i32.mul (call $h (i32.add (local.get $p) (i32.const 64))) (i32.const 7))
             (call $h (i32.add (local.get $p) (i32.const 80)))))
)
