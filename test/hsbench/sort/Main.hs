-- Data.List.sort and sortOn over lists of random numbers and pairs, then
-- group on the sorted result.
--
-- GHC's list sort is a merge sort that first splits its input into ascending
-- and descending runs: every merge step allocates a cons cell and the
-- comparisons go through Ord, specialised here to Int and to tuples.  The
-- opposite of cppbench/sort, which moves records in place.
module Main (main) where

import Data.Bits (shiftL, shiftR, xor, (.&.))
import Data.List (foldl', group, sort, sortOn)

randoms :: Int -> Int -> [Int]
randoms n seed = take n (tail (iterate next seed))
  where
    next x0 =
      let x1 = x0 `xor` ((x0 `shiftL` 13) .&. 0xffffffff)
          x2 = x1 `xor` (x1 `shiftR` 17)
       in x2 `xor` ((x2 `shiftL` 5) .&. 0xffffffff)

checksum :: [Int] -> Int
checksum = foldl' (\a v -> (a * 31 + v) .&. 0xfffffff) 0

main :: IO ()
main = do
  let rounds = [1 .. 4] :: [Int]
      one r =
        let xs = randoms 60000 (r * 7919)
            s = sort (map (`mod` 100000) xs)
            p = sortOn snd (zip xs (map (`mod` 977) xs))
            g = map length (group s)
         in checksum s + checksum (map fst (take 1000 p)) + checksum g
  print (map one rounds)
