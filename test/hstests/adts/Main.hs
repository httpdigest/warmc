{-# LANGUAGE LambdaCase #-}
{-# LANGUAGE MultiWayIf #-}
{-# LANGUAGE PatternSynonyms #-}
{-# LANGUAGE ViewPatterns #-}
-- Algebraic data and pattern matching: sum types with many constructors,
-- records with field access and update syntax (including a field shared by
-- several constructors), strict and unpacked fields, newtypes, guards with
-- `where` bindings, as-patterns, nested and literal patterns, view patterns,
-- pattern synonyms, LambdaCase and MultiWayIf, `case` on tuples, and records
-- of functions used as objects with private state in their closures.
--
-- GHC tags pointers with the constructor number when it fits in the low bits
-- and otherwise reads it from the info table, so the eight-constructor type
-- here takes both paths on a 32-bit target.  Record update is allocation of a
-- copy; a strict field forces its argument before the constructor is built.
module Main (main) where

import Data.Char (isDigit, isSpace, toUpper)
import Data.List (sortOn, stripPrefix)

-- A sum type with more constructors than the pointer tag can name.
data Token
  = TNum Int
  | TIdent String
  | TPlus
  | TMinus
  | TStar
  | TSlash
  | TLParen
  | TRParen
  | TEnd
  deriving (Show, Eq)

classify :: Token -> String
classify = \case
  TNum n | n < 0 -> "negative number"
         | n == 0 -> "zero"
         | n < 10 -> "digit"
         | otherwise -> "number"
  TIdent (c : _) | c == '_' -> "private identifier"
  TIdent s@(_ : _ : _ : _) -> "long identifier " ++ s
  TIdent s -> "short identifier " ++ s
  t | t `elem` [TPlus, TMinus] -> "additive"
    | t `elem` [TStar, TSlash] -> "multiplicative"
  TEnd -> "end"
  _ -> "paren"

tokenize :: String -> [Token]
tokenize [] = [TEnd]
tokenize s@(c : cs)
  | isSpace c = tokenize cs
  | isDigit c = let (d, rest) = span isDigit s in TNum (read d) : tokenize rest
  | c `elem` "_abcdefghijklmnopqrstuvwxyz" =
      let (w, rest) = span (`notElem` " +-*/()") s in TIdent w : tokenize rest
  | otherwise = sym c : tokenize cs
  where
    sym '+' = TPlus
    sym '-' = TMinus
    sym '*' = TStar
    sym '/' = TSlash
    sym '(' = TLParen
    sym ')' = TRParen
    sym x = TIdent [x]

-- Records: field selectors, update syntax, a field common to two constructors.
data Employee = Employee
  { ename :: String,
    dept :: String,
    salary :: !Int,
    manager :: Maybe Employee
  }
  deriving (Show)

data Shape
  = Circle {colour :: String, radius :: Double}
  | Rect {colour :: String, width :: Double, height :: Double}
  deriving (Show, Eq)

data Config = Config
  { verbose :: Bool,
    depth :: {-# UNPACK #-} !Int,
    label :: String,
    weights :: [Int]
  }
  deriving (Show, Eq)

defaultConfig :: Config
defaultConfig = Config {verbose = False, depth = 3, label = "default", weights = [1, 2, 3]}

raise :: Int -> Employee -> Employee
raise pct e = e {salary = salary e + salary e * pct `div` 100}

chain :: Employee -> [String]
chain e = ename e : maybe [] chain (manager e)

-- Newtypes with their own instances.
newtype Celsius = Celsius Double deriving (Show, Eq, Ord)

newtype Fahrenheit = Fahrenheit Double deriving (Show, Eq, Ord)

toF :: Celsius -> Fahrenheit
toF (Celsius c) = Fahrenheit (c * 9 / 5 + 32)

newtype Wrapped a = Wrapped {unwrap :: a} deriving (Show)

-- Nested patterns and as-patterns.
pairs :: [a] -> [(a, a)]
pairs (x : rest@(y : _)) = (x, y) : pairs rest
pairs _ = []

compress :: (Eq a) => [a] -> [(a, Int)]
compress [] = []
compress (x : xs) = go x 1 xs
  where
    go c n (y : ys) | y == c = go c (n + 1) ys
    go c n ys = (c, n) : compress ys

describeList :: (Show a) => [Maybe (Either a (a, a))] -> [String]
describeList = map d
  where
    d Nothing = "nothing"
    d (Just (Left v)) = "left " ++ show v
    d (Just (Right (a, b))) = "right pair " ++ show a ++ "/" ++ show b

nested :: (Int, (Int, [Int]), Maybe (Int, Int)) -> Int
nested (a, (b, [c, d]), Just (e, _)) = a + b + c + d + e
nested (a, (_, c : _), Nothing) = a * c
nested (_, (b, []), _) = b
nested (a, _, _) = negate a

literal :: String -> Int -> String
literal "hello" 0 = "greeting zero"
literal "hello" _ = "greeting"
literal ('x' : _) 1 = "x-word one"
literal _ n | n > 100 = "big"
literal s n = s ++ "/" ++ show n

-- View patterns and pattern synonyms.
pattern Zero :: Int
pattern Zero = 0

pattern Pair :: a -> b -> (a, b)
pattern Pair a b = (a, b)

pattern Point :: Int -> Int -> [Int]
pattern Point x y = [x, y]

firstWordUpper :: String -> String
firstWordUpper (words -> w : _) = map toUpper w
firstWordUpper _ = "<none>"

command :: String -> String
command (stripPrefix "go " -> Just dir) = "moving " ++ dir
command (stripPrefix "take " -> Just item) = "taking " ++ item
command (length -> n) | n > 10 = "too long"
command s = "unknown: " ++ s

isZero :: Int -> Bool
isZero Zero = True
isZero _ = False

grade :: Int -> Char
grade s =
  if
    | s >= 90 -> 'A'
    | s >= 80 -> 'B'
    | s >= 70 -> 'C'
    | s >= 60 -> 'D'
    | otherwise -> 'F'

-- Guards with where bindings shared between the guards.
bmi :: Double -> Double -> String
bmi w h
  | v < under = "under " ++ shown
  | v < normal = "normal " ++ shown
  | v < over = "over " ++ shown
  | otherwise = "obese " ++ shown
  where
    v = w / (h * h)
    shown = show (fromIntegral (round (v * 10) :: Int) / 10 :: Double)
    (under, normal, over) = (18.5, 25.0, 30.0)

-- Records of functions: objects with private state captured in closures.
data Counter = Counter
  { incr :: Counter,
    addN :: Int -> Counter,
    current :: Int,
    describeC :: String
  }

mkCounter :: String -> Int -> Int -> Counter
mkCounter nm step v =
  Counter
    { incr = mkCounter nm step (v + step),
      addN = \k -> mkCounter nm step (v + k * step),
      current = v,
      describeC = nm ++ "@" ++ show v ++ " step " ++ show step
    }

data Stack a = Stack
  { push :: a -> Stack a,
    pop :: Maybe (a, Stack a),
    size :: Int,
    contents :: [a]
  }

listStack :: [a] -> Stack a
listStack xs =
  Stack
    { push = \v -> listStack (v : xs),
      pop = case xs of
        [] -> Nothing
        (v : rest) -> Just (v, listStack rest),
      size = length xs,
      contents = xs
    }

data Ops = Ops {opName :: String, apply :: Int -> Int -> Int, unit :: Int}

ops :: [Ops]
ops =
  [ Ops "sum" (+) 0,
    Ops "product" (*) 1,
    Ops "max" max minBound,
    Ops {opName = "xor-ish", apply = \a b -> (a + b) `mod` 7, unit = 0}
  ]

main :: IO ()
main = do
  putStrLn "-- tokens and classification"
  let toks = tokenize "foo + 12*(bar_1 - 7) / _x 0 ab"
  print toks
  mapM_ (putStrLn . classify) toks
  mapM_ (putStrLn . classify) [TNum (-5), TIdent "q"]
  putStrLn "-- records"
  let boss = Employee {ename = "Grace", dept = "R&D", salary = 9000, manager = Nothing}
      lead = Employee "Alan" "R&D" 7000 (Just boss)
      dev = Employee {ename = "Ada", dept = "R&D", salary = 5000, manager = Just lead}
      dev' = (raise 10 dev) {dept = "Compilers"}
  print dev'
  print (chain dev', map salary [boss, lead, dev, dev', raise 3 (raise 3 dev)])
  print (ename <$> manager dev, fmap ename (manager boss))
  let shapes = [Circle "red" 1.5, Rect "blue" 2 3, Circle {radius = 2, colour = "green"}]
  print (map colour shapes)
  print (map (\s -> s {colour = map toUpper (colour s)}) shapes)
  print [r | Circle {radius = r} <- shapes]
  print [w * h | Rect {width = w, height = h} <- shapes]
  let c1 = defaultConfig {depth = 7, label = "seven"}
      c2 = c1 {weights = map (* 2) (weights c1), verbose = True}
  print c1
  print c2
  print (c1 == c2, c1 == c1 {verbose = False}, depth c2 + sum (weights c2))
  putStrLn "-- newtypes"
  print (map toF [Celsius 0, Celsius 100, Celsius (-40), Celsius 37])
  print (maximum [Celsius 3, Celsius (-2), Celsius 8.5], Wrapped (Just 'w'), unwrap (Wrapped [1, 2 :: Int]))
  putStrLn "-- nested patterns"
  print (pairs "abcde", pairs [1 :: Int], compress "aaabccddddde")
  print (compress [1, 1, 2, 3, 3, 3, 1 :: Int])
  mapM_ putStrLn (describeList [Nothing, Just (Left 'q'), Just (Right ('a', 'b'))])
  print (map nested [(1, (2, [3, 4]), Just (5, 6)), (2, (0, [9, 1, 1]), Nothing), (0, (8, []), Just (1, 1)), (4, (0, [1]), Just (0, 0))])
  print (map (uncurry literal) [("hello", 0), ("hello", 3), ("xyz", 1), ("abc", 101), ("abc", 5)])
  putStrLn "-- view patterns, synonyms, multiway if"
  print (map firstWordUpper ["  hello world", "", "one"])
  print (map command ["go north", "take lamp", "dance wildly now", "sing"])
  print (map isZero [0, 1, -1], [a + b | Pair a b <- [(1, 2), (3, 4 :: Int)]])
  print [x * y | Point x y <- [[1, 2], [3], [4, 5], [6, 7, 8 :: Int]]]
  print (map grade [95, 85, 75, 65, 55, 90, 89])
  print (map (uncurry bmi) [(50, 1.8), (70, 1.75), (90, 1.8), (120, 1.7)])
  let classifyPair = \case
        (0, 0) -> "origin"
        (0, _) -> "y axis"
        (_, 0) -> "x axis"
        (a, b) | a == b -> "diagonal"
               | a == negate b -> "anti-diagonal"
               | otherwise -> "plane"
  print (map classifyPair [(0, 0), (0, 3), (2, 0), (4, 4), (3, -3), (1, 2 :: Int)])
  putStrLn "-- records of functions"
  let c = mkCounter "ctr" 3 10
  print (map current [c, incr c, incr (incr c), addN c 5, incr (addN (incr c) 2)])
  putStrLn (describeC (incr (addN c 10)))
  let s0 = listStack [] :: Stack Int
      s3 = push (push (push s0 1) 2) 3
  print (size s0, size s3, contents s3)
  case pop s3 of
    Just (v, s2) -> print (v, contents s2, fmap fst (pop s2), contents (push s2 99))
    Nothing -> putStrLn "empty"
  print (fmap fst (pop s0))
  mapM_ (\o -> putStrLn (opName o ++ " = " ++ show (foldr (apply o) (unit o) [3, 9, 4, 6]))) ops
  print (sortOn (negate . (\o -> apply o 5 6)) ops `seq` map opName (sortOn (\o -> apply o 5 6) ops))
