-- Green threads and MVars: a ring of threads made with forkIO passes a token
-- around many times, each thread adding to it before handing it on.
--
-- Concurrency without parallelism.  The wasm RTS is GHC's non-threaded one, so
-- every forkIO is a heap-allocated thread the scheduler switches to when the
-- current one blocks on an empty MVar, and each hop of the token is a takeMVar,
-- a putMVar, a block and a context switch inside the RTS's scheduler loop --
-- code no other corpus exercises.
module Main (main) where

import Control.Concurrent (forkIO)
import Control.Concurrent.MVar
import Control.Monad (forM, forM_, replicateM_)
import Data.Bits ((.&.))

main :: IO ()
main = do
  let threads = 64
      hops = 4000
  boxes <- forM [1 .. threads] (const newEmptyMVar)
  done <- newEmptyMVar
  let ring = zip3 [1 :: Int ..] boxes (tail boxes ++ [head boxes])
  forM_ ring $ \(i, inbox, outbox) -> forkIO $ do
    replicateM_ hops $ do
      t <- takeMVar inbox
      putMVar outbox ((t * 31 + i) .&. 0xfffffff)
    putMVar done ()
  putMVar (head boxes) (1 :: Int)
  replicateM_ threads (takeMVar done)
  final <- takeMVar (head boxes)
  print final
