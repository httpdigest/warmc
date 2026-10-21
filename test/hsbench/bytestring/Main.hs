-- Data.ByteString: render records with a Builder, then split the bytes into
-- lines and words and parse the numbers back with readInt.
--
-- Strict ByteStrings are pinned byte arrays with an offset and a length, so
-- unlike a String this is memchr-style scanning over contiguous bytes, much of
-- it in C from the bytestring package -- built into the same module by
-- wasi-sdk's clang, which makes the row a mix of GHC's code and LLVM's.
module Main (main) where

import qualified Data.ByteString.Builder as B
import qualified Data.ByteString.Char8 as C
import qualified Data.ByteString.Lazy as L
import Data.Bits (shiftL, shiftR, xor, (.&.))
import Data.List (foldl')

next :: Int -> Int
next x0 =
  let x1 = x0 `xor` ((x0 `shiftL` 13) .&. 0xffffffff)
      x2 = x1 `xor` (x1 `shiftR` 17)
   in x2 `xor` ((x2 `shiftL` 5) .&. 0xffffffff)

render :: Int -> Int -> B.Builder
render 0 _ = mempty
render n x =
  let x' = next x
   in B.string7 "id " <> B.intDec (x' .&. 0xffff) <> B.char7 ' ' <> B.intDec (x' `shiftR` 20)
        <> B.string7 (if odd x' then " odd\n" else " even\n")
        <> render (n - 1) x'

total :: C.ByteString -> Int
total bs = foldl' line 0 (C.lines bs)
  where
    line acc l = case C.words l of
      [_, a, b, w] -> case (C.readInt a, C.readInt b) of
        (Just (p, _), Just (q, _)) -> (acc * 31 + p + q + C.length w) .&. 0xfffffff
        _ -> acc
      _ -> acc

main :: IO ()
main = do
  let one r = total (L.toStrict (B.toLazyByteString (render 20000 (r * 104729))))
  print (map one [1 .. 10 :: Int])
