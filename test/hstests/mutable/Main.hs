{-# LANGUAGE BangPatterns #-}
-- Mutable references without concurrency: IORef (read/write/modify,
-- modifyIORef' vs lazy modifyIORef building a thunk chain, atomicModifyIORef'
-- returning a result, references to references, a list of refs updated in
-- turn, a closure-captured counter), STRef in runST (an imperative Fibonacci,
-- a mutable linked list built from STRefs, union-find with path
-- compression, a memo table of STRefs), and MVar used as a lock and a
-- one-slot mailbox in a single thread (newMVar, takeMVar, putMVar, readMVar,
-- modifyMVar_, modifyMVar, tryTakeMVar, tryPutMVar, swapMVar, isEmptyMVar).
--
-- An IORef is a MutVar# on GHC's heap; every write goes through the
-- generational write barrier (dirty_MUT_VAR) and an MVar operation through
-- the RTS's blocking-queue code even when nothing ever blocks.
module Main (main) where

import Control.Concurrent.MVar
import Control.Monad (forM, forM_, replicateM, when)
import Control.Monad.ST (ST, runST)
import Data.IORef
import Data.STRef

fibST :: Int -> Integer
fibST n = runST $ do
  a <- newSTRef 0
  b <- newSTRef 1
  forM_ [1 .. n] $ \_ -> do
    x <- readSTRef a
    y <- readSTRef b
    writeSTRef a y
    writeSTRef b $! x + y
  readSTRef a

-- A singly linked list whose links are STRefs, reversed in place.
data Cell s = Cell Int (STRef s (Maybe (Cell s)))

buildList :: [Int] -> ST s (Maybe (Cell s))
buildList [] = return Nothing
buildList (x : xs) = do
  rest <- buildList xs
  ref <- newSTRef rest
  return (Just (Cell x ref))

toListST :: Maybe (Cell s) -> ST s [Int]
toListST Nothing = return []
toListST (Just (Cell v ref)) = do
  next <- readSTRef ref
  (v :) <$> toListST next

reverseInPlace :: Maybe (Cell s) -> ST s (Maybe (Cell s))
reverseInPlace = go Nothing
  where
    go prev Nothing = return prev
    go prev (Just c@(Cell _ ref)) = do
      next <- readSTRef ref
      writeSTRef ref prev
      go (Just c) next

linkedDemo :: [Int] -> ([Int], [Int])
linkedDemo xs = runST $ do
  l <- buildList xs
  before <- toListST l
  r <- reverseInPlace l
  after <- toListST r
  return (before, after)

-- Union-find with path compression over an array of STRefs.
unionFind :: Int -> [(Int, Int)] -> ([Int], Int)
unionFind n edges = runST $ do
  parents <- mapM newSTRef [0 .. n - 1]
  ranks <- mapM (const (newSTRef (0 :: Int))) [0 .. n - 1]
  let parentRef i = parents !! i
      find i = do
        p <- readSTRef (parentRef i)
        if p == i
          then return i
          else do
            r <- find p
            writeSTRef (parentRef i) r
            return r
      union a b = do
        ra <- find a
        rb <- find b
        when (ra /= rb) $ do
          ka <- readSTRef (ranks !! ra)
          kb <- readSTRef (ranks !! rb)
          if ka < kb
            then writeSTRef (parentRef ra) rb
            else do
              writeSTRef (parentRef rb) ra
              when (ka == kb) $ modifySTRef' (ranks !! ra) (+ 1)
  mapM_ (uncurry union) edges
  roots <- mapM find [0 .. n - 1]
  let distinct = length (filter id (zipWith (==) roots [0 ..]))
  return (roots, distinct)

collatzMemo :: Int -> [Int]
collatzMemo limit = runST $ do
  memo <- mapM (const (newSTRef Nothing)) [0 .. limit]
  let steps 1 = return 0
      steps k
        | k <= limit = do
            cached <- readSTRef (memo !! k)
            case cached of
              Just v -> return v
              Nothing -> do
                v <- (+ 1) <$> steps (next k)
                writeSTRef (memo !! k) (Just v)
                return v
        | otherwise = (+ 1) <$> steps (next k)
      next k = if even k then k `div` 2 else 3 * k + 1
  mapM steps [1 .. limit]

makeCounter :: IO (IO Int, IO ())
makeCounter = do
  r <- newIORef 0
  return (readIORef r, modifyIORef' r (+ 1))

main :: IO ()
main = do
  putStrLn "-- IORef"
  r <- newIORef (0 :: Int)
  forM_ [1 .. 10] $ \i -> do
    modifyIORef' r (+ i)
    v <- readIORef r
    putStrLn ("after +" ++ show i ++ ": " ++ show v)
  writeIORef r 100
  old <- atomicModifyIORef' r (\v -> (v * 2, v))
  new <- readIORef r
  print (old, new)
  results <- forM [1 .. 5 :: Int] $ \i -> atomicModifyIORef' r (\v -> (v - i, v `mod` (i + 1)))
  readIORef r >>= \v -> print (results, v)
  lazyR <- newIORef (0 :: Integer)
  forM_ [1 .. 1000] $ \i -> modifyIORef lazyR (+ i)
  readIORef lazyR >>= print
  atomicWriteIORef lazyR 7
  readIORef lazyR >>= print
  refs <- mapM newIORef "abcde"
  forM_ (zip refs [1 :: Int ..]) $ \(ref, k) -> modifyIORef ref (\c -> toEnum (fromEnum c + k))
  mapM readIORef refs >>= print
  outer <- newIORef =<< newIORef "inner"
  inner <- readIORef outer
  modifyIORef inner (++ "!")
  readIORef outer >>= readIORef >>= print
  other <- newIORef "replacement"
  writeIORef outer other
  readIORef outer >>= readIORef >>= print
  readIORef inner >>= print
  (getA, incA) <- makeCounter
  (getB, incB) <- makeCounter
  replicateM 5 incA >> replicateM 2 incB >> incA
  (,) <$> getA <*> getB >>= print
  stack <- newIORef ([] :: [Int])
  let push v = modifyIORef' stack (v :)
      pop = atomicModifyIORef' stack (\s -> case s of [] -> ([], Nothing); (x : xs) -> (xs, Just x))
  mapM_ push [1 .. 5]
  popped <- replicateM 7 pop
  print popped
  putStrLn "-- STRef"
  print (map fibST [0, 1, 2, 10, 50, 100])
  print (runST (do ref <- newSTRef [1 :: Int]; forM_ [2 .. 10] (\i -> modifySTRef ref (i :)); readSTRef ref))
  print (runST (do ref <- newSTRef (1 :: Int, 1 :: Int); forM_ [1 .. 20 :: Int] (\_ -> modifySTRef' ref (\(a, b) -> (b, (a + b) `mod` 1000))); readSTRef ref))
  print (linkedDemo [1 .. 10], linkedDemo [], linkedDemo [42])
  print (unionFind 12 [(0, 1), (2, 3), (1, 3), (5, 6), (7, 8), (8, 9), (9, 7), (11, 10), (4, 4)])
  print (unionFind 6 [(i, (i + 1) `mod` 6) | i <- [0 .. 5]])
  let cm = collatzMemo 60
  print cm
  print (maximum cm, length (filter (> 100) cm))
  putStrLn "-- MVar in one thread"
  lock <- newMVar (0 :: Int)
  forM_ [1 .. 5] $ \i -> modifyMVar_ lock (return . (+ i))
  readMVar lock >>= print
  got <- modifyMVar lock (\v -> return (v * 10, v + 1))
  readMVar lock >>= \v -> print (got, v)
  box <- newEmptyMVar :: IO (MVar String)
  isEmptyMVar box >>= print
  putMVar box "first"
  isEmptyMVar box >>= print
  ok <- tryPutMVar box "second"
  print ok
  takeMVar box >>= print
  t1 <- tryTakeMVar box
  print t1
  ok2 <- tryPutMVar box "third"
  previous <- swapMVar box "fourth"
  final <- takeMVar box
  print (ok2, previous, final)
  tr <- tryReadMVar box
  print tr
  queue <- newMVar ([] :: [Int])
  forM_ [1 .. 8] $ \i -> modifyMVar_ queue (return . (++ [i * i]))
  drained <- forM [1 .. 3 :: Int] $ \_ -> modifyMVar queue (\q -> return (drop 2 q, take 2 q))
  rest <- takeMVar queue
  print (drained, rest)
  counter <- newMVar (0 :: Int)
  let withCounter f = modifyMVar counter (\c -> let !c' = c + 1 in return (c', f c'))
  outs <- mapM (\k -> withCounter (* k)) [10, 20, 30]
  readMVar counter >>= \c -> print (outs, c)
