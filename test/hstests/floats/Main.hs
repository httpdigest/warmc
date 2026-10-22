-- Double and Float: arithmetic, the elementary functions (sqrt, exp, log,
-- the trigonometric and hyperbolic families and their inverses, atan2, **,
-- logBase), rounding to integers (floor, ceiling, truncate and round, which
-- rounds half to even), show of floats (shortest round-trip digits, the
-- switch to exponent notation, negative zero, infinities, NaN), read of
-- floats in every syntax, Numeric's showFFloat/showEFloat/showGFloat/
-- showHex/floatToDigits, the RealFloat interface (decodeFloat, encodeFloat,
-- significand, exponent, isNaN, isInfinite, isDenormalized,
-- isNegativeZero, floatDigits), Float/Double conversion, and a few
-- numerical loops (Newton's method, Kahan summation, a series for pi).
--
-- GHC calls libm in the wasm module for the transcendental functions and
-- implements show/read in Haskell over Integer, so this checks warmc's
-- floating-point lowering (including min/max/NaN/negative-zero cases and
-- f32<->f64 conversions) through code that prints every bit it computes.
module Main (main) where

import Data.List (foldl')
import Numeric (floatToDigits, showEFloat, showFFloat, showGFloat, showHex)

kahan :: [Double] -> Double
kahan = fst . foldl' step (0, 0)
  where
    step (s, c) x =
      let y = x - c
          t = s + y
       in (t, (t - s) - y)

newtonSqrt :: Double -> [Double]
newtonSqrt a = take 8 (iterate (\x -> (x + a / x) / 2) a)

leibniz :: Int -> Double
leibniz n = 4 * sum [(-1) ^ k / fromIntegral (2 * k + 1) | k <- [0 .. n]]

describe :: (RealFloat a, Show a) => a -> String
describe v =
  unwords
    [ show v,
      show (decodeFloat v),
      show (significand v),
      show (exponent v),
      show (isNaN v, isInfinite v, isDenormalized v, isNegativeZero v, isIEEE v)
    ]

main :: IO ()
main = do
  putStrLn "-- arithmetic"
  let xs = [0.1, 0.2, 0.3, 1 / 3, 2 / 3, 1.0e-10, 123456.789, -2.5, 1.0e300, 5.0e-324] :: [Double]
  print xs
  print (0.1 + 0.2 :: Double, 0.1 + 0.2 == (0.3 :: Double), 1 - 0.9 :: Double, 3 * 1.1 :: Double)
  print (sum xs, product (take 6 xs), maximum xs, minimum xs)
  print (1.0e300 * 1.0e10 :: Double, -1.0e300 * 1.0e10 :: Double, 5.0e-324 / 2 :: Double, 1 / 0 :: Double, 0 / 0 :: Double)
  print (map (/ 7) [1, 10, 100, 1000 :: Double], map (* 1.0e-5) [3, 7 :: Double])
  print (kahan (replicate 1000 0.1), sum (replicate 1000 (0.1 :: Double)), kahan (map (1 /) [1 .. 1000]))
  print (0.1 + 0.2 :: Float, 1 / 3 :: Float, 16777216 + 1 :: Float, 3.4028235e38 * 2 :: Float, 1.0e-45 :: Float)
  putStrLn "-- elementary functions"
  let args = [0, 0.5, 1, 2, 10, -1.5] :: [Double]
  mapM_ (\x -> print (x, sqrt (abs x), exp x, log (abs x + 1), sin x, cos x, tan x)) args
  mapM_ (\x -> print (x, asin (x / 10), acos (x / 10), atan x, sinh x, cosh x, tanh x)) args
  print (map (\x -> (asinh x, acosh (abs x + 1), atanh (x / 11))) args)
  print [atan2 y x | (y, x) <- [(1, 1), (1, -1), (-1, -1), (-1, 1), (0, -1), (-0.0, -1), (0, 0), (1, 0) :: (Double, Double)]]
  print (2 ** 0.5 :: Double, 2 ** 10 :: Double, 10 ** (-3) :: Double, (-8) ** (1 / 3) :: Double, 0 ** 0 :: Double)
  print (logBase 2 1024 :: Double, logBase 10 0.001 :: Double, log 0 :: Double, sqrt (-1) :: Double, exp 710 :: Double)
  print (pi :: Double, pi :: Float, exp 1 :: Double, sqrt 2 :: Float, sin pi :: Double, cos (pi / 2) :: Float)
  print (map (\x -> sqrt x :: Float) [2, 3, 5, 1.0e-6], map (\x -> exp x :: Float) [1, -1, 20])
  print (newtonSqrt 2, newtonSqrt 1.0e6 !! 7)
  print (leibniz 1000, leibniz 1001, abs (leibniz 5000 - pi) < 1.0e-3)
  putStrLn "-- rounding"
  let rs = [2.5, 3.5, -2.5, -3.5, 2.4999, 2.5001, -0.5, 0.5, 1.5, -1.5, 1.0e10 + 0.5, -7.99] :: [Double]
  mapM_ (\r -> print (r, floor r :: Int, ceiling r :: Int, truncate r :: Integer, round r :: Integer, properFraction r :: (Int, Double))) rs
  print (map (round :: Float -> Int) [0.5, 1.5, 2.5, -0.5, -1.5], map (truncate :: Float -> Int) [1.0e9, -1.0e9])
  print (round (1.0e20 :: Double) :: Integer, floor (-1.0e20 :: Double) :: Integer, ceiling (123456789.1 :: Float) :: Integer)
  putStrLn "-- show"
  print [0.1, 1.0, 1.0e7, 1.0e-2, 0.1e-1, 9999999.0, 12345678.9, 1.0e22, 1.0e21, 5.0e-324, 1.7976931348623157e308 :: Double]
  print [0.1, 1.0e7, 9999999.0, 1.0e-2, 3.4028235e38, 1.0e-45, 1.17549435e-38 :: Float]
  print (-0.0 :: Double, negate 0 :: Double, 1 / (-0.0) :: Double, recip (-1 / 0) :: Double, show (0 / 0 :: Float))
  print (showsPrec 7 (-1.5 :: Double) "", Just (-2.0e-3 :: Double), [Left 1.0e-4, Right (-1.0e100)] :: [Either Double Double])
  mapM_ (\v -> putStrLn (showFFloat (Just 3) v "" ++ " " ++ showEFloat (Just 4) v "" ++ " " ++ showGFloat (Just 2) v "" ++ " " ++ showFFloat Nothing v "" ++ " " ++ showEFloat Nothing v "")) [0, 1, -1.5, 3.14159265, 1234567.891, 1.0e-5, 0.000123456, 9.9996, 1.0e25 :: Double]
  print (floatToDigits 10 (0.3 :: Double), floatToDigits 10 (1234.5 :: Double), floatToDigits 2 (0.75 :: Double), floatToDigits 16 (255.5 :: Double))
  print (showHex (truncate (1.0e10 :: Double) :: Integer) "", showFFloat (Just 0) (2.5 :: Double) "", showFFloat (Just 0) (3.5 :: Double) "", showFFloat (Just 1) (0.05 :: Double) "")
  putStrLn "-- read"
  print (map read ["1", "1.5", "-2.25", "1e3", "1.5E-3", "  7.0  ", "0.1", "123456789012345678901234567890", "1e400", "-1e-400", "Infinity", "-Infinity"] :: [Double])
  print (map read ["3.14159274", "1e38", "1e39", "0.000001"] :: [Float])
  print (read "[1.0,2.5e-2,-3]" :: [Double], read "(1.5,-0.25)" :: (Double, Float), isNaN (read "NaN" :: Double))
  print (map (\s -> read s == (read (show (read s :: Double)) :: Double)) ["0.1", "2.718281828459045", "5e-324", "1.7976931348623157e308"])
  putStrLn "-- RealFloat"
  mapM_ (putStrLn . describe) [1.0, 0.1, -3.75, 1.0e-310, 0, -0.0, 1 / 0, 1.0e308 :: Double]
  mapM_ (putStrLn . describe) [1.0, 0.1, -3.75, 1.0e-40, 3.0e38 :: Float]
  print (isNaN (0 / 0 :: Double), isNaN (sqrt (-2) :: Float), isInfinite (exp 1000 :: Double), floatDigits (1 :: Double), floatDigits (1 :: Float))
  print (floatRadix (1 :: Double), floatRange (1 :: Double), floatRange (1 :: Float), encodeFloat 1 10 :: Double, encodeFloat (-3) (-2) :: Float)
  print (scaleFloat 5 (1.5 :: Double), scaleFloat (-1080) (1 :: Double), significand (8 :: Double), atan2 (1 :: Float) 2)
  let nan = 0 / 0 :: Double
  print (nan == nan, nan /= nan, nan < 1, nan > 1, compare 1 (2 :: Double), max 1 nan, min nan 1, max (-0.0) (0 :: Double), min 0 (-0.0 :: Double))
  print (maximum [1, 5, -2, 3 :: Double], minimum [0.5, -0.0, 0 :: Double], abs (-0.0 :: Double), signum (-2.5 :: Double), signum (-0.0 :: Double))
  putStrLn "-- Float and Double conversion"
  let ds = [0.1, 1 / 3, 1.0e-40, 1.0e39, 16777217, -2.5, 3.4028235677973366e38] :: [Double]
  print (map realToFrac ds :: [Float])
  print (map (realToFrac . (realToFrac :: Double -> Float)) ds :: [Double])
  print (map (\f -> realToFrac f :: Double) [0.1, 1 / 3, 1.0e-45 :: Float])
  print (map fromIntegral [maxBound :: Int, minBound, 16777217] :: [Float], fromIntegral (2 ^ (60 :: Int) + 1 :: Integer) :: Double)
  print (sum (map fromIntegral [1 .. 100 :: Int]) / 100 :: Float, foldl' (+) 0 (replicate 10 (0.1 :: Float)))
