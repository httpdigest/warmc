{-# LANGUAGE BangPatterns #-}
-- Data.Set: build sets from random keys, then union, intersect and difference
-- them, split one at a pivot, map and filter the halves, and probe with
-- member and lookupGE.
--
-- GHC specialises insert, union, intersection, difference, split and map
-- to Int inside this module, so a key comparison is an inline i32 compare
-- rather than a call through an Ord dictionary; rebalancing and linking, which
-- never compare, stay the containers library's own code.  Next to map, whose
-- work is one key at a time, this row is whole-set merges: divide and conquer
-- over split and link, allocating a new tree for every result.
module Main (main) where

import Data.Bits (shiftL, shiftR, xor, (.&.))
import Data.List (foldl')
import qualified Data.Set as S
import Data.Word (Word32)

next :: Word32 -> Word32
next x0 = x3
  where
    x1 = x0 `xor` (x0 `shiftL` 13)
    x2 = x1 `xor` (x1 `shiftR` 17)
    x3 = x2 `xor` (x2 `shiftL` 5)

keyOf :: Word32 -> Int
keyOf x = fromIntegral (x `mod` 200000)

build :: Int -> Word32 -> (S.Set Int, Word32)
build n seed = go n seed S.empty
  where
    go 0 x !s = (s, x)
    go i x !s = let x' = next x in go (i - 1) x' (S.insert (keyOf x') s)

probe :: S.Set Int -> Int -> Word32 -> (Int, Word32)
probe s n seed = go n seed 0
  where
    go 0 x !acc = (acc, x)
    go i x !acc =
      let x' = next x
       in go (i - 1) x' (if S.member (keyOf x') s then acc + 1 else acc)

fold :: S.Set Int -> Int
fold = S.foldl' (\acc v -> (acc * 31 + v) .&. 0xfffffff) 0

round' :: Int -> Word32 -> (Int, Word32)
round' r seed =
  let (a, s1) = build 20000 seed
      (b, s2) = build 20000 s1
      u = S.union a b
      i = S.intersection a b
      d = S.difference u i
      (hits, s3) = probe d 20000 s2
      (lo, hi) = S.split (keyOf s3) d
      m = S.map (\v -> v `div` 7 + r) lo
      f = S.filter odd hi
      ge = maybe 0 id (S.lookupGE 100000 d)
   in ((fold m + fold f + S.size i * 3 + hits + ge) .&. 0xfffffff, s3)

main :: IO ()
main = do
  let step (!acc, !seed) r = let (h, s) = round' r seed in ((acc * 7 + h) .&. 0xfffffff, s)
  print (fst (foldl' step (0, 2463534242) [1 .. 4]))
