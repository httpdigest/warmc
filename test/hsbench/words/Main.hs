{-# LANGUAGE BangPatterns #-}
-- String as a list of Char: generate a document from a vocabulary that mixes
-- ASCII, Latin-1, Greek and Cyrillic words, take it apart with lines and
-- words, rewrite each word with Data.Char (rot13, case mapping, digit doubling),
-- put it back together with unwords and unlines, and classify every character
-- of the result.
--
-- A String is a list of boxed Chars, so lines, words, unwords and unlines are
-- list traversals that allocate a cons cell per character they pass on.  The
-- classifiers other than isSpace look each character up in GHC's
-- general-category bitmap, and toUpper and toLower are each one function of
-- about 12,900 instructions, a case over code points compiled to 42 br_tables.  The text kernel
-- does the same kind of work over UTF-8 in a byte array; this is the list
-- version most Haskell programs actually use.
module Main (main) where

import Data.Bits (shiftL, shiftR, xor, (.&.))
import Data.Char
import Data.List (foldl')
import Data.Word (Word32)

vocab :: [String]
vocab =
  [ "the", "quick", "brown", "fox", "jumps", "over", "lazy", "dog", "Ærøskøbing",
    "naïve", "café", "ελληνικά", "Ωμέγα", "кириллица", "Москва", "x9", "2048", "hello,",
    "world!", "(paren)", "semi;colon", "MiXeD", "ÉCOLE", "straße", "año", "13", "über",
    "Zürich", "quote\"d", "tab-free"
  ]

next :: Word32 -> Word32
next x0 = x3
  where
    x1 = x0 `xor` (x0 `shiftL` 13)
    x2 = x1 `xor` (x1 `shiftR` 17)
    x3 = x2 `xor` (x2 `shiftL` 5)

document :: Int -> Word32 -> String
document nLines = unlines . go nLines
  where
    nv = length vocab
    go 0 _ = []
    go k x =
      let x1 = next x
          len = 4 + fromIntegral (x1 `mod` 12)
          (ws, x2) = pick len x1 []
       in unwords ws : go (k - 1) x2
    pick 0 x acc = (acc, x)
    pick m x acc = let x' = next x in pick (m - 1 :: Int) x' (vocab !! fromIntegral (x' `mod` fromIntegral nv) : acc)

rot13 :: Char -> Char
rot13 c
  | isAsciiLower c = chr ((ord c - ord 'a' + 13) `mod` 26 + ord 'a')
  | isAsciiUpper c = chr ((ord c - ord 'A' + 13) `mod` 26 + ord 'A')
  | otherwise = c

rewrite :: Int -> String -> String
rewrite n w
  | all isDigit w = map (\c -> intToDigit ((digitToInt c * 2 + n) `mod` 10)) w
  | even n = case map rot13 w of
      (c : cs) -> toUpper c : map toLower cs
      [] -> []
  | otherwise = reverse (map (\c -> if isUpper c then toLower c else toUpper c) w)

process :: String -> String
process = unlines . zipWith line [0 ..] . lines
  where
    line i l = unwords (zipWith (\j w -> rewrite (i + j) w) [0 ..] (words l))

classify :: String -> (Int, Int, Int, Int, Int, Int)
classify = foldl' f (0, 0, 0, 0, 0, 0)
  where
    f (!u, !l, !s, !p, !a, !h) c =
      ( if isUpper c then u + 1 else u,
        if isLower c then l + 1 else l,
        if isSpace c then s + 1 else s,
        if isPunctuation c then p + 1 else p,
        if isAlpha c && not (isAscii c) then a + 1 else a,
        (h * 31 + ord c) .&. 0xfffffff
      )

main :: IO ()
main = do
  let doc = document 3600 2463534242
      out = process doc
  print (classify out, length (lines out), length (words out))
