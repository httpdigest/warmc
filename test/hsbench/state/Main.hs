{-# LANGUAGE BangPatterns #-}
-- A stack-machine interpreter in the strict State monad from mtl, running a
-- small program in a loop many times.
--
-- Monad transformer code is where GHC's optimiser earns its keep: with the
-- State instance inlined the machine is a loop over a record passed in
-- registers, and when it is not, every step is a closure and a call through
-- the Monad dictionary.  The row is how much of that GHC removed, and how
-- well what remains runs.
module Main (main) where

import Control.Monad.State.Strict (State, execState, get, modify', put)
import Data.Bits ((.&.))

data Op = Push !Int | Add | Mul | Dup | Swap | Drop | JumpNZ !Int | Dec

data Machine = Machine {pc :: !Int, stack :: [Int], steps :: !Int}

program :: [Op]
program = [Push 1, Push 300, Swap, Dup, Push 3, Mul, Push 7, Add, Swap, Drop, Swap, Dec, Dup, JumpNZ 2, Drop]

opAt :: Int -> Op
opAt i = program !! i

step :: State Machine Bool
step = do
  m <- get
  if pc m >= length program
    then return False
    else do
      let !i = pc m
          s = stack m
          s' = case (opAt i, s) of
            (Push n, _) -> n : s
            (Add, a : b : r) -> (a + b) .&. 0xffffff : r
            (Mul, a : b : r) -> (a * b) .&. 0xffffff : r
            (Dup, a : r) -> a : a : r
            (Swap, a : b : r) -> b : a : r
            (Drop, _ : r) -> r
            (Dec, a : r) -> a - 1 : r
            (JumpNZ _, _ : r) -> r
            (_, _) -> s
          pc' = case (opAt i, s) of
            (JumpNZ t, a : _) | a /= 0 -> t
            _ -> i + 1
      put m {pc = pc', stack = s', steps = steps m + 1}
      return True

runMachine :: State Machine ()
runMachine = do
  continue <- step
  if continue then runMachine else return ()

main :: IO ()
main = do
  let one r = execState (modify' id >> runMachine) (Machine 0 [r] 0)
      results = map one [1 .. 400]
  print (sum (map steps results), sum (map (sum . stack) results) .&. 0xfffffff)
