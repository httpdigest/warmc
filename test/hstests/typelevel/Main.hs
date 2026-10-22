{-# LANGUAGE NoStarIsType #-}
{-# LANGUAGE AllowAmbiguousTypes #-}
{-# LANGUAGE DataKinds #-}
{-# LANGUAGE DerivingVia #-}
{-# LANGUAGE FunctionalDependencies #-}
{-# LANGUAGE GADTs #-}
{-# LANGUAGE GeneralizedNewtypeDeriving #-}
{-# LANGUAGE KindSignatures #-}
{-# LANGUAGE PolyKinds #-}
{-# LANGUAGE ScopedTypeVariables #-}
{-# LANGUAGE StandaloneDeriving #-}
{-# LANGUAGE TypeApplications #-}
{-# LANGUAGE TypeFamilies #-}
{-# LANGUAGE TypeOperators #-}
{-# LANGUAGE UndecidableInstances #-}
-- Type-level programming as it is commonly used: open and closed type
-- families, associated types and data families, DataKinds with a
-- length-indexed vector (safe head, zip, append whose length is a type-level
-- sum, replicate driven by a singleton), GHC.TypeLits (natVal, symbolVal,
-- KnownNat arithmetic, a fixed-size modular integer type), Proxy and
-- TypeApplications, phantom types for units and for a state machine,
-- multi-parameter classes with functional dependencies, type-class
-- recursion over type-level lists, GeneralizedNewtypeDeriving and
-- DerivingVia, and Typeable's runtime type representations.
--
-- None of this exists at run time except as dictionaries: a KnownNat
-- constraint is an Integer passed as an argument, an associated type is
-- whatever representation the instance picked.  The point is that the code
-- GHC erases these features down to still computes the right numbers.
module Main (main) where

import Data.Kind (Type)
import Data.List (intercalate)
import Data.Monoid (Sum (..))
import Data.Proxy (Proxy (..))
import Data.Typeable (Typeable, typeOf, typeRep)
import GHC.TypeLits (KnownNat, KnownSymbol, Nat, Symbol, natVal, symbolVal, type (*), type (+), type (-), type (^))

-- Closed type family computing a result type.
type family Elem c where
  Elem [a] = a
  Elem (Maybe a) = a
  Elem (a, a) = a

class Container c where
  cfirst :: c -> Maybe (Elem c)
  ctoList :: c -> [Elem c]

instance Container [a] where
  cfirst = foldr (const . Just) Nothing
  ctoList = id

instance Container (Maybe a) where
  cfirst = id
  ctoList = maybe [] pure

instance Container (a, a) where
  cfirst (a, _) = Just a
  ctoList (a, b) = [a, b]

-- Open type family plus a class using it.
type family Promote a

type instance Promote Int = Integer

type instance Promote Float = Double

type instance Promote Bool = Int

class Widen a where widen :: a -> Promote a

instance Widen Int where widen = toInteger

instance Widen Float where widen = realToFrac

instance Widen Bool where widen = fromEnum

-- Associated type and associated data family.
class Collection c where
  type Item c
  data Cursor c
  cinsert :: Item c -> c -> c
  cempty :: c
  start :: c -> Cursor c
  advance :: Cursor c -> Maybe (Item c, Cursor c)

newtype IntBag = IntBag [Int] deriving (Show)

instance Collection IntBag where
  type Item IntBag = Int
  data Cursor IntBag = BagCursor [Int]
  cinsert x (IntBag xs) = IntBag (insertSorted x xs)
    where
      insertSorted y [] = [y]
      insertSorted y (z : zs) = if y <= z then y : z : zs else z : insertSorted y zs
  cempty = IntBag []
  start (IntBag xs) = BagCursor xs
  advance (BagCursor []) = Nothing
  advance (BagCursor (x : xs)) = Just (x, BagCursor xs)

newtype CharSet = CharSet String deriving (Show)

instance Collection CharSet where
  type Item CharSet = Char
  data Cursor CharSet = CSCursor Int String
  cinsert c (CharSet s) = CharSet (if c `elem` s then s else c : s)
  cempty = CharSet ""
  start (CharSet s) = CSCursor 0 s
  advance (CSCursor _ []) = Nothing
  advance (CSCursor i (c : cs)) = Just (c, CSCursor (i + 1) cs)

drain :: (Collection c) => c -> [Item c]
drain = go . start
  where
    go cur = case advance cur of
      Nothing -> []
      Just (x, cur') -> x : go cur'

-- Length-indexed vectors.
data Nat' = Z | S Nat'

data Vec (n :: Nat') (a :: Type) where
  VNil :: Vec 'Z a
  VCons :: a -> Vec n a -> Vec ('S n) a

deriving instance (Show a) => Show (Vec n a)

type family Plus (m :: Nat') (n :: Nat') :: Nat' where
  Plus 'Z n = n
  Plus ('S m) n = 'S (Plus m n)

vhead :: Vec ('S n) a -> a
vhead (VCons x _) = x

vtail :: Vec ('S n) a -> Vec n a
vtail (VCons _ xs) = xs

vappend :: Vec m a -> Vec n a -> Vec (Plus m n) a
vappend VNil ys = ys
vappend (VCons x xs) ys = VCons x (vappend xs ys)

vzip :: Vec n a -> Vec n b -> Vec n (a, b)
vzip VNil VNil = VNil
vzip (VCons a as) (VCons b bs) = VCons (a, b) (vzip as bs)

vmap :: (a -> b) -> Vec n a -> Vec n b
vmap _ VNil = VNil
vmap f (VCons x xs) = VCons (f x) (vmap f xs)

vtoList :: Vec n a -> [a]
vtoList VNil = []
vtoList (VCons x xs) = x : vtoList xs

data SNat (n :: Nat') where
  SZ :: SNat 'Z
  SS :: SNat n -> SNat ('S n)

vreplicate :: SNat n -> a -> Vec n a
vreplicate SZ _ = VNil
vreplicate (SS n) x = VCons x (vreplicate n x)

class KnownNat' (n :: Nat') where natSing :: SNat n

instance KnownNat' 'Z where natSing = SZ

instance (KnownNat' n) => KnownNat' ('S n) where natSing = SS natSing

vpure :: forall n a. (KnownNat' n) => a -> Vec n a
vpure = vreplicate (natSing @n)

-- GHC.TypeLits: modular arithmetic with the modulus in the type.
newtype Zn (m :: Nat) = Zn Integer deriving (Eq)

instance (KnownNat m) => Show (Zn m) where
  show (Zn x) = show x ++ " mod " ++ show (natVal (Proxy :: Proxy m))

instance (KnownNat m) => Num (Zn m) where
  Zn a + Zn b = mkMod (a + b)
  Zn a - Zn b = mkMod (a - b)
  Zn a * Zn b = mkMod (a * b)
  abs = id
  signum _ = 1
  fromInteger = mkMod

mkMod :: forall m. (KnownNat m) => Integer -> Zn m
mkMod x = Zn (x `mod` natVal (Proxy :: Proxy m))

newtype Tagged (s :: Symbol) a = Tagged a

describeTagged :: forall s a. (KnownSymbol s, Show a) => Tagged s a -> String
describeTagged (Tagged a) = symbolVal (Proxy :: Proxy s) ++ "=" ++ show a

sizeOfArray :: forall (n :: Nat) (m :: Nat). (KnownNat n, KnownNat m, KnownNat (n * m)) => Integer
sizeOfArray = natVal (Proxy @(n * m)) + 0 * natVal (Proxy @n) * natVal (Proxy @m)

-- Phantom types: units and a door state machine.
data Meters

data Feet

newtype Distance u = Distance Double deriving (Show)

addD :: Distance u -> Distance u -> Distance u
addD (Distance a) (Distance b) = Distance (a + b)

feetToMeters :: Distance Feet -> Distance Meters
feetToMeters (Distance f) = Distance (f * 0.3048)

data DoorState = Opened | Closed | Locked

data Door (s :: DoorState) = Door String [String]

openDoor :: Door 'Closed -> Door 'Opened
openDoor (Door n l) = Door n ("open" : l)

closeDoor :: Door 'Opened -> Door 'Closed
closeDoor (Door n l) = Door n ("close" : l)

lockDoor :: Door 'Closed -> Door 'Locked
lockDoor (Door n l) = Door n ("lock" : l)

unlockDoor :: Door 'Locked -> Door 'Closed
unlockDoor (Door n l) = Door n ("unlock" : l)

history :: Door s -> String
history (Door n l) = n ++ ": " ++ unwords (reverse l)

-- Multi-parameter class with a functional dependency.
class Convert a b | a -> b where convert :: a -> b

instance Convert Bool Int where convert b = if b then 1 else 0

instance Convert Char String where convert c = [c, c]

instance (Convert a b) => Convert [a] [b] where convert = map convert

-- Recursion over a type-level list of symbols.
class ShowNames (ss :: [Symbol]) where names :: [String]

instance ShowNames '[] where names = []

instance (KnownSymbol s, ShowNames ss) => ShowNames (s ': ss) where
  names = symbolVal (Proxy @s) : names @ss

type family Length (xs :: [k]) :: Nat where
  Length '[] = 0
  Length (x ': xs) = 1 + Length xs

-- Deriving strategies.
newtype Score = Score Int deriving newtype (Show, Eq, Ord, Num)

newtype Total = Total Int
  deriving (Semigroup, Monoid) via (Sum Int)
  deriving stock (Show)

main :: IO ()
main = do
  putStrLn "-- type families"
  print (cfirst [3, 4, 5 :: Int], cfirst (Nothing :: Maybe Char), cfirst ('x', 'y'), ctoList (Just True), ctoList ((1, 2) :: (Int, Int)))
  print (widen (maxBound :: Int) + 1, widen (1.1 :: Float), widen True + widen False)
  let bag = foldr cinsert cempty [5, 3, 9, 1, 3 :: Int] :: IntBag
      cs = foldr cinsert cempty "mississippi" :: CharSet
  print (bag, drain bag, cs, drain cs)
  putStrLn "-- length-indexed vectors"
  let v3 = VCons 1 (VCons 2 (VCons (3 :: Int) VNil))
      v2 = VCons 10 (VCons (20 :: Int) VNil)
      v5 = vappend v3 v2
  print v5
  print (vhead v5, vhead (vtail (vtail v5)), vtoList (vzip v3 (vmap show v3)), sum (vtoList v5))
  print (vtoList (vreplicate (SS (SS (SS SZ))) 'r'), vtoList (vpure 7 :: Vec ('S ('S ('S ('S 'Z)))) Int))
  print (vtoList (vzip (vpure 'a' :: Vec ('S ('S 'Z)) Char) (vmap (* 2) (vtail v3))))
  putStrLn "-- type-level naturals and symbols"
  print (natVal (Proxy :: Proxy 42), natVal (Proxy @(2 ^ 70)), natVal (Proxy @(10 - 3)), symbolVal (Proxy :: Proxy "hello"))
  let a = 5 :: Zn 7
      b = 4 :: Zn 7
  print (a + b, a * b, a - b, a * a * a, sum (map fromInteger [1 .. 100]) :: Zn 13)
  print (product (replicate 100 (3 :: Zn 1000000007)), (2 :: Zn 1000000007) ^ (64 :: Int), fromInteger (-1) :: Zn 10)
  putStrLn (describeTagged (Tagged 3.5 :: Tagged "width" Double) ++ ", " ++ describeTagged (Tagged "x" :: Tagged "label" String))
  print (sizeOfArray @3 @4, sizeOfArray @100 @100, natVal (Proxy @(Length '[Int, Bool, Char])), natVal (Proxy @(Length '[])))
  print (names @'["alpha", "beta", "gamma"], length (names @'[]))
  putStrLn "-- phantom types"
  print (addD (Distance 3) (Distance 4.5) :: Distance Meters, feetToMeters (addD (Distance 10) (Distance 2)))
  let door = Door "front" [] :: Door 'Closed
  putStrLn (history (lockDoor (closeDoor (openDoor (unlockDoor (lockDoor door))))))
  putStrLn (history (openDoor (closeDoor (openDoor door))))
  putStrLn "-- functional dependencies and deriving"
  print (convert True, convert 'z', convert [True, False, True], convert "ab", sum (convert [True, True]))
  print (Score 3 + Score 4, maximum [Score 2, Score 9, Score 5], Score 7 == 7)
  print (mconcat [Total 1, Total 2, Total 39], mempty :: Total)
  putStrLn "-- Typeable"
  print (typeOf (1 :: Int), typeOf "s", typeOf (Just [True]), typeOf (Proxy :: Proxy 3), typeOf (undefined :: Int -> Maybe Char))
  print (typeRep (Proxy :: Proxy (Either String (Zn 7))), typeOf (Score 1), typeOf v2)
  putStrLn (intercalate ", " [show (typeOf x) | x <- [Left 1, Right 'c' :: Either Int Char]])
  print (showsTypeOf (3.5 :: Double) "", showsTypeOf (Tagged 'c' :: Tagged "t" Char) "")
  where
    showsTypeOf :: (Typeable t) => t -> ShowS
    showsTypeOf x = shows (typeOf x)
