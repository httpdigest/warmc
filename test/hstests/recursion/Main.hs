{-# LANGUAGE BangPatterns #-}
{-# OPTIONS_GHC -Wno-x-partial #-}
-- Recursion and strictness: deep non-tail recursion (a naive sum and a
-- foldr over a million elements, a deep tree walk) that GHC handles by
-- growing its stack in chunks, mutual recursion (even/odd, Hofstadter's
-- female/male sequences, a mutually recursive descent over nested lists),
-- classic recursive functions (Ackermann, Takeuchi, McCarthy 91, Catalan
-- by recursion, the Towers of Hanoi move list, permutations by recursion),
-- continuation-passing style and an explicit trampoline, and strictness
-- control: bang patterns in accumulators and let bindings, seq and ($!),
-- foldl vs foldl' on large inputs, strict constructor fields, a strict pair
-- accumulator for mean and variance, and `until`.
--
-- GHC's stack lives in linear memory and grows by allocating new stack
-- chunks when a function's stack check fails; deep recursion therefore runs
-- through the RTS's stack-overflow handling many times, and the
-- non-tail-recursive calls are return frames GHC pushes and pops itself
-- rather than wasm calls.
module Main (main) where

import Data.List (foldl')

-- Deep non-tail recursion.
sumTo :: Int -> Integer
sumTo 0 = 0
sumTo n = toInteger n + sumTo (n - 1)

lengthR :: [a] -> Int
lengthR [] = 0
lengthR (_ : xs) = 1 + lengthR xs

mapR :: (a -> b) -> [a] -> [b]
mapR _ [] = []
mapR f (x : xs) = let !y = f x in y : mapR f xs

data Tree = Leaf | Node Tree !Int Tree

-- A degenerate (list-shaped) tree, so walking it recurses a long way.
spine :: Int -> Tree
spine 0 = Leaf
spine n = Node (spine (n - 1)) n Leaf

sumTree :: Tree -> Integer
sumTree Leaf = 0
sumTree (Node l v r) = sumTree l + toInteger v + sumTree r

depth :: Tree -> Int
depth Leaf = 0
depth (Node l _ r) = 1 + max (depth l) (depth r)

balanced :: Int -> Int -> Tree
balanced lo hi
  | lo > hi = Leaf
  | otherwise = let mid = (lo + hi) `div` 2 in Node (balanced lo (mid - 1)) mid (balanced (mid + 1) hi)

-- Mutual recursion.
isEven', isOdd' :: Int -> Bool
isEven' 0 = True
isEven' n = isOdd' (n - 1)
isOdd' 0 = False
isOdd' n = isEven' (n - 1)

hofF, hofM :: Int -> Int
hofF 0 = 1
hofF n = n - hofM (hofF (n - 1))
hofM 0 = 0
hofM n = n - hofF (hofM (n - 1))

data Nested = Atom Int | List [Nested]

sumNested :: Nested -> Int
sumNested (Atom n) = n
sumNested (List xs) = sumList xs

sumList :: [Nested] -> Int
sumList [] = 0
sumList (x : xs) = sumNested x + sumList xs

depthNested :: Nested -> Int
depthNested (Atom _) = 0
depthNested (List xs) = 1 + maximum (0 : map depthNested xs)

nestDeep :: Int -> Nested
nestDeep 0 = Atom 1
nestDeep n = List [Atom n, nestDeep (n - 1), Atom (-1)]

-- Classics.
ackermann :: Int -> Int -> Int
ackermann 0 n = n + 1
ackermann m 0 = ackermann (m - 1) 1
ackermann m n = ackermann (m - 1) (ackermann m (n - 1))

tak :: Int -> Int -> Int -> Int
tak x y z
  | y < x = tak (tak (x - 1) y z) (tak (y - 1) z x) (tak (z - 1) x y)
  | otherwise = z

mc91 :: Int -> Int
mc91 n
  | n > 100 = n - 10
  | otherwise = mc91 (mc91 (n + 11))

catalan :: Int -> Integer
catalan 0 = 1
catalan n = sum [catalan i * catalan (n - 1 - i) | i <- [0 .. n - 1]]

hanoi :: Int -> Char -> Char -> Char -> [(Char, Char)]
hanoi 0 _ _ _ = []
hanoi n from to via = hanoi (n - 1) from via to ++ [(from, to)] ++ hanoi (n - 1) via to from

perms :: [Int] -> [[Int]]
perms [] = [[]]
perms xs = [x : p | (x, rest) <- picks xs, p <- perms rest]
  where
    picks [] = []
    picks (y : ys) = (y, ys) : [(z, y : zs) | (z, zs) <- picks ys]

-- Continuation-passing style and a trampoline.
factCPS :: Integer -> (Integer -> r) -> r
factCPS 0 k = k 1
factCPS n k = factCPS (n - 1) (\v -> k (n * v))

fibCPS :: Int -> (Integer -> r) -> r
fibCPS n k
  | n < 2 = k (toInteger n)
  | otherwise = fibCPS (n - 1) (\a -> fibCPS (n - 2) (\b -> k (a + b)))

data Trampoline a = Done a | More (() -> Trampoline a)

runTrampoline :: Trampoline a -> a
runTrampoline (Done a) = a
runTrampoline (More k) = runTrampoline (k ())

countDown :: Int -> Int -> Trampoline Int
countDown !acc 0 = Done acc
countDown !acc n = More (\() -> countUp (acc + n) (n - 1))

countUp :: Int -> Int -> Trampoline Int
countUp !acc 0 = Done acc
countUp !acc n = More (\() -> countDown (acc * 3 `mod` 1000003) (n - 1))

-- Strictness.
data Stats = Stats !Int !Double !Double

meanVar :: [Double] -> (Double, Double)
meanVar xs =
  let Stats n s s2 = foldl' (\(Stats k a b) x -> Stats (k + 1) (a + x) (b + x * x)) (Stats 0 0 0) xs
      mean = s / fromIntegral n
   in (mean, s2 / fromIntegral n - mean * mean)

sumStrict :: [Int] -> Int
sumStrict = go 0
  where
    go !acc [] = acc
    go !acc (x : xs) = go (acc + x) xs

mean2 :: [Double] -> Double
mean2 = go 0 0
  where
    go :: Double -> Int -> [Double] -> Double
    go s n [] = s / fromIntegral n
    go s n (x : xs) = let s' = s + x; n' = n + 1 in s' `seq` n' `seq` go s' n' xs

collatzLen :: Int -> Int
collatzLen = go 1
  where
    go !len 1 = len
    go !len n = go (len + 1) (if even n then n `div` 2 else 3 * n + 1)

main :: IO ()
main = do
  putStrLn "-- deep recursion"
  print (map sumTo [0, 1, 10, 1000, 100000, 400000])
  print (lengthR [1 .. 500000 :: Int], foldr (+) 0 [1 .. 400000 :: Integer], length (mapR (* 2) [1 .. 300000 :: Int]))
  print (sum (mapR (`mod` 7) [1 .. 200000 :: Int]), foldr (\x acc -> x - acc) 0 [1 .. 100001 :: Int])
  let s = spine 200000
  print (sumTree s, depth s)
  let b = balanced 1 100000
  print (sumTree b, depth b)
  putStrLn "-- mutual recursion"
  print (map isEven' [0, 1, 10, 777, 300000], map isOdd' [0, 1, 299999])
  print (map hofF [0 .. 30])
  print (map hofM [0 .. 30])
  print (sumNested (nestDeep 10), depthNested (nestDeep 10), sumNested (nestDeep 50000), depthNested (nestDeep 50000))
  print (sumNested (List [Atom 1, List [Atom 2, List [Atom 3, List []]], Atom 4]))
  putStrLn "-- classics"
  print [ackermann m n | m <- [0 .. 3], n <- [0 .. 4]]
  print (ackermann 2 300, ackermann 3 7)
  print (tak 18 12 6, tak 20 14 8, [tak x 4 2 | x <- [1 .. 12]])
  print (map mc91 [0, 50, 99, 100, 101, 150, -5])
  print (map catalan [0 .. 11])
  print (hanoi 3 'a' 'c' 'b', length (hanoi 16 'a' 'c' 'b'), take 5 (drop 1000 (hanoi 12 'a' 'c' 'b')))
  print (perms [1, 2, 3], length (perms [1 .. 7]), take 3 (drop 2000 (perms [1 .. 7])))
  putStrLn "-- CPS and trampolines"
  print (factCPS 25 id, factCPS 20 show, fibCPS 22 id, fibCPS 20 (`mod` 1000))
  print (factCPS 3000 (length . show))
  print (runTrampoline (countDown 0 10), runTrampoline (countDown 0 300000), runTrampoline (countUp 1 299999))
  putStrLn "-- strictness"
  print (meanVar [1 .. 100000], meanVar [2, 4, 4, 4, 5, 5, 7, 9])
  print (sumStrict [1 .. 1000000], mean2 [1 .. 300000], foldl' (+) 0 [1 .. 1000000 :: Int], foldl (+) 0 [1 .. 100000 :: Int])
  print (foldl' (\(!a, !b) x -> (a + x, b * x `mod` 1000003)) (0, 1) [1 .. 100000 :: Int])
  let lazyPairs = foldl (\(a, b) x -> (a + x, b + 1)) (0, 0 :: Int) [1 .. 100000 :: Int]
  print (fst lazyPairs `seq` lazyPairs)
  print (maximum (map collatzLen [1 .. 10000]), until (> 1000000) (* 3) 1 :: Int, until ((< 1.0e-9) . abs . subtract 2 . (^ (2 :: Int))) (\x -> (x + 2 / x) / 2) (1 :: Double))
  let applyN :: Int -> (a -> a) -> a -> a
      applyN 0 _ x = x
      applyN n f x = applyN (n - 1) f $! f x
  print (applyN 300000 (\x -> (x * 7 + 3) `mod` 1000003) (1 :: Int), applyN 50 (* 2) (1 :: Integer))
  let xs = [1 .. 10 :: Int]
      ys = map (* 2) xs
  print (ys `seq` length ys, head ys `seq` sum ys, (const 1 $! (5 :: Int)) :: Int)
