{-# OPTIONS_GHC -Wno-x-partial -Wno-incomplete-uni-patterns #-}
-- Monadic code: Maybe and Either as failure monads (a safe calculator, a
-- validation pipeline with early exit), the list monad as backtracking
-- search (SEND+MORE-style puzzle, N-queens, change making), Control.Monad's
-- combinators (mapM/forM/mapM_/foldM/foldM_/zipWithM/filterM/replicateM/
-- when/unless/forever-with-exit/msum/mplus/guard/join/liftM2/ap/>=>),
-- a State monad and a Reader monad written by hand, and the mtl stack
-- (StateT over Writer over Either via ExceptT, ReaderT over IO) running a
-- small stack-machine interpreter that logs every instruction.
--
-- Monadic code after GHC's optimiser is a chain of known and unknown calls
-- passing continuations and state tuples; the transformer stack adds
-- dictionary-passing that -O2 only partly specialises.
module Main (main) where

import Control.Monad
import Control.Monad.Except (ExceptT, runExceptT, throwError)
import Control.Monad.Reader (ReaderT, ask, asks, local, runReaderT)
import Control.Monad.State.Strict (StateT, evalStateT, execStateT, get, gets, modify', put, runStateT)
import Control.Monad.Writer.Strict (Writer, runWriter, tell)
import Control.Monad.Trans.Class (lift)
import Data.Char (isDigit)
import Data.IORef
import Data.List (sort)
import qualified Data.Map.Strict as M

-- Maybe / Either.
safeDiv :: Int -> Int -> Maybe Int
safeDiv _ 0 = Nothing
safeDiv a b = Just (a `div` b)

calc :: [Int] -> Maybe Int
calc xs = do
  a <- lookupAt 0
  b <- lookupAt 1
  c <- safeDiv a b
  d <- lookupAt 2
  guard (d /= 13)
  return (c * d + a)
  where
    lookupAt i = if i < length xs then Just (xs !! i) else Nothing

data Err = TooShort Int | NotNumeric String | OutOfRange Int deriving (Show)

parseAge :: String -> Either Err Int
parseAge s = do
  when (length s < 1) (Left (TooShort (length s)))
  unless (all isDigit s) (Left (NotNumeric s))
  let n = read s
  when (n > 150) (Left (OutOfRange n))
  return n

-- List monad searches.
queens :: Int -> [[Int]]
queens n = go n
  where
    go 0 = [[]]
    go k = do
      qs <- go (k - 1)
      q <- [1 .. n]
      guard (safe q qs)
      return (q : qs)
    safe q qs = and [q /= c && abs (q - c) /= d | (d, c) <- zip [1 ..] qs]

change :: Int -> [Int] -> [[Int]]
change 0 _ = [[]]
change _ [] = []
change amt coins@(c : cs)
  | amt < 0 = []
  | otherwise = map (c :) (change (amt - c) coins) ++ change amt cs

puzzle :: [(Int, Int, Int)]
puzzle = do
  -- TWO + TWO = FOUR with distinct digits
  t <- [1 .. 9]
  w <- [0 .. 9] \\\ [t]
  o <- [0 .. 9] \\\ [t, w]
  let two = 100 * t + 10 * w + o
      four = two + two
  guard (four >= 1000)
  let [f, o', u, r] = map (read . pure) (show four)
  guard (o' == o && f `notElem` [t, w, o] && u `notElem` [t, w, o, f] && r `notElem` [t, w, o, f, u] && u /= r)
  return (two, four, f)
  where
    xs \\\ ys = filter (`notElem` ys) xs

-- Hand-written State and Reader.
newtype State s a = State {runState :: s -> (a, s)}

instance Functor (State s) where fmap = liftM

instance Applicative (State s) where
  pure a = State (\s -> (a, s))
  (<*>) = ap

instance Monad (State s) where
  m >>= k = State (\s -> let (a, s') = runState m s in runState (k a) s')

getS :: State s s
getS = State (\s -> (s, s))

putS :: s -> State s ()
putS s = State (const ((), s))

modifyS :: (s -> s) -> State s ()
modifyS f = State (\s -> ((), f s))

labelTree :: Tree Char -> State Int (Tree (Int, Char))
labelTree (Leaf c) = do
  n <- getS
  putS (n + 1)
  return (Leaf (n, c))
labelTree (Node l r) = Node <$> labelTree l <*> labelTree r

data Tree a = Leaf a | Node (Tree a) (Tree a) deriving (Show)

fibMemo :: Int -> State (M.Map Int Integer) Integer
fibMemo n
  | n < 2 = return (toInteger n)
  | otherwise = do
      memo <- getS
      case M.lookup n memo of
        Just v -> return v
        Nothing -> do
          a <- fibMemo (n - 1)
          b <- fibMemo (n - 2)
          let v = a + b
          modifyS (M.insert n v)
          return v

newtype Reader r a = Reader {runReader :: r -> a}

instance Functor (Reader r) where fmap = liftM

instance Applicative (Reader r) where
  pure = Reader . const
  (<*>) = ap

instance Monad (Reader r) where
  m >>= k = Reader (\r -> runReader (k (runReader m r)) r)

data Env = Env {indent :: Int, bullet :: String}

render :: [String] -> Reader Env [String]
render items = forM items $ \it -> do
  Env i b <- Reader id
  return (replicate i ' ' ++ b ++ " " ++ it)

-- An mtl interpreter: StateT stack over ExceptT error over Writer log.
data Instr = Push Int | Pop | Add | Mul | Sub | Dup | Swap | Over | Jz Int | Jmp Int | Print deriving (Show)

type VM = StateT (Int, [Int]) (ExceptT String (Writer [String]))

step :: [Instr] -> VM Bool
step prog = do
  (pc, stack) <- get
  if pc >= length prog
    then return False
    else do
      let ins = prog !! pc
      lift (lift (tell [show pc ++ ": " ++ show ins ++ " " ++ show stack]))
      case (ins, stack) of
        (Push n, s) -> put (pc + 1, n : s)
        (Pop, _ : s) -> put (pc + 1, s)
        (Add, a : b : s) -> put (pc + 1, a + b : s)
        (Mul, a : b : s) -> put (pc + 1, a * b : s)
        (Sub, a : b : s) -> put (pc + 1, b - a : s)
        (Dup, a : s) -> put (pc + 1, a : a : s)
        (Swap, a : b : s) -> put (pc + 1, b : a : s)
        (Over, a : b : s) -> put (pc + 1, b : a : b : s)
        (Jz t, a : s) -> put (if a == 0 then t else pc + 1, s)
        (Jmp t, s) -> put (t, s)
        (Print, a : _) -> lift (lift (tell ["out " ++ show a])) >> put (pc + 1, stack)
        _ -> throwError ("stack underflow at " ++ show pc ++ " executing " ++ show ins)
      return True

runVM :: Int -> [Instr] -> (Either String (Int, [Int]), [String])
runVM fuel prog = runWriter (runExceptT (execStateT (loop fuel) (0, [])))
  where
    loop 0 = lift (throwError "out of fuel")
    loop k = do
      continue <- step prog
      when continue (loop (k - 1))

-- ReaderT over IO with local.
walk :: Int -> ReaderT (Int, String) IO ()
walk 0 = do
  (d, path) <- ask
  lift (putStrLn (replicate d '.' ++ path))
walk n = do
  d <- asks fst
  lift (putStrLn (replicate d '.' ++ "level " ++ show n))
  forM_ "ab" $ \c -> local (\(dd, p) -> (dd + 2, p ++ [c])) (walk (n - 1))

counterDemo :: StateT Int IO [Int]
counterDemo = do
  xs <- forM [1 .. 5] $ \i -> do
    modify' (* 2)
    modify' (+ i)
    get
  s <- gets show
  lift (putStrLn ("final state " ++ s))
  return xs

main :: IO ()
main = do
  putStrLn "-- Maybe and Either"
  print (map calc [[10, 2, 3], [10, 0, 3], [10], [7, 7, 13], [100, 7, 5]])
  print (map parseAge ["42", "", "4x2", "151", "150", "007"])
  print (mapM parseAge ["1", "2", "3"], mapM parseAge ["1", "x", "999"], traverse (`safeDiv` 2) [10, 20])
  print (sequence [Just 1, Just 2], sequence [Just 1, Nothing], sequence_ [Right 1, Left "stop", Left "later" :: Either String Int])
  print (Just 5 >>= safeDiv 100 >>= safeDiv 1000, Just 0 >>= safeDiv 100 >>= safeDiv 1000, either length negate (Left "abc" :: Either String Int))
  print (msum [Nothing, Just 3, Just 4], mplus [1, 2] [3 :: Int], join [[1], [2, 3 :: Int]], join (Just (Just 'j')), liftM2 (+) (Just 3) (Just (4 :: Int)))
  print ((safeDiv 1000 >=> safeDiv 5000) 7, (safeDiv 1000 <=< safeDiv 50) 2, return (+ 3) `ap` [] :: [Int], [(+ 1), (* 2)] `ap` [10, 20 :: Int])
  putStrLn "-- list monad"
  print (length (queens 6), head (queens 8), length (queens 8))
  print (length (change 100 [50, 25, 10, 5, 1]), take 5 (change 20 [10, 5, 2]))
  print puzzle
  print (do x <- [1 .. 4 :: Int]; y <- "ab"; [(x, y) | odd x])
  print [(a, b, c) | a <- [1 .. 3 :: Int], b <- [a .. 3], c <- [b .. 3], a + b + c == 6]
  putStrLn "-- Control.Monad combinators"
  r1 <- foldM (\acc x -> do putStrLn ("acc=" ++ show acc ++ " x=" ++ show x); return (acc * 2 + x)) 0 [1, 2, 3, 4 :: Int]
  print r1
  print (foldM (\acc x -> if x < 0 then Nothing else Just (acc + x)) 0 [1, 2, 3 :: Int], foldM (\acc x -> if x < 0 then Nothing else Just (acc + x)) 0 [1, -2, 3 :: Int])
  print (foldM (\acc x -> [acc + x, acc * x]) 1 [2, 3 :: Int])
  zs <- zipWithM (\i s -> return (show i ++ s)) [1 :: Int ..] ["a", "b", "c"]
  print zs
  zipWithM_ (\i s -> putStrLn (show i ++ ") " ++ s)) [1 :: Int ..] ["alpha", "beta"]
  print (filterM (const [True, False]) [1, 2, 3 :: Int], filterM (\x -> Just (x > 1)) [1, 2, 3 :: Int])
  print (replicateM 2 "xyz", replicateM 3 [0, 1 :: Int], replicateM_ 3 (Just ()), mapM_ Just [1, 2 :: Int])
  forM_ [1 .. 12 :: Int] $ \i -> do
    when (i `mod` 3 == 0) (putStr "fizz")
    when (i `mod` 4 == 0) (putStr "buzz")
    unless (i `mod` 3 == 0 || i `mod` 4 == 0) (putStr (show i))
    putStr " "
  putStrLn ""
  ref <- newIORef (1 :: Int)
  let loop = do
        v <- readIORef ref
        if v > 1000 then return v else writeIORef ref (v * 3) >> loop
  loop >>= print
  print (void (Just 'x'), forever Nothing :: Maybe (), (fmap . fmap) (+ 1) [Just 1, Nothing :: Maybe Int], mfilter even (Just (4 :: Int)), mfilter even (Just (5 :: Int)))
  putStrLn "-- hand-written State and Reader"
  let tree = Node (Node (Leaf 'a') (Leaf 'b')) (Node (Leaf 'c') (Node (Leaf 'd') (Leaf 'e')))
  print (runState (labelTree tree) 100)
  let (f90, memo) = runState (fibMemo 90) M.empty
  print (f90, M.size memo, M.lookup 45 memo)
  print (fst (runState (replicateM 4 (do n <- getS; putS (n * n `mod` 1000 + 1); return n)) 7))
  mapM_ putStrLn (runReader (render ["one", "two"]) (Env 2 "*"))
  mapM_ putStrLn (runReader (do a <- render ["x"]; b <- Reader (\e -> runReader (render ["y"]) e {indent = 6, bullet = "-"}); return (a ++ b)) (Env 0 "+"))
  putStrLn "-- mtl stacks"
  let progs =
        [ [Push 1, Push 5, Dup, Jz 12, Swap, Over, Mul, Print, Swap, Push 1, Sub, Jmp 2, Pop, Print],
          [Push 2, Push 3, Add, Print, Pop, Pop],
          [Push 1, Jmp 0],
          [Push 6, Push 7, Mul, Print]
        ]
  forM_ progs $ \p -> do
    let (res, logs) = runVM 80 p
    mapM_ putStrLn (take 70 logs)
    putStrLn ("result: " ++ show res ++ " after " ++ show (length logs) ++ " log lines")
  runReaderT (walk 2) (0, "/")
  (xs, final) <- runStateT counterDemo 1
  print (xs, final)
  v <- evalStateT (do modify' (+ 1); s <- get; lift (print s); return (s * 10)) (41 :: Int)
  print v
  print (sort (M.keys (M.fromList [(k, ()) | k <- "monad"])))
