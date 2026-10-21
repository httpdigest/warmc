{-# LANGUAGE BangPatterns #-}
-- Data.Map.Strict under a mixed workload of inserts, updates, lookups and
-- deletes on random keys.
--
-- A persistent balanced tree: every insert or delete copies the path from the
-- root and rebalances it, leaving the old version to the collector, so the row
-- is allocation and comparisons through the Ord dictionary GHC specialises
-- for Word32.  cppbench/map does the same with a mutable red-black tree.
module Main (main) where

import Data.Bits (shiftL, shiftR, xor)
import qualified Data.Map.Strict as M
import Data.Word (Word32)

next :: Word32 -> Word32
next x0 = x3
  where
    x1 = x0 `xor` (x0 `shiftL` 13)
    x2 = x1 `xor` (x1 `shiftR` 17)
    x3 = x2 `xor` (x2 `shiftL` 5)

run :: Int -> Word32 -> M.Map Word32 Int -> Int -> (M.Map Word32 Int, Int)
run 0 _ m !acc = (m, acc)
run i x m !acc =
  let x' = next x
      k = x' `mod` 50000
   in case x' `shiftR` 30 of
        0 -> run (i - 1) x' (M.insertWith (+) k i m) acc
        1 -> run (i - 1) x' (M.insert k (i * 3) m) acc
        2 -> run (i - 1) x' m (acc + M.findWithDefault 1 k m)
        _ -> run (i - 1) x' (M.delete k m) acc

main :: IO ()
main = do
  let (m, acc) = run 600000 2463534242 M.empty 0
  print (M.size m, acc, M.foldl' (+) 0 m)
