-- The containers package: Data.Map (strict and lazy APIs), Data.Set,
-- Data.IntMap, Data.IntSet and Data.Sequence.  Construction from lists with
-- duplicate keys (fromList vs fromListWith), insert/insertWith/adjust/alter/
-- update/delete, lookups (lookup, findWithDefault, member, lookupLT/GE,
-- lookupMin/Max), unions/intersections/differences with combining
-- functions, filter/partition/split/splitLookup, map/mapWithKey/
-- mapMaybe/mapKeysWith, folds in both directions with keys, toList/
-- toDescList, elemAt/findIndex/take/drop, alterF, Set algebra and powerset
-- size, IntMap with negative keys, and a Seq used as a deque.  Every
-- intermediate structure is printed.
--
-- The containers are size-balanced trees (Patricia tries for IntMap) of
-- small heap objects with strict fields, rebalanced by rotations on every
-- insert: allocation-heavy, branchy code over pointer-tagged constructors.
module Main (main) where

import Data.Char (toUpper)
import qualified Data.IntMap.Strict as IM
import qualified Data.IntSet as IS
import qualified Data.Map as ML
import qualified Data.Map.Strict as M
import Data.Sequence (Seq, ViewL (..), ViewR (..), (<|), (><), (|>))
import qualified Data.Sequence as Seq
import qualified Data.Set as S

wordsText :: String
wordsText = "the cat sat on the mat the dog sat on the log a cat and a dog met on a mat"

main :: IO ()
main = do
  putStrLn "-- Map construction and update"
  let ws = words wordsText
      freq = M.fromListWith (+) [(w, 1 :: Int) | w <- ws]
  print freq
  print (M.fromList (zip ws [1 :: Int ..]))
  print (M.size freq, M.lookup "cat" freq, M.lookup "cow" freq, M.findWithDefault 0 "cow" freq, M.member "dog" freq, M.notMember "dog" freq)
  let m1 = M.insert "cow" 7 (M.insertWith (+) "cat" 10 (M.adjust (* 100) "the" freq))
  print m1
  let m2 = M.alter (fmap (+ 1)) "mat" (M.alter (const (Just 0)) "zebra" (M.alter (const Nothing) "on" m1))
  print m2
  print (M.update (\v -> if v > 5 then Nothing else Just (v * 2)) "cow" m2, M.update (\v -> Just (v - 1)) "a" m2 M.! "a")
  print (M.delete "the" m2, M.delete "absent" m2 == m2)
  print (M.insertLookupWithKey (\_ n o -> n + o) "dog" 5 m2)
  print (M.updateLookupWithKey (\_ v -> if even v then Just (v `div` 2) else Nothing) "sat" m2)
  print (fst <$> M.lookupMin m2, fst <$> M.lookupMax m2, M.lookupLT "dog" m2, M.lookupGE "dot" m2, M.lookupGT "zebra" m2, M.lookupLE "a" m2)
  putStrLn "-- Map combination"
  let a = M.fromList [(k, k * k) | k <- [1 .. 10 :: Int]]
      b = M.fromList [(k, negate k) | k <- [5, 7 .. 15 :: Int]]
  print (M.union a b)
  print (M.unionWith (+) a b, M.unionsWith (+) [a, b, M.singleton 1 1000])
  print (M.intersection a b, M.intersectionWith (,) a b, M.difference a b, b M.\\ a)
  print (M.differenceWith (\x y -> if x > 40 then Just (x + y) else Nothing) a b)
  print (M.filter even a, M.filterWithKey (\k v -> odd k && v > 10) a, M.partition (> 30) a)
  print (M.split 5 a, M.splitLookup 7 a, M.splitLookup 0 a)
  print (M.map (* 2) b, M.mapWithKey (\k v -> show k ++ ":" ++ show v) b, M.mapMaybe (\v -> if v > 20 then Just (v - 20) else Nothing) a)
  print (M.mapKeys (* 2) b, M.mapKeysWith (+) (`div` 3) a, M.mapKeysMonotonic (+ 100) b)
  print (M.foldr (:) [] a, M.foldl (flip (:)) [] a, M.foldrWithKey (\k v acc -> k + v + acc) 0 a, M.foldlWithKey (\acc k v -> acc ++ [(k, v)]) [] b)
  print (M.foldr' (+) 0 a, sum a, maximum a, length b, elem 49 a, M.keys b, M.elems b, M.toDescList b)
  print (M.elemAt 2 a, M.findIndex 4 a, M.lookupIndex 11 a, M.take 3 a, M.drop 8 a, M.splitAt 2 b)
  print (M.minViewWithKey b, M.maxView b, M.deleteMin b, M.deleteMax b, M.findMin a, M.findMax a)
  print (M.alterF (\v -> [Nothing, fmap succ v]) 3 (M.fromList [(3, 'a'), (4, 'b')] :: M.Map Int Char), M.alterF (\v -> [v, Just 0]) 2 (M.fromList [(1, 1 :: Int)]))
  print (M.fromAscList [(1, 'a'), (2, 'b'), (2, 'c')], M.fromDistinctAscList [(i, i) | i <- [1 .. 5 :: Int]], M.toAscList (M.fromList [(3 :: Int, ()), (1, ())]))
  print (M.isSubmapOf (M.fromList [(1, 1)]) a, M.isSubmapOf (M.fromList [(1, 2)]) a, M.keysSet b, M.restrictKeys a (S.fromList [2, 4, 99]), M.withoutKeys a (S.fromList [1 .. 8]))
  print (fmap (map toUpper) (M.fromList [(1 :: Int, "x"), (2, "yz")]), traverse (\v -> if v > 0 then Just v else Nothing) a, sequenceA (M.fromList [(1 :: Int, [1, 2 :: Int]), (2, [3])]))
  let lazyMap = ML.fromList [(1 :: Int, 10 :: Int), (2, error "never forced"), (3, 30)]
  print (ML.size lazyMap, ML.lookup 1 lazyMap, ML.lookup 3 lazyMap, ML.keys lazyMap)
  let grouped = M.fromListWith (flip (++)) [(length w, [w]) | w <- ws]
  print grouped
  putStrLn "-- Set"
  let s1 = S.fromList "mississippi river"
      s2 = S.fromList "pennsylvania"
  print (s1, s2, S.size s1, S.toList s1, S.elems s2)
  print (S.union s1 s2, S.intersection s1 s2, S.difference s1 s2, s2 S.\\ s1)
  print (S.member 's' s1, S.notMember 'z' s1, S.insert 'z' s1, S.delete 'i' s1, S.map succ s2, S.filter (> 'm') s1)
  print (S.partition (< 'n') s2, S.split 'n' s2, S.splitMember 'p' s2, S.lookupMin s1, S.lookupMax s2, S.findMin s2)
  print (S.isSubsetOf (S.fromList "sip") s1, S.isSubsetOf s2 s1, S.disjoint (S.fromList "xyz") s1, S.elemAt 3 s1, S.findIndex 'r' s1)
  print (S.toDescList s2, S.foldr (:) [] s2, S.lookupLT 'n' s2, S.lookupGE 'w' s2, S.deleteMin s2, S.maxView s2)
  let nums = S.fromList [x * x `mod` 101 | x <- [1 .. 200 :: Int]]
  print (S.size nums, S.take 10 nums, S.drop 45 nums, sum (S.toList nums))
  print (S.size (S.powerSet (S.fromList [1 .. 10 :: Int])), S.toList (S.map S.toList (S.powerSet (S.fromList "abc"))))
  print (S.cartesianProduct (S.fromList [1, 2 :: Int]) (S.fromList "xy"), S.unions [S.fromList [1, 2], S.fromList [2, 3], S.singleton (9 :: Int)])
  putStrLn "-- IntMap and IntSet"
  let im = IM.fromListWith (++) [(k `mod` 7 - 3, [k]) | k <- [1 .. 20 :: Int]]
  print im
  print (IM.lookup (-3) im, IM.findWithDefault [] 10 im, IM.size im, IM.keys im, IM.member 0 im)
  print (IM.insertWith (++) 0 [0] im IM.! 0, IM.adjust reverse 1 im IM.! 1, IM.alter (const (Just [])) 99 im IM.! 99)
  let im2 = IM.fromList [(k, k * 3) | k <- [-5, -3 .. 15]]
      im3 = IM.fromList [(k, k) | k <- [0 .. 10]]
  print (IM.unionWith (+) im2 im3, IM.intersectionWith (-) im2 im3, IM.difference im2 im3)
  print (IM.foldrWithKey (\k v acc -> (k, v) : acc) [] im2, IM.foldlWithKey (\acc k v -> acc + k * v) 0 im2)
  print (IM.split 3 im2, IM.lookupLT 0 im2, IM.lookupGE 4 im2, IM.lookupMin im2, IM.lookupMax im2, IM.toDescList im3)
  print (IM.mapWithKey (+) im3, IM.filter odd im2, IM.partitionWithKey (\k _ -> k < 0) im2, IM.minViewWithKey im2)
  let big = IM.fromList [(k * 7919 `mod` 100003, k) | k <- [1 .. 3000]]
  print (IM.size big, IM.foldr (+) 0 big, IM.lookupMin big, IM.lookupMax big, take 5 (IM.toList big))
  let is1 = IS.fromList [k * k `mod` 97 | k <- [0 .. 96]]
      is2 = IS.fromList [-10, -5 .. 50]
  print (is1, IS.size is1)
  print (IS.union is1 is2, IS.intersection is1 is2, IS.difference is2 is1, IS.member 50 is1, IS.findMin is2, IS.findMax is1)
  print (IS.split 30 is1, IS.elems (IS.map (`div` 10) is1), IS.foldr (+) 0 is2, IS.toDescList (IS.filter even is2))
  putStrLn "-- Seq as a deque"
  let q0 = Seq.fromList [1 .. 5 :: Int]
      q1 = (0 <| q0) |> 6
      q2 = q1 >< Seq.fromList [10, 20]
  print (q0, q1, q2, Seq.length q2, Seq.index q2 3, Seq.lookup 100 q2)
  print (Seq.update 2 99 q2, Seq.adjust (* 100) 0 q2, Seq.insertAt 1 (-1) q0, Seq.deleteAt 0 q0, Seq.reverse q1)
  print (Seq.splitAt 3 q2, Seq.take 2 q2, Seq.drop 6 q2, Seq.sort (Seq.fromList [3, 1, 2 :: Int]), Seq.sortBy (flip compare) q0)
  case Seq.viewl q2 of
    x :< rest -> print (x, rest)
    EmptyL -> putStrLn "empty"
  case Seq.viewr q2 of
    rest :> x -> print (rest, x)
    EmptyR -> putStrLn "empty"
  let rotate n s = iterate (\t -> case Seq.viewl t of x :< r -> r |> x; EmptyL -> t) s !! n
  print (map (\n -> rotate n (Seq.fromList "abcde")) [0 .. 5])
  print (foldr (:) [] q2, sum q2, fmap (* 2) q0, Seq.zip q0 (Seq.fromList "abc"), Seq.findIndexL (> 3) q2, Seq.elemIndexL 20 q2)
  print (Seq.scanl (+) 0 q0, Seq.tails (Seq.fromList [1, 2 :: Int]), Seq.chunksOf 3 q2, Seq.replicate 3 'x', Seq.empty :: Seq Int)
  -- Josephus problem with a Seq, printing elimination order.
  let josephus k s
        | Seq.null s = []
        | otherwise = let i = (k - 1) `mod` Seq.length s; (front, back) = Seq.splitAt i s in case Seq.viewl back of
            x :< rest -> x : josephus k (rest >< front)
            EmptyL -> []
  print (josephus 3 (Seq.fromList [1 .. 20 :: Int]), josephus 7 (Seq.fromList [1 .. 41 :: Int]))
