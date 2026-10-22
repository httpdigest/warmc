{-# LANGUAGE BangPatterns #-}
-- Data.Sequence: the Josephus problem by index and deleteAt, a sliding-window
-- maximum with the sequence as a deque, and random rotations by splitAt and
-- append with an adjust' at the front.
--
-- A sequence is a finger tree of 2-3 nodes annotated with their sizes: index
-- descends by subtracting sizes, deleteAt and splitAt also rebuild the path
-- they took, and the deque operations touch only the digits at either end.  None of it is specialised into this module: the drivers are Main's and
-- the tree code is containers' own, every rebuilt node a heap allocation.  The
-- persistent structure with fast access at both ends, next to set and map's
-- balanced trees.
module Main (main) where

import Data.Bits (shiftL, shiftR, xor, (.&.))
import Data.Foldable (foldl')
import Data.Sequence (Seq, ViewL (..), ViewR (..), (><), (|>))
import qualified Data.Sequence as Seq
import Data.Word (Word32)

next :: Word32 -> Word32
next x0 = x3
  where
    x1 = x0 `xor` (x0 `shiftL` 13)
    x2 = x1 `xor` (x1 `shiftR` 17)
    x3 = x2 `xor` (x2 `shiftL` 5)

josephus :: Int -> Int -> Int
josephus n k = go (Seq.fromList [1 .. n]) 0 0
  where
    go s !i !acc
      | Seq.null s = acc
      | otherwise =
          let j = (i + k - 1) `mod` Seq.length s
              x = Seq.index s j
           in go (Seq.deleteAt j s) j ((acc * 31 + x) .&. 0xfffffff)

windowMax :: Int -> Int -> Word32 -> Int
windowMax w n seed = go Seq.empty 0 seed 0
  where
    go :: Seq (Int, Int) -> Int -> Word32 -> Int -> Int
    go !dq !i !x !acc
      | i >= n = acc
      | otherwise =
          let x' = next x
              v = fromIntegral (x' `shiftR` 8)
              dropBack q = case Seq.viewr q of
                q' :> (_, u) | u <= v -> dropBack q'
                _ -> q
              dq1 = dropBack dq |> (i, v)
              dq2 = case Seq.viewl dq1 of
                (j, _) :< q' | j <= i - w -> q'
                _ -> dq1
              acc' = case Seq.viewl dq2 of
                (_, m) :< _ | i >= w - 1 -> (acc * 31 + m) .&. 0xfffffff
                _ -> acc
           in go dq2 (i + 1) x' acc'

rotations :: Int -> Int -> Word32 -> Int
rotations len n seed = go n seed (Seq.fromList [0 .. len - 1])
  where
    go 0 _ s = foldl' (\acc v -> (acc * 31 + v) .&. 0xfffffff) 0 s
    go i x s =
      let x' = next x
          j = fromIntegral (x' `mod` fromIntegral len)
          (a, b) = Seq.splitAt j s
       in go (i - 1) x' (Seq.adjust' (+ i) 0 (b >< a))

main :: IO ()
main = print (josephus 32000 7, windowMax 64 160000 2463534242, rotations 5000 48000 88172645)
