-- Data.Text: build text with non-ASCII characters in it, then replace,
-- split, case-map and measure it.
--
-- text 2 stores UTF-8, so every length, take and toUpper decodes code points
-- from bytes as it goes, and replace and splitOn search for a needle with the
-- package's own string matcher.  bytestring is the same kind of library over
-- bytes that need no decoding.
module Main (main) where

import Data.Bits ((.&.))
import Data.List (foldl')
import qualified Data.Text as T

piece :: Int -> T.Text
piece i = T.pack (show i) <> T.pack (if even i then " grüße " else " ŝtupo ") <> T.replicate (i `mod` 4) (T.pack "λ")

checksum :: T.Text -> Int
checksum = T.foldl' (\a c -> (a * 31 + fromEnum c) .&. 0xfffffff) 0

one :: Int -> Int
one r =
  let t = T.concat (map piece [r .. r + 2000])
      t1 = T.replace (T.pack "grüße") (T.pack "GRUSS") t
      parts = T.splitOn (T.pack "λ") t1
      upper = T.toUpper (T.take 5000 t1)
      ws = T.words t1
   in (checksum upper + length parts + T.length t1 + sum (map T.length ws)) .&. 0xfffffff

main :: IO ()
main = print (foldl' (\a r -> (a * 7 + one r) .&. 0xfffffff) 0 [1, 101 .. 3000])
