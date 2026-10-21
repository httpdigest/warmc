{-# LANGUAGE BangPatterns #-}
-- The sieve of Eratosthenes in the ST monad over an unboxed mutable array,
-- run for several limits.
--
-- Imperative Haskell: an STUArray of Bool is a byte array GHC indexes with
-- primops, and the loops are local functions GHC compiles to join points, so
-- this is the kernel closest to cbench/sieve in what it asks of the code
-- generator -- with GHC's bounds checks on every read and write.
module Main (main) where

import Control.Monad (forM_, when)
import Control.Monad.ST (ST, runST)
import Data.Array.ST (STUArray, newArray, readArray, writeArray)

countPrimes :: Int -> Int
countPrimes n = runST $ do
  arr <- newArray (2, n) True :: ST s (STUArray s Int Bool)
  forM_ [2 .. n] $ \i -> do
    isP <- readArray arr i
    when (isP && i <= n `div` i) $
      forM_ [i * i, i * i + i .. n] $ \j -> writeArray arr j False
  let count !acc i
        | i > n = return acc
        | otherwise = do
            p <- readArray arr i
            count (if p then acc + 1 else acc) (i + 1)
  count 0 2

main :: IO ()
main = print (map countPrimes [1000000, 2000000, 3000000, 4000000])
