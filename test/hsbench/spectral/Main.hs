{-# LANGUAGE BangPatterns #-}
-- spectral-norm from the Benchmarks Game: the largest singular value of an
-- infinite matrix, by power iteration on A^T A, with the matrix-vector
-- products as strict loops over unboxed Double arrays.
--
-- A UArray Double is a byte array, so v ! j is a bounds check and an f64.load,
-- and the inner loop is a single wasm loop with its accumulator in a local:
-- f64.convert_i32_s, f64.div, f64.mul and f64.add, with the `div` by two an
-- arithmetic shift.  Floating point in a loop that stays inside one wasm
-- function, where typeclass's Doubles go through dictionary calls instead.
module Main (main) where

import Data.Array.Unboxed (UArray, elems, listArray, (!))
import Data.List (foldl')

a :: Int -> Int -> Double
a i j = 1 / fromIntegral ((i + j) * (i + j + 1) `div` 2 + i + 1)

mulAv :: Int -> UArray Int Double -> UArray Int Double
mulAv n v = listArray (0, n - 1) [row i 0 0 | i <- [0 .. n - 1]]
  where
    row !i !j !acc
      | j >= n = acc
      | otherwise = row i (j + 1) (acc + a i j * v ! j)

mulAtv :: Int -> UArray Int Double -> UArray Int Double
mulAtv n v = listArray (0, n - 1) [row i 0 0 | i <- [0 .. n - 1]]
  where
    row !i !j !acc
      | j >= n = acc
      | otherwise = row i (j + 1) (acc + a j i * v ! j)

mulAtAv :: Int -> UArray Int Double -> UArray Int Double
mulAtAv n = mulAtv n . mulAv n

spectral :: Int -> Double
spectral n =
  let u0 = listArray (0, n - 1) (replicate n 1) :: UArray Int Double
      go :: Int -> UArray Int Double -> UArray Int Double -> (UArray Int Double, UArray Int Double)
      go 0 u v = (u, v)
      go k u _ = let v = mulAtAv n u in go (k - 1) (mulAtAv n v) v
      (uf, vf) = go 10 u0 u0
      dot x y = foldl' (+) 0 (zipWith (*) (elems x) (elems y))
   in sqrt (dot uf vf / dot vf vf)

main :: IO ()
main = print (map spectral [500, 1000, 1200])
