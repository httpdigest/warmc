{-# LANGUAGE BangPatterns #-}
-- Data.IntMap.Strict: build maps from random keys, merge them with unionWith,
-- filter, and fold over the result with its keys.
--
-- A big-endian Patricia trie rather than a balanced tree, so a lookup is a
-- descent on key bits with no comparisons through a dictionary, and a union
-- is a structural merge of two tries.  Next to map, the row separates the cost
-- of persistence from the cost of keeping a tree balanced.
module Main (main) where

import Data.Bits (shiftL, shiftR, xor, (.&.))
import qualified Data.IntMap.Strict as IM
import Data.List (foldl')

next :: Int -> Int
next x0 = x3 .&. 0xffffffff
  where
    x1 = x0 `xor` ((x0 `shiftL` 13) .&. 0xffffffff)
    x2 = x1 `xor` (x1 `shiftR` 17)
    x3 = x2 `xor` ((x2 `shiftL` 5) .&. 0xffffffff)

build :: Int -> Int -> (IM.IntMap Int, Int)
build n seed = go n seed IM.empty
  where
    go 0 x m = (m, x)
    go i x m = let x' = next x in go (i - 1) x' (IM.insertWith (+) (x' .&. 0xfffff) i m)

round' :: Int -> Int -> (Int, Int)
round' r seed =
  let (a, s1) = build 40000 seed
      (b, s2) = build 40000 s1
      u = IM.unionWith (\p q -> p * 3 + q) a b
      f = IM.filterWithKey (\k v -> (k + v + r) .&. 3 /= 0) u
      !h = IM.foldlWithKey' (\acc k v -> (acc * 31 + k + v) .&. 0xfffffff) 0 f
   in (h + IM.size f, s2)

main :: IO ()
main = do
  let step (acc, seed) r = let (h, s) = round' r seed in ((acc * 7 + h) .&. 0xfffffff, s)
  print (fst (foldl' step (0, 88172645) [1 .. 8]))
