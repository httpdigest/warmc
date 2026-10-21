{-# LANGUAGE BangPatterns #-}
-- Control.Exception in IO: a custom exception thrown and caught, arithmetic
-- exceptions from pure code forced with evaluate, and bracket and finally
-- running their cleanups on the way out.
--
-- GHC's exceptions are not wasm's: a throw is the RTS walking the Haskell
-- stack for a catch frame, all of it on the stack GHC keeps in linear memory,
-- so this is ordinary code to warmc.  The row sits next to the exception
-- rows of cppbench and ktbench, which use wasm's throw and try_table.
module Main (main) where

import Control.Exception
import Data.IORef

data Bad = Bad !Int deriving (Show)

instance Exception Bad

work :: IORef Int -> Int -> IO Int
work cleanups i =
  (if i `mod` 5 == 0 then throwIO (Bad i) else evaluate (1000000 `div` (i `mod` 7)))
    `finally` modifyIORef' cleanups (+ 1)

main :: IO ()
main = do
  cleanups <- newIORef 0
  let loop :: Int -> Int -> Int -> IO (Int, Int)
      loop i !ok !bad
        | i > 300000 = return (ok, bad)
        | otherwise = do
            r <- try (bracket (return i) (\_ -> modifyIORef' cleanups (+ 2)) (work cleanups))
            case r of
              Right v -> loop (i + 1) (ok + v) bad
              Left e -> case fromException e of
                Just (Bad n) -> loop (i + 1) ok (bad + n)
                Nothing -> case fromException e of
                  Just DivideByZero -> loop (i + 1) ok (bad + 1)
                  _ -> throwIO e
  (ok, bad) <- loop 1 0 0
  c <- readIORef cleanups
  print (ok, bad, c)
