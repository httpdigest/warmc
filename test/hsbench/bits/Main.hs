{-# LANGUAGE BangPatterns #-}
-- Data.Bits on fixed-width types: a splitmix64 stream mixed into a murmur3
-- style Word32 hash, popCount and leading and trailing zero counts on
-- Word64, a Word32 bit reversal by masks and shifts, and running sums in
-- Word8, Int16 and Int32 that are meant to wrap.
--
-- Word64 arithmetic is native i64 even on this 32-bit target, and the narrow
-- types are i32 values masked after every operation.  rotateL is a shift pair
-- and an or; popCount on a Word64 is a call to the C helper hs_popcnt64, a
-- byte-table lookup, and countLeadingZeros and countTrailingZeros are calls to
-- one-instruction wrappers around i64.clz and i64.ctz.  The loop is one wasm
-- loop with its accumulators in locals, plus a heap check every iteration for
-- the record allocated only when it exits.
module Main (main) where

import Data.Bits
import Data.Int (Int16, Int32)
import Data.Word (Word32, Word64, Word8)

mix64 :: Word64 -> Word64
mix64 z0 =
  let z1 = (z0 `xor` (z0 `shiftR` 30)) * 0xbf58476d1ce4e5b9
      z2 = (z1 `xor` (z1 `shiftR` 27)) * 0x94d049bb133111eb
   in z2 `xor` (z2 `shiftR` 31)

reverse32 :: Word32 -> Word32
reverse32 x0 =
  let x1 = ((x0 `shiftR` 1) .&. 0x55555555) .|. ((x0 .&. 0x55555555) `shiftL` 1)
      x2 = ((x1 `shiftR` 2) .&. 0x33333333) .|. ((x1 .&. 0x33333333) `shiftL` 2)
      x3 = ((x2 `shiftR` 4) .&. 0x0f0f0f0f) .|. ((x2 .&. 0x0f0f0f0f) `shiftL` 4)
      x4 = ((x3 `shiftR` 8) .&. 0x00ff00ff) .|. ((x3 .&. 0x00ff00ff) `shiftL` 8)
   in (x4 `shiftR` 16) .|. (x4 `shiftL` 16)

murmur :: Word32 -> Word32 -> Word32
murmur h k0 =
  let k1 = rotateL (k0 * 0xcc9e2d51) 15 * 0x1b873593
      h1 = rotateL (h `xor` k1) 13
   in h1 * 5 + 0xe6546b64

fmix32 :: Word32 -> Word32
fmix32 h0 =
  let h1 = (h0 `xor` (h0 `shiftR` 16)) * 0x85ebca6b
      h2 = (h1 `xor` (h1 `shiftR` 13)) * 0xc2b2ae35
   in h2 `xor` (h2 `shiftR` 16)

data Acc = Acc !Word32 !Int !Int !Word32 !Word8 !Int16 !Int32

run :: Int -> Word64 -> Acc
run n seed = go 0 seed (Acc 0 0 0 0 0 0 0)
  where
    go !i !s acc@(Acc h pc zs rv b8 i16 i32)
      | i >= n = acc
      | otherwise =
          let s' = s + 0x9e3779b97f4a7c15
              z = mix64 s'
              lo = fromIntegral z :: Word32
              hi = fromIntegral (z `shiftR` 32) :: Word32
              h' = murmur (murmur h lo) hi
              pc' = pc + popCount z
              zs' = zs + countLeadingZeros (z .&. 0x00ffffffffffffff) + countTrailingZeros (z .|. 0x8000000000000000)
              rv' = rv `xor` reverse32 (lo `xor` complement hi)
              b8' = b8 * 3 + fromIntegral z
              i16' = i16 * 5 - fromIntegral (z `shiftR` 7)
              i32' = (i32 `xor` fromIntegral hi) * 1103515245 + (if testBit z 17 then 12345 else 54321)
           in go (i + 1) s' (Acc h' pc' zs' rv' b8' i16' i32')

main :: IO ()
main = do
  let Acc h pc zs rv b8 i16 i32 = run 9000000 42
  print (fmix32 h, pc, zs, rv, b8, i16, i32)
