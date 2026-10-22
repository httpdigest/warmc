{-# LANGUAGE BangPatterns #-}
{-# OPTIONS_GHC -Wno-x-partial #-}
-- The garbage collector and the heap under load: live data that survives
-- many minor and major collections (a 100k-entry Map rebuilt and pruned in
-- rounds, a long list retained while garbage is churned past it), explicit
-- performMinorGC/performMajorGC between phases, large objects (unboxed
-- arrays of several megabytes, which make the heap grow with memory.grow), a
-- generational pointer from an old IORef/IOArray to freshly allocated
-- values, weak pointers to live keys, StableName identity, StablePtr round
-- trips, and a compact region.  Every phase prints checksums of what
-- survived, so an object moved incorrectly or a missed root shows as a wrong
-- number rather than a crash.
--
-- GHC's copying collector runs inside the module: it scans the Haskell
-- stack and heap in linear memory, follows info pointers to layout tables
-- and copies objects between blocks, and the block allocator asks the host
-- for more pages as the heap grows.  To warmc that is the hottest, most
-- pointer-intensive code in any Haskell program.
module Main (main) where

import Control.Monad (forM, forM_, when)
import Data.Array.IO (IOArray, IOUArray, getElems, newArray, readArray, writeArray)
import Data.Array.Unboxed (UArray, bounds, listArray, (!))
import Data.Bits (shiftL, shiftR, xor, (.&.))
import Data.IORef
import Data.List (foldl')
import qualified Data.Map.Strict as M
import Foreign.StablePtr (deRefStablePtr, freeStablePtr, newStablePtr)
import GHC.Compact (compact, getCompact)
import System.Mem (performGC, performMajorGC, performMinorGC)
import System.Mem.StableName (eqStableName, hashStableName, makeStableName)
import System.Mem.Weak (deRefWeak, mkWeakPtr)

data Tree = Leaf | Node Tree !Int Tree

build :: Int -> Int -> Tree
build lo hi
  | lo > hi = Leaf
  | otherwise = let m = (lo + hi) `div` 2 in Node (build lo (m - 1)) m (build (m + 1) hi)

sumT :: Tree -> Int
sumT Leaf = 0
sumT (Node l v r) = sumT l + v + sumT r

sizeT :: Tree -> Int
sizeT Leaf = 0
sizeT (Node l _ r) = sizeT l + 1 + sizeT r

xorshift :: Int -> Int
xorshift x0 =
  let x1 = x0 `xor` ((x0 `shiftL` 13) .&. 0x7fffffff)
      x2 = x1 `xor` (x1 `shiftR` 17)
   in (x2 `xor` ((x2 `shiftL` 5) .&. 0x7fffffff)) .&. 0x7fffffff

churn :: Int -> Int
churn n = foldl' (\acc k -> acc + length (show k) + sum (map fromEnum (show (k * k)))) 0 [1 .. n]

main :: IO ()
main = do
  putStrLn "-- a map rebuilt and pruned across collections"
  let keys = take 50000 (iterate xorshift 12345)
      m0 = M.fromList (zip keys [1 :: Int ..])
  print (M.size m0, M.foldl' (+) 0 m0 `mod` 1000003, fst <$> M.lookupMin m0, fst <$> M.lookupMax m0)
  mref <- newIORef m0
  forM_ [1 .. 6 :: Int] $ \r -> do
    modifyIORef' mref (\m -> M.filterWithKey (\k _ -> (k `div` 7 + r) `mod` 5 /= 0) m)
    modifyIORef' mref (\m -> foldl' (\acc k -> M.insertWith (+) (k `mod` 25000) r acc) m (take 10000 (iterate xorshift (r * 99991))))
    when (even r) performMajorGC
    when (odd r) performMinorGC
    m <- readIORef mref
    putStrLn ("round " ++ show r ++ ": size " ++ show (M.size m) ++ " sum " ++ show (M.foldl' (+) 0 m `mod` 1000003) ++ " keysum " ++ show (M.foldlWithKey' (\a k _ -> (a + k) `mod` 1000003) 0 m))
  putStrLn "-- retained structures while garbage is churned"
  let t = build 1 100000
      longList = [x * 3 `mod` 1009 | x <- [1 .. 150000 :: Int]]
  print (sumT t, sizeT t, sum longList)
  forM_ [1 .. 4 :: Int] $ \i -> do
    let g = churn (8000 * i)
    performGC
    print (i, g, sumT t, length longList, longList !! (i * 35000))
  putStrLn "-- large objects"
  let big = listArray (0, 599999) [xorshift k .&. 0xffff | k <- [1 .. 600000]] :: UArray Int Int
  print (bounds big, big ! 0, big ! 599999, foldl' (\a i -> (a + big ! i) `mod` 1000003) 0 [0, 997 .. 599999])
  arrs <- forM [1 .. 6 :: Int] $ \k -> do
    a <- newArray (0, 200000) k :: IO (IOUArray Int Int)
    forM_ [0, 1000 .. 200000] $ \i -> writeArray a i (i * k)
    return a
  performMajorGC
  sums <- forM arrs $ \a -> do
    xs <- getElems a
    return (foldl' (+) 0 xs `mod` 1000003)
  print sums
  putStrLn "-- old objects pointing at young ones"
  boxes <- newArray (0, 999) [] :: IO (IOArray Int [Int])
  performMajorGC
  forM_ [1 .. 20 :: Int] $ \gen -> do
    forM_ [0, 37 .. 999] $ \i -> do
      old <- readArray boxes i
      writeArray boxes i (take 5 (gen * i : old))
    when (gen `mod` 5 == 0) performMinorGC
  lists <- getElems boxes
  print (sum (map sum lists), length (filter (not . null) lists), lists !! 37, lists !! 999)
  counter <- newIORef (0 :: Int, [] :: [String])
  forM_ [1 .. 3000 :: Int] $ \i -> do
    modifyIORef' counter (\(n, s) -> let !n' = n + i in (n', take 3 (show n' : s)))
    when (i `mod` 1000 == 0) performMinorGC
  readIORef counter >>= print
  putStrLn "-- weak pointers, stable names, stable pointers, compact regions"
  let key = [1 .. 1000 :: Int]
  w <- mkWeakPtr key Nothing
  performMajorGC
  alive <- deRefWeak w
  print (fmap sum alive, sum key)
  let shared = M.fromList [(i, show i) | i <- [1 .. 100 :: Int]]
      other = M.insert 0 "zero" shared
  sn1 <- makeStableName shared
  performMajorGC
  sn2 <- makeStableName shared
  sn3 <- makeStableName other
  print (sn1 == sn2, eqStableName sn1 sn3, hashStableName sn1 == hashStableName sn2, M.size shared + M.size other)
  sp <- newStablePtr (M.fromList [(k, k * k) | k <- [1 .. 500 :: Int]])
  performMajorGC
  restored <- deRefStablePtr sp
  print (M.size restored, M.lookup 321 restored, M.foldl' (+) 0 restored)
  freeStablePtr sp
  region <- compact (M.fromList [(k, replicate (k `mod` 7) (toEnum (97 + k `mod` 26) :: Char)) | k <- [1 .. 2000 :: Int]])
  performMajorGC
  let cm = getCompact region
  print (M.size cm, M.lookup 1234 cm, sum (map length (M.elems cm)))
  putStrLn "-- final checks after everything"
  performMajorGC
  m <- readIORef mref
  print (M.size m, sumT t, sum longList, big ! 500000, M.size cm)
