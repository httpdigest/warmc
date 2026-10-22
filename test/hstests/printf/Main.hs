-- Text.Printf: every conversion (%d %i %u %x %X %o %b %e %f %g %s %c %v %%)
-- with widths, precisions, the - 0 + and space flags, `*` widths, and
-- arguments of every integral and floating type including negatives,
-- zero, the extremes of each fixed-width type and big Integers; printf
-- returning a String as well as printing; hPrintf to stdout; and a few
-- tables formatted the way a report would be.
--
-- printf is a type-class-driven variadic function: each argument is wrapped
-- by its PrintfArg dictionary into a formatter closure, and the format string
-- is interpreted at run time over a list of them.  The float conversions go
-- through Numeric's digit generation on Integers.
module Main (main) where

import Data.Int (Int16, Int64, Int8)
import Data.Word (Word16, Word32, Word64, Word8)
import System.IO (stdout)
import Text.Printf (hPrintf, printf)

main :: IO ()
main = do
  putStrLn "-- integers"
  let ints = [0, 1, -1, 42, -42, 255, 65535, 2147483647, -2147483648] :: [Int]
  mapM_ (\n -> printf "[%d] [%5d] [%-5d] [%05d] [%+d] [% d] [%x] [%X] [%o] [%#x] [%#o]\n" n n n n n n n n n n n) ints
  mapM_ (\n -> printf "[%i] [%10i] [%-10i|] [%u]\n" n n n n) [7, -7, 123456 :: Int]
  printf "%d %d %d %d\n" (127 :: Int8) (-128 :: Int8) (-32768 :: Int16) (minBound :: Int64)
  printf "%d %d %d %d %d\n" (255 :: Word8) (65535 :: Word16) (maxBound :: Word32) (maxBound :: Word64) (maxBound :: Word)
  printf "%x %X %o %x\n" (maxBound :: Word64) (3735928559 :: Word32) (511 :: Word16) (-1 :: Int8)
  printf "%b %b %08b %b\n" (5 :: Int) (255 :: Word8) (6 :: Int) (1024 :: Integer)
  printf "%d\n" (2 ^ (100 :: Int) :: Integer)
  printf "%x\n" (2 ^ (100 :: Int) - 1 :: Integer)
  printf "%40d|\n" (negate (3 ^ (60 :: Int)) :: Integer)
  printf "%-40d|\n" (3 ^ (60 :: Int) :: Integer)
  printf "%.3d %.5d %8.3d\n" (7 :: Int) (-42 :: Int) (9 :: Int)
  putStrLn "-- floating point"
  let ds = [0, 1, -1, 3.14159265358979, -2.5e-5, 123456.789, 1.0e21, 0.5, 1.5, 2.5, 9.995] :: [Double]
  mapM_ (\d -> printf "[%f] [%.2f] [%10.3f] [%-10.1f] [%+.4f] [%e] [%.3e] [%g] [%.0f]\n" d d d d d d d d d) ds
  mapM_ (\d -> printf "%08.3f|% .2f|%+e\n" d d d) [3.14159 :: Double, -3.14159, 0]
  printf "%f %.2f %e %g\n" (1.5 :: Float) (2.675 :: Float) (1.0e-3 :: Float) (100000 :: Float)
  printf "%5.1f%%\n" (99.44 :: Double)
  printf "%.10f %.15e\n" (1 / 3 :: Double) (2 / 3 :: Double)
  printf "%g %g %g %g %g\n" (1.0e-5 :: Double) (123.456 :: Double) (1.0e7 :: Double) (0.1 :: Double) (100 :: Double)
  putStrLn "-- strings and chars"
  let ws = ["", "a", "hello", "a longer string"]
  mapM_ (\s -> printf "[%s] [%10s] [%-10s] [%.3s] [%-8.2s]\n" s s s s s) ws
  printf "%c%c%c %c\n" 'a' 'b' 'c' 'Z'
  printf "[%5c] [%-5c]\n" 'x' 'y'
  printf "%s has %d items costing %.2f (%c)\n" "cart" (3 :: Int) (19.989 :: Double) '$'
  printf "%v %v %v %v\n" (1 :: Int) (2.5 :: Double) "str" 'c'
  printf "100%% done, %s\n" (show [1, 2, 3 :: Int])
  printf "%*d|%-*d|\n" (6 :: Int) (42 :: Int) (6 :: Int) (42 :: Int)
  putStrLn "-- printf to String and hPrintf"
  let s1 = printf "%d-%d-%d" (2026 :: Int) (10 :: Int) (22 :: Int) :: String
      s2 = printf "%05.1f" (3.14159 :: Double) :: String
      s3 = printf "%s/%s" "a" "b" :: String
  print (s1, s2, s3, length s1)
  mapM_ (\i -> putStrLn (printf "%3d %6.2f %s" i (sqrt (fromIntegral i) :: Double) (replicate i '*'))) [1 .. 12 :: Int]
  hPrintf stdout "hPrintf %s %d %.3f\n" "works" (12 :: Int) (2.0 / 3 :: Double)
  putStrLn "-- a report"
  let rows = [("apples", 12, 0.5), ("bananas", 7, 0.25), ("cherries", 250, 0.02), ("dates", 3, 4.75), ("elderberries", 1000, 0.015)] :: [(String, Int, Double)]
  printf "%-14s %6s %8s %10s\n" "item" "qty" "price" "total"
  mapM_ (\(n, q, p) -> printf "%-14s %6d %8.3f %10.2f\n" n q p (fromIntegral q * p)) rows
  printf "%-14s %6d %8s %10.2f\n" "TOTAL" (sum [q | (_, q, _) <- rows]) "" (sum [fromIntegral q * p | (_, q, p) <- rows])
  mapM_ (\k -> printf "%2d: %08x %12o %s\n" k (k * (-1640531535) :: Int) (k * k * k :: Int) (show (fromIntegral k / 7 :: Double))) [1 .. 10 :: Int]
