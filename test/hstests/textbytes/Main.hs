{-# LANGUAGE OverloadedStrings #-}
-- Data.Text and Data.ByteString: Text construction, pack/unpack with
-- non-ASCII characters, length vs UTF-8 byte length, case conversion,
-- splitOn/breakOn/replace/strip/words/lines/justify/center, isPrefixOf/
-- isInfixOf/stripSuffix, intercalate/transpose/chunksOf, folds and zips,
-- read of numbers with Data.Text.Read; UTF-8 encode/decode round trips
-- through Data.Text.Encoding; strict ByteString (Char8 and Word8 views:
-- split, foldl', elemIndex, isInfixOf, breakSubstring, unpack, readInt);
-- lazy ByteString built from chunks; and ByteString Builder producing
-- decimal, hex and fixed-width binary encodings, consumed again byte by byte.
--
-- text-2 stores UTF-8 in a ByteArray#, so its inner loops are byte scans with
-- the UTF-8 decoder inlined, and bytestring's are pinned buffers walked with
-- memchr/memcmp-style loops: tight memory loops where an off-by-one in a
-- load width or an address computation is immediately visible.
module Main (main) where

import qualified Data.ByteString as B
import qualified Data.ByteString.Builder as BB
import qualified Data.ByteString.Char8 as C
import qualified Data.ByteString.Lazy as BL
import Data.Char (isDigit, isUpper, ord)
import Data.List (foldl')
import qualified Data.Text as T
import qualified Data.Text.Encoding as TE
import qualified Data.Text.Read as TR
import Data.Word (Word8)

main :: IO ()
main = do
  putStrLn "-- Text basics"
  let t = T.pack "Hello, Wörld! Ünïcödé façade — naïve café ☕ 日本語"
  B.putStr (TE.encodeUtf8 (t <> "\n"))
  print (T.length t, B.length (TE.encodeUtf8 t), T.toUpper t, T.toLower t)
  print (T.take 7 t, T.drop 40 t, T.takeEnd 3 t, T.dropEnd 4 t, T.reverse (T.take 12 t))
  print (T.words "  split   these\twords\nplease ", T.lines "a\nb\r\nc\n\nd", T.unwords ["x", "y"], T.unlines ["p", "q"])
  print (T.splitOn ", " "a, b, c,, d", T.splitOn "ab" "cabbabd", T.split (== '/') "/usr//local/bin/")
  print (T.breakOn "::" "key::value::rest", T.breakOnEnd "::" "key::value::rest", T.breakOnAll "an" "banana")
  print (T.replace "na" "NA" "banana", T.replace "ö" "oe" "Göttingen Köln", T.strip "  \t trim me \n ", T.stripStart "  x ", T.stripEnd "  x ")
  print (T.justifyLeft 8 '.' "ab", T.justifyRight 8 '.' "ab", T.center 9 '*' "mid", T.replicate 3 "ab", T.intercalate "-" ["2026", "10", "22"])
  print (T.isPrefixOf "Hel" t, T.isSuffixOf "語" t, T.isInfixOf "façade" t, T.stripPrefix "Hello" t, T.stripSuffix "日本語" "abc日本語", T.commonPrefixes "foobar" "fooquux")
  print (T.chunksOf 4 "abcdefghij", T.transpose ["abc", "de", "f"], T.zip "abc" "xy", T.zipWith max "abc" "bab")
  print (T.foldl' (\n c -> n * 31 + ord c) 7 t, T.foldr (:) [] "fold", T.any isUpper t, T.all (/= 'z') t, T.count "a" "banana bandana")
  print (T.filter isUpper t, T.map succ "HAL", T.concatMap (\c -> T.pack [c, c]) "abc", T.partition isDigit "a1b2c3", T.span (/= ',') t)
  print (T.findIndex (== 'W') t, T.index t 8, T.elem '☕' t, T.splitAt 5 "abcdefg", T.group "aabbbcd", T.inits "abc", T.tails "abc")
  print (T.toTitle "the quick brown fox", T.toCaseFold "Straße ǅ", T.toUpper "ß ﬁ")
  print (T.null "", T.null " ", T.compareLength "abc" 2, compare ("abc" :: T.Text) "abd", T.empty == "", T.singleton 'λ', T.cons 'x' "yz", T.snoc "xy" 'z')
  putStrLn "-- Text.Read"
  print (TR.decimal "12345 rest" :: Either String (Int, T.Text), TR.signed TR.decimal "-77" :: Either String (Integer, T.Text))
  print (TR.hexadecimal "ff10z" :: Either String (Int, T.Text), TR.double "3.25e2x", TR.rational "-0.5" :: Either String (Double, T.Text), TR.decimal "abc" :: Either String (Int, T.Text))
  print (map (fmap fst . TR.decimal) (T.splitOn "," "10,20,x,40") :: [Either String Int])
  putStrLn "-- encoding"
  let samples = ["", "ascii", "ä", "€uro", "𝄞 clef", "mixed ☃ snow ❄ and 🎉"] :: [T.Text]
  mapM_ (\s -> let bs = TE.encodeUtf8 s in print (s, B.unpack bs, TE.decodeUtf8 bs == s, T.length s, B.length bs)) samples
  print (TE.decodeUtf8' (B.pack [0x68, 0x69, 0xff]), TE.decodeUtf8' (B.pack [0xe2, 0x82, 0xac]), TE.decodeLatin1 (B.pack [0x63, 0x61, 0x66, 0xe9]))
  print (TE.encodeUtf16LE "a€", TE.encodeUtf32BE "𝄞")
  putStrLn "-- strict ByteString"
  let bs = C.pack "the,quick,,brown,fox,jumps,over,the,lazy,dog"
  print (C.split ',' bs, C.length bs, C.count ',' bs, C.elemIndex 'q' bs, C.elemIndices 'o' bs, C.findIndex (== 'z') bs)
  print (C.takeWhile (/= ',') bs, C.dropWhile (/= 'x') bs, C.breakSubstring "jumps" bs, C.isInfixOf "lazy" bs, C.isPrefixOf "the" bs, C.isSuffixOf "cat" bs)
  print (C.map succ "HAL", C.reverse "stressed", C.intercalate ", " ["a", "b"], C.words "  some  words here ", C.lines "l1\nl2\n", C.unwords ["x", "y"])
  print (B.foldl' (\acc w -> acc * 33 + fromIntegral w) (5381 :: Int) bs, B.foldr (\w acc -> fromIntegral w + acc) (0 :: Int) bs, B.maximum bs, B.minimum bs)
  print (B.unpack (B.pack [0, 127, 128, 255]), B.map (`div` 2) (B.pack [10, 20, 255]), B.zipWith (+) (B.pack [1, 2, 3]) (B.pack [250, 251, 252]))
  print (map C.readInt ["123", "-45abc", "x1", "  7"], C.readInteger "123456789012345678901234567890!", C.unsnoc "abc", C.uncons "")
  print (B.replicate 5 0x41, C.replicate 3 'z', B.append "ab" "cd", B.concat ["x", "", "yz"], B.splitAt 3 "abcdef", B.drop 10 "short", B.take 0 "x", B.null B.empty)
  print (C.filter (`elem` ("aeiou" :: String)) bs, C.span (/= ',') bs, C.spanEnd (/= ',') bs, C.group "aaabcc", B.sort (C.pack "sortme"), B.elem 0x7a bs)
  let hist = foldl' (\acc w -> take (fromIntegral w) acc ++ [acc !! fromIntegral w + 1] ++ drop (fromIntegral w + 1) acc) (replicate 256 (0 :: Int)) (B.unpack bs)
  print [(toEnum i :: Char, n) | (i, n) <- zip [0 ..] hist, n > 2]
  putStrLn "-- lazy ByteString and Builder"
  let lazyBs = BL.fromChunks [C.pack "chunk one|", C.pack "", C.pack "chunk two|", C.pack "three"]
  print (lazyBs, BL.length lazyBs, BL.toChunks lazyBs, BL.split 124 lazyBs, BL.toStrict (BL.take 12 lazyBs))
  print (BL.foldl' (\n w -> n + fromIntegral w) (0 :: Int) lazyBs, BL.elemIndex 116 lazyBs, BL.unpack (BL.drop 26 lazyBs))
  let builder =
        mconcat
          [ BB.string7 "ints:",
            mconcat [BB.char7 ' ' <> BB.intDec i | i <- [-3, 0, 42, maxBound]],
            BB.string7 " hex:",
            mconcat [BB.char7 ' ' <> BB.word32HexFixed w | w <- [0, 255, 0xdeadbeef]],
            BB.string7 " dbl:",
            BB.char7 ' ' <> BB.doubleDec 3.25,
            BB.char7 ' ' <> BB.integerDec (2 ^ (80 :: Int)),
            BB.string7 " utf8:",
            BB.stringUtf8 " ⇒✓",
            BB.char7 '\n'
          ]
      out = BB.toLazyByteString builder
  BL.putStr out
  print (BL.length out, BL.unpack (BL.takeEnd 8 out))
  let binary = BB.toLazyByteString (mconcat [BB.word8 1, BB.word16BE 0x0203, BB.word16LE 0x0405, BB.word32BE 0x06070809, BB.word64LE 0x1112131415161718, BB.int32LE (-2), BB.floatBE 1.5, BB.doubleLE (-0.1)])
  print (BL.unpack binary)
  let decodeBE :: [Word8] -> Integer
      decodeBE = foldl' (\acc w -> acc * 256 + fromIntegral w) 0
      bytes = BL.unpack binary
  print (decodeBE (take 2 (drop 1 bytes)), decodeBE (reverse (take 2 (drop 3 bytes))), decodeBE (take 4 (drop 5 bytes)), decodeBE (reverse (take 8 (drop 9 bytes))))
  print (BB.toLazyByteString (foldMap (\w -> BB.word8HexFixed w <> BB.char7 ':') (B.unpack "Hi!")), BB.toLazyByteString (BB.lazyByteString lazyBs <> BB.byteString "|end"))
