{-# LANGUAGE ScopedTypeVariables #-}
-- Fixed-width integers: Int (32 bits on wasm32), Int8/16/32/64 and
-- Word/Word8/16/32/64.  Wraparound of +, -, * and negate at both ends of
-- every type; minBound/maxBound; abs and negate of minBound; div, mod, quot
-- and rem with every sign at every width; conversions between all of them
-- with fromIntegral (truncation, sign and zero extension); Enum ranges that
-- end at maxBound; exponentiation that overflows; and three hash/PRNG loops
-- (FNV-1a 32 and 64, a 64-bit LCG, xorshift32) whose every step is printed.
--
-- GHC narrows the sub-word types with explicit sign or zero extension after
-- each operation and keeps Int64/Word64 in i64 on a 32-bit target, so this is
-- where a wrong extend, a wrong i32/i64 width or an unsigned/signed mix-up
-- in warmc's lowering would show.
module Main (main) where

import Data.Int (Int16, Int32, Int64, Int8)
import Data.List (foldl')
import Data.Word (Word16, Word32, Word64, Word8)

edges :: forall a. (Show a, Integral a, Bounded a) => String -> a -> [String]
edges nm _ =
  [ nm ++ " bounds " ++ show (minBound :: a) ++ " " ++ show (maxBound :: a),
    nm ++ " max+1 " ++ show (maxBound + 1 :: a) ++ " min-1 " ++ show (minBound - 1 :: a),
    nm ++ " max*2 " ++ show (maxBound * 2 :: a) ++ " max*max " ++ show (maxBound * maxBound :: a),
    nm ++ " negate min " ++ show (negate minBound :: a) ++ " abs min " ++ show (abs minBound :: a),
    nm ++ " negate 1 " ++ show (negate 1 :: a) ++ " 0-max " ++ show (0 - maxBound :: a),
    nm ++ " fromInteger 300 " ++ show (fromInteger 300 :: a) ++ " fromInteger -300 " ++ show (fromInteger (-300) :: a),
    nm ++ " 7^19 " ++ show (7 ^ (19 :: Int) :: a) ++ " 3^40 " ++ show (3 ^ (40 :: Int) :: a),
    nm ++ " sum 1..300 " ++ show (sum (map fromIntegral [1 .. 300 :: Int]) :: a),
    nm ++ " product 1..20 " ++ show (product (map fromIntegral [1 .. 20 :: Int]) :: a),
    nm ++ " toInteger max " ++ show (toInteger (maxBound :: a)) ++ " even/odd max " ++ show (even (maxBound :: a), odd (minBound :: a))
  ]

divTable :: forall a. (Show a, Integral a) => String -> [a] -> [a] -> [String]
divTable nm as bs =
  [ nm ++ " " ++ show a ++ " " ++ show b ++ ": " ++ show (div a b, mod a b, quot a b, rem a b)
    | a <- as,
      b <- bs,
      b /= 0
  ]

fnv32 :: String -> [Word32]
fnv32 = scanl (\h c -> (h `xorW` fromIntegral (fromEnum c)) * 16777619) 2166136261
  where
    xorW a b = fromIntegral ((toInteger a + toInteger b) - 2 * toInteger (andW a b))
    andW :: Word32 -> Word32 -> Word32
    andW a b = sum [2 ^ i | i <- [0 .. 31 :: Int], odd (a `div` 2 ^ i), odd (b `div` 2 ^ i)]

fnv64 :: String -> Word64
fnv64 = foldl' (\h c -> (h `xor64` fromIntegral (fromEnum c)) * 1099511628211) 14695981039346656037
  where
    xor64 a b = (a + b) - 2 * andSmall a b
    andSmall a b = fromIntegral ((a `mod` 256) `andByte` (b `mod` 256))
    andByte :: Word64 -> Word64 -> Word64
    andByte x y = sum [2 ^ i | i <- [0 .. 7 :: Int], odd (x `div` 2 ^ i), odd (y `div` 2 ^ i)]

lcg :: Word64 -> Word64
lcg s = s * 6364136223846793005 + 1442695040888963407

xorshift32 :: Word32 -> Word32
xorshift32 x0 =
  let x1 = x0 `xorW` (x0 * 8192)
      x2 = x1 `xorW` (x1 `div` 131072)
   in x2 `xorW` (x2 * 32)
  where
    xorW :: Word32 -> Word32 -> Word32
    xorW a b = go a b 0 1 (32 :: Int)
    go _ _ acc _ 0 = acc
    go a b acc p n =
      let bitv = (a `mod` 2 + b `mod` 2) `mod` 2
       in go (a `div` 2) (b `div` 2) (acc + bitv * p) (p * 2) (n - 1)

main :: IO ()
main = do
  putStrLn "-- edges"
  mapM_ putStrLn (edges "Int" (0 :: Int))
  mapM_ putStrLn (edges "Int8" (0 :: Int8))
  mapM_ putStrLn (edges "Int16" (0 :: Int16))
  mapM_ putStrLn (edges "Int32" (0 :: Int32))
  mapM_ putStrLn (edges "Int64" (0 :: Int64))
  mapM_ putStrLn (edges "Word" (0 :: Word))
  mapM_ putStrLn (edges "Word8" (0 :: Word8))
  mapM_ putStrLn (edges "Word16" (0 :: Word16))
  mapM_ putStrLn (edges "Word32" (0 :: Word32))
  mapM_ putStrLn (edges "Word64" (0 :: Word64))
  putStrLn "-- division"
  mapM_ putStrLn (divTable "Int8" ([127, -128, 100, -100, 7, -7] :: [Int8]) [3, -3, 127, -128, 1])
  mapM_ putStrLn (divTable "Int32" ([maxBound, minBound + 1, 1000000007, -1000000007] :: [Int32]) [10, -10, 65536, -3])
  mapM_ putStrLn (divTable "Int64" ([maxBound, minBound + 1, 10 ^ (18 :: Int), -(10 ^ (18 :: Int))] :: [Int64]) [7, -7, 4294967296, -4294967311])
  mapM_ putStrLn (divTable "Word8" ([255, 200, 1] :: [Word8]) [2, 7, 255])
  mapM_ putStrLn (divTable "Word32" ([maxBound, 4000000000, 12345] :: [Word32]) [3, 65535, 2147483648])
  mapM_ putStrLn (divTable "Word64" ([maxBound, 18000000000000000000, 9223372036854775808] :: [Word64]) [3, 10, 4294967297, 9223372036854775807])
  mapM_ putStrLn (divTable "Int" ([maxBound, minBound + 1, 46341 * 46341, -17] :: [Int]) [2, -2, 46341, -46341, 5])
  putStrLn "-- conversions"
  let vals = [0, 1, -1, 127, 128, -129, 255, 256, 32767, -32768, 65535, 2147483647, -2147483648, 4294967295, 4294967296, 9223372036854775807, -9223372036854775808] :: [Integer]
  mapM_
    ( \v ->
        putStrLn
          ( show v ++ " -> "
              ++ unwords
                [ show (fromInteger v :: Int8),
                  show (fromInteger v :: Int16),
                  show (fromInteger v :: Int32),
                  show (fromInteger v :: Int64),
                  show (fromInteger v :: Int),
                  show (fromInteger v :: Word8),
                  show (fromInteger v :: Word16),
                  show (fromInteger v :: Word32),
                  show (fromInteger v :: Word64),
                  show (fromInteger v :: Word)
                ]
          )
    )
    vals
  let i8s = [minBound .. maxBound] :: [Int8]
  print (sum (map (fromIntegral :: Int8 -> Int) i8s), sum (map (fromIntegral :: Int8 -> Word8) i8s), sum (map (fromIntegral :: Int8 -> Word16) i8s))
  print (map (fromIntegral :: Int8 -> Word32) [-1, -128, 127], map (fromIntegral :: Word8 -> Int8) [255, 128, 127])
  print (map (fromIntegral :: Int32 -> Int64) [-1, minBound], map (fromIntegral :: Word32 -> Int64) [maxBound, 2147483648], map (fromIntegral :: Int64 -> Int32) [4294967297, -4294967297])
  print (map (fromIntegral :: Int -> Word) [-1, minBound], map (fromIntegral :: Word -> Int) [maxBound, 2147483648], map (fromIntegral :: Int64 -> Word64) [-1, minBound])
  print (map (fromIntegral :: Word64 -> Int) [2 ^ (32 :: Int) + 5, maxBound], map (fromIntegral :: Int16 -> Int8) [300, -300, 128], map (fromIntegral :: Word16 -> Int16) [65535, 32768])
  print (map (fromIntegral :: Int64 -> Double) [maxBound, minBound], map (truncate :: Double -> Int32) [3.9, -3.9, 2147483647.0], map (round :: Double -> Word8) [254.5, 253.5, 2.5])
  print (map (fromIntegral :: Word64 -> Double) [maxBound, 2 ^ (53 :: Int) + 1], map (floor :: Double -> Int64) [-0.5, 1.0e15 + 0.5], map (ceiling :: Float -> Int16) [1.25, -1.25])
  putStrLn "-- enum ranges at the edges"
  print ([250 ..] :: [Word8], [125 ..] :: [Int8], [maxBound - 2 ..] :: [Int64])
  print ([minBound, minBound + 50 ..] :: [Int8], [250, 252 ..] :: [Word8], [3, 1 ..] :: [Word8])
  print (length ([minBound .. maxBound] :: [Word16]), [maxBound - 3 .. maxBound] :: [Word32], [minBound .. minBound + 2] :: [Int32])
  print (map fromEnum [minBound, maxBound :: Int16], toEnum 65 :: Word8, fromEnum (maxBound :: Word16), fromEnum (minBound :: Int32))
  putStrLn "-- gcd, lcm, powers"
  print (gcd (minBound + 2) (6 :: Int8), lcm (100 :: Word8) 3, gcd (12 :: Word64) 18, lcm (2 ^ (31 :: Int) :: Int64) 3, gcd (-12 :: Int32) (-18))
  print ([2 ^ k :: Word16 | k <- [14 .. 17 :: Int]], [(-2) ^ k :: Int8 | k <- [5 .. 8 :: Int]], [10 ^ k :: Int32 | k <- [8 .. 11 :: Int]])
  print (foldl' (\a b -> a * 31 + b) 7 [1 .. 1000 :: Int32], foldl' (\a b -> a * 31 + b) 7 [1 .. 1000 :: Word16], foldl' (\a b -> a * 131 + b) 1 [1 .. 500 :: Int64])
  putStrLn "-- hashes and generators"
  print (fnv32 "hello, world")
  print (map fnv64 ["", "a", "foobar", "The quick brown fox jumps over the lazy dog"])
  print (take 12 (iterate lcg 42))
  print (take 16 (iterate xorshift32 2463534242))
  let w8 = take 20 (iterate (\x -> x * 37 + 11) (1 :: Word8))
      i16 = take 20 (iterate (\x -> x * 251 - 7919) (1 :: Int16))
  print (w8, i16)
  print (map (\w -> fromIntegral w :: Int8) w8, map (\v -> fromIntegral v :: Word16) i16)
