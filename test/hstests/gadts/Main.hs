{-# LANGUAGE ExistentialQuantification #-}
{-# LANGUAGE GADTs #-}
{-# LANGUAGE KindSignatures #-}
{-# LANGUAGE RankNTypes #-}
{-# LANGUAGE ScopedTypeVariables #-}
-- GADTs, existential types and rank-N polymorphism: a typed expression
-- language whose evaluator returns the type the constructor promised, a type
-- checker that turns untyped syntax into that GADT behind an existential
-- (with a type-equality witness), a typed stack machine the compiler targets,
-- heterogeneous lists packed with their class dictionaries, and functions
-- that take polymorphic functions as arguments.
--
-- Type refinement is erased: at run time a GADT is a sum type, an
-- existential is a constructor holding a dictionary pointer next to its
-- value, and a rank-N argument is a function that takes the dictionary as its
-- first argument.  What warmc sees is the dispatch.
module Main (main) where

import Data.Kind (Type)
import Data.List (intercalate)

data Expr :: Type -> Type where
  IntE :: Int -> Expr Int
  BoolE :: Bool -> Expr Bool
  StrE :: String -> Expr String
  Add :: Expr Int -> Expr Int -> Expr Int
  Mul :: Expr Int -> Expr Int -> Expr Int
  Neg :: Expr Int -> Expr Int
  Eq :: Expr Int -> Expr Int -> Expr Bool
  Lt :: Expr Int -> Expr Int -> Expr Bool
  And :: Expr Bool -> Expr Bool -> Expr Bool
  Not :: Expr Bool -> Expr Bool
  If :: Expr Bool -> Expr a -> Expr a -> Expr a
  Len :: Expr String -> Expr Int
  Cat :: Expr String -> Expr String -> Expr String
  ShowI :: Expr Int -> Expr String
  Pair :: Expr a -> Expr b -> Expr (a, b)
  Fst :: Expr (a, b) -> Expr a
  Snd :: Expr (a, b) -> Expr b

eval :: Expr a -> a
eval (IntE n) = n
eval (BoolE b) = b
eval (StrE s) = s
eval (Add a b) = eval a + eval b
eval (Mul a b) = eval a * eval b
eval (Neg a) = negate (eval a)
eval (Eq a b) = eval a == eval b
eval (Lt a b) = eval a < eval b
eval (And a b) = eval a && eval b
eval (Not a) = not (eval a)
eval (If c t e) = if eval c then eval t else eval e
eval (Len s) = length (eval s)
eval (Cat a b) = eval a ++ eval b
eval (ShowI a) = show (eval a)
eval (Pair a b) = (eval a, eval b)
eval (Fst p) = fst (eval p)
eval (Snd p) = snd (eval p)

render :: Expr a -> String
render e = case e of
  IntE n -> show n
  BoolE b -> show b
  StrE s -> show s
  Add a b -> bin "+" a b
  Mul a b -> bin "*" a b
  Neg a -> "-" ++ render a
  Eq a b -> bin "==" a b
  Lt a b -> bin "<" a b
  And a b -> bin "&&" a b
  Not a -> "!" ++ render a
  If c t f -> "(if " ++ render c ++ " then " ++ render t ++ " else " ++ render f ++ ")"
  Len s -> "len(" ++ render s ++ ")"
  Cat a b -> bin "++" a b
  ShowI a -> "show(" ++ render a ++ ")"
  Pair a b -> "<" ++ render a ++ ", " ++ render b ++ ">"
  Fst p -> "fst " ++ render p
  Snd p -> "snd " ++ render p
  where
    bin :: String -> Expr x -> Expr y -> String
    bin op a b = "(" ++ render a ++ " " ++ op ++ " " ++ render b ++ ")"

size :: Expr a -> Int
size e = case e of
  IntE _ -> 1
  BoolE _ -> 1
  StrE _ -> 1
  Add a b -> 1 + size a + size b
  Mul a b -> 1 + size a + size b
  Neg a -> 1 + size a
  Eq a b -> 1 + size a + size b
  Lt a b -> 1 + size a + size b
  And a b -> 1 + size a + size b
  Not a -> 1 + size a
  If c t f -> 1 + size c + size t + size f
  Len s -> 1 + size s
  Cat a b -> 1 + size a + size b
  ShowI a -> 1 + size a
  Pair a b -> 1 + size a + size b
  Fst p -> 1 + size p
  Snd p -> 1 + size p

-- Constant folding that keeps the types.
optimize :: Expr a -> Expr a
optimize e = case e of
  Add a b -> case (optimize a, optimize b) of
    (IntE 0, b') -> b'
    (a', IntE 0) -> a'
    (IntE x, IntE y) -> IntE (x + y)
    (a', b') -> Add a' b'
  Mul a b -> case (optimize a, optimize b) of
    (IntE 1, b') -> b'
    (IntE x, IntE y) -> IntE (x * y)
    (a', b') -> Mul a' b'
  Not a -> case optimize a of
    BoolE b -> BoolE (not b)
    Not a' -> a'
    a' -> Not a'
  If c t f -> case optimize c of
    BoolE True -> optimize t
    BoolE False -> optimize f
    c' -> If c' (optimize t) (optimize f)
  Lt a b -> Lt (optimize a) (optimize b)
  Eq a b -> Eq (optimize a) (optimize b)
  Fst (Pair a _) -> optimize a
  Snd (Pair _ b) -> optimize b
  _ -> e

-- Untyped syntax, type-checked into the GADT.
data UExpr
  = UInt Int
  | UBool Bool
  | UStr String
  | UAdd UExpr UExpr
  | UEq UExpr UExpr
  | UIf UExpr UExpr UExpr
  | ULen UExpr
  | UCat UExpr UExpr
  | UNot UExpr
  deriving (Show)

data Ty a where
  TInt :: Ty Int
  TBool :: Ty Bool
  TStr :: Ty String

data Equal a b where
  Refl :: Equal a a

tyEq :: Ty a -> Ty b -> Maybe (Equal a b)
tyEq TInt TInt = Just Refl
tyEq TBool TBool = Just Refl
tyEq TStr TStr = Just Refl
tyEq _ _ = Nothing

showTy :: Ty a -> String
showTy TInt = "Int"
showTy TBool = "Bool"
showTy TStr = "String"

data Typed = forall a. Typed (Ty a) (Expr a)

expect :: Ty a -> UExpr -> Either String (Expr a)
expect ty u = do
  Typed ty' e <- check u
  case tyEq ty ty' of
    Just Refl -> Right e
    Nothing -> Left ("expected " ++ showTy ty ++ " but got " ++ showTy ty' ++ " in " ++ show u)

check :: UExpr -> Either String Typed
check u = case u of
  UInt n -> Right (Typed TInt (IntE n))
  UBool b -> Right (Typed TBool (BoolE b))
  UStr s -> Right (Typed TStr (StrE s))
  UAdd a b -> Typed TInt <$> (Add <$> expect TInt a <*> expect TInt b)
  UEq a b -> Typed TBool <$> (Eq <$> expect TInt a <*> expect TInt b)
  ULen s -> Typed TInt . Len <$> expect TStr s
  UCat a b -> Typed TStr <$> (Cat <$> expect TStr a <*> expect TStr b)
  UNot a -> Typed TBool . Not <$> expect TBool a
  UIf c t f -> do
    c' <- expect TBool c
    Typed tt t' <- check t
    f' <- expect tt f
    return (Typed tt (If c' t' f'))

runTyped :: Typed -> String
runTyped (Typed TInt e) = "Int " ++ show (eval e)
runTyped (Typed TBool e) = "Bool " ++ show (eval e)
runTyped (Typed TStr e) = "String " ++ show (eval e)

-- A typed stack machine: the instruction type records the stack shape.
data Instr :: Type -> Type -> Type where
  Push :: Int -> Instr s (Int, s)
  IAdd :: Instr (Int, (Int, s)) (Int, s)
  IMul :: Instr (Int, (Int, s)) (Int, s)
  Dup :: Instr (a, s) (a, (a, s))
  Swap :: Instr (a, (b, s)) (b, (a, s))
  Seq :: Instr a b -> Instr b c -> Instr a c

exec :: Instr a b -> a -> b
exec (Push n) s = (n, s)
exec IAdd (a, (b, s)) = (a + b, s)
exec IMul (a, (b, s)) = (a * b, s)
exec Dup (a, s) = (a, (a, s))
exec Swap (a, (b, s)) = (b, (a, s))
exec (Seq i j) s = exec j (exec i s)

listing :: Instr a b -> [String]
listing (Push n) = ["push " ++ show n]
listing IAdd = ["add"]
listing IMul = ["mul"]
listing Dup = ["dup"]
listing Swap = ["swap"]
listing (Seq a b) = listing a ++ listing b

data Arith = Lit Int | Plus Arith Arith | Times Arith Arith | Square Arith

compileA :: Arith -> Instr s (Int, s)
compileA (Lit n) = Push n
compileA (Plus a b) = compileA a `Seq` compileA b `Seq` IAdd
compileA (Times a b) = compileA a `Seq` compileA b `Seq` IMul
compileA (Square a) = compileA a `Seq` Dup `Seq` IMul

evalA :: Arith -> Int
evalA (Lit n) = n
evalA (Plus a b) = evalA a + evalA b
evalA (Times a b) = evalA a * evalA b
evalA (Square a) = evalA a * evalA a

-- Heterogeneous lists through existentials with dictionaries.
data Showable = forall a. (Show a) => MkShowable a

instance Show Showable where show (MkShowable a) = show a

data Obj = forall s. Obj s (s -> s) (s -> String)

runObj :: Int -> Obj -> [String]
runObj n (Obj s step out) = map out (take n (iterate step s))

data Some f = forall a. (Show a, Ord a) => Some (f a)

-- Rank-N: arguments that must work at every type.
applyBoth :: (forall x. [x] -> [x]) -> ([Int], String) -> ([Int], String)
applyBoth f (a, b) = (f a, f b)

withLengths :: (forall x. Show x => x -> String) -> (Int, Bool, [Double]) -> [String]
withLengths f (a, b, c) = [f a, f b, f c]

newtype NatTrans f g = NatTrans (forall a. f a -> g a)

runNT :: NatTrans f g -> f a -> g a
runNT (NatTrans h) = h

-- Church-encoded lists and numerals.
newtype Church = Church (forall a. (a -> a) -> a -> a)

toInt :: Church -> Int
toInt (Church n) = n (+ 1) 0

csucc :: Church -> Church
csucc (Church n) = Church (\f x -> f (n f x))

cadd, cmul :: Church -> Church -> Church
cadd (Church m) (Church n) = Church (\f x -> m f (n f x))
cmul (Church m) (Church n) = Church (\f -> m (n f))

czero :: Church
czero = Church (\_ x -> x)

newtype CList a = CList (forall r. (a -> r -> r) -> r -> r)

fromL :: [a] -> CList a
fromL xs = CList (\c n -> foldr c n xs)

toL :: CList a -> [a]
toL (CList f) = f (:) []

cmap :: (a -> b) -> CList a -> CList b
cmap g (CList f) = CList (\c n -> f (c . g) n)

cappend :: CList a -> CList a -> CList a
cappend (CList f) (CList g) = CList (\c n -> f c (g c n))

main :: IO ()
main = do
  putStrLn "-- typed evaluation"
  let e1 = If (Lt (IntE 3) (Add (IntE 1) (IntE 1))) (StrE "small") (Cat (StrE "big:") (ShowI (Mul (IntE 6) (IntE 7))))
      e2 = Pair (Len e1) (And (Not (BoolE False)) (Eq (Neg (IntE 4)) (Add (IntE (-6)) (IntE 2))))
      e3 = Add (Fst e2) (If (Snd e2) (IntE 100) (IntE 0))
      e4 = If (Not (Not (BoolE True))) (Add (IntE 0) (Mul (IntE 1) (Add (IntE 2) (IntE 3)))) (IntE 9)
  putStrLn (render e1 ++ " => " ++ eval e1)
  putStrLn (render e2 ++ " => " ++ show (eval e2))
  putStrLn (render e3 ++ " => " ++ show (eval e3))
  print (map size [e3, optimize e3], size e1, size e2)
  putStrLn (render (optimize e4) ++ " => " ++ show (eval (optimize e4)) ++ " == " ++ show (eval e4))
  putStrLn (render (optimize (Fst (Pair (Add (IntE 2) (IntE 0)) (StrE "x")))))
  putStrLn "-- type checking untyped syntax"
  let progs =
        [ UAdd (UInt 2) (UInt 40),
          UIf (UEq (UInt 1) (UInt 1)) (UStr "yes") (UStr "no"),
          UIf (UNot (UBool True)) (UInt 1) (ULen (UCat (UStr "abc") (UStr "de"))),
          UAdd (UInt 1) (UBool True),
          UIf (UInt 0) (UInt 1) (UInt 2),
          UIf (UBool True) (UInt 1) (UStr "one"),
          ULen (UInt 5),
          UNot (UEq (ULen (UStr "four")) (UAdd (UInt 2) (UInt 2)))
        ]
  mapM_ (\p -> putStrLn (either ("type error: " ++) runTyped (check p))) progs
  putStrLn "-- typed stack machine"
  let arith = [Plus (Lit 2) (Lit 3), Times (Plus (Lit 1) (Lit 2)) (Square (Lit 4)), Square (Square (Plus (Lit 1) (Lit 1))), Plus (Times (Lit 7) (Lit 6)) (Square (Lit (-3)))]
  mapM_ (\a -> let code = compileA a :: Instr () (Int, ()) in putStrLn (intercalate "; " (listing code) ++ " => " ++ show (fst (exec code ())) ++ " / " ++ show (evalA a))) arith
  print (exec (Push 1 `Seq` Push 2 `Seq` Swap `Seq` Dup) ())
  putStrLn "-- existentials"
  let hs = [MkShowable (1 :: Int), MkShowable "two", MkShowable (Just 3.5 :: Maybe Double), MkShowable [True, False], MkShowable ('c', ())]
  print hs
  print (map show hs)
  let objs = [Obj (0 :: Int) (+ 3) show, Obj "a" ('b' :) reverse, Obj (1 :: Integer, 1 :: Integer) (\(a, b) -> (b, a + b)) (show . fst), Obj [1 :: Int] (\l -> sum l : l) (show . length)]
  mapM_ (print . runObj 6) objs
  let somesM = [Some (Just (3 :: Int)), Some (Nothing :: Maybe Bool), Some (Just 'q')]
      somesL = [Some [5, 1, 4 :: Int], Some "zyx", Some ([] :: [()]), Some [LT, GT, EQ]]
  mapM_ (\(Some f) -> putStrLn (show (length f) ++ " " ++ show (maximum' f))) somesM
  mapM_ (\(Some f) -> putStrLn (show (length f) ++ " " ++ show (maximum' f) ++ " " ++ show (reverse f))) somesL
  putStrLn "-- rank-N"
  print (applyBoth reverse ([1, 2, 3], "abc"), applyBoth (take 2) ([9, 8, 7], "xyz"), applyBoth (concat . replicate 2) ([1], "o"))
  print (withLengths show (5, True, [1.5]), withLengths (\v -> show (length (show v))) (12345, False, [0.25, 2]))
  let nt = NatTrans (\m -> case m of Nothing -> []; Just v -> [v, v])
  print (runNT nt (Just 'k'), runNT nt (Nothing :: Maybe Int), runNT (NatTrans (take 1)) "hello")
  let three = csucc (csucc (csucc czero))
      five = cadd three (csucc (csucc czero))
  print (map toInt [czero, three, five, cmul three five, cmul five (cmul five five), cadd (cmul three three) five])
  print (toL (cmap (* 2) (fromL [1, 2, 3 :: Int])), toL (cappend (fromL "ab") (cmap succ (fromL "abc"))))
  where
    maximum' :: (Foldable t, Ord a) => t a -> Maybe a
    maximum' f = if null f then Nothing else Just (maximum f)
