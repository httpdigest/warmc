{-# LANGUAGE BangPatterns #-}
-- The RWS monad from mtl: a robot walks a torus with obstacles, reading the
-- world from the Reader, counting moves, bumps and turns in the Writer, and
-- keeping its position, heading and random seed in the State, with `local`
-- running some legs in a denser world.
--
-- GHC specialises the loop to this monad and passes the world's four fields
-- unboxed, but the robot and the writer's Stats travel as boxed values rebuilt
-- every step, and the step is cut into several functions, one per case
-- continuation, that return to the RTS's StgRun loop between them rather than
-- running as one wasm loop.  Next to state, which is a single State monad, the
-- row is a three-layer monad and how much of it GHC removed.
module Main (main) where

import Control.Monad.RWS.CPS (RWS, asks, get, local, put, runRWS, tell)
import Data.Bits (shiftL, shiftR, xor, (.&.))
import Data.Word (Word32)

data World = World {width :: !Int, height :: !Int, density :: !Int, salt :: !Int}

data Stats = Stats !Int !Int !Int !Int

instance Semigroup Stats where
  Stats a b c d <> Stats a' b' c' d' = Stats (a + a') (b + b') (c + c') ((d + d') .&. 0xfffffff)

instance Monoid Stats where
  mempty = Stats 0 0 0 0

data Robot = Robot {px :: !Int, py :: !Int, heading :: !Int, seed :: !Word32}

rand :: Word32 -> Word32
rand x0 = x3
  where
    x1 = x0 `xor` (x0 `shiftL` 13)
    x2 = x1 `xor` (x1 `shiftR` 17)
    x3 = x2 `xor` (x2 `shiftL` 5)

blocked :: Int -> Int -> RWS World Stats Robot Bool
blocked x y = do
  d <- asks density
  s <- asks salt
  let h = (x * 73856093) `xor` (y * 19349663) `xor` s
  return ((h `shiftR` 4) .&. 15 < d)

step :: RWS World Stats Robot ()
step = do
  w <- asks width
  h <- asks height
  r <- get
  let s' = rand (seed r)
      turn = fromIntegral (s' .&. 7)
      hd = if turn == 0 then (heading r + 1) .&. 3 else if turn == 1 then (heading r + 3) .&. 3 else heading r
      (dx, dy) = case hd of
        0 -> (1, 0)
        1 -> (0, 1)
        2 -> (-1, 0)
        _ -> (0, -1)
      nx = (px r + dx) `mod` w
      ny = (py r + dy) `mod` h
  b <- blocked nx ny
  if b
    then do
      put r {heading = (hd + 2) .&. 3, seed = s'}
      tell (Stats 0 1 (if hd /= heading r then 1 else 0) 0)
    else do
      put r {px = nx, py = ny, heading = hd, seed = s'}
      tell (Stats 1 0 (if hd /= heading r then 1 else 0) (nx * 31 + ny))

leg :: Int -> RWS World Stats Robot ()
leg 0 = return ()
leg k = step >> leg (k - 1)

journey :: Int -> RWS World Stats Robot ()
journey 0 = return ()
journey k = do
  if k .&. 3 == 0
    then local (\w -> w {density = density w + 3, salt = salt w + k}) (leg 500)
    else leg 500
  journey (k - 1)

main :: IO ()
main = do
  let world = World 257 129 4 12345
      ((), r, Stats moves bumps turns trail) = runRWS (journey 9600) world (Robot 0 0 0 2463534242)
  print (moves, bumps, turns, trail, px r, py r)
