{-# OPTIONS_GHC -Wno-x-partial -Wno-incomplete-uni-patterns #-}
-- Rational arithmetic with Data.Ratio: construction and normalisation with
-- negative denominators, the four operations, comparison, numerator and
-- denominator, harmonic numbers and Bernoulli numbers (exact sums whose
-- numerators and denominators grow past 64 bits), Egyptian fractions,
-- continued fractions and their convergents, toRational of Doubles (exact
-- binary expansions), fromRational back, approxRational, properFraction /
-- floor / ceiling / round / truncate of negative ratios, and a small exact
-- Gaussian elimination over rationals.
--
-- Every Ratio Integer operation is a gcd on Integers, so this is long
-- division and Euclid's algorithm on ghc-bignum's limbs, with the sign
-- handling of `reduce` on top.
module Main (main) where

import Data.List (foldl')
import Data.Ratio
import Numeric (showFFloat)

harmonic :: Integer -> Rational
harmonic n = sum [1 % k | k <- [1 .. n]]

-- Bernoulli numbers by the Akiyama-Tanigawa algorithm.
bernoulli :: Int -> Rational
bernoulli n = head (go n [1 % fromIntegral (k + 1) | k <- [0 .. n]])
  where
    go 0 as = as
    go m as = go (m - 1) (zipWith3 (\j a b -> fromIntegral j * (a - b)) [1 :: Int ..] as (tail as))

egyptian :: Rational -> [Integer]
egyptian r
  | r <= 0 = []
  | numerator r == 1 = [denominator r]
  | otherwise =
      let d = (denominator r + numerator r - 1) `div` numerator r
       in d : egyptian (r - 1 % d)

contFrac :: Rational -> [Integer]
contFrac r =
  let (q, f) = properFraction r :: (Integer, Rational)
      q' = if f < 0 then q - 1 else q
      f' = if f < 0 then f + 1 else f
   in q' : (if f' == 0 then [] else contFrac (1 / f'))

convergents :: [Integer] -> [Rational]
convergents as = zipWith (%) (drop 2 hs) (drop 2 ks)
  where
    hs = 0 : 1 : zipWith3 (\a h1 h2 -> a * h1 + h2) as (tail hs) hs
    ks = 1 : 0 : zipWith3 (\a k1 k2 -> a * k1 + k2) as (tail ks) ks

showR :: Rational -> String
showR r
  | denominator r == 1 = show (numerator r)
  | otherwise = show (numerator r) ++ "/" ++ show (denominator r)

-- Solve A x = b exactly.
solve :: [[Rational]] -> [Rational] -> [Rational]
solve a b = backSub (elim (zipWith (\row v -> row ++ [v]) a b))
  where
    elim [] = []
    elim rows =
      let (p : rest) = pivotFirst rows
          pv = head p
          p' = map (/ pv) p
          reduceRow r = zipWith (\x y -> x - head r * y) r p'
       in p' : elim (map (tail . reduceRow) rest)
    pivotFirst rows = let (zs, nz) = span ((== 0) . head) rows in case nz of
      (r : rs) -> r : zs ++ rs
      [] -> rows
    backSub [] = []
    backSub (r : rs) =
      let xs = backSub rs
          v = last r - sum (zipWith (*) (init (tail r)) xs)
       in v : xs

hilbert :: Int -> [[Rational]]
hilbert n = [[1 % fromIntegral (i + j - 1) | j <- [1 .. n]] | i <- [1 .. n]]

main :: IO ()
main = do
  putStrLn "-- construction and normalisation"
  print [3 % 6, (-3) % 6, 3 % (-6), (-3) % (-6), 0 % 5, 10 % 1 :: Rational]
  print (map numerator [6 % 4, (-6) % 4, 6 % (-4) :: Rational], map denominator [6 % 4, (-6) % 4, 6 % (-4) :: Rational])
  print (12 % 18 :: Ratio Int, (-1000000) % 999999 :: Ratio Int, fromIntegral (7 :: Int) :: Rational)
  putStrLn "-- arithmetic"
  let a = 3 % 4 :: Rational
      b = (-5) % 6
      c = 7 % 15
  print (a + b, a - b, a * b, a / b, negate a, abs b, signum b, recip c)
  print (a + b * c, (a + b) * c, a / b / c, a / (b / c), a ^ (10 :: Int), b ^^ (-3 :: Int))
  print (compare a b, a > c, max a c, minimum [a, b, c], sum [a, b, c], product [a, b, c])
  print (map showR (scanl1 (+) (map (1 %) [2, 3, 7, 43, 1807])))
  putStrLn "-- harmonic and Bernoulli numbers"
  mapM_ (\n -> putStrLn (show n ++ ": " ++ showR (harmonic n))) [1, 2, 3, 10, 20, 30]
  print (numerator (harmonic 100) `mod` 1000000007, denominator (harmonic 100) `mod` 1000000007, length (show (denominator (harmonic 100))))
  mapM_ (\n -> putStrLn ("B" ++ show n ++ " = " ++ showR (bernoulli n))) [0, 1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 30]
  print (map bernoulli [3, 5, 7])
  putStrLn "-- Egyptian and continued fractions"
  mapM_ (\r -> putStrLn (showR r ++ " = " ++ unwords (map (("1/" ++) . show) (egyptian r)))) [4 % 13, 5 % 121, 2 % 3, 99 % 100, 1 % 7]
  mapM_ (\r -> putStrLn (showR r ++ " -> " ++ show (contFrac r))) [415 % 93, (-415) % 93, 649 % 200, 13 % 8, 1 % 1000]
  print (map showR (convergents [1, 2, 2, 2, 2, 2, 2, 2]))
  print (map showR (take 8 (convergents (3 : 7 : 15 : 1 : 292 : 1 : 1 : 1 : 2 : 1 : []))))
  let e = 2 : concat [[1, 2 * k, 1] | k <- [1 .. 10]]
  print (map showR (take 10 (convergents e)))
  print (showFFloat (Just 15) (fromRational (last (convergents e)) :: Double) "")
  putStrLn "-- Double and Rational"
  mapM_ (\d -> putStrLn (show d ++ " -> " ++ showR (toRational d))) [0.5, 0.1, -0.75, 1.0e-3, 3.141592653589793, 1.0e20, 2.5e-10 :: Double]
  print (map (fromRational :: Rational -> Double) [1 % 3, 2 % 3, (-1) % 7, 10 ^ (30 :: Int) % 3, 1 % 10 ^ (30 :: Int)])
  print (map (fromRational :: Rational -> Float) [1 % 3, 22 % 7, 355 % 113])
  print (approxRational (pi :: Double) 0.01, approxRational (pi :: Double) 0.0001, approxRational (pi :: Double) 1.0e-7, approxRational (0.3333 :: Double) 0.001)
  print (toRational (1 / 0 :: Double) > 0, realToFrac (1 % 8 :: Rational) :: Double, realToFrac (0.1 :: Float) :: Double)
  putStrLn "-- rounding negative ratios"
  let rs = [7 % 2, (-7) % 2, 5 % 2, (-5) % 2, 1 % 3, (-1) % 3, 5 % 3, (-5) % 3, 0, 9 % 1] :: [Rational]
  mapM_ (\r -> putStrLn (showR r ++ ": " ++ show (properFraction r :: (Integer, Rational)) ++ " " ++ show (floor r :: Integer, ceiling r :: Integer, round r :: Integer, truncate r :: Integer))) rs
  putStrLn "-- exact linear algebra"
  let m = [[2, 1, -1], [-3, -1, 2], [-2, 1, 2]] :: [[Rational]]
  print (map showR (solve m [8, -11, -3]))
  print (map showR (solve [[0, 1], [1, 1]] [3, 5]))
  let h = hilbert 6
      x = solve h (map sum h)
  print (map showR x)
  let hx = solve h [1, 0, 0, 0, 0, 0]
  print (map showR hx)
  print (sum hx, foldl' (\acc r -> acc + r * r) 0 hx)
