{-# LANGUAGE BangPatterns #-}
-- Text.Printf and show/read: format records with printf into fixed-width
-- lines of integers, hex, padded strings and Doubles in fixed and exponent
-- notation, then split the lines back into fields and read the numbers out of
-- them again.
--
-- printf interprets its format string at run time over a list of argument
-- formatters built by the PrintfType class.  Doubles are rendered by
-- floatToDigits and read back through ReadP's lexer and fromRational, both in
-- Integer arithmetic, and on this 32-bit target a Double's 53-bit mantissa is a
-- big Integer, so the row reaches GMP's mpn code (__gmpn_tdiv_qr shows in a
-- profile).  Formatting and parsing numbers as text, the way Haskell does it.
module Main (main) where

import Data.Bits (shiftL, shiftR, xor, (.&.))
import Data.Char (ord)
import Data.List (foldl')
import Data.Word (Word32)
import Text.Printf (printf)

next :: Word32 -> Word32
next x0 = x3
  where
    x1 = x0 `xor` (x0 `shiftL` 13)
    x2 = x1 `xor` (x1 `shiftR` 17)
    x3 = x2 `xor` (x2 `shiftL` 5)

names :: [String]
names = ["alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta"]

render :: Int -> Word32 -> String
render i x =
  let n = fromIntegral (x `shiftR` 12) :: Int
      d = fromIntegral (x .&. 0xfffff) / 1024 - 512 :: Double
      e = fromIntegral (x `shiftR` 3) * 1.0e-7 :: Double
   in printf "%6d|%08x|%-8s|%10.3f|%.5e|%+d" i n (names !! (i .&. 7)) d e (n - 500000)

fields :: String -> [String]
fields s = case break (== '|') s of
  (f, []) -> [f]
  (f, _ : rest) -> f : fields rest

parse :: String -> Int
parse l = case fields l of
  [i, _, name, d, e, n] ->
    let di = round (read d * 1000 :: Double) :: Int
        ei = truncate (read e * 1.0e6 :: Double) :: Int
     in (read i + di + ei + read (dropWhile (== '+') n) + length name) .&. 0xfffffff
  _ -> -1

main :: IO ()
main = do
  let go :: Int -> Word32 -> Int -> Int -> (Int, Int)
      go i x !h !p
        | i > 2000 = (h, p)
        | otherwise =
            let x' = next x
                l = render i x'
                h' = foldl' (\a c -> (a * 31 + ord c) .&. 0xfffffff) h l
             in go (i + 1) x' h' ((p * 7 + parse l) .&. 0xfffffff)
  print (go 1 2463534242 0 0)
