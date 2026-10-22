{-# OPTIONS_GHC -Wno-x-partial #-}
-- Data.List end to end: sort/sortBy/sortOn (stability checked on records
-- with equal keys), insert, group/groupBy, nub/nubBy, partition, transpose
-- of ragged lists, foldl/foldl'/foldr/foldl1/foldr1, scanl/scanl1/scanr/
-- scanr1, unfoldr, iterate, zip3/zipWith3/unzip3, isPrefixOf/isSuffixOf/
-- isInfixOf/isSubsequenceOf, stripPrefix, intercalate/intersperse,
-- subsequences, permutations, inits/tails, span/break/dropWhileEnd/
-- splitAt, lookup/elemIndex/elemIndices/findIndex/find, union/intersect/
-- (\\)/delete, maximumBy/minimumBy, genericLength, mapAccumL/R, replicate,
-- and list comprehensions with multiple generators, guards and lets.
--
-- Lists are GHC's cons cells; with -O2 much of this fuses into loops and the
-- rest is recursion over heap-allocated cells, both of which stress
-- allocation, pointer tagging and the evaluation of thunks in the tails.
module Main (main) where

import Data.Function (on)
import Data.List
import Data.Ord (Down (..), comparing)

data Rec = Rec {key :: Int, tag :: Char} deriving (Show, Eq)

lcg :: Int -> Int
lcg s = (s * 75 + 74) `mod` 65537

randomList :: Int -> Int -> [Int]
randomList n seed = map (`mod` 100) (take n (tail (iterate lcg seed)))

chunksOf :: Int -> [a] -> [[a]]
chunksOf k = takeWhile (not . null) . unfoldr (Just . splitAt k)

splitOn :: Char -> String -> [String]
splitOn d s = case break (== d) s of
  (a, []) -> [a]
  (a, _ : rest) -> a : splitOn d rest

main :: IO ()
main = do
  let xs = randomList 30 7
      ys = randomList 12 99
  print xs
  print ys
  putStrLn "-- sorting"
  print (sort xs, sortBy (flip compare) xs, sortOn negate ys, sortOn Down ys)
  let recs = zipWith Rec (map (`mod` 4) xs) (cycle ['a' .. 'z'])
  print (sortOn key recs)
  print (sortBy (comparing key <> comparing (Down . tag)) recs)
  print (insert 50 (sort ys), insert 0 [], foldr insert [] ys == sort ys)
  print (sort ["banana", "Apple", "cherry", "apple", "Banana", ""], sort [(2, 'b'), (1, 'z'), (2, 'a'), (1, 'a')])
  putStrLn "-- grouping and dedup"
  print (group (sort xs))
  print (map (\g -> (head g, length g)) (group (sort (map (`mod` 10) xs))))
  print (groupBy ((==) `on` (`div` 10)) (sort xs), groupBy (\a b -> a < b) [1, 2, 3, 2, 5, 1, 0, 4, 9 :: Int])
  print (nub xs, nubBy (\a b -> a `mod` 7 == b `mod` 7) xs, partition even xs)
  print (transpose ["abc", "de", "fghi", "", "j"], transpose [[1, 2, 3], [4, 5, 6 :: Int]], transpose ([] :: [[Int]]))
  putStrLn "-- folds and scans"
  print (foldl (-) 100 ys, foldr (-) 100 ys, foldl' (flip (:)) [] ys, foldl1 max ys, foldr1 min ys)
  print (scanl (+) 0 ys, scanl1 max ys, scanr (+) 0 ys, scanr1 (-) ys)
  print (scanl (flip (:)) [] "abc", take 10 (scanl (*) 1 [1 :: Integer ..]))
  print (unfoldr (\n -> if n > 100 then Nothing else Just (n, n * 2)) 1, chunksOf 4 xs)
  print (take 10 (iterate (`div` 2) 1000), takeWhile (/= 1) (iterate (\n -> if even n then n `div` 2 else 3 * n + 1) 27) !! 50)
  print (mapAccumL (\acc v -> (acc + v, acc * v)) 0 ys, mapAccumR (\acc v -> (acc + v, acc * v)) 0 ys)
  print (and [], or [], all even [2, 4 :: Int], any odd [2, 4 :: Int], sum [] :: Int, product [] :: Int, concat [[1], [], [2, 3 :: Int]], concatMap show [1, 22, 333 :: Int])
  putStrLn "-- zips"
  print (zip3 xs ys "abcdefghijkl", zipWith3 (\a b c -> a * b + c) xs ys [1000 ..])
  print (unzip3 [(1 :: Int, 'a', True), (2, 'b', False)], unzip (zip ys (tail ys)), lookup 3 (zip [1 ..] "hello"))
  putStrLn "-- searching"
  print (isPrefixOf "has" "haskell", isSuffixOf "ell" "haskell", isInfixOf "ske" "haskell", isInfixOf "eks" "haskell", isSubsequenceOf "hkl" "haskell")
  print (stripPrefix "foo" "foobar", stripPrefix "x" "foobar", elemIndex 42 xs, elemIndices 1 [1, 2, 1, 3, 1 :: Int], findIndex (> 90) xs, find (> 90) xs, findIndices even ys)
  print (filter ((> 2) . length) (group (sort xs)), lookup 'c' [('a', 1), ('c', 3 :: Int)], elem 0 xs, notElem 0 xs)
  print (span even [2, 4, 6, 7, 8 :: Int], break (> 3) [1, 2, 3, 4, 5 :: Int], dropWhileEnd (== ' ') "trail   ", splitAt 3 "abcdef", splitAt (-1) "ab", splitAt 10 "ab")
  print (takeWhile (< 50) xs, dropWhile (< 50) xs, splitOn ',' "a,b,,c,", words (map (\c -> if c == ',' then ' ' else c) "x,y,,z"))
  putStrLn "-- combinatorics"
  print (subsequences "abcd")
  print (permutations [1, 2, 3 :: Int], length (permutations [1 .. 6 :: Int]), sort (permutations "aab"))
  print (inits "abcd", tails "abcd", zip (inits [1, 2, 3 :: Int]) (tails [1, 2, 3 :: Int]))
  print ([(a, b) | (a : rest) <- tails "abcd", b <- rest], length [() | s <- subsequences [1 .. 10 :: Int], sum s == 20])
  putStrLn "-- set-like operations"
  print (union [1, 2, 3, 2] [3, 4, 5, 4 :: Int], intersect [1, 2, 2, 3, 4] [2, 4, 6 :: Int], [1, 2, 3, 4, 3, 2, 1] \\ [2, 3 :: Int], delete 'b' "abcb")
  print (maximumBy (comparing snd) (zip [0 :: Int ..] xs), minimumBy (comparing abs) [-3, 2, -1, 4 :: Int], maximum "hello", minimum [[3], [1, 2 :: Int]])
  print (genericLength xs :: Integer, length xs, reverse ys, last xs, init ys, null [], replicate 3 'x', intersperse ',' "abc", intercalate ", " ["a", "b", "c"])
  print (lines (intercalate "\n" (map show ys)) == map show ys, zip "ab" (repeat 0 :: [Int]), lookup 5 ([] :: [(Int, Char)]), sortOn snd (zip "hello" [5, 4, 3, 2, 1 :: Int]))
  putStrLn "-- comprehensions"
  print [(i, j) | i <- [1 .. 4 :: Int], j <- "xy", odd i]
  print [x * y | x <- [1 .. 5 :: Int], let y = x + 1, even (x * y), x /= 3]
  print [[c | c <- w, c `elem` "aeiou"] | w <- words "list comprehensions are neat"]
  print [(a, b, c) | c <- [1 .. 30 :: Int], b <- [1 .. c], a <- [1 .. b], a * a + b * b == c * c, gcd a b == 1]
  print (take 20 [n | n <- [2 :: Int ..], all (\d -> n `mod` d /= 0) [2 .. n - 1]])
  let matrix = [[i * 4 + j | j <- [0 .. 3]] | i <- [0 .. 3 :: Int]]
  print (matrix, transpose matrix, map sum matrix, map sum (transpose matrix), [matrix !! i !! i | i <- [0 .. 3]])
  print [sum [a * b | (a, b) <- zip row col] | row <- matrix, col <- transpose matrix]
