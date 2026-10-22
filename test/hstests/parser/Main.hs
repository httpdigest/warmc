{-# OPTIONS_GHC -Wno-x-partial #-}
{-# LANGUAGE LambdaCase #-}
-- Parser combinators written from scratch (a newtype over String -> Either
-- error (a, rest) with Functor/Applicative/Monad/Alternative instances,
-- many/some/sepBy/chainl1/between/option, error positions), used for three
-- languages: arithmetic expressions with precedence and associativity
-- (printed back fully parenthesised and evaluated, with errors reported),
-- JSON (parsed, pretty-printed, queried), and a small imperative language
-- with variables, while loops, if/else, functions with recursion, and
-- print statements, run by a tree-walking interpreter whose environment is
-- a Data.Map and whose output is collected and printed.
--
-- All closures and continuations: every combinator builds a function, so
-- parsing is a chain of unknown calls through GHC's apply code, and the
-- interpreter dispatches on a dozen AST constructors.
module Main (main) where

import Control.Applicative
import Data.Char (isAlpha, isAlphaNum, isDigit, isSpace)
import Data.List (intercalate)
import qualified Data.Map.Strict as M

newtype Parser a = Parser {runParser :: String -> Either (Int, String) (a, String)}

instance Functor Parser where
  fmap f (Parser p) = Parser $ \s -> case p s of
    Left e -> Left e
    Right (a, rest) -> Right (f a, rest)

instance Applicative Parser where
  pure a = Parser $ \s -> Right (a, s)
  Parser pf <*> Parser pa = Parser $ \s -> case pf s of
    Left e -> Left e
    Right (f, rest) -> case pa rest of
      Left e -> Left e
      Right (a, rest') -> Right (f a, rest')

instance Monad Parser where
  Parser p >>= k = Parser $ \s -> case p s of
    Left e -> Left e
    Right (a, rest) -> runParser (k a) rest

instance Alternative Parser where
  empty = Parser $ \s -> Left (length s, "empty")
  Parser p <|> Parser q = Parser $ \s -> case p s of
    Left (n1, e1) -> case q s of
      Left (n2, e2) -> Left (if n1 <= n2 then (n1, e1) else (n2, e2))
      r -> r
    r -> r

failP :: String -> Parser a
failP msg = Parser $ \s -> Left (length s, msg)

satisfy :: String -> (Char -> Bool) -> Parser Char
satisfy what p = Parser $ \case
  (c : cs) | p c -> Right (c, cs)
  s@(c : _) -> Left (length s, "expected " ++ what ++ " but found " ++ show c)
  [] -> Left (0, "expected " ++ what ++ " but found end of input")

spaces :: Parser ()
spaces = () <$ many (satisfy "space" isSpace)

lexeme :: Parser a -> Parser a
lexeme p = p <* spaces

symbol :: String -> Parser String
symbol str = lexeme (traverse (\c -> satisfy (show str) (== c)) str)

eof :: Parser ()
eof = Parser $ \case
  [] -> Right ((), [])
  s -> Left (length s, "expected end of input but found " ++ show (take 8 s))

sepBy, sepBy1 :: Parser a -> Parser b -> Parser [a]
sepBy p sep = sepBy1 p sep <|> pure []
sepBy1 p sep = (:) <$> p <*> many (sep *> p)

chainl1 :: Parser a -> Parser (a -> a -> a) -> Parser a
chainl1 p op = p >>= rest
  where
    rest a = (do f <- op; b <- p; rest (f a b)) <|> pure a

chainr1 :: Parser a -> Parser (a -> a -> a) -> Parser a
chainr1 p op = do
  a <- p
  (do f <- op; b <- chainr1 p op; return (f a b)) <|> pure a

between :: Parser o -> Parser c -> Parser a -> Parser a
between o c p = o *> p <* c

parseAll :: Parser a -> String -> Either String a
parseAll p input = case runParser (spaces *> p <* eof) input of
  Right (a, _) -> Right a
  Left (remaining, msg) -> Left ("at column " ++ show (length input - remaining + 1) ++ ": " ++ msg)

-- Arithmetic.
data Expr
  = Num Integer
  | Var String
  | BinOp Char Expr Expr
  | Neg Expr
  | Call String [Expr]
  deriving (Show)

number :: Parser Integer
number = lexeme (read <$> some (satisfy "digit" isDigit))

identifier :: Parser String
identifier = lexeme ((:) <$> satisfy "letter" isAlpha <*> many (satisfy "letter or digit" isAlphaNum))

keyword :: String -> Parser ()
keyword k = lexeme $ do
  w <- (:) <$> satisfy "letter" isAlpha <*> many (satisfy "letter or digit" isAlphaNum)
  if w == k then return () else failP ("expected keyword " ++ k ++ " but found " ++ w)

expr :: Parser Expr
expr = cmpE
  where
    cmpE = chainl1 addE (binop "<" '<' <|> binop "==" '=' <|> binop ">" '>')
    addE = chainl1 mulE (binop "+" '+' <|> binop "-" '-')
    mulE = chainl1 powE (binop "*" '*' <|> binop "/" '/' <|> binop "%" '%')
    powE = chainr1 unary (binop "^" '^')
    unary = (Neg <$> (symbol "-" *> unary)) <|> atom
    atom =
      (Num <$> number)
        <|> callOrVar
        <|> between (symbol "(") (symbol ")") expr
    callOrVar = do
      name <- identifier
      (Call name <$> between (symbol "(") (symbol ")") (sepBy expr (symbol ","))) <|> pure (Var name)
    binop s c = BinOp c <$ symbol s

render :: Expr -> String
render (Num n) = show n
render (Var v) = v
render (Neg e) = "(-" ++ render e ++ ")"
render (BinOp c a b) = "(" ++ render a ++ " " ++ opName c ++ " " ++ render b ++ ")"
render (Call f as) = f ++ "(" ++ intercalate ", " (map render as) ++ ")"

opName :: Char -> String
opName '=' = "=="
opName c = [c]

evalPure :: Expr -> Either String Integer
evalPure (Num n) = Right n
evalPure (Var v) = Left ("unbound variable " ++ v)
evalPure (Neg e) = negate <$> evalPure e
evalPure (Call f _) = Left ("no functions here: " ++ f)
evalPure (BinOp c a b) = do
  x <- evalPure a
  y <- evalPure b
  arith c x y

arith :: Char -> Integer -> Integer -> Either String Integer
arith c x y = case c of
  '+' -> Right (x + y)
  '-' -> Right (x - y)
  '*' -> Right (x * y)
  '/' -> if y == 0 then Left "division by zero" else Right (x `div` y)
  '%' -> if y == 0 then Left "modulo by zero" else Right (x `mod` y)
  '^' -> if y < 0 then Left "negative exponent" else Right (x ^ y)
  '<' -> Right (if x < y then 1 else 0)
  '>' -> Right (if x > y then 1 else 0)
  '=' -> Right (if x == y then 1 else 0)
  _ -> Left ("bad operator " ++ [c])

-- JSON.
data JSON = JNull | JBool Bool | JNum Integer | JStr String | JArr [JSON] | JObj [(String, JSON)]
  deriving (Show, Eq)

json :: Parser JSON
json =
  (JNull <$ symbol "null")
    <|> (JBool True <$ symbol "true")
    <|> (JBool False <$ symbol "false")
    <|> (JNum <$> signed)
    <|> (JStr <$> str)
    <|> (JArr <$> between (symbol "[") (symbol "]") (sepBy json (symbol ",")))
    <|> (JObj <$> between (symbol "{") (symbol "}") (sepBy pair (symbol ",")))
  where
    signed = (negate <$> (symbol "-" *> number)) <|> number
    pair = (,) <$> str <* symbol ":" <*> json
    str = lexeme (between (satisfy "quote" (== '"')) (satisfy "closing quote" (== '"')) (many strChar))
    strChar = (satisfy "escape" (== '\\') *> (escape <$> satisfy "escaped char" (`elem` "\"\\nt"))) <|> satisfy "string char" (\c -> c /= '"' && c /= '\\')
    escape 'n' = '\n'
    escape 't' = '\t'
    escape c = c

pretty :: Int -> JSON -> String
pretty ind v = case v of
  JNull -> "null"
  JBool b -> if b then "true" else "false"
  JNum n -> show n
  JStr s -> show s
  JArr [] -> "[]"
  JArr xs -> "[\n" ++ intercalate ",\n" [pad (ind + 2) ++ pretty (ind + 2) x | x <- xs] ++ "\n" ++ pad ind ++ "]"
  JObj [] -> "{}"
  JObj kvs -> "{\n" ++ intercalate ",\n" [pad (ind + 2) ++ show k ++ ": " ++ pretty (ind + 2) x | (k, x) <- kvs] ++ "\n" ++ pad ind ++ "}"
  where
    pad n = replicate n ' '

query :: [String] -> JSON -> Maybe JSON
query [] v = Just v
query (k : ks) (JObj kvs) = lookup k kvs >>= query ks
query (k : ks) (JArr xs) | all isDigit k, let i = read k, i < length xs = query ks (xs !! i)
query _ _ = Nothing

-- A small imperative language.
data Stmt
  = Assign String Expr
  | Print Expr
  | If Expr [Stmt] [Stmt]
  | While Expr [Stmt]
  | Def String [String] [Stmt]
  | Return Expr
  deriving (Show)

block :: Parser [Stmt]
block = between (symbol "{") (symbol "}") (many stmt)

stmt :: Parser Stmt
stmt =
  (Print <$> (keyword "print" *> expr <* symbol ";"))
    <|> (If <$> (keyword "if" *> expr) <*> block <*> ((keyword "else" *> block) <|> pure []))
    <|> (While <$> (keyword "while" *> expr) <*> block)
    <|> (Def <$> (keyword "def" *> identifier) <*> between (symbol "(") (symbol ")") (sepBy identifier (symbol ",")) <*> block)
    <|> (Return <$> (keyword "return" *> expr <* symbol ";"))
    <|> (Assign <$> identifier <* symbol "=" <*> expr <* symbol ";")

data Ctx = Ctx {vars :: M.Map String Integer, funs :: M.Map String ([String], [Stmt]), out :: [String], steps :: Int}

data Flow = Next | Returned Integer

exec :: [Stmt] -> Ctx -> Either String (Flow, Ctx)
exec [] ctx = Right (Next, ctx)
exec (s : rest) ctx
  | steps ctx > 50000 = Left "step limit exceeded"
  | otherwise = do
      (flow, ctx') <- execOne s ctx {steps = steps ctx + 1}
      case flow of
        Returned v -> Right (Returned v, ctx')
        Next -> exec rest ctx'

execOne :: Stmt -> Ctx -> Either String (Flow, Ctx)
execOne s ctx = case s of
  Assign v e -> do
    (x, ctx') <- evalE e ctx
    Right (Next, ctx' {vars = M.insert v x (vars ctx')})
  Print e -> do
    (x, ctx') <- evalE e ctx
    Right (Next, ctx' {out = show x : out ctx'})
  If c t f -> do
    (x, ctx') <- evalE c ctx
    exec (if x /= 0 then t else f) ctx'
  While c body -> do
    (x, ctx') <- evalE c ctx
    if x == 0
      then Right (Next, ctx')
      else do
        (flow, ctx'') <- exec body ctx'
        case flow of
          Returned v -> Right (Returned v, ctx'')
          Next -> execOne s ctx''
  Def name params body -> Right (Next, ctx {funs = M.insert name (params, body) (funs ctx)})
  Return e -> do
    (x, ctx') <- evalE e ctx
    Right (Returned x, ctx')

evalE :: Expr -> Ctx -> Either String (Integer, Ctx)
evalE e ctx = case e of
  Num n -> Right (n, ctx)
  Var v -> maybe (Left ("undefined variable " ++ v)) (\x -> Right (x, ctx)) (M.lookup v (vars ctx))
  Neg a -> do
    (x, c) <- evalE a ctx
    Right (negate x, c)
  BinOp op a b -> do
    (x, c1) <- evalE a ctx
    (y, c2) <- evalE b c1
    r <- arith op x y
    Right (r, c2)
  Call f args -> do
    (params, body) <- maybe (Left ("undefined function " ++ f)) Right (M.lookup f (funs ctx))
    if length params /= length args then Left ("arity mismatch calling " ++ f) else Right ()
    (vals, c1) <- evalArgs args ctx
    (flow, c2) <- exec body c1 {vars = M.fromList (zip params vals)}
    let result = case flow of Returned v -> v; Next -> 0
    Right (result, c2 {vars = vars c1})

evalArgs :: [Expr] -> Ctx -> Either String ([Integer], Ctx)
evalArgs [] ctx = Right ([], ctx)
evalArgs (a : as) ctx = do
  (x, c1) <- evalE a ctx
  (xs, c2) <- evalArgs as c1
  Right (x : xs, c2)

runProgram :: String -> [String]
runProgram src = case parseAll (many stmt) src of
  Left err -> ["parse error " ++ err]
  Right prog -> case exec prog (Ctx M.empty M.empty [] 0) of
    Left err -> ["runtime error: " ++ err]
    Right (_, ctx) -> reverse (out ctx) ++ ["(" ++ show (steps ctx) ++ " steps, vars " ++ show (M.toList (vars ctx)) ++ ")"]

main :: IO ()
main = do
  putStrLn "-- arithmetic"
  let inputs =
        [ "1 + 2 * 3",
          "(1 + 2) * 3",
          "2 ^ 3 ^ 2",
          "100 - 10 - 1",
          "-3 * -(4 + 1)",
          "2 ^ 100 / 3 % 1000",
          "7 / (3 - 3)",
          "1 < 2 == 1",
          "  42  ",
          "1 + ",
          "(1 + 2",
          "1 + 2)",
          "3 $ 4",
          "f(1, 2 * x, g())",
          "x + 1"
        ]
  mapM_
    ( \s -> putStrLn $ show s ++ " => " ++ case parseAll expr s of
        Left err -> "parse error " ++ err
        Right e -> render e ++ " = " ++ either ("error: " ++) show (evalPure e)
    )
    inputs
  putStrLn "-- JSON"
  let doc = "{\"name\": \"warmc\", \"version\": [1, 2, -3], \"tags\": [\"aot\", \"wasm\\n\", \"q\\\"uote\"], \"nested\": {\"ok\": true, \"none\": null, \"empty\": {}, \"list\": [[], [1], [[2]]]}, \"n\": 12345678901234567890}"
  case parseAll json doc of
    Left err -> putStrLn ("json error " ++ err)
    Right v -> do
      putStrLn (pretty 0 v)
      mapM_ (\q -> putStrLn (intercalate "." q ++ " -> " ++ maybe "missing" (pretty 0) (query q v))) [["name"], ["version", "2"], ["nested", "list", "2", "0"], ["nested", "ok"], ["tags", "9"], ["n"], ["nested", "missing"]]
      print (parseAll json (pretty 0 v) == Right v)
  mapM_ (\s -> putStrLn (s ++ " -> " ++ either id show (parseAll json s))) ["[1, 2,]", "{\"a\" 1}", "[true, fals]", "\"unterminated", "[[[[]]]]", "  {  }  "]
  putStrLn "-- interpreter"
  let programs =
        [ "x = 10; y = 0; while x > 0 { y = y + x; x = x - 1; } print y;",
          "def fact(n) { if n < 2 { return 1; } return n * fact(n - 1); } print fact(10); print fact(30);",
          "def fib(n) { if n < 2 { return n; } return fib(n - 1) + fib(n - 2); } i = 0; while i < 15 { print fib(i); i = i + 1; }",
          "def gcd(a, b) { while b > 0 { t = b; b = a % b; a = t; } return a; } print gcd(1071, 462); print gcd(2 ^ 40, 6 ^ 20);",
          "n = 1; count = 0; while n < 30 { k = n; while k == 1 == 0 { if k % 2 == 0 { k = k / 2; } else { k = 3 * k + 1; } count = count + 1; } n = n + 1; } print count;",
          "def isprime(n) { if n < 2 { return 0; } d = 2; while d * d < n + 1 { if n % d == 0 { return 0; } d = d + 1; } return 1; } p = 0; i = 0; while i < 100 { if isprime(i) { p = p + 1; print i; } i = i + 1; } print p;",
          "print 1 / 0;",
          "print undefinedVar;",
          "x = 1; while 1 { x = x + 1; }",
          "def f(a) { return a; } print f(1, 2);",
          "if 1 { print 1; } else { print 2 ",
          "def ack(m, n) { if m == 0 { return n + 1; } if n == 0 { return ack(m - 1, 1); } return ack(m - 1, ack(m, n - 1)); } print ack(2, 3);"
        ]
  mapM_ (\p -> mapM_ putStrLn (runProgram p) >> putStrLn "--") programs
