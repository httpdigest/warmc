{-# LANGUAGE BangPatterns #-}
-- Laziness as an algorithm: a prime list that filters itself by trial division
-- against its own prefix, the Hamming numbers as an infinite list merged from
-- three scaled copies of itself, and longest common subsequences memoised in
-- a lazy boxed array whose cells are thunks that index the array they live in.
--
-- Each table cell is a thunk allocated when the array is built.  Forcing one
-- pushes an update frame on the Haskell stack that GHC keeps in linear memory,
-- enters the neighbouring cells by loading their info pointers and returning
-- the entry code to StgRun, and when the value comes back the frame overwrites
-- the thunk with an indirection to it; the two infinite lists are CAFs,
-- blackholed under stg_bh_upd_frame while they grow.  The other kernels pay for laziness incidentally; here it is the
-- algorithm, so the row measures thunks, updates and indirections themselves.
module Main (main) where

import Data.Array (Array, array, listArray, (!))
import Data.Bits (shiftL, shiftR, xor, (.&.))
import Data.Int (Int64)
import Data.List (foldl')
import Data.Word (Word32)

primes :: [Int]
primes = 2 : filter isPrime [3, 5 ..]
  where
    isPrime n = all (\p -> n `rem` p /= 0) (takeWhile (\p -> p * p <= n) primes)

hamming :: [Int64]
hamming = 1 : merge3 (map (* 2) hamming) (map (* 3) hamming) (map (* 5) hamming)
  where
    merge3 xs ys zs = merge xs (merge ys zs)
    merge xa@(x : xs) ya@(y : ys)
      | x < y = x : merge xs ya
      | x > y = y : merge xa ys
      | otherwise = x : merge xs ys
    merge xs [] = xs
    merge [] ys = ys

randoms :: Word32 -> [Int]
randoms = map (\x -> fromIntegral (x `shiftR` 7) .&. 3) . tail . iterate next
  where
    next x0 =
      let x1 = x0 `xor` (x0 `shiftL` 13)
          x2 = x1 `xor` (x1 `shiftR` 17)
       in x2 `xor` (x2 `shiftL` 5)

lcs :: Int -> Word32 -> Int
lcs n seed = table ! (0, 0)
  where
    xs = listArray (0, n - 1) (randoms seed) :: Array Int Int
    ys = listArray (0, n - 1) (randoms (seed * 7 + 1)) :: Array Int Int
    table :: Array (Int, Int) Int
    table = array ((0, 0), (n, n)) [((i, j), cell i j) | i <- [0 .. n], j <- [0 .. n]]
    cell i j
      | i == n || j == n = 0
      | xs ! i == ys ! j = 1 + table ! (i + 1, j + 1)
      | otherwise = max (table ! (i + 1, j)) (table ! (i, j + 1))

main :: IO ()
main = do
  let p = foldl' (\acc q -> (acc * 31 + q) .&. 0xfffffff) 0 (take 30000 primes)
      h = hamming !! 9000
      l = map (lcs 300) [1 .. 12]
  print (p, h, l)
