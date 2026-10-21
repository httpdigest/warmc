{-# LANGUAGE BangPatterns #-}
-- binary-trees from the Benchmarks Game: build complete trees of a few
-- depths, walk them, drop them, and keep one long-lived tree throughout.
--
-- The same depths as cbench/bintree, and the same algorithm as the bintree
-- rows of gobench, asbench and ktbench, so this row puts GHC's generational
-- copying collector next to malloc and free, a TinyGo collector, a TLSF
-- allocator and a wasm GC engine on one allocation pattern.  The tree is built
-- strictly, or laziness would interleave building and walking, and each one
-- carries its iteration's number: a tree that did not depend on the loop
-- would be floated out of it by full laziness and built once per depth.
module Main (main) where

import Data.Bits ((.&.))
import Data.List (foldl')

data Tree = Nil | Node !Int !Tree !Tree

make :: Int -> Int -> Tree
make i 0 = Node i Nil Nil
make i d = Node i (make i (d - 1)) (make i (d - 1))

check :: Tree -> Int
check Nil = 0
check (Node i l r) = 1 + (i .&. 1) + check l + check r

main :: IO ()
main = do
  let minD = 4
      maxD = 15
      !keep = make 0 maxD
      depth d = foldl' (\acc i -> acc + check (make i d)) 0 [1 .. (2 :: Int) ^ (maxD - d + minD)]
      total = sum (map depth [minD, minD + 2 .. maxD]) + check keep
  print total
