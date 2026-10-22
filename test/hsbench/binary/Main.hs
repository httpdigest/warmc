{-# LANGUAGE DeriveGeneric #-}
-- Data.Binary: a list of nested records with Generic-derived instances is
-- encoded to a lazy ByteString, decoded back, and checked field by field.
--
-- GHC specialises most of the Generic-derived put and get to these types inside
-- this module.  Encoding runs bytestring's Builder into lazy chunks, and every
-- multi-byte field is big-endian: a call to the C helper hs_bswap16, 32 or 64,
-- then an unaligned store; decoding is binary's continuation-passing Get
-- monad, a closure entered for every field.  Serialisation as most Haskell
-- programs do it, rather than the hand-written byte loop of bytestring.
module Main (main) where

import Data.Binary (Binary, decode, encode)
import Data.Bits (shiftL, shiftR, xor, (.&.))
import qualified Data.ByteString.Lazy as L
import Data.Int (Int16, Int64)
import Data.List (foldl')
import Data.Word (Word32, Word8)
import GHC.Generics (Generic)

data Shape
  = Circle !Int16
  | Rect !Int16 !Int16
  | Poly [Word32]
  deriving (Generic)

instance Binary Shape

data Item = Item
  { itemId :: !Word32,
    label :: String,
    weight :: !Int64,
    flags :: [Bool],
    shape :: Shape,
    parent :: Maybe Word8
  }
  deriving (Generic)

instance Binary Item

next :: Word32 -> Word32
next x0 = x3
  where
    x1 = x0 `xor` (x0 `shiftL` 13)
    x2 = x1 `xor` (x1 `shiftR` 17)
    x3 = x2 `xor` (x2 `shiftL` 5)

items :: Int -> Word32 -> [Item]
items 0 _ = []
items n x0 =
  let x1 = next x0
      x2 = next x1
      x3 = next x2
      sh = case x1 .&. 3 of
        0 -> Circle (fromIntegral x2)
        1 -> Rect (fromIntegral x2) (fromIntegral (x2 `shiftR` 16))
        _ -> Poly (take (fromIntegral (x2 .&. 7)) (iterate next x3))
      it =
        Item
          { itemId = x1,
            label = take (fromIntegral (x3 .&. 15)) (cycle "abcdefghij"),
            weight = fromIntegral x2 * fromIntegral x3,
            flags = map (\b -> x3 `shiftR` b .&. 1 == 1) [0 .. fromIntegral (x1 `shiftR` 28)],
            shape = sh,
            parent = if odd x2 then Just (fromIntegral x3) else Nothing
          }
   in it : items (n - 1) x3

digest :: Item -> Int
digest it =
  let s = case shape it of
        Circle r -> fromIntegral r
        Rect w h -> fromIntegral w * 3 + fromIntegral h
        Poly ps -> foldl' (\a p -> a * 7 + fromIntegral p) 0 ps
   in fromIntegral (itemId it) + length (label it) + fromIntegral (weight it .&. 0xffff)
        + length (filter id (flags it)) + s + maybe 0 fromIntegral (parent it)

one :: Int -> Int
one r =
  let xs = items 4000 (fromIntegral r * 2654435761 + 1)
      bytes = encode xs
      ys = decode bytes :: [Item]
      byteSum = L.foldl' (\a b -> (a * 31 + fromIntegral b) .&. 0xfffffff) 0 bytes
   in (foldl' (\a it -> (a * 31 + digest it) .&. 0xfffffff) 0 ys + byteSum + fromIntegral (L.length bytes)) .&. 0xfffffff

main :: IO ()
main = print (map one [1 .. 8])
