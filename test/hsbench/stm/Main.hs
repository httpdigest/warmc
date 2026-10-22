{-# LANGUAGE BangPatterns #-}
-- Software transactional memory in one thread: random transfers between
-- accounts held in TVars, each its own atomically block whose withdrawal
-- retries into an orElse fallback when the balance is short, and a periodic
-- audit that reads every account in a single transaction.
--
-- STM is the RTS's, not the program's: readTVar and writeTVar are calls from
-- GHC's primop code into the C functions stmReadTVar and stmWriteTVar, which
-- log each access in the transaction record; the atomically frame commits with
-- stmCommitTransaction, orElse's frame with stmCommitNestedTransaction, and a
-- retry runs stg_retryzh, which finds the enclosing orElse with
-- findRetryFrameHelper and aborts the nested record.  With one thread nothing
-- ever conflicts, so the row is the bookkeeping cost of a transaction.
module Main (main) where

import Control.Concurrent.STM
import Control.Monad (forM, when)
import Data.Array (Array, listArray, (!))
import Data.Bits (shiftL, shiftR, xor, (.&.))
import Data.Word (Word32)

next :: Word32 -> Word32
next x0 = x3
  where
    x1 = x0 `xor` (x0 `shiftL` 13)
    x2 = x1 `xor` (x1 `shiftR` 17)
    x3 = x2 `xor` (x2 `shiftL` 5)

transfer :: TVar Int -> TVar Int -> Int -> STM ()
transfer from to amt = do
  b <- readTVar from
  when (b < amt) retry
  writeTVar from (b - amt)
  modifyTVar' to (+ amt)

main :: IO ()
main = do
  let n = 256
  accts <- forM [1 .. n] (\i -> newTVarIO (500 + i))
  short <- newTVarIO (0 :: Int)
  moved <- newTVarIO (0 :: Int)
  let arr = listArray (0, n - 1) accts :: Array Int (TVar Int)
      audit = foldr (\t m -> (+) <$> readTVar t <*> m) (return 0) accts
      loop :: Int -> Word32 -> Int -> IO Int
      loop i x !chk
        | i > 700000 = return chk
        | otherwise = do
            let x' = next x
                a = fromIntegral (x' .&. 0xff)
                b = fromIntegral ((x' `shiftR` 8) .&. 0xff)
                amt = fromIntegral ((x' `shiftR` 16) `mod` 700)
            atomically $
              (transfer (arr ! a) (arr ! b) amt >> modifyTVar' moved (+ amt))
                `orElse` modifyTVar' short (+ 1)
            if i .&. 1023 == 0
              then do
                t <- atomically audit
                loop (i + 1) x' ((chk * 31 + t) .&. 0xfffffff)
              else loop (i + 1) x' chk
  chk <- loop 1 2463534242 0
  balances <- mapM readTVarIO accts
  s <- readTVarIO short
  m <- readTVarIO moved
  print (chk, s, m .&. 0xfffffff, foldr (\v acc -> (acc * 31 + v) .&. 0xfffffff) 0 balances)
