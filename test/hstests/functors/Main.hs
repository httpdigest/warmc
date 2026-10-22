{-# LANGUAGE DeriveTraversable #-}
-- The Functor / Applicative / Monad / Foldable / Traversable tower written by
-- hand for user types -- a rose tree, a binary tree, a non-empty list, a
-- validation applicative that accumulates errors, a writer monad and a
-- parser-free state monad -- next to the same classes derived with
-- DeriveTraversable, plus Semigroup and Monoid: the base newtypes (Sum,
-- Product, Min, Max, First, Last, Any, All, Endo, Dual), user instances,
-- `foldMap`, `mconcat`, `stimes`, and Maybe/Ordering/function monoids.
--
-- All of it is higher-order code over dictionaries; GHC specialises much of
-- it at -O2, so what reaches warmc is a mix of known calls into specialised
-- worker functions and unknown calls through `stg_ap_*` for the rest.
module Main (main) where

import Data.Foldable (foldl', toList)
import Data.Functor.Compose (Compose (..))
import Data.Functor.Identity (Identity (..))
import Data.List.NonEmpty (NonEmpty (..))
import Data.Monoid
import Data.Semigroup (Max (..), Min (..), mtimesDefault, sconcat, stimes)
import Data.Traversable (mapAccumL, mapAccumR)

-- A binary tree with every instance written by hand.
data Tree a = Tip | Bin (Tree a) a (Tree a)

instance (Show a) => Show (Tree a) where
  show Tip = "."
  show (Bin l v r) = "(" ++ show l ++ " " ++ show v ++ " " ++ show r ++ ")"

instance Functor Tree where
  fmap _ Tip = Tip
  fmap f (Bin l v r) = Bin (fmap f l) (f v) (fmap f r)

instance Foldable Tree where
  foldr _ z Tip = z
  foldr f z (Bin l v r) = foldr f (f v (foldr f z r)) l

instance Traversable Tree where
  traverse _ Tip = pure Tip
  traverse f (Bin l v r) = Bin <$> traverse f l <*> f v <*> traverse f r

insertT :: (Ord a) => a -> Tree a -> Tree a
insertT v Tip = Bin Tip v Tip
insertT v t@(Bin l w r)
  | v < w = Bin (insertT v l) w r
  | v > w = Bin l w (insertT v r)
  | otherwise = t

fromListT :: (Ord a) => [a] -> Tree a
fromListT = foldl' (flip insertT) Tip

-- A rose tree with derived instances, for comparison.
data Rose a = Rose a [Rose a] deriving (Show, Functor, Foldable, Traversable)

rose :: Int -> Rose Int
rose 0 = Rose 0 []
rose n = Rose n [rose k | k <- [n - 1, n - 3 .. 0]]

-- Validation: an Applicative that is not a Monad, collecting all failures.
data Validation e a = Failure e | Success a deriving (Show)

instance Functor (Validation e) where
  fmap _ (Failure e) = Failure e
  fmap f (Success a) = Success (f a)

instance (Semigroup e) => Applicative (Validation e) where
  pure = Success
  Failure a <*> Failure b = Failure (a <> b)
  Failure a <*> Success _ = Failure a
  Success _ <*> Failure b = Failure b
  Success f <*> Success v = Success (f v)

data Person = Person {pname :: String, age :: Int, email :: String} deriving (Show)

validate :: String -> Int -> String -> Validation [String] Person
validate n a e =
  Person
    <$> check (not (null n)) "empty name" n
    <*> check (a >= 0 && a < 150) ("bad age " ++ show a) a
    <*> check ('@' `elem` e) ("bad email " ++ show e) e
  where
    check ok msg v = if ok then Success v else Failure [msg]

-- A writer monad by hand.
newtype Writer w a = Writer {runWriter :: (a, w)}

instance Functor (Writer w) where fmap f (Writer (a, w)) = Writer (f a, w)

instance (Monoid w) => Applicative (Writer w) where
  pure a = Writer (a, mempty)
  Writer (f, w1) <*> Writer (a, w2) = Writer (f a, w1 <> w2)

instance (Monoid w) => Monad (Writer w) where
  Writer (a, w) >>= k = let Writer (b, w') = k a in Writer (b, w <> w')

tell :: w -> Writer w ()
tell w = Writer ((), w)

collatzW :: Int -> Writer [Int] Int
collatzW 1 = tell [1] >> return 0
collatzW n = do
  tell [n]
  steps <- collatzW (if even n then n `div` 2 else 3 * n + 1)
  return (steps + 1)

-- A state monad by hand, used through traverse.
newtype State s a = State {runState :: s -> (a, s)}

instance Functor (State s) where
  fmap f (State g) = State (\s -> let (a, s') = g s in (f a, s'))

instance Applicative (State s) where
  pure a = State (\s -> (a, s))
  State f <*> State g = State (\s -> let (h, s1) = f s; (a, s2) = g s1 in (h a, s2))

instance Monad (State s) where
  State g >>= k = State (\s -> let (a, s1) = g s in runState (k a) s1)

fresh :: State Int Int
fresh = State (\n -> (n, n + 1))

label :: Traversable t => t a -> t (Int, a)
label t = fst (runState (traverse (\a -> (\n -> (n, a)) <$> fresh) t) 100)

-- A non-empty list with Semigroup but no Monoid.
newtype MaxLen = MaxLen String deriving (Show)

instance Semigroup MaxLen where
  MaxLen a <> MaxLen b = if length b > length a then MaxLen b else MaxLen a

-- A monoid of 2x2 matrices under multiplication.
data M2 = M2 Integer Integer Integer Integer deriving (Show, Eq)

instance Semigroup M2 where
  M2 a b c d <> M2 e f g h = M2 (a * e + b * g) (a * f + b * h) (c * e + d * g) (c * f + d * h)

instance Monoid M2 where mempty = M2 1 0 0 1

fibM :: Int -> Integer
fibM n = let M2 _ f _ _ = mtimesDefault n (M2 1 1 1 0) in f

-- Statistics gathered in a single foldMap.
data Stats = Stats {count :: !Int, total :: !Int, lo :: !Int, hi :: !Int} deriving (Show)

instance Semigroup Stats where
  Stats c1 t1 l1 h1 <> Stats c2 t2 l2 h2 = Stats (c1 + c2) (t1 + t2) (min l1 l2) (max h1 h2)

instance Monoid Stats where mempty = Stats 0 0 maxBound minBound

stat :: Int -> Stats
stat v = Stats 1 v v v

main :: IO ()
main = do
  putStrLn "-- hand-written Tree instances"
  let t = fromListT [50, 30, 70, 20, 40, 60, 80, 35, 65, 10 :: Int]
  print t
  print (fmap (* 2) t)
  print (sum t, product (fmap (`mod` 7) t), length t, maximum t, minimum t, elem 35 t, elem 36 t)
  print (toList t, foldr (:) [] t, foldl (flip (:)) [] t)
  print (foldMap (\v -> [v, v]) t)
  print (traverse (\v -> if v > 5 then Just (v - 5) else Nothing) t)
  print (traverse (\v -> if v > 15 then Just v else Nothing) t)
  print (fmap toList (traverse (\v -> [v, -v]) (fromListT [1, 2 :: Int])))
  print (label t)
  print (mapAccumL (\acc v -> (acc + v, acc)) 0 t)
  print (mapAccumR (\acc v -> (acc + v, acc)) 0 t)
  print (sequenceA [Just 1, Just 2, Just (3 :: Int)], sequenceA [Just 1, Nothing, Just (3 :: Int)])
  print (sequence_ [Just 1, Just (2 :: Int)], traverse (\v -> [v, v + 1]) [10, 20 :: Int])
  putStrLn "-- derived Rose instances"
  let r = rose 5
  print (length r, sum r, toList r)
  print (fmap show (rose 3))
  print (label (rose 3))
  print (maximum (fmap negate r), foldr (\v acc -> v - acc) 0 r)
  putStrLn "-- validation applicative"
  print (validate "ann" 33 "ann@example.org")
  print (validate "" 200 "nope")
  print (validate "bob" (-1) "bob@x")
  print (traverse (\v -> if even v then Success v else Failure [v]) [2, 4, 5, 6, 7, 9 :: Int])
  putStrLn "-- writer and state monads"
  print (runWriter (collatzW 6))
  print (runWriter (mapM collatzW [3, 4]))
  print (runState (mapM (\v -> fmap (+ v) fresh) [10, 20, 30]) 1)
  print (runState (sequence (replicate 5 fresh)) 7)
  print (fst (runState (do a <- fresh; b <- fresh; c <- fresh; return [a, b, c, a * b * c]) 3))
  putStrLn "-- base functors"
  print (fmap (+ 1) (Right 3 :: Either String Int), fmap (+ 1) (Left "e" :: Either String Int))
  print (fmap length ("key", "value"), (+ 1) <$> [1, 2, 3 :: Int], (* 2) <$> Just (4 :: Int))
  print ((,) <$> Just 'a' <*> Just True, (+) <$> [1, 2] <*> [10, 20 :: Int], pure 5 :: [Int])
  print (Just 3 <* Just 'x', Just 3 *> Just 'x', (Nothing :: Maybe Int) <* Just 'x', [1, 2 :: Int] <* "ab")
  print ((fmap . fmap) (* 10) [Just 1, Nothing, Just (3 :: Int)])
  print (getCompose (fmap (+ 1) (Compose [Just 1, Nothing, Just (5 :: Int)])))
  print (runIdentity (traverse (Identity . (* 3)) [1, 2, 3 :: Int]))
  print (((+) <*> (* 2)) 5, (fmap (+ 1) (* 3)) (4 :: Int), (do a <- (+ 1); b <- (* 2); return (a, b)) (7 :: Int))
  print ([1, 2, 3] >>= \v -> replicate v (v :: Int), Just 4 >>= \v -> if v > 3 then Just (v * v) else Nothing)
  print (concat <$> mapM (\v -> [[v], [v, v]]) [1, 2 :: Int])
  putStrLn "-- semigroups and monoids"
  let xs = [3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5 :: Int]
  print (getSum (foldMap Sum xs), getProduct (foldMap Product xs))
  print (getAll (foldMap (All . (> 0)) xs), getAny (foldMap (Any . (> 8)) xs))
  print (getMin (sconcat (fmap Min (3 :| [1, 4 :: Int]))), getMax (foldMap Max (2 :| [7, 1 :: Int])))
  print (getFirst (foldMap (First . Just) xs), getLast (foldMap (Last . Just) xs), getFirst (mempty :: First Int))
  print (appEndo (foldMap (Endo . (+)) xs) 0, appEndo (foldMap Endo [(* 2), (+ 1)]) (5 :: Int))
  print (getDual (foldMap (Dual . show) xs), foldMap show xs)
  print (mconcat ["ab", "cd", "ef"], mempty :: String, [1, 2] <> [3 :: Int])
  print (Just [1] <> Nothing <> Just [2, 3 :: Int], (Nothing :: Maybe [Int]) <> Nothing)
  print (compare 1 2 <> compare 3 (3 :: Int), EQ <> GT, mconcat [EQ, EQ, LT, GT])
  print ((show <> const "-" <> show) (42 :: Int))
  print (stimes (3 :: Int) "ab", stimes (4 :: Int) (Sum (5 :: Int)), sconcat (MaxLen "a" :| [MaxLen "abc", MaxLen "ab"]))
  print (map fibM [0, 1, 2, 10, 50, 90])
  print (mconcat [M2 1 2 3 4, M2 0 1 1 0, mempty])
  print (foldMap stat xs, foldMap stat [42])
  print (foldMap (\v -> (Sum v, Product v, Max v, Min v)) (1 :| [2, 3, 4 :: Int]))
  print (map (fmap getSum) (foldMap (\(k, v) -> [(k, Sum v)]) [('a', 1 :: Int), ('b', 2)]))
