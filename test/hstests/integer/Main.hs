{-# OPTIONS_GHC -Wno-x-partial #-}
-- Integer: GHC's arbitrary-precision numbers, small and large.  Factorials,
-- powers and binomials far past 64 bits; gcd/lcm; all four of div, mod,
-- quot and rem (and divMod/quotRem) over every sign combination, small and
-- big, checked against the identities that relate them; integer square and
-- k-th roots by Newton's method; modular exponentiation and inverses; base
-- conversion and digit sums; show/read round trips; conversion to and from
-- the fixed-width types (truncating); Bits on Integer, including negatives.
--
-- On wasm32 GHC's Integer is ghc-bignum's native backend: small values are
-- an immediate `IS` constructor and large ones a `ByteArray#` of 32-bit
-- limbs, with the carries, the 64-bit products and the long division all done
-- in wasm integer arithmetic -- the add-with-carry and 32x32->64 multiply
-- shapes warmc has to get exactly right.
module Main (main) where

import Data.Bits
import Data.Char (intToDigit)
import Data.Int (Int16, Int32, Int64, Int8)
import Data.List (foldl', unfoldr)
import Data.Word (Word16, Word32, Word64, Word8)

factorial :: Integer -> Integer
factorial n = product [1 .. n]

choose :: Integer -> Integer -> Integer
choose n k = factorial n `div` (factorial k * factorial (n - k))

isqrt :: Integer -> Integer
isqrt 0 = 0
isqrt n = go n
  where
    go x =
      let y = (x + n `div` x) `div` 2
       in if y >= x then x else go y

iroot :: Int -> Integer -> Integer
iroot k n = go n
  where
    k' = toInteger k
    go x =
      let y = ((k' - 1) * x + n `div` (x ^ (k - 1))) `div` k'
       in if y >= x then x else go y

powMod :: Integer -> Integer -> Integer -> Integer
powMod _ 0 m = 1 `mod` m
powMod b e m
  | even e = let h = powMod b (e `div` 2) m in h * h `mod` m
  | otherwise = b * powMod b (e - 1) m `mod` m

egcd :: Integer -> Integer -> (Integer, Integer, Integer)
egcd a 0 = (a, 1, 0)
egcd a b = let (g, x, y) = egcd b (a `mod` b) in (g, y, x - (a `div` b) * y)

modInv :: Integer -> Integer -> Maybe Integer
modInv a m = let (g, x, _) = egcd a m in if g == 1 then Just (x `mod` m) else Nothing

toBase :: Integer -> Integer -> String
toBase b 0 = "0"
toBase b n
  | n < 0 = '-' : toBase b (negate n)
  | otherwise = reverse (unfoldr step n)
  where
    step 0 = Nothing
    step v = Just (intToDigit (fromInteger (v `mod` b)), v `div` b)

digitSum :: Integer -> Integer
digitSum = sum . map (read . pure) . show . abs

fibPair :: Int -> (Integer, Integer)
fibPair 0 = (0, 1)
fibPair n =
  let (a, b) = fibPair (n `div` 2)
      c = a * (2 * b - a)
      d = a * a + b * b
   in if even n then (c, d) else (d, c + d)

millerRabin :: Integer -> Bool
millerRabin n
  | n < 2 = False
  | n < 4 = True
  | even n = False
  | otherwise = all witness (takeWhile (< n) [2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37])
  where
    (s, d) = until (odd . snd) (\(a, b) -> (a + 1, b `div` 2)) (0 :: Int, n - 1)
    witness a =
      let x = powMod a d n
       in x == 1 || x == n - 1 || any (== n - 1) (take (s - 1) (tail (iterate (\y -> y * y `mod` n) x)))

main :: IO ()
main = do
  putStrLn "-- factorials and powers"
  mapM_ (\n -> putStrLn (show n ++ "! = " ++ show (factorial n))) [0, 1, 5, 20, 21, 25, 30, 52]
  print (factorial 100)
  print (length (show (factorial 1000)), digitSum (factorial 1000), factorial 1000 `mod` (10 ^ 9 + 7))
  print (2 ^ (64 :: Int), 2 ^ (64 :: Int) - 1, 2 ^ (127 :: Int) - 1, (-3) ^ (41 :: Int))
  print (3 ^ (200 :: Int))
  print [choose 50 k | k <- [0, 1, 2, 10, 25]]
  print (choose 100 50, choose 200 100 `mod` 1000000007)
  let (f300, _) = fibPair 300
  print (f300, fst (fibPair 1000) `mod` 1000000000000)
  putStrLn "-- division with every sign"
  let pairs = [(a, b) | a <- [17, -17, 3, -3, 0, 100, -100], b <- [5, -5, 3, -3, 1, -1, 7]]
  mapM_ (\(a, b) -> putStrLn (unwords [show a, show b, show (a `div` b), show (a `mod` b), show (a `quot` b), show (a `rem` b), show (divMod a b), show (quotRem a b)])) (take 21 pairs)
  print (all (\(a, b) -> (a `div` b) * b + a `mod` b == a && (a `quot` b) * b + a `rem` b == a) pairs)
  let big = factorial 40 + 12345
      bigs = [big, negate big, big * big + 1, negate (2 ^ (100 :: Int) + 7)]
      divisors = [7, -7, 1000000007, -(2 ^ (65 :: Int)), factorial 22 + 1, -(3 ^ (50 :: Int))]
  mapM_ (\a -> mapM_ (\b -> print (a `divMod` b, a `quotRem` b)) divisors) bigs
  print (and [let (q, r) = divMod a b in q * b + r == a && abs r < abs b && (r == 0 || signum r == signum b) | a <- bigs, b <- divisors])
  print (and [let (q, r) = quotRem a b in q * b + r == a && abs r < abs b && (r == 0 || signum r == signum a) | a <- bigs, b <- divisors])
  putStrLn "-- gcd and lcm"
  print (gcd 12 18, gcd (-12) 18, gcd 0 5, gcd 0 0, lcm 4 6, lcm (-4) 6, lcm 0 3 :: Integer)
  print (gcd (factorial 30) (2 ^ (40 :: Int)), gcd (fst (fibPair 120)) (fst (fibPair 90)), fst (fibPair 30))
  print (lcm (factorial 15) (3 ^ (20 :: Int)), foldl' lcm 1 [1 .. 50 :: Integer])
  print (egcd 240 46, egcd (2 ^ (61 :: Int) - 1) 1000003)
  print (map (`modInv` 97) [1, 2, 3, 50, 96], modInv 6 9, modInv (2 ^ (64 :: Int) + 1) (10 ^ (18 :: Int) + 9))
  putStrLn "-- roots and modular arithmetic"
  print (map isqrt [0, 1, 2, 3, 4, 15, 16, 17, 99, 100, 10 ^ (20 :: Int), 10 ^ (21 :: Int), 2 ^ (128 :: Int)])
  print (isqrt (factorial 50), isqrt (factorial 50) ^ (2 :: Int) <= factorial 50, (isqrt (factorial 50) + 1) ^ (2 :: Int) > factorial 50)
  print (iroot 3 (10 ^ (30 :: Int) + 5), iroot 5 (3 ^ (100 :: Int)), iroot 7 (factorial 60))
  print (powMod 2 1000 1000000007, powMod 3 (10 ^ (18 :: Int)) (2 ^ (61 :: Int) - 1), powMod 12345 67890 1)
  print (filter millerRabin [1 .. 60], filter millerRabin [2 ^ (61 :: Int) - 1, 2 ^ (64 :: Int) + 13, 1000000007 * 998244353, 2 ^ (89 :: Int) - 1])
  putStrLn "-- base conversion, show and read"
  print (map (toBase 2) [0, 5, 255, -6], toBase 16 (2 ^ (100 :: Int) - 1), toBase 7 (factorial 25))
  let rt = [read (show v) == v | v <- bigs ++ divisors]
  print (and rt, read "-000123456789012345678901234567890" :: Integer, read "  42  " :: Integer)
  print (show (negate (factorial 27)), showsPrec 7 (-5 :: Integer) "", [10 ^ (k :: Int) - 1 :: Integer | k <- [15 .. 22]])
  putStrLn "-- conversions"
  let huge = 2 ^ (70 :: Int) + 2 ^ (40 :: Int) + 2 ^ (20 :: Int) + 255 :: Integer
  print (fromInteger huge :: Int, fromInteger huge :: Int64, fromInteger huge :: Word32, fromInteger huge :: Word8)
  print (fromInteger (negate huge) :: Int32, fromInteger (negate huge) :: Word64, fromInteger (negate huge) :: Int16, fromInteger (negate huge) :: Int8)
  print (toInteger (maxBound :: Int64) + 1, toInteger (minBound :: Int64) - 1, toInteger (maxBound :: Word64) * 2, fromIntegral (maxBound :: Word16) + (1 :: Integer))
  print (fromIntegral (toInteger (maxBound :: Int)) + (0 :: Integer), fromInteger (2 ^ (31 :: Int)) :: Int, fromInteger (2 ^ (63 :: Int)) :: Int64)
  print (fromIntegral huge :: Double, fromInteger (factorial 30) :: Double, truncate (1.0e30 :: Double) :: Integer, round (-2.5e20 :: Double) :: Integer)
  print (floor (12345.678 :: Double) :: Integer, ceiling (-0.5 :: Double) :: Integer, toRational (factorial 20) == fromInteger (factorial 20))
  putStrLn "-- bits on Integer"
  print (huge .&. 0xffff, huge .|. 1, xor huge (2 ^ (70 :: Int)), shiftR huge 20, shiftL 1 100 :: Integer)
  print (popCount huge, popCount (-1 :: Integer), popCount (negate huge), testBit huge 70, testBit huge 69)
  print (complement 0 :: Integer, complement huge, (-1) `shiftR` 5 :: Integer, (-1024) `shiftR` 3 :: Integer, (-7) .&. 0xff :: Integer)
  print (shiftR (negate huge) 30, negate huge .&. (2 ^ (72 :: Int) - 1), (negate huge) .|. 0xf, xor (negate huge) (-1))
  print (setBit 0 200 :: Integer, clearBit huge 70, bit 65 :: Integer)
  print (map signum [huge, negate huge, 0], abs (negate huge), negate 0 :: Integer)
  print (compare huge (huge + 1), maximum bigs == maximum (reverse bigs), minimum bigs, sum bigs, product (take 3 bigs) `mod` 1000000007)
