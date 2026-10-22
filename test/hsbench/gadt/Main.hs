{-# LANGUAGE BangPatterns #-}
{-# LANGUAGE GADTs #-}
{-# LANGUAGE KindSignatures #-}
-- A typed expression language as a GADT: random well-typed programs over Int,
-- Bool and pairs are generated, simplified by a constant folder whose
-- pattern matches refine the result type, and evaluated by a tagless
-- interpreter; a recursive function written in the same language with
-- higher-order abstract syntax and a fixpoint runs alongside.
--
-- At run time a GADT is an ordinary sum type: the type refinement is checked
-- and erased by GHC.  With sixteen constructors and two tag bits in a 32-bit
-- pointer, eval's case knows the first two constructors from the pointer tag
-- and finds the rest by loading the constructor tag from the info table and a
-- br_table.  With higher-order abstract syntax a Lam holds a Haskell function,
-- so evaluating an App is a call to an unknown function through GHC's generic
-- apply, stg_ap_p_fast.  An interpreter's dispatch: the shape of the parsec
-- row's evaluator, with sixteen node kinds rather than four.
module Main (main) where

import Data.Bits (shiftL, shiftR, xor, (.&.))
import Data.Kind (Type)
import Data.List (foldl')
import Data.Word (Word32)

data Expr :: Type -> Type where
  IntE :: !Int -> Expr Int
  BoolE :: !Bool -> Expr Bool
  Add :: Expr Int -> Expr Int -> Expr Int
  Sub :: Expr Int -> Expr Int -> Expr Int
  Mul :: Expr Int -> Expr Int -> Expr Int
  Leq :: Expr Int -> Expr Int -> Expr Bool
  Not :: Expr Bool -> Expr Bool
  And :: Expr Bool -> Expr Bool -> Expr Bool
  If :: Expr Bool -> Expr a -> Expr a -> Expr a
  Pair :: Expr a -> Expr b -> Expr (a, b)
  Fst :: Expr (a, b) -> Expr a
  Snd :: Expr (a, b) -> Expr b
  Lift :: a -> Expr a
  Lam :: (Expr a -> Expr b) -> Expr (a -> b)
  App :: Expr (a -> b) -> Expr a -> Expr b
  Fix :: (Expr a -> Expr a) -> Expr a

eval :: Expr a -> a
eval (IntE n) = n
eval (BoolE b) = b
eval (Add a b) = eval a + eval b
eval (Sub a b) = eval a - eval b
eval (Mul a b) = eval a * eval b
eval (Leq a b) = eval a <= eval b
eval (Not a) = not (eval a)
eval (And a b) = eval a && eval b
eval (If c t e) = if eval c then eval t else eval e
eval (Pair a b) = (eval a, eval b)
eval (Fst p) = fst (eval p)
eval (Snd p) = snd (eval p)
eval (Lift v) = v
eval (Lam f) = \v -> eval (f (Lift v))
eval (App f a) = eval f (eval a)
eval (Fix f) = let r = eval (f (Lift r)) in r

simplify :: Expr a -> Expr a
simplify e = case e of
  Add a b -> arith (+) Add (simplify a) (simplify b)
  Sub a b -> arith (-) Sub (simplify a) (simplify b)
  Mul a b -> arith (*) Mul (simplify a) (simplify b)
  Leq a b -> case (simplify a, simplify b) of
    (IntE x, IntE y) -> BoolE (x <= y)
    (a', b') -> Leq a' b'
  Not a -> case simplify a of
    BoolE x -> BoolE (not x)
    Not a' -> a'
    a' -> Not a'
  And a b -> case simplify a of
    BoolE False -> BoolE False
    BoolE True -> simplify b
    a' -> And a' (simplify b)
  If c t f -> case simplify c of
    BoolE True -> simplify t
    BoolE False -> simplify f
    c' -> If c' (simplify t) (simplify f)
  Fst p -> case simplify p of
    Pair a _ -> a
    p' -> Fst p'
  Snd p -> case simplify p of
    Pair _ b -> b
    p' -> Snd p'
  Pair a b -> Pair (simplify a) (simplify b)
  _ -> e
  where
    arith :: (Int -> Int -> Int) -> (Expr Int -> Expr Int -> Expr Int) -> Expr Int -> Expr Int -> Expr Int
    arith op _ (IntE x) (IntE y) = IntE (op x y)
    arith _ k a b = k a b

next :: Word32 -> Word32
next x0 = x3
  where
    x1 = x0 `xor` (x0 `shiftL` 13)
    x2 = x1 `xor` (x1 `shiftR` 17)
    x3 = x2 `xor` (x2 `shiftL` 5)

genInt :: Int -> Word32 -> (Expr Int, Word32)
genInt d x0
  | d <= 0 = (IntE (fromIntegral (x .&. 255)), x)
  | otherwise = case x .&. 7 of
      0 -> bin Add
      1 -> bin Sub
      2 -> bin Mul
      3 ->
        let (c, x1) = genBool (d - 1) x
            (t, x2) = genInt (d - 1) x1
            (f, x3) = genInt (d - 1) x2
         in (If c t f, x3)
      4 ->
        let (a, x1) = genInt (d - 1) x
            (b, x2) = genBool (d - 1) x1
         in (Fst (Pair a b), x2)
      5 ->
        let (a, x1) = genInt (d - 2) x
            (b, x2) = genInt (d - 1) x1
         in (Snd (Pair (Leq a b) b), x2)
      _ -> (IntE (fromIntegral (x `shiftR` 20)), x)
  where
    x = next x0
    bin k =
      let (a, x1) = genInt (d - 1) x
          (b, x2) = genInt (d - 1) x1
       in (k a b, x2)

genBool :: Int -> Word32 -> (Expr Bool, Word32)
genBool d x0
  | d <= 0 = (BoolE (odd x), x)
  | otherwise = case x .&. 3 of
      0 ->
        let (a, x1) = genInt (d - 1) x
            (b, x2) = genInt (d - 1) x1
         in (Leq a b, x2)
      1 -> let (a, x1) = genBool (d - 1) x in (Not a, x1)
      2 ->
        let (a, x1) = genBool (d - 1) x
            (b, x2) = genBool (d - 1) x1
         in (And a b, x2)
      _ -> (BoolE (even x), x)
  where
    x = next x0

-- the number of Collatz steps from n to 1, as a program in the language
collatz :: Expr (Int -> Int)
collatz =
  Fix $ \self -> Lam $ \n ->
    If (Leq n (IntE 1)) (IntE 0) $
      Add (IntE 1) (App self (If (isEven n) (half n) (Add (Mul (IntE 3) n) (IntE 1))))
  where
    isEven n = Leq (Sub n (Mul (IntE 2) (half n))) (IntE 0)
    half = App (Lam (\n -> Lift (eval n `shiftR` 1)))

main :: IO ()
main = do
  let one (!acc, !x) _ =
        let (e, x') = genInt 12 x
            v = eval (simplify e) + eval e
         in ((acc * 31 + v) .&. 0xfffffff, x')
      (h, _) = foldl' one (0, 2463534242) [1 .. 640 :: Int]
      c = foldl' (\a n -> a + eval collatz n) 0 [1 .. 5000]
  print (h, c)
