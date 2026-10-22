{-# LANGUAGE ScopedTypeVariables #-}
-- Data.Bits at every width: .&. .|. xor complement, shiftL/shiftR (with
-- shift counts at and beyond the width, which Haskell defines as 0 or the
-- sign fill), unsafeShiftL/R, rotate in both directions, popCount,
-- countLeadingZeros/countTrailingZeros (including of 0), testBit/setBit/
-- clearBit/complementBit, bit, finiteBitSize, the arithmetic right shift of
-- negative values, and bit-twiddling algorithms built from them (bit
-- reversal, Gray codes, parity, next permutation of bits, a CRC-32, a
-- bitboard flood fill, isolate/clear lowest set bit).
--
-- These map onto wasm's i32/i64 and, or, xor, shl, shr_s, shr_u, rotl,
-- rotr, popcnt, clz and ctz, with GHC's range checks around the shifts and
-- explicit narrowing for the 8- and 16-bit types.
module Main (main) where

import Data.Bits
import Data.Char (ord)
import Data.Int (Int16, Int32, Int64, Int8)
import Data.List (foldl', unfoldr)
import Data.Word (Word16, Word32, Word64, Word8)

report :: forall a. (FiniteBits a, Show a, Num a) => String -> [a] -> IO ()
report nm vals = mapM_ line vals
  where
    w = finiteBitSize (undefined :: a)
    line v =
      putStrLn
        ( nm ++ " " ++ show v ++ ": "
            ++ unwords
              [ show (popCount v),
                show (countLeadingZeros v),
                show (countTrailingZeros v),
                show (complement v),
                show [shiftL v k | k <- [1, 3, w - 1, w, w + 5]],
                show [shiftR v k | k <- [1, 3, w - 1, w, w + 5]],
                show [rotateL v k | k <- [1, 4, w - 1, w, w + 3]],
                show [rotateR v k | k <- [1, 4, w - 1]],
                show [i | i <- [0 .. w - 1], testBit v i]
              ]
        )

bitRev32 :: Word32 -> Word32
bitRev32 x0 =
  let x1 = ((x0 `shiftR` 1) .&. 0x55555555) .|. ((x0 .&. 0x55555555) `shiftL` 1)
      x2 = ((x1 `shiftR` 2) .&. 0x33333333) .|. ((x1 .&. 0x33333333) `shiftL` 2)
      x3 = ((x2 `shiftR` 4) .&. 0x0F0F0F0F) .|. ((x2 .&. 0x0F0F0F0F) `shiftL` 4)
      x4 = ((x3 `shiftR` 8) .&. 0x00FF00FF) .|. ((x3 .&. 0x00FF00FF) `shiftL` 8)
   in (x4 `shiftR` 16) .|. (x4 `shiftL` 16)

gray :: Word16 -> Word16
gray n = n `xor` (n `shiftR` 1)

ungray :: Word16 -> Word16
ungray g = foldl' (\acc k -> acc `xor` (g `shiftR` k)) 0 [0 .. 15]

parity64 :: Word64 -> Int
parity64 v = popCount v .&. 1

-- Next larger integer with the same number of set bits (Gosper's hack).
nextPerm :: Word32 -> Word32
nextPerm v =
  let t = v .|. (v - 1)
   in (t + 1) .|. (((complement t .&. negate (complement t)) - 1) `shiftR` (countTrailingZeros v + 1))

crcTable :: [Word32]
crcTable = [go (fromIntegral n) 8 | n <- [0 .. 255 :: Int]]
  where
    go c 0 = c
    go c k = go (if testBit c 0 then 0xEDB88320 `xor` (c `shiftR` 1) else c `shiftR` 1) (k - 1 :: Int)

crc32 :: String -> Word32
crc32 = complement . foldl' step 0xFFFFFFFF
  where
    step c ch = (crcTable !! fromIntegral ((c `xor` fromIntegral (ord ch)) .&. 0xFF)) `xor` (c `shiftR` 8)

-- 8x8 bitboard: flood fill from a square through empty cells.
flood :: Word64 -> Word64 -> Word64
flood walls start = go start
  where
    notA = 0xFEFEFEFEFEFEFEFE
    notH = 0x7F7F7F7F7F7F7F7F
    go s =
      let grow = s .|. (s `shiftL` 8) .|. (s `shiftR` 8) .|. ((s `shiftL` 1) .&. notA) .|. ((s `shiftR` 1) .&. notH)
          s' = grow .&. complement walls
       in if s' == s then s else go s'

showBoard :: Word64 -> [String]
showBoard b = [[if testBit b (r * 8 + c) then '#' else '.' | c <- [0 .. 7]] | r <- [0 .. 7]]

bitsOf :: Word64 -> [Int]
bitsOf = unfoldr step
  where
    step 0 = Nothing
    step v = Just (countTrailingZeros v, v .&. (v - 1))

main :: IO ()
main = do
  putStrLn "-- per-width reports"
  report "W8" [0, 1, 0x80, 0xA5, 0xFF :: Word8]
  report "I8" [0, 1, -1, -128, 0x5A, -0x5B :: Int8]
  report "W16" [0x8001, 0x00F0, 0xFFFF :: Word16]
  report "I16" [-2, 0x1234, minBound :: Int16]
  report "W32" [1, 0x80000000, 0xDEADBEEF, 0xFFFFFFFF :: Word32]
  report "I32" [-1, minBound, 0x12345678, -0x12345678 :: Int32]
  report "W64" [1, 0x8000000000000000, 0x0123456789ABCDEF, maxBound :: Word64]
  report "I64" [-1, minBound, 0x7EDCBA9876543210, -3 :: Int64]
  report "Int" [0, -1, 0x55AA55AA, minBound :: Int]
  report "Word" [3, 0xF0000000 :: Word]
  putStrLn "-- single-bit operations"
  print ([bit k :: Word8 | k <- [0 .. 7]], bit 31 :: Int32, bit 63 :: Int64, bit 0 :: Word)
  print (setBit (0 :: Word16) 15, clearBit (0xFFFF :: Word16) 0, complementBit (0x0F :: Int8) 7, setBit (-1 :: Int64) 3, clearBit (-1 :: Int64) 63)
  print (map (testBit (0xA5 :: Word8)) [0 .. 7], testBit (minBound :: Int) 31, zeroBits :: Word32)
  print (finiteBitSize (0 :: Int), finiteBitSize (0 :: Word), finiteBitSize (0 :: Int8), finiteBitSize (0 :: Word64), isSigned (0 :: Int16), isSigned (0 :: Word16))
  print (bitSizeMaybe (0 :: Integer), bitSizeMaybe (0 :: Word32), countLeadingZeros (0 :: Word64), countTrailingZeros (0 :: Int16))
  putStrLn "-- signed shifts"
  print [(-100 :: Int32) `shiftR` k | k <- [0, 1, 2, 5, 31, 32, 40]]
  print [(-100 :: Int8) `shiftR` k | k <- [0, 1, 2, 5, 7, 8, 9]]
  print [(minBound :: Int64) `shiftR` k | k <- [1, 32, 62, 63, 64]]
  print [(-1 :: Int16) `shiftL` k | k <- [1, 8, 15, 16]]
  print [(0x40 :: Int8) `shiftL` k | k <- [0, 1, 2]]
  print (unsafeShiftL (1 :: Word32) 31, unsafeShiftR (0x80000000 :: Word32) 31, unsafeShiftR (-8 :: Int) 2, unsafeShiftL (3 :: Int64) 62)
  print ((.&.) (-6 :: Int) 0xFF, (.|.) (-256 :: Int32) 0x7F, xor (-1 :: Int64) 0x0F0F, complement (5 :: Int))
  putStrLn "-- algorithms"
  print (map bitRev32 [1, 0x80000000, 0x12345678, 0xF0F0F0F0, 0xDEADBEEF])
  print (map gray [0 .. 16], map (ungray . gray) [0, 5, 100, 1000, 65535] )
  print (map parity64 [0, 1, 3, 0xFFFFFFFFFFFFFFFF, 0x8000000000000001, 0x0123456789ABCDEF])
  print (take 15 (iterate nextPerm 7), take 6 (iterate nextPerm 0x0F000000))
  print (take 8 crcTable)
  print (map crc32 ["", "a", "abc", "123456789", "The quick brown fox jumps over the lazy dog"])
  let walls = foldl' setBit 0 ([3, 11, 19, 27, 35, 43] ++ [40, 41, 42] ++ [56 .. 62]) :: Word64
      filled = flood walls (bit 0)
  mapM_ putStrLn (zipWith (\a b -> a ++ "  " ++ b) (showBoard walls) (showBoard filled))
  print (popCount filled, filled, bitsOf (filled .&. 0xFF00FF))
  let v = 0xB4 :: Word8
  print (v .&. negate v, v .&. (v - 1), v .|. (v + 1), complement v .&. (v + 1))
  print (foldl' (\acc k -> acc `xor` (fromIntegral k `rotateL` k)) (0 :: Word64) [1 .. 200 :: Int])
  print (foldl' (\acc k -> (acc `shiftL` 5) `xor` (acc `shiftR` 27) `xor` k) (0 :: Word32) (map (fromIntegral . ord) "hash me please"))
  print (sum [popCount (k :: Word16) | k <- [0 .. 4095]], sum [countLeadingZeros (k :: Word32) | k <- [1 .. 1000]], sum [countTrailingZeros (k :: Int64) | k <- [1 .. 1000]])
