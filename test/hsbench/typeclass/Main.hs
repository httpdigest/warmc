{-# LANGUAGE ExistentialQuantification #-}
-- Type classes used for dynamic dispatch: a list of shapes of four types,
-- each wrapped with its Shape dictionary in an existential, scaled and
-- measured every round.
--
-- The Haskell form of cppbench/virtual.  GHC specialises a class method away
-- wherever it can see the type, and an existential is where it cannot: each
-- call selects a method from a dictionary record and enters it as an unknown
-- function, through GHC's generic apply code.
module Main (main) where

import Data.Bits (shiftL, shiftR, xor, (.&.))
import Data.List (foldl')

class Shape s where
  area :: s -> Double
  scale :: Double -> s -> s

data Circle = Circle !Double

data Rect = Rect !Double !Double

data Tri = Tri !Double !Double

data Ring = Ring !Double !Double

instance Shape Circle where
  area (Circle r) = pi * r * r
  scale k (Circle r) = Circle (r * k)

instance Shape Rect where
  area (Rect w h) = w * h
  scale k (Rect w h) = Rect (w * k) (h * k)

instance Shape Tri where
  area (Tri b h) = 0.5 * b * h
  scale k (Tri b h) = Tri (b * k) (h * k)

instance Shape Ring where
  area (Ring r0 r1) = pi * (r1 * r1 - r0 * r0)
  scale k (Ring r0 r1) = Ring (r0 * k) (r1 * k)

data AnyShape = forall s. (Shape s) => AnyShape !s

shapes :: [AnyShape]
shapes = take 2048 (go 2463534242)
  where
    go :: Int -> [AnyShape]
    go x0 =
      let x1 = x0 `xor` ((x0 `shiftL` 13) .&. 0xffffffff)
          x2 = x1 `xor` (x1 `shiftR` 17)
          x = x2 `xor` ((x2 `shiftL` 5) .&. 0xffffffff)
          d = 1 + fromIntegral (x `mod` 1000) / 100
          s = case x `shiftR` 30 of
            0 -> AnyShape (Circle d)
            1 -> AnyShape (Rect d (d * 0.5))
            2 -> AnyShape (Tri d (d + 1))
            _ -> AnyShape (Ring d (d + 0.25))
       in s : go x

roundStep :: (Double, [AnyShape]) -> Int -> (Double, [AnyShape])
roundStep (total, ss) r =
  let k = if odd r then 1.001 else 1 / 1.001
      ss' = map (\(AnyShape s) -> AnyShape (scale k s)) ss
      t = foldl' (\acc (AnyShape s) -> acc + area s) total ss'
   in t `seq` (t, ss')

main :: IO ()
main = print (fst (foldl' roundStep (0, shapes) [1 .. 1000]))
