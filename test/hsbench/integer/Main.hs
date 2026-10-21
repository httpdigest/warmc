-- Arbitrary-precision Integer: a large factorial, Fibonacci numbers in the
-- tens of thousands of digits, gcds and a decimal rendering of each.
--
-- ghc-bignum's GMP backend, with GMP's mpn layer built into the module by
-- clang in its generic C form -- there is no wasm assembly for it -- so the
-- limb loops for multiply, add and the divisions show needs are LLVM's code
-- called from GHC's.  The one kernel in any corpus where the arithmetic
-- itself is the program.
module Main (main) where

import Data.List (foldl')

fibs :: Int -> Integer
fibs n = go n 0 1
  where
    go 0 a _ = a
    go k a b = go (k - 1) b $! a + b

digitSum :: Integer -> Int
digitSum = foldl' (\s c -> s + fromEnum c - fromEnum '0') 0 . show

main :: IO ()
main = do
  let f = product [1 .. 4000 :: Integer]
      a = fibs 60000
      b = fibs 59999
      g = gcd (f + 1) (a * 7 + 3)
  print (digitSum f, digitSum a, length (show b), g `mod` 1000000007)
