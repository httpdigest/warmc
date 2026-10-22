{-# OPTIONS_GHC -Wno-x-partial -Wno-incomplete-uni-patterns #-}
-- Data.Char and String processing: classification predicates and
-- generalCategory over ASCII, Latin-1 and a sample of wider Unicode;
-- toUpper/toLower/toTitle including non-ASCII letters; ord/chr,
-- digitToInt/intToDigit; show of strings and characters with every kind of
-- escape (and the "\&" separator), read back; words/unwords/lines/unlines
-- with irregular whitespace; reading numbers, tuples, lists, Maybe and
-- Either from strings with `reads` and failure cases; a Caesar cipher, a
-- run-length codec, a word-frequency count, string justification and a
-- palindrome check, each printed step by step.
--
-- The Unicode tables GHC consults live in the module's data segment and are
-- searched with binary search; show/read are large pure functions over lists
-- of boxed Chars.
module Main (main) where

import Data.Char
import Data.List (group, intercalate, sort, sortBy)
import Data.Ord (comparing)

classify :: Char -> String
classify c =
  [ flag isControl 'c',
    flag isSpace 's',
    flag isLower 'l',
    flag isUpper 'u',
    flag isAlpha 'a',
    flag isAlphaNum 'n',
    flag isDigit 'd',
    flag isOctDigit 'o',
    flag isHexDigit 'h',
    flag isPunctuation 'p',
    flag isSymbol 'y',
    flag isPrint 'r',
    flag isAscii 'A',
    flag isLatin1 'L',
    flag isSeparator 'S',
    flag isMark 'M',
    flag isNumber 'N',
    flag isLetter 'T'
  ]
  where
    flag p ch = if p c then ch else '-'

caesar :: Int -> String -> String
caesar k = map shift
  where
    shift c
      | isAsciiUpper c = rot 'A' c
      | isAsciiLower c = rot 'a' c
      | otherwise = c
    rot base c = chr (ord base + (ord c - ord base + k) `mod` 26)

rle :: String -> String
rle = concatMap (\g -> show (length g) ++ [head g]) . group

unrle :: String -> String
unrle [] = []
unrle s = let (n, c : rest) = span isDigit s in replicate (read n) c ++ unrle rest

justify :: Int -> [String] -> [String]
justify w = go
  where
    go [] = []
    go ws =
      let (line, rest) = takeLine ws 0 []
       in pad line rest : go rest
    takeLine [] _ acc = (reverse acc, [])
    takeLine (x : xs) n acc
      | null acc = takeLine xs (length x) [x]
      | n + 1 + length x <= w = takeLine xs (n + 1 + length x) (x : acc)
      | otherwise = (reverse acc, x : xs)
    pad [x] _ = x
    pad line [] = unwords line
    pad line _ =
      let gaps = length line - 1
          spaces = w - sum (map length line)
          (q, r) = spaces `divMod` gaps
          seps = [replicate (q + (if i < r then 1 else 0)) ' ' | i <- [0 .. gaps - 1]]
       in concat (zipWith (++) line (seps ++ [""]))

isPalindrome :: String -> Bool
isPalindrome s = let t = map toLower (filter isAlpha s) in t == reverse t

main :: IO ()
main = do
  putStrLn "-- classification"
  let sample = "aZ5 \t\n_!~+$\DEL\0\160\233\223\255\x100\x3b1\x3a9\x416\x5d0\x660\x2028\x20ac\x4e2d\x1f600\x301\xbd\x2163"
  mapM_ (\c -> putStrLn (show c ++ " " ++ show (ord c) ++ " " ++ classify c ++ " " ++ show (generalCategory c))) sample
  print (map generalCategory "Aa1 .-+$^\x300\x2160")
  print (length (filter isAlpha ['\0' .. '\x2fff']), length (filter isUpper ['\0' .. '\x2fff']), length (filter isSpace ['\0' .. '\x3000']), length (filter isPunctuation ['\0' .. '\xff']))
  putStrLn "-- case mapping"
  let mixed = "Hello, World! \223\233\255\x3b1\x3c2\x3a3\x416\x436 \x1c4\x1c5\x1c6"
  print (map toUpper mixed)
  print (map toLower mixed)
  print (map toTitle "abc \x1c6\x1c4")
  print (map (ord . toUpper) "\181\255\x131\x17f", map (ord . toLower) "\x130\x212a")
  putStrLn "-- ord, chr, digits"
  print (map ord "Az09~", map chr [72, 105, 33, 0x263A, 0x1F600], ord maxBound, chr 0x10FFFF == maxBound)
  print (map digitToInt "0123456789abcdefABCDEF", map intToDigit [0 .. 15])
  print (succ 'a', pred 'b', ['a' .. 'k'], ['a', 'c' .. 'm'], fromEnum 'A', toEnum 97 :: Char)
  putStrLn "-- show and read of strings"
  let tricky = ["plain", "tab\there", "nl\nx", "quote\"s", "back\\slash", "\0\1\2\DEL", "\200\1234", "\SOH\&H", "\x7f\&9", "caf\233", "\x1F600!", "\1\&23", "\SO\&H"]
  mapM_ (\s -> putStrLn (show s ++ " " ++ show (length s) ++ " " ++ show (read (show s) == s))) tricky
  print (map show "\n\t\\'\"a\0\200", show '\'', show '"')
  print (read "'x'" :: Char, read "'\\n'" :: Char, read "\"a\\tb\\1234\\&5\"" :: String, read "'\\SOH'" :: Char)
  putStrLn "-- words and lines"
  let text = "  the quick\tbrown  fox\n\njumps over\r\n the   lazy dog \n"
  print (words text)
  print (lines text)
  print (unwords (words text), unlines (lines text) == text)
  print (lines "a\nb", lines "a\nb\n", lines "\n", lines "", words "", words "   ")
  print (map length (lines text), map (map length . words) (lines text))
  putStrLn "-- reads"
  print (reads "42 rest" :: [(Int, String)], reads "-7" :: [(Int, String)], reads "x" :: [(Int, String)])
  print (reads "  3.5e2xyz" :: [(Double, String)], reads "(1,\"a\")" :: [((Int, String), String)])
  print (read "[(1,True),(2,False)]" :: [(Int, Bool)], read " ( Just  (-3) ) " :: Maybe Int, read "Left 'q'" :: Either Char Int)
  print (read "  [ [1] , [] , [2,3] ] " :: [[Integer]], read "(  )" :: (), read "\"\\\"\"" :: String)
  print ((reads :: ReadS Int) "0x1F", (reads :: ReadS Int) "0o17", (reads :: ReadS Int) "1_000", (reads :: ReadS Bool) "True!")
  putStrLn "-- string algorithms"
  let msg = "Attack at Dawn! Zebra-crossing @ 10:45."
  mapM_ (\k -> putStrLn (show k ++ ": " ++ caesar k msg)) [1, 3, 13, 25]
  print (caesar 13 (caesar 13 msg) == msg, caesar (-3) (caesar 3 msg) == msg)
  let rles = ["aaabccddddd", "abc", "", "zzzzzzzzzzzzzzz", "aabbaa"]
  mapM_ (\s -> putStrLn (show s ++ " -> " ++ show (rle s) ++ " -> " ++ show (unrle (rle s)))) rles
  let corpus = "it was the best of times it was the worst of times it was the age of wisdom it was the age of foolishness"
      freq = sortBy (comparing (\(w, n) -> (negate n, w))) [(head g, length g) | g <- group (sort (words corpus))]
  mapM_ (\(w, n) -> putStrLn (w ++ " " ++ replicate n '*')) freq
  mapM_ putStrLn (justify 30 (words corpus))
  print (map isPalindrome ["A man, a plan, a canal: Panama", "No lemon, no melon", "Haskell", "Was it a car or a cat I saw?"])
  print (intercalate ", " (map (map toUpper) (words "comma separated upper case")), filter (not . isSpace) " s p a c e s ")
  print (zipWith (\i l -> show i ++ ". " ++ l) [1 :: Int ..] (lines "first\nsecond\nthird"))
  print (map (\w -> (w, sum (map ord w) `mod` 97)) (words "hash these words please"))
