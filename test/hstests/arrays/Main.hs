-- The array package: immutable boxed Data.Array with listArray, array,
-- accumArray, (//), accum, bounds/indices/elems/assocs, ixmap, tuple and
-- Char indices with non-zero lower bounds; unboxed UArray of Int, Double,
-- Bool, Word8 and Char; STUArray and STArray inside runST (a sieve, an
-- in-place insertion sort, a Fenwick tree, a prefix-sum table, and a
-- knapsack DP); IOArray and IOUArray mutated in IO (a matrix product, a
-- histogram, Pascal's triangle row by row); freeze/thaw between them; and a
-- lazy boxed array whose cells refer to each other (edit distance).
--
-- An unboxed array is a MutableByteArray# in GHC's heap: reads and writes
-- become plain loads and stores at computed offsets in linear memory, with
-- bounds checks as ordinary compares, so this is warmc's address arithmetic
-- and memory access on GHC's own data layout.
module Main (main) where

import Control.Monad (forM_, when)
import Control.Monad.ST (ST, runST)
import Data.Array
import Data.Array.IO (IOArray, IOUArray, getElems, newArray, newListArray, readArray, writeArray)
import Data.Array.MArray (freeze, getBounds, thaw)
import Data.Array.ST (STArray, STUArray, runSTArray, runSTUArray)
import qualified Data.Array.ST as STA
import Data.Array.Unboxed (UArray)
import qualified Data.Array.Unboxed as U
import Data.Word (Word8)

sieve :: Int -> UArray Int Bool
sieve n = runSTUArray $ do
  arr <- STA.newArray (2, n) True
  forM_ [2 .. n] $ \i -> do
    isP <- STA.readArray arr i
    when (isP && i <= n `div` i) $ forM_ [i * i, i * i + i .. n] $ \j -> STA.writeArray arr j False
  return arr

insertionSort :: [Int] -> [Int]
insertionSort xs = U.elems (runSTUArray (do
  let n = length xs
  arr <- STA.newListArray (0, n - 1) xs
  forM_ [1 .. n - 1] $ \i -> do
    v <- STA.readArray arr i
    let shift j
          | j < 0 = return j
          | otherwise = do
              w <- STA.readArray arr j
              if w > v then STA.writeArray arr (j + 1) w >> shift (j - 1) else return j
    j <- shift (i - 1)
    STA.writeArray arr (j + 1) v
  return arr))

-- Fenwick tree: point updates and prefix sums.
fenwick :: Int -> [(Int, Int)] -> [Int] -> [Int]
fenwick n updates queries = runST $ do
  tree <- STA.newArray (1, n) 0 :: ST s (STUArray s Int Int)
  let lsb i = until (\p -> i `mod` (2 * p) /= 0) (* 2) 1
      update i d = when (i <= n) $ do
        v <- STA.readArray tree i
        STA.writeArray tree i (v + d)
        update (i + lsb i) d
      query i
        | i <= 0 = return 0
        | otherwise = do
            v <- STA.readArray tree i
            rest <- query (i - lsb i)
            return (v + rest)
  mapM_ (uncurry update) updates
  mapM query queries

knapsack :: Int -> [(Int, Int)] -> UArray Int Int
knapsack cap items = runSTUArray $ do
  dp <- STA.newArray (0, cap) 0
  forM_ items $ \(w, v) ->
    forM_ [cap, cap - 1 .. w] $ \c -> do
      without <- STA.readArray dp c
      with <- STA.readArray dp (c - w)
      when (with + v > without) $ STA.writeArray dp c (with + v)
  return dp

reverseBoxed :: [String] -> Array Int String
reverseBoxed ws = runSTArray $ do
  let n = length ws
  arr <- STA.newListArray (1, n) ws :: ST s (STArray s Int String)
  forM_ [1 .. n `div` 2] $ \i -> do
    a <- STA.readArray arr i
    b <- STA.readArray arr (n + 1 - i)
    STA.writeArray arr i b
    STA.writeArray arr (n + 1 - i) a
  return arr

editDistance :: String -> String -> Int
editDistance s t = table ! (m, n)
  where
    m = length s
    n = length t
    sa = listArray (1, m) s
    ta = listArray (1, n) t
    table = array ((0, 0), (m, n)) [((i, j), cell i j) | i <- [0 .. m], j <- [0 .. n]] :: Array (Int, Int) Int
    cell 0 j = j
    cell i 0 = i
    cell i j
      | sa ! i == ta ! j = table ! (i - 1, j - 1)
      | otherwise = 1 + minimum [table ! (i - 1, j), table ! (i, j - 1), table ! (i - 1, j - 1)]

main :: IO ()
main = do
  putStrLn "-- immutable boxed arrays"
  let a = listArray (0, 9) [i * i | i <- [0 .. 9]] :: Array Int Int
  print a
  print (bounds a, a ! 7, elems a, take 3 (assocs a), indices a == [0 .. 9])
  let a2 = a // [(0, 100), (5, -5), (9, 999)]
  print (a2, a ! 0, sum (elems a2))
  print (accum (+) a [(1, 10), (1, 10), (2, 1)], fmap (`div` 2) a, ixmap (0, 4) (\i -> 9 - 2 * i) a)
  let hist = accumArray (+) 0 ('a', 'z') [(c, 1 :: Int) | c <- "the quick brown fox jumps over the lazy dog and keeps running", c /= ' '] :: Array Char Int
  print [(c, k) | (c, k) <- assocs hist, k > 1]
  let grid = listArray ((1, 1), (3, 4)) [1 ..] :: Array (Int, Int) Int
  print grid
  print (grid ! (2, 3), bounds grid, [grid ! (r, 4) | r <- [1 .. 3]], [sum [grid ! (r, c) | c <- [1 .. 4]] | r <- [1 .. 3]])
  let transposed = ixmap ((1, 1), (4, 3)) (\(r, c) -> (c, r)) grid
  print (transposed, elems transposed)
  let neg = listArray (-3, 3) "abcdefg" :: Array Int Char
  print (neg ! (-3), neg ! 0, neg ! 3, rangeSize (bounds neg), inRange (bounds neg) 4, index (-3, 3) 1)
  let cube = accumArray (flip (:)) [] ((0, 0, 0), (1, 1, 1)) [((x `mod` 2, y `mod` 2, (x + y) `mod` 2), x * 10 + y) | x <- [0 .. 3], y <- [0 .. 3 :: Int]] :: Array (Int, Int, Int) [Int]
  print cube
  print (range ((0, 0), (2, 1)), range ('x', 'z'), rangeSize ((1, 1), (10, 20)), index ((1, 1), (10, 20)) (3, 5))
  putStrLn "-- unboxed arrays"
  let u = U.listArray (1, 8) [1.5, 2.5 ..] :: UArray Int Double
  print (u, u U.! 4, U.bounds u, sum (U.elems u))
  let ub = U.listArray (0, 15) (cycle [True, False, False]) :: UArray Int Bool
      uw = U.listArray (0, 9) [246 ..] :: UArray Int Word8
      uc = U.listArray (0, 4) "hello" :: UArray Int Char
  print (ub, uw, uc, uw U.// [(0, 1)], U.amap (* 3) uw)
  print (U.accumArray (+) 0 (0, 4) [(i `mod` 5, i) | i <- [1 .. 50]] :: UArray Int Int)
  let primes = [i | (i, True) <- U.assocs (sieve 200)]
  print primes
  print (length (filter id (U.elems (sieve 100000))))
  putStrLn "-- ST arrays"
  let rs = take 40 (iterate (\s -> (s * 1103 + 7) `mod` 997) 5)
  print (insertionSort rs)
  print (insertionSort [5, 4, 3, 2, 1], insertionSort [], insertionSort [7])
  print (fenwick 16 [(i, i * i) | i <- [1 .. 16]] [0, 1, 2, 5, 8, 15, 16])
  print (fenwick 10 [(3, 5), (7, -2), (3, 1), (10, 100)] [1 .. 10])
  let items = [(12, 4), (2, 2), (1, 1), (1, 2), (4, 10), (3, 7), (7, 13), (5, 8)]
  print (U.elems (knapsack 20 items))
  print (reverseBoxed (words "one two three four five six seven"))
  putStrLn "-- IO arrays"
  m1 <- newListArray ((0, 0), (2, 2)) [1 .. 9] :: IO (IOUArray (Int, Int) Int)
  m2 <- newListArray ((0, 0), (2, 2)) [9, 8 .. 1] :: IO (IOUArray (Int, Int) Int)
  prod <- newArray ((0, 0), (2, 2)) 0 :: IO (IOUArray (Int, Int) Int)
  forM_ [0 .. 2] $ \i -> forM_ [0 .. 2] $ \j -> forM_ [0 .. 2] $ \k -> do
    x <- readArray m1 (i, k)
    y <- readArray m2 (k, j)
    acc <- readArray prod (i, j)
    writeArray prod (i, j) (acc + x * y)
  getElems prod >>= print
  frozen <- freeze prod :: IO (UArray (Int, Int) Int)
  print (frozen, frozen U.! (1, 1))
  boxed <- newArray (0, 9) [] :: IO (IOArray Int [Int])
  forM_ [1 .. 40 :: Int] $ \v -> do
    let b = (v * v) `mod` 10
    old <- readArray boxed b
    writeArray boxed b (v : old)
  getElems boxed >>= mapM_ print
  row <- newArray (0, 15) 0 :: IO (IOArray Int Integer)
  writeArray row 0 1
  forM_ [1 .. 15] $ \n -> do
    forM_ [n, n - 1 .. 1] $ \k -> do
      x <- readArray row k
      y <- readArray row (k - 1)
      writeArray row k (x + y)
    when (n `mod` 3 == 0) $ getElems row >>= print . takeWhile (/= 0)
  th <- thaw a :: IO (IOArray Int Int)
  writeArray th 3 (-9)
  getBounds th >>= print
  getElems th >>= print
  print (a ! 3)
  putStrLn "-- lazy array DP"
  print [editDistance s t | (s, t) <- [("kitten", "sitting"), ("flaw", "lawn"), ("intention", "execution"), ("", "abc"), ("same", "same"), ("haskell", "pascal")]]
