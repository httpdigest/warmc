-- Doubly recursive Fibonacci on Int, over a range of arguments.
--
-- The one kernel here with no allocation in its loop: GHC's strictness
-- analysis turns fib into a worker on unboxed Int#, so what reaches warmc is a
-- recursive function -- but GHC's wasm backend has no direct jumps between
-- STG functions: each one returns the next to the RTS's StgRun loop, which
-- enters it with call_indirect.  This row is the cost of that trampoline on
-- the simplest code it can carry.
module Main (main) where

fib :: Int -> Int
fib n = if n < 2 then n else fib (n - 1) + fib (n - 2)

main :: IO ()
main = print (sum (map fib [20 .. 30]))
