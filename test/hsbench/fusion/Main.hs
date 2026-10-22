{-# LANGUAGE BangPatterns #-}
-- List code that foldr/build fusion turns into loops: nested comprehensions
-- with guards and lets, zip and zipWith over enumerations, concatMap, iterate
-- and takeWhile, each summed or counted without a list being built.
--
-- GHC rewrites each of these into loops over unboxed Ints that never build
-- the list: join points that become wasm loops inside a single function, with
-- `rem` an i32.rem_s.  zipWith3 fuses only with its first list, so the other
-- two are floated out as shared lists walked cell by cell.  Next to nqueens,
-- which is list code that allocates, this is list code that does not, and the
-- row is how well warmc compiles GHC's tight integer loops.
module Main (main) where

import Data.Bits (shiftR, xor, (.&.))
import Data.List (foldl')

pairs :: Int -> Int -> Int
pairs n r = sum [(x * y + r) .&. 1023 | x <- [1 .. n], y <- [x .. n], (x + y + r) `rem` 3 /= 0]

triples :: Int -> Int
triples n = length [() | a <- [1 .. n], b <- [a .. n], let c2 = a * a + b * b, c2 .&. 7 == 1, c2 `rem` 13 < 4]

zipped :: Int -> Int -> Int
zipped n r = sum (zipWith3 (\a b c -> (a * b) `xor` c) [r .. r + n] [n, n - 1 .. 0] (map (* 3) [0 .. n]))

spread :: Int -> Int
spread n = foldl' (\acc v -> (acc * 31 + v) .&. 0xfffffff) 0 (concatMap (\x -> [x, x `shiftR` 1, x * 3]) [1 .. n])

orbit :: Int -> Int
orbit s = sum (takeWhile (/= 1) (iterate (\v -> if even v then v `div` 2 else 3 * v + 1) s))

main :: IO ()
main = do
  let one r =
        pairs 600 r + triples (300 + r) + zipped 200000 r + spread (100000 + r)
          + foldl' (\a s -> a + orbit s) 0 [r * 1000 + 1 .. r * 1000 + 2000]
  print (map ((.&. 0xfffffff) . one) [1 .. 32])
