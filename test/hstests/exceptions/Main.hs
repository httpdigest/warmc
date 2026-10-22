{-# LANGUAGE ExistentialQuantification #-}
{-# OPTIONS_GHC -Wno-x-partial -Wno-incomplete-patterns #-}
{-# LANGUAGE ScopedTypeVariables #-}
-- Control.Exception: throwIO and throw (from pure code), catch, handle,
-- try with specific and SomeException types, evaluate to force a pure
-- error at a chosen point, ArithException (DivideByZero from div/mod/quot/
-- rem on Int and Integer, Overflow from minBound `div` (-1), and RatioZero
-- denominator), ErrorCall from error and from partial functions (head of
-- [], an incomplete pattern, a missing record field, array index out of
-- range), custom exception types and an exception hierarchy with a
-- superclass wrapper, fromException/toException/displayException, catches
-- with several handlers, nested handlers and rethrow, bracket/bracket_/
-- finally/onException running their actions in the documented order (with
-- the order logged to an IORef), mask_ and uninterruptibleMask_, throwing
-- inside a cleanup, and a loop that raises and catches thousands of times.
--
-- GHC exceptions are raised by the RTS walking the Haskell stack in linear
-- memory for catch frames (raiseExceptionHelper) and unwinding update frames
-- by turning the thunks under evaluation into re-raising closures, so a wrong
-- stack-frame layout or info-table load shows up as the wrong handler running
-- or a thunk re-evaluated.
module Main (main) where

import Control.Exception
import Control.Monad (forM_, when)
import Data.Array (listArray, (!))
import Data.IORef
import Data.List (isInfixOf)
import qualified Data.Map as M
import Data.Ratio ((%))
import Data.Typeable (cast)

data BankError
  = InsufficientFunds Int Int
  | UnknownAccount String
  | Frozen
  deriving (Show)

instance Exception BankError

newtype ParseError = ParseError String deriving (Show)

instance Exception ParseError where
  displayException (ParseError s) = "parse error: " ++ s

-- A hierarchy: AppException wraps any app-level exception.
data AppException = forall e. (Exception e) => AppException e

instance Show AppException where show (AppException e) = "AppException(" ++ show e ++ ")"

instance Exception AppException

appToException :: (Exception e) => e -> SomeException
appToException = toException . AppException

appFromException :: (Exception e) => SomeException -> Maybe e
appFromException x = do
  AppException a <- fromException x
  cast a

data Timeout = Timeout Int deriving (Show)

instance Exception Timeout where
  toException = appToException
  fromException = appFromException

data Disconnected = Disconnected deriving (Show)

instance Exception Disconnected where
  toException = appToException
  fromException = appFromException

data R = R {fieldA :: Int} | S {fieldB :: Int} deriving (Show)

withdraw :: IORef (M.Map String Int) -> String -> Int -> IO Int
withdraw bank acct amt = do
  m <- readIORef bank
  case M.lookup acct m of
    Nothing -> throwIO (UnknownAccount acct)
    Just bal
      | acct == "frozen" -> throwIO Frozen
      | bal < amt -> throwIO (InsufficientFunds bal amt)
      | otherwise -> do
          writeIORef bank (M.insert acct (bal - amt) m)
          return (bal - amt)

firstLine :: String -> String
firstLine = takeWhile (/= '\n')

describe :: SomeException -> String
describe e
  | Just (ae :: ArithException) <- fromException e = "arith: " ++ show ae
  | Just (ErrorCall msg) <- fromException e = "error: " ++ firstLine msg
  | Just (be :: BankError) <- fromException e = "bank: " ++ show be
  | Just (pe :: ParseError) <- fromException e = displayException pe
  | Just (PatternMatchFail msg) <- fromException e = "pattern: " ++ (if "Main.hs" `isInfixOf` msg then "incomplete pattern in Main.hs" else msg)
  | Just (RecSelError msg) <- fromException e = "record selector: " ++ firstLine msg
  | Just (ae :: AppException) <- fromException e = "app: " ++ show ae
  | otherwise = "other: " ++ firstLine (show e)

tryShow :: IO a -> (a -> String) -> IO ()
tryShow act f = do
  r <- try act
  putStrLn (either describe f r)

partialCase :: Int -> String
partialCase 1 = "one"
partialCase 2 = "two"

parseDigit :: Char -> Int
parseDigit c
  | c >= '0' && c <= '9' = fromEnum c - fromEnum '0'
  | otherwise = throw (ParseError ("not a digit: " ++ show c))

main :: IO ()
main = do
  putStrLn "-- arithmetic exceptions"
  tryShow (evaluate (10 `div` (0 :: Int))) show
  tryShow (evaluate (10 `mod` (0 :: Int))) show
  tryShow (evaluate (10 `quot` (0 :: Integer))) show
  tryShow (evaluate (10 `rem` (0 :: Integer))) show
  tryShow (evaluate ((minBound :: Int) `div` (-1))) show
  tryShow (evaluate ((minBound :: Int) `quot` (-1))) show
  tryShow (evaluate (1 % (0 :: Integer))) show
  tryShow (evaluate (10 `div` (3 :: Int))) show
  tryShow (evaluate (2 ^ (-1 :: Int) :: Int)) show
  tryShow (evaluate (toEnum 300 :: Bool)) show
  putStrLn "-- error calls and partial functions"
  tryShow (evaluate (error "boom" :: Int)) show
  tryShow (evaluate (head ([] :: [Int]))) show
  tryShow (evaluate (partialCase 3)) id
  tryShow (evaluate (partialCase 2)) id
  tryShow (evaluate (fieldB (R 1))) show
  tryShow (evaluate (listArray (0, 3) "abcd" ! (7 :: Int))) show
  tryShow (evaluate (M.fromList [(1 :: Int, 'a')] M.! 2)) show
  tryShow (evaluate (sum (map parseDigit "12x4"))) show
  tryShow (evaluate (sum (map parseDigit "1234"))) show
  tryShow (evaluate (length [1, 2, undefined :: Int])) show
  tryShow (evaluate (sum [1, 2, undefined :: Int])) show
  tryShow (evaluate (read "12abc" :: Int)) show
  r <- try (evaluate (foldr (\x acc -> if x == 50 then throw (ErrorCall ("hit " ++ show x)) else x + acc) 0 [1 .. 100 :: Int]))
  case r of
    Left (ErrorCall m) -> putStrLn ("caught ErrorCall: " ++ m)
    Right v -> print v
  putStrLn "-- custom exceptions"
  bank <- newIORef (M.fromList [("alice", 100), ("bob", 20), ("frozen", 1000)])
  forM_ [("alice", 30), ("bob", 50), ("carol", 1), ("frozen", 5), ("alice", 70), ("alice", 1)] $ \(acct, amt) ->
    tryShow (withdraw bank acct amt) (\bal -> acct ++ " now has " ++ show bal)
  readIORef bank >>= print
  res <- try (throwIO (ParseError "unexpected eof")) :: IO (Either ParseError ())
  print res
  print (fmap (displayException :: ParseError -> String) (fromException (toException (ParseError "x"))))
  handle (\(e :: BankError) -> putStrLn ("handled " ++ show e)) (throwIO (InsufficientFunds 1 2))
  handle (\(e :: SomeException) -> putStrLn ("handled generically: " ++ describe e)) (throwIO Frozen)
  putStrLn "-- hierarchy"
  forM_ [toException (Timeout 30), toException Disconnected, toException Frozen] $ \e -> do
    putStrLn (describe e)
    print (fmap show (fromException e :: Maybe Timeout), fmap show (fromException e :: Maybe Disconnected), fmap show (fromException e :: Maybe AppException))
  r2 <- try (throwIO (Timeout 5)) :: IO (Either AppException ())
  print r2
  r3 <- try (try (throwIO Disconnected) :: IO (Either Timeout ()))
  case r3 of
    Left (e :: SomeException) -> putStrLn ("escaped: " ++ describe e)
    Right inner -> print inner
  putStrLn "-- catches, nesting and rethrow"
  let handlers =
        [ Handler (\(e :: ArithException) -> return ("H-arith " ++ show e)),
          Handler (\(e :: ErrorCall) -> return ("H-error " ++ show e)),
          Handler (\(e :: BankError) -> return ("H-bank " ++ show e))
        ]
  forM_ [throwIO DivideByZero, throwIO (ErrorCall "ec"), throwIO Frozen, return "no exception", evaluate (show (1 `div` (0 :: Int)))] $ \act ->
    (act `catches` handlers) >>= putStrLn
  nested <- try (catch (throwIO (UnknownAccount "inner")) (\(e :: ArithException) -> putStrLn ("wrong handler " ++ show e)))
  print (nested :: Either BankError ())
  rethrown <- try (catch (throwIO Overflow) (\(e :: ArithException) -> putStrLn ("saw " ++ show e ++ ", rethrowing") >> throwIO (ErrorCall ("wrapped " ++ show e))))
  print (either (\(ErrorCall m) -> m) (const "none") rethrown)
  deep <- try (evaluate (let go :: Int -> Int; go 0 = error "bottom of the recursion"; go n = n + go (n - 1) in go 5000))
  print (either (\(ErrorCall m) -> m) show deep)
  putStrLn "-- bracket, finally, onException ordering"
  logRef <- newIORef []
  let say s = modifyIORef logRef (s :)
      dump label = do
        l <- readIORef logRef
        putStrLn (label ++ ": " ++ unwords (reverse l))
        writeIORef logRef []
  _ <- bracket (say "acquire" >> return (5 :: Int)) (\_ -> say "release") (\x -> say ("use" ++ show x) >> return x)
  dump "normal bracket"
  _ <- try (bracket (say "acquire" >> return ()) (\_ -> say "release") (\_ -> say "use" >> throwIO Frozen >> say "unreachable")) :: IO (Either BankError ())
  dump "failing bracket"
  _ <- try (bracket_ (say "in") (say "out") (say "body" >> evaluate (1 `div` (0 :: Int)))) :: IO (Either ArithException Int)
  dump "bracket_"
  _ <- (say "action" >> return ()) `finally` say "finalizer"
  dump "finally ok"
  _ <- try ((say "action" >> throwIO Disconnected) `finally` say "finalizer") :: IO (Either SomeException ())
  dump "finally throwing"
  _ <- try ((say "action" >> throwIO (ErrorCall "x")) `onException` say "onException") :: IO (Either ErrorCall ())
  _ <- (say "quiet action" >> return ()) `onException` say "never"
  dump "onException"
  _ <- try (bracket (say "outer-acquire") (\_ -> say "outer-release") (\_ -> bracket (say "inner-acquire") (\_ -> say "inner-release") (\_ -> say "work" >> throwIO (Timeout 1)))) :: IO (Either Timeout ())
  dump "nested brackets"
  cleanupFail <- try (bracket (return ()) (\_ -> say "cleanup throws" >> throwIO (ErrorCall "from cleanup")) (\_ -> say "body ok" >> return 'k'))
  dump "cleanup failing"
  print (cleanupFail :: Either ErrorCall Char)
  bothFail <- try (bracket (return ()) (\_ -> throwIO (ErrorCall "cleanup error")) (\_ -> throwIO (ErrorCall "body error") :: IO ()))
  print (either (\(ErrorCall m) -> m) (const "none") bothFail)
  v <- mask_ (say "masked" >> return (42 :: Int))
  w <- uninterruptibleMask_ (say "uninterruptible" >> return (v + 1))
  m <- mask (\restore -> restore (say "restored" >> return (w * 2)))
  dump "masks"
  print (v, w, m)
  putStrLn "-- many throws"
  counts <- newIORef (0 :: Int, 0 :: Int, 0 :: Int)
  forM_ [1 .. 3000 :: Int] $ \i -> do
    out <- try (when (i `mod` 3 == 0) (throwIO (InsufficientFunds i 0)) >> evaluate (100 `div` (i `mod` 5)))
    case out of
      Right _ -> modifyIORef' counts (\(a, b, c) -> (a + 1, b, c))
      Left e -> case fromException e of
        Just DivideByZero -> modifyIORef' counts (\(a, b, c) -> (a, b + 1, c))
        _ -> modifyIORef' counts (\(a, b, c) -> (a, b, c + 1))
  readIORef counts >>= print
  final <- try (evaluate (foldr (\x acc -> x `seq` acc + 1) 0 (map (\k -> if k == 777 then error "stop at 777" else k) [1 .. 1000 :: Int])))
  putStrLn (either (\(e :: SomeException) -> describe e) show final)
