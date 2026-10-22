{-# LANGUAGE BangPatterns #-}
{-# OPTIONS_GHC -Wno-x-partial #-}
-- Laziness as the program's control flow: infinite lists (primes, Fibonacci,
-- Hamming numbers, a self-referential running sum), knot-tying (a doubly
-- linked ring and a "repmin" tree rewritten in one pass), where-bound sharing
-- observed through a counter that only advances when a thunk is forced,
-- irrefutable and lazy patterns, `fix`, and lazy accumulation that would
-- diverge if anything were evaluated early.
--
-- Every value printed here is a thunk until `print` forces it.  To warmc that
-- is GHC's update frames, blackholing and indirections on a heap and a stack
-- in linear memory, and closures entered through `call_indirect`: a wrong
-- store or a wrong tag test shows as a wrong number or a list that stops
-- early.
module Main (main) where

import Data.Function (fix)
import Data.IORef
import Data.List (foldl')
import System.IO.Unsafe (unsafePerformIO)

primes :: [Int]
primes = sieve [2 ..] where sieve (p : xs) = p : sieve [x | x <- xs, x `mod` p /= 0]

fibs :: [Integer]
fibs = 0 : 1 : zipWith (+) fibs (tail fibs)

hamming :: [Integer]
hamming = 1 : merge3 (map (* 2) hamming) (map (* 3) hamming) (map (* 5) hamming)
  where
    merge3 a b c = merge a (merge b c)
    merge xa@(x : xs) ya@(y : ys)
      | x < y = x : merge xs ya
      | x > y = y : merge xa ys
      | otherwise = x : merge xs ys
    merge xs [] = xs
    merge [] ys = ys

-- running sums of the naturals defined in terms of themselves
triangles :: [Int]
triangles = scanl1 (+) [1 ..]

selfSums :: [Int]
selfSums = 1 : zipWith (+) selfSums [2 ..]

collatz :: Int -> [Int]
collatz = takeWhile (/= 1) . iterate step
  where
    step n
      | even n = n `div` 2
      | otherwise = 3 * n + 1

-- A ring of nodes, each pointing to its neighbours, built in one lazy pass.
data Node = Node {label :: Int, prevN :: Node, nextN :: Node}

ring :: [Int] -> Node
ring [] = error "empty ring"
ring (x : xs) = first
  where
    (first, lastN) = build lastN (x : xs) first
    build :: Node -> [Int] -> Node -> (Node, Node)
    build prev [y] nxt = let n = Node y prev nxt in (n, n)
    build prev (y : ys) nxt =
      let n = Node y prev rest
          (rest, l) = build n ys nxt
       in (n, l)
    build _ [] _ = error "unreachable"

walk :: (Node -> Node) -> Int -> Node -> [Int]
walk step k n = map label (take k (iterate step n))

-- repmin: replace every leaf by the tree's minimum in a single traversal.
data Tree = Leaf Int | Fork Tree Tree

instance Show Tree where
  showsPrec _ (Leaf n) = shows n
  showsPrec _ (Fork l r) = showChar '(' . shows l . showChar ' ' . shows r . showChar ')'

repmin :: Tree -> Tree
repmin t = t'
  where
    (t', m) = go t
    go (Leaf n) = (Leaf m, n)
    go (Fork l r) =
      let (l', ml) = go l
          (r', mr) = go r
       in (Fork l' r', min ml mr)

mkTree :: Int -> Int -> Tree
mkTree 0 s = Leaf ((s * 37 + 11) `mod` 101)
mkTree d s = Fork (mkTree (d - 1) (s * 2 + 1)) (mkTree (d - 1) (s * 3 + 2))

-- A counter bumped by a thunk when it is evaluated, so sharing is observable.
counter :: IORef Int
counter = unsafePerformIO (newIORef 0)
{-# NOINLINE counter #-}

tick :: Int -> Int
tick x = unsafePerformIO (modifyIORef' counter (+ 1) >> return (x * x))
{-# NOINLINE tick #-}

sharedUse :: Int -> Int
sharedUse n = y + y + y
  where
    y = tick n

lazyPair :: (Int, Int) -> Int
lazyPair ~(a, b) = if length [a, b] > 5 then a + b else 42

lazyFst :: Int
lazyFst = fst (7, undefined :: Int)

-- Lists that are only partially defined.
partial :: [Int]
partial = [1, 2, 3, undefined, 5]

-- `fix` used for both a recursive function and a recursive value.
factFix :: Integer -> Integer
factFix = fix (\rec n -> if n <= 1 then 1 else n * rec (n - 1))

onesFix :: [Int]
onesFix = fix (1 :)

-- A lazily built table of answers that refer to earlier answers.
memoCatalan :: [Integer]
memoCatalan = map cat [0 ..]
  where
    cat 0 = 1
    cat n = sum [memoCatalan !! i * memoCatalan !! (n - 1 - i) | i <- [0 .. n - 1]]

-- An infinite structure of infinite lists.
pascal :: [[Integer]]
pascal = iterate (\row -> zipWith (+) (0 : row) (row ++ [0])) [1]

-- Lazy evaluation of `&&`, `||`, and `takeWhile` on a list that diverges after the prefix.
shortCircuit :: [Bool]
shortCircuit =
  [ False && undefined,
    True || undefined,
    any even [1, 3, 5, 6, undefined],
    all odd [1, 3, 4, undefined],
    elem 10 [1 ..],
    null (undefined : undefined)
  ]

main :: IO ()
main = do
  putStrLn "-- infinite lists"
  print (take 30 primes)
  print (primes !! 300)
  print (takeWhile (< 2000) (dropWhile (< 1900) primes))
  print (take 25 fibs)
  print (fibs !! 200)
  print (take 40 hamming)
  print (hamming !! 1500)
  print (take 15 triangles, take 15 selfSums, take 15 triangles == take 15 selfSums)
  mapM_ (\n -> putStrLn (show n ++ ": " ++ show (length (collatz n)) ++ " " ++ show (maximum (n : collatz n)))) [7, 27, 97, 871]
  print (take 10 (cycle [3, 1, 4]))
  print (take 8 (repeat 'z'), replicate 3 "ab")
  print (zip [1 :: Int ..] "lazy")
  print (take 5 [(a, b, c) | c <- [1 :: Int ..], b <- [1 .. c], a <- [1 .. b], a * a + b * b == c * c])
  putStrLn "-- knot tying"
  let r = ring [10, 20 .. 100]
  print (walk nextN 23 r)
  print (walk prevN 23 r)
  print (walk (nextN . nextN . prevN) 12 r)
  print (label (prevN (nextN (nextN (prevN r)))))
  let t = mkTree 4 1
  print t
  print (repmin t)
  let t2 = Fork (Fork (Leaf 9) (Leaf 4)) (Fork (Leaf 12) (Fork (Leaf 7) (Leaf 5)))
  print (t2, repmin t2)
  putStrLn "-- sharing"
  writeIORef counter 0
  print (sharedUse 9)
  readIORef counter >>= \c -> putStrLn ("ticks after shared use: " ++ show c)
  let xs = map tick [1 .. 10]
  print (xs !! 3)
  readIORef counter >>= \c -> putStrLn ("ticks after one element: " ++ show c)
  print (sum xs)
  readIORef counter >>= \c -> putStrLn ("ticks after sum: " ++ show c)
  print (sum xs + length xs)
  readIORef counter >>= \c -> putStrLn ("ticks after second sum: " ++ show c)
  let unused = tick 1000
  print (length [unused, unused, unused])
  readIORef counter >>= \c -> putStrLn ("ticks after length: " ++ show c)
  putStrLn "-- lazy patterns and partial values"
  print (lazyPair undefined, lazyFst)
  print (take 3 partial, length partial, partial !! 4)
  print shortCircuit
  let (evens, odds) = foldr (\x ~(es, os) -> if even x then (x : es, os) else (es, x : os)) ([], []) [1 :: Int ..]
  print (take 6 evens, take 6 odds)
  print (factFix 25, take 4 onesFix)
  print (take 12 memoCatalan)
  mapM_ print (take 9 pascal)
  print (pascal !! 30 !! 15)
  putStrLn "-- lazy accumulation"
  let big = foldr (\x acc -> x : take 2 acc) [] [1 :: Int ..]
  print big
  let lazySum = foldl (+) 0 [1 .. 1000 :: Int]
      strictSum = foldl' (+) 0 [1 .. 100000 :: Int]
  print (lazySum, strictSum)
  let !forced = length (show (product [1 .. 50 :: Integer]))
  print forced
  let ws = words (concatMap (\i -> show i ++ " ") [1 :: Int ..])
  print (take 10 ws, ws !! 999)
  let digits = concatMap show [1 :: Int ..]
  print (take 40 digits, digits !! 10000)
  let table = [(i, j, i * j) | i <- [1 :: Int ..], j <- [1 .. i]]
  print (take 12 table)
  print (lookup 7569 [(x * x, x) | x <- [1 :: Int ..]])
