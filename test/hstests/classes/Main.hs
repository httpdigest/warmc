{-# LANGUAGE ExistentialQuantification #-}
{-# LANGUAGE ScopedTypeVariables #-}
-- Type classes: user classes with default methods and superclasses, instances
-- for user and base types, polymorphism resolved by return type, constrained
-- polymorphic functions, instance contexts (`instance Pretty a => Pretty [a]`),
-- derived Eq/Ord/Show/Read/Enum/Bounded on enumerations, records and
-- parameterised types, hand-written Show with showsPrec and precedence, and
-- dispatch through dictionaries GHC cannot specialise away (a NOINLINE
-- polymorphic function and an existential list).
--
-- A dictionary is a record of closures in GHC's heap, and a method call that
-- survives specialisation is a field load and an unknown call: to warmc a
-- `call_indirect` whose target came out of linear memory.
module Main (main) where

import Data.List (intercalate, sort, sortBy)
import Data.Ord (Down (..), comparing)

-- A class with a superclass and default methods, some overridden.
class (Show a) => Shape a where
  name :: a -> String
  name = takeWhile (/= ' ') . show
  area :: a -> Double
  perimeter :: a -> Double
  describe :: a -> String
  describe s = name s ++ " area=" ++ show (area s) ++ " perimeter=" ++ show (perimeter s)
  {-# MINIMAL area, perimeter #-}

data Circle = Circle Double deriving (Show, Eq)

data Rect = Rect Double Double deriving (Show, Eq)

data Square = Square Double deriving (Show, Eq)

instance Shape Circle where
  area (Circle r) = 3 * r * r
  perimeter (Circle r) = 6 * r

instance Shape Rect where
  area (Rect w h) = w * h
  perimeter (Rect w h) = 2 * (w + h)
  name _ = "Rectangle"

instance Shape Square where
  area (Square s) = s * s
  perimeter (Square s) = 4 * s
  describe s = "a square of side " ++ show (sqrt (area s))

data AnyShape = forall s. (Shape s) => AnyShape s

-- A class with a polymorphic return type.
class Default a where
  def :: a

instance Default Int where def = 42

instance Default Bool where def = True

instance Default [a] where def = []

instance (Default a, Default b) => Default (a, b) where def = (def, def)

instance (Default a) => Default (Maybe a) where def = Just def

-- A class over type constructors' contents, with instance contexts.
class Pretty a where
  pretty :: a -> String
  prettyList :: [a] -> String
  prettyList xs = "[" ++ intercalate ", " (map pretty xs) ++ "]"

instance Pretty Int where pretty n = "#" ++ show n

instance Pretty Bool where
  pretty True = "yes"
  pretty False = "no"

instance Pretty Char where
  pretty c = [c]
  prettyList s = "\"" ++ s ++ "\""

instance (Pretty a) => Pretty [a] where pretty = prettyList

instance (Pretty a, Pretty b) => Pretty (a, b) where
  pretty (a, b) = "<" ++ pretty a ++ " | " ++ pretty b ++ ">"

instance (Pretty a) => Pretty (Maybe a) where
  pretty Nothing = "none"
  pretty (Just x) = "some " ++ pretty x

instance (Pretty a, Pretty b) => Pretty (Either a b) where
  pretty (Left a) = "L:" ++ pretty a
  pretty (Right b) = "R:" ++ pretty b

-- A numeric class hierarchy of our own, with a vector-space instance.
class Monoidal a where
  zero :: a
  plus :: a -> a -> a

class (Monoidal a) => Group a where
  neg :: a -> a
  minus :: a -> a -> a
  minus x y = plus x (neg y)

data V2 = V2 Int Int deriving (Show, Eq, Ord)

instance Monoidal V2 where
  zero = V2 0 0
  plus (V2 a b) (V2 c d) = V2 (a + c) (b + d)

instance Group V2 where neg (V2 a b) = V2 (negate a) (negate b)

instance Monoidal Int where
  zero = 0
  plus = (+)

instance Group Int where
  neg = negate
  minus = (-)

msum' :: (Monoidal a) => [a] -> a
msum' = foldr plus zero

-- Num instance for a user type: arithmetic modulo 7.
newtype Mod7 = Mod7 Int deriving (Eq, Ord)

instance Show Mod7 where show (Mod7 n) = show n ++ " (mod 7)"

instance Num Mod7 where
  Mod7 a + Mod7 b = Mod7 ((a + b) `mod` 7)
  Mod7 a * Mod7 b = Mod7 ((a * b) `mod` 7)
  negate (Mod7 a) = Mod7 ((7 - a) `mod` 7)
  abs = id
  signum (Mod7 0) = 0
  signum _ = 1
  fromInteger n = Mod7 (fromInteger (n `mod` 7))

-- Polynomials with a Num instance.
newtype Poly = Poly [Integer] deriving (Eq)

instance Show Poly where
  show (Poly cs) =
    case [term c i | (c, i) <- zip cs [0 :: Int ..], c /= 0] of
      [] -> "0"
      ts -> intercalate " + " (reverse ts)
    where
      term c 0 = show c
      term c 1 = show c ++ "x"
      term c i = show c ++ "x^" ++ show i

instance Num Poly where
  Poly a + Poly b = Poly (zipLong a b)
    where
      zipLong (x : xs) (y : ys) = (x + y) : zipLong xs ys
      zipLong xs [] = xs
      zipLong [] ys = ys
  Poly a * Poly b = sum [Poly (replicate i 0 ++ map (* x) b) | (x, i) <- zip a [0 ..]]
  negate (Poly a) = Poly (map negate a)
  abs = undefined
  signum = undefined
  fromInteger n = Poly [n]

x :: Poly
x = Poly [0, 1]

-- Derived instances on enumerations, records, and parameterised types.
data Colour = Red | Orange | Yellow | Green | Blue | Indigo | Violet
  deriving (Show, Read, Eq, Ord, Enum, Bounded)

data Suit = Clubs | Diamonds | Hearts | Spades deriving (Show, Eq, Ord, Enum, Bounded)

data Card = Card {rank :: Int, suit :: Suit} deriving (Show, Eq, Ord)

data Pair a b = Pair a b deriving (Show, Read, Eq, Ord)

data Expr = Lit Int | Neg Expr | Expr :+: Expr | Expr :*: Expr
  deriving (Show, Read, Eq, Ord)

infixl 6 :+:

infixl 7 :*:

-- A hand-written showsPrec that inserts only the parentheses it needs.
newtype Infix = Infix Expr

instance Show Infix where
  showsPrec d (Infix e) = go d e
    where
      go _ (Lit n) = shows n
      go p (Neg a) = showParen (p > 9) (showString "-" . go 10 a)
      go p (a :+: b) = showParen (p > 6) (go 6 a . showString " + " . go 7 b)
      go p (a :*: b) = showParen (p > 7) (go 7 a . showString " * " . go 8 b)

allValues :: (Bounded a, Enum a) => [a]
allValues = [minBound .. maxBound]

cycleNext :: forall a. (Bounded a, Enum a, Eq a) => a -> a
cycleNext v
  | v == maxBound = minBound
  | otherwise = succ v

-- Kept out of line so the dictionary really is passed at run time.
describeAll :: (Shape a) => [a] -> [String]
describeAll = map describe
{-# NOINLINE describeAll #-}

sumAreas :: (Shape a) => [a] -> Double
sumAreas = sum . map area
{-# NOINLINE sumAreas #-}

main :: IO ()
main = do
  putStrLn "-- defaults and superclasses"
  mapM_ putStrLn (describeAll [Circle 1, Circle 2.5])
  mapM_ putStrLn (describeAll [Rect 2 3, Rect 0.5 8])
  mapM_ putStrLn (describeAll [Square 4])
  print (sumAreas [Rect 1 2, Rect 3 4], sumAreas [Circle 1, Circle 2])
  let shapes = [AnyShape (Circle 1), AnyShape (Rect 2 5), AnyShape (Square 3), AnyShape (Circle 0.5)]
  mapM_ (\(AnyShape s) -> putStrLn (name s ++ ": " ++ show (area s) ++ " / " ++ show s)) shapes
  print (sum [perimeter s | AnyShape s <- shapes])
  putStrLn "-- return-type polymorphism"
  print (def :: Int, def :: Bool, def :: (Int, Bool), def :: Maybe (Maybe Int))
  print (def :: [Int], (def :: (Maybe Bool, [Char])))
  putStrLn "-- instance contexts"
  putStrLn (pretty (3 :: Int))
  putStrLn (pretty [1, 2, 3 :: Int])
  putStrLn (pretty "hello")
  putStrLn (pretty [(1 :: Int, True), (2, False)])
  putStrLn (pretty [Just "a", Nothing, Just "bc"])
  putStrLn (pretty [Left (1 :: Int), Right [True, False], Left 7])
  putStrLn (pretty (Just (Just ([] :: [Int]))))
  putStrLn "-- user hierarchies"
  print (msum' [V2 1 2, V2 3 4, V2 (-10) 5], msum' [1, 2, 3 :: Int])
  print (minus (V2 5 5) (V2 1 9), minus (10 :: Int) 3, neg (V2 1 (-1)))
  print [Mod7 3 + Mod7 5, Mod7 3 * Mod7 5, negate (Mod7 2), 100, sum (map fromIntegral [1 .. 20 :: Int])]
  print (product (replicate 10 (Mod7 3)), (2 :: Mod7) ^ (20 :: Int))
  let p = (x + 1) ^ (5 :: Int)
  print p
  print ((x - 1) * (x + 1), (x * x + 2 * x + 1) * (x - 3), p - p)
  print [(x + fromInteger k) ^ (2 :: Int) | k <- [-2 .. 2]]
  putStrLn "-- derived Enum and Bounded"
  print (allValues :: [Colour])
  print (allValues :: [Suit], allValues :: [Bool], [minBound .. maxBound :: Ordering])
  print (map fromEnum [Red ..], toEnum 3 :: Colour, [Red, Yellow ..], [Violet, Indigo .. Orange])
  print (take 10 (iterate cycleNext Blue), take 6 (iterate cycleNext Hearts))
  print (succ Red, pred Violet, [Orange .. Blue], fromEnum (maxBound :: Colour))
  print (minimum [Green, Red, Violet], maximum [Green, Red, Violet], compare Blue Green)
  putStrLn "-- derived Ord, Show, Read"
  let cards = [Card r s | s <- [Spades, Hearts], r <- [12, 3, 7]]
  mapM_ print (sort cards)
  print (sortBy (comparing suit <> comparing (Down . rank)) cards)
  print (Card {rank = 1, suit = Clubs} < Card 1 Diamonds, maximum cards)
  print (Pair (-3 :: Int) (Just 'x'), Pair [1.5 :: Double] (Left 2 :: Either Int ()))
  print (read "Pair 5 (-7)" :: Pair Int Int, read " Pair  \"s\" [True,False] " :: Pair String [Bool])
  print (read "[Red,Violet,Green]" :: [Colour], map (read :: String -> Colour) (words "Blue Indigo"))
  let e = Lit 1 :+: Lit 2 :*: Neg (Lit 3 :+: Lit 4)
  print e
  print (read (show e) == e, read "Lit 4 :*: Lit 5" :: Expr)
  print (Infix e)
  print (Infix ((Lit 1 :+: Lit 2) :*: Lit 3), Infix (Lit 1 :+: (Lit 2 :+: Lit 3)), Infix (Neg (Neg (Lit 0))))
  print (sort [Lit 3 :+: Lit 1, Lit 2, Neg (Lit 9), Lit 1 :*: Lit 1, Lit (-1)])
  print (showsPrec 11 (-5 :: Int) "", show (Just (-2 :: Int)), show [Just (Left (3 :: Int)) :: Maybe (Either Int ())])
  print (compare (1 :: Int, 'b') (1, 'a'), compare [1, 2 :: Int] [1, 2, 0], max "abc" "abd", min (Just 3) (Nothing :: Maybe Int))
