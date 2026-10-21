-- Parser combinators: generate arithmetic expressions as strings, parse them
-- with parsec into a syntax tree, and evaluate the tree.
--
-- The program shape Haskell is known for: a parser is a function in a
-- continuation-passing monad, and running one is a long chain of closures
-- built and entered for every character -- higher-order, allocation-heavy code
-- whose calls are almost all to functions unknown at the call site.
module Main (main) where

import Data.Bits (shiftL, shiftR, xor, (.&.))
import Data.List (foldl')
import Text.Parsec
import Text.Parsec.String (Parser)

data Expr = Num !Int | Add Expr Expr | Sub Expr Expr | Mul Expr Expr

eval :: Expr -> Int
eval (Num n) = n
eval (Add a b) = (eval a + eval b) .&. 0xffffff
eval (Sub a b) = (eval a - eval b) .&. 0xffffff
eval (Mul a b) = (eval a * eval b) .&. 0xffffff

expr :: Parser Expr
expr = term `chainl1` (Add <$ char '+' <|> Sub <$ char '-')

term :: Parser Expr
term = factor `chainl1` (Mul <$ char '*')

factor :: Parser Expr
factor = Num . read <$> many1 digit <|> between (char '(') (char ')') expr

gen :: Int -> Int -> (String, Int)
gen d x | d <= 0 = (show (x .&. 1023), x)
gen d x0 =
  let x1 = x0 `xor` ((x0 `shiftL` 13) .&. 0xffffffff)
      x2 = x1 `xor` (x1 `shiftR` 17)
      x = x2 `xor` ((x2 `shiftL` 5) .&. 0xffffffff)
      (l, xl) = gen (d - 1 - x .&. 1) x
      (r, xr) = gen (d - 1 - (x `shiftR` 1) .&. 1) xl
      op = "+-*" !! (x `mod` 3)
      s = if x .&. 4 /= 0 then "(" ++ l ++ [op] ++ r ++ ")" else l ++ [op] ++ r
   in (s, xr)

main :: IO ()
main = do
  let one r = case parse (expr <* eof) "" (fst (gen 12 (r * 7919 + 1))) of
        Right e -> eval e
        Left _ -> -1
  print (foldl' (\a r -> (a * 31 + one r) .&. 0xfffffff) 0 [1 .. 60])
