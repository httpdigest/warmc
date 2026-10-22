{-# LANGUAGE BangPatterns #-}
-- An LRU cache written imperatively: a circular doubly-linked list of nodes
-- whose links are IORefs, an IOArray from key to node, and IORef counters,
-- driven by a skewed stream of random keys.
--
-- Every writeIORef is a store followed by GHC's generational write barrier: a
-- compare of the MutVar's header against the clean info pointer and, when it
-- is clean, a call into the RTS's C function dirty_MUT_VAR.  A writeArray on
-- the IOArray tests whether the nonmoving collector's barrier is on, then
-- stores, marks the array dirty and sets a card byte.  Each node read out of an
-- IORef is a case that may have to enter a thunk, so the code is cut into a
-- function per continuation.  Imperative pointer chasing, the Haskell way, with
-- the collector's bookkeeping on every write.
module Main (main) where

import Data.Array.IO (IOArray, newArray, readArray, writeArray)
import Data.Bits (shiftL, shiftR, xor, (.&.))
import Data.IORef
import Data.Word (Word32)
import System.IO (fixIO)

data Node = Node
  { key :: !Int,
    val :: !(IORef Int),
    prev :: !(IORef Node),
    next :: !(IORef Node)
  }

data Cache = Cache
  { sentinel :: !Node,
    table :: !(IOArray Int (Maybe Node)),
    size :: !(IORef Int),
    hits :: !(IORef Int),
    capacity :: !Int
  }

newCache :: Int -> Int -> IO Cache
newCache keys cap = do
  s <- fixIO (\s -> Node (-1) <$> newIORef 0 <*> newIORef s <*> newIORef s)
  t <- newArray (0, keys - 1) Nothing
  Cache s t <$> newIORef 0 <*> newIORef 0 <*> pure cap

unlink :: Node -> IO ()
unlink n = do
  p <- readIORef (prev n)
  q <- readIORef (next n)
  writeIORef (next p) q
  writeIORef (prev q) p

pushFront :: Node -> Node -> IO ()
pushFront s n = do
  f <- readIORef (next s)
  writeIORef (prev n) s
  writeIORef (next n) f
  writeIORef (prev f) n
  writeIORef (next s) n

access :: Cache -> Int -> Int -> IO ()
access c k i = do
  m <- readArray (table c) k
  case m of
    Just n -> do
      modifyIORef' (hits c) (+ 1)
      modifyIORef' (val n) (\v -> (v * 3 + i) .&. 0xffff)
      unlink n
      pushFront (sentinel c) n
    Nothing -> do
      sz <- readIORef (size c)
      if sz >= capacity c
        then do
          lru <- readIORef (prev (sentinel c))
          unlink lru
          writeArray (table c) (key lru) Nothing
        else writeIORef (size c) (sz + 1)
      n <- Node k <$> newIORef i <*> newIORef (sentinel c) <*> newIORef (sentinel c)
      pushFront (sentinel c) n
      writeArray (table c) k (Just n)

rand :: Word32 -> Word32
rand x0 = x3
  where
    x1 = x0 `xor` (x0 `shiftL` 13)
    x2 = x1 `xor` (x1 `shiftR` 17)
    x3 = x2 `xor` (x2 `shiftL` 5)

walk :: Cache -> IO Int
walk c = go (sentinel c) 0
  where
    go n !acc = do
      n' <- readIORef (next n)
      if key n' < 0
        then return acc
        else do
          v <- readIORef (val n')
          go n' ((acc * 31 + key n' + v) .&. 0xfffffff)

main :: IO ()
main = do
  let keys = 8192
  c <- newCache keys 1500
  let loop :: Int -> Word32 -> IO ()
      loop i x
        | i > 2500000 = return ()
        | otherwise = do
            let x' = rand x
                a = fromIntegral (x' .&. 0xffff) `mod` keys
                b = fromIntegral (x' `shiftR` 16) `mod` keys
            access c (min a b) i
            loop (i + 1) x'
  loop 1 2463534242
  h <- readIORef (hits c)
  w <- walk c
  print (h, w)
