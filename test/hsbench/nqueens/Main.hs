-- N-queens by list comprehension: every partial board is a list, every
-- candidate square is tested against the queens already placed with `and`
-- over a zip, and the solutions for each board size are counted.
--
-- Haskell as it is written when nobody is optimising it: lazy lists built and
-- consumed at once, closures for the comprehension's generators, and the
-- allocation of all of it falling to GHC's nursery.  Most of the time is in
-- the code GHC generates for list traversal and in its heap checks, not in
-- arithmetic.
module Main (main) where

queens :: Int -> [[Int]]
queens n = go n
  where
    go 0 = [[]]
    go k = [q : qs | qs <- go (k - 1), q <- [1 .. n], safe q qs]
    safe q qs = and [q /= c && abs (q - c) /= d | (d, c) <- zip [1 ..] qs]

main :: IO ()
main = print (map (length . queens) [4 .. 10])
