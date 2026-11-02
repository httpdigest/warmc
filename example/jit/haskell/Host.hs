-- A Haskell host of libwarmc: compiles example/jit/guest.wat, gives it a
-- host function written in Haskell, calls its exports and reads its memory,
-- through the standard WebAssembly C API (wasm.h) and GHC's foreign
-- function interface.
--
--   ghc -O -outputdir build/hs -o host example/jit/haskell/Host.hs -Lbuild -lwarmc -optl-Wl,-rpath,build
--   ./host example/jit/guest.wat
{-# LANGUAGE ForeignFunctionInterface #-}
module Main (main) where

import Control.Monad (forM_, when)
import qualified Data.ByteString as B
import qualified Data.ByteString.Unsafe as BU
import Data.IORef
import Data.Int (Int32, Int64)
import Data.Word (Word8)
import Foreign
import Foreign.C
import System.Environment (getArgs)
import System.Exit (exitFailure)

-- every C API object is opaque here: a pointer the library hands out
type Obj = Ptr ()

-- wasm_byte_vec_t, wasm_val_vec_t, wasm_extern_vec_t...: { size_t size; T *data; }
vecSize, vecData :: Int
vecSize = 16
vecData = 8

-- wasm_val_t: { uint8_t kind; union { i32, i64, f32, f64, ref } of; }
valSize, valOf :: Int
valSize = 16
valOf = 8

kI32, kI64 :: Word8
kI32 = 0
kI64 = 1

foreign import ccall unsafe "wasm_engine_new" engineNew :: IO Obj
foreign import ccall unsafe "wasm_store_new" storeNew :: Obj -> IO Obj
foreign import ccall unsafe "warm_wat2wasm" wat2wasm :: CString -> CSize -> Obj -> Obj -> IO CBool
foreign import ccall safe "warm_module_new" moduleNew :: Obj -> Obj -> Obj -> IO Obj
foreign import ccall unsafe "wasm_valtype_new" valtypeNew :: Word8 -> IO Obj
foreign import ccall unsafe "wasm_valtype_vec_new" valtypeVecNew :: Obj -> CSize -> Ptr Obj -> IO ()
foreign import ccall unsafe "wasm_valtype_vec_new_empty" valtypeVecNewEmpty :: Obj -> IO ()
foreign import ccall unsafe "wasm_functype_new" functypeNew :: Obj -> Obj -> IO Obj
foreign import ccall unsafe "wasm_functype_delete" functypeDelete :: Obj -> IO ()
foreign import ccall unsafe "wasm_func_new" funcNew :: Obj -> Obj -> FunPtr Callback -> IO Obj
foreign import ccall unsafe "wasm_func_as_extern" funcAsExtern :: Obj -> IO Obj
foreign import ccall safe "wasm_instance_new" instanceNew :: Obj -> Obj -> Obj -> Ptr Obj -> IO Obj
foreign import ccall unsafe "wasm_instance_exports" instanceExports :: Obj -> Obj -> IO ()
foreign import ccall unsafe "wasm_module_exports" moduleExports :: Obj -> Obj -> IO ()
foreign import ccall unsafe "wasm_exporttype_name" exporttypeName :: Obj -> IO Obj
foreign import ccall unsafe "wasm_extern_as_func" externAsFunc :: Obj -> IO Obj
foreign import ccall unsafe "wasm_extern_as_memory" externAsMemory :: Obj -> IO Obj
foreign import ccall unsafe "wasm_memory_data" memoryData :: Obj -> IO (Ptr Word8)
-- safe: the wasm it runs calls back into Haskell
foreign import ccall safe "wasm_func_call" funcCall :: Obj -> Obj -> Obj -> IO Obj
foreign import ccall unsafe "wasm_trap_message" trapMessage :: Obj -> Obj -> IO ()
foreign import ccall unsafe "wasm_trap_delete" trapDelete :: Obj -> IO ()
foreign import ccall unsafe "wasm_byte_vec_delete" byteVecDelete :: Obj -> IO ()
foreign import ccall unsafe "wasm_extern_vec_delete" externVecDelete :: Obj -> IO ()
foreign import ccall unsafe "wasm_exporttype_vec_delete" exporttypeVecDelete :: Obj -> IO ()
foreign import ccall unsafe "wasm_instance_delete" instanceDelete :: Obj -> IO ()
foreign import ccall unsafe "wasm_func_delete" funcDelete :: Obj -> IO ()
foreign import ccall unsafe "wasm_module_delete" moduleDelete :: Obj -> IO ()
foreign import ccall unsafe "wasm_store_delete" storeDelete :: Obj -> IO ()
foreign import ccall unsafe "wasm_engine_delete" engineDelete :: Obj -> IO ()

-- wasm_func_callback_t: the arguments' vector, the results' to fill; the
-- trap it raises, or NULL
type Callback = Obj -> Obj -> IO Obj
foreign import ccall "wrapper" mkCallback :: Callback -> IO (FunPtr Callback)

-- a vector's size and data
vecGet :: Obj -> IO (Int, Ptr a)
vecGet v = do
  n <- peekByteOff v 0 :: IO CSize
  d <- peekByteOff v vecData
  return (fromIntegral n, d)

withVec :: Int -> Ptr a -> (Obj -> IO b) -> IO b
withVec n d f = allocaBytes vecSize $ \v -> do
  pokeByteOff v 0 (fromIntegral n :: CSize)
  pokeByteOff v vecData d
  f v

-- a wasm_byte_vec_t's text, up to its NUL
vecText :: Obj -> IO String
vecText v = do
  (_, d) <- vecGet v
  peekCString d

-- host.log(ptr, len): the bytes the guest points at, printed
hostLog :: IORef Obj -> Callback
hostLog memRef args _ = do
  (_, vals) <- vecGet args :: IO (Int, Ptr Word8)
  ptr <- peekByteOff vals valOf :: IO Word32
  len <- peekByteOff vals (valSize + valOf) :: IO Word32
  mem <- readIORef memRef
  base <- memoryData mem
  bytes <- B.packCStringLen (castPtr (base `plusPtr` fromIntegral ptr), fromIntegral len)
  putStrLn ("log: " ++ map (toEnum . fromIntegral) (B.unpack bytes))
  return nullPtr

-- the instance's export named name
exportNamed :: Obj -> Obj -> String -> IO Obj
exportNamed modul exports name = allocaBytes vecSize $ \types -> do
  moduleExports modul types
  (n, typeArray) <- vecGet types :: IO (Int, Ptr Obj)
  (_, externs) <- vecGet exports :: IO (Int, Ptr Obj)
  names <- mapM (\i -> do
                   nm <- exporttypeName =<< peekElemOff typeArray i
                   (len, d) <- vecGet nm
                   peekCStringLen (d, len)) [0 .. n - 1]
  exporttypeVecDelete types
  case lookup name (zip names [0 ..]) of
    Just i -> peekElemOff externs i
    Nothing -> fail ("no export " ++ name)

-- calls func with args (kind, bits), results to res: the trap's message, or Nothing
call :: Obj -> [(Word8, Int64)] -> Ptr Word8 -> Int -> IO (Maybe String)
call func args res nres = allocaBytes (max 1 (length args) * valSize) $ \argv -> do
  forM_ (zip [0 ..] args) $ \(i, (k, v)) -> do
    pokeByteOff argv (i * valSize) k
    if k == kI64 then pokeByteOff argv (i * valSize + valOf) v
                 else pokeByteOff argv (i * valSize + valOf) (fromIntegral v :: Int32)
  trap <- withVec (length args) argv $ \av ->
    withVec nres res $ \rv -> funcCall func av rv
  if trap == nullPtr
    then return Nothing
    else allocaBytes vecSize $ \msg -> do
      trapMessage trap msg
      s <- vecText msg
      byteVecDelete msg
      trapDelete trap
      return (Just s)

main :: IO ()
main = do
  argv <- getArgs
  wat <- B.readFile (case argv of (p : _) -> p; _ -> "example/jit/guest.wat")
  engine <- engineNew
  store <- storeNew engine

  allocaBytes vecSize $ \binary -> allocaBytes vecSize $ \err -> do
    ok <- BU.unsafeUseAsCStringLen wat $ \(p, n) -> wat2wasm p (fromIntegral n) binary err
    when (ok == 0) $ vecText err >>= putStrLn >> exitFailure
    modul <- moduleNew store binary err
    when (modul == nullPtr) $ vecText err >>= putStrLn >> exitFailure

    -- host.log, of type (i32, i32) -> (), calling hostLog
    memRef <- newIORef nullPtr
    callback <- mkCallback (hostLog memRef)
    logFunc <- allocaBytes vecSize $ \params -> allocaBytes vecSize $ \results -> do
      kinds <- mapM valtypeNew [kI32, kI32]
      withArray kinds $ \ks -> valtypeVecNew params 2 ks
      valtypeVecNewEmpty results
      ty <- functypeNew params results
      f <- funcNew store ty callback
      functypeDelete ty
      return f

    ext <- funcAsExtern logFunc
    inst <- withArray [ext] $ \imps -> withVec 1 imps $ \iv -> instanceNew store modul iv nullPtr
    when (inst == nullPtr) $ putStrLn "cannot instantiate" >> exitFailure
    allocaBytes vecSize $ \exports -> do
      instanceExports inst exports
      writeIORef memRef =<< externAsMemory =<< exportNamed modul exports "memory"
      greet <- externAsFunc =<< exportNamed modul exports "greet"
      fib <- externAsFunc =<< exportNamed modul exports "fib"
      dv <- externAsFunc =<< exportNamed modul exports "div"
      peek' <- externAsFunc =<< exportNamed modul exports "peek"

      allocaBytes valSize $ \res -> do
        _ <- call greet [] res 0
        _ <- call fib [(kI64, 90)] res 1
        r <- peekByteOff res valOf :: IO Int64
        putStrLn ("fib(90) = " ++ show r)
        forM_ [(7, 2), (1, 0)] $ \(a, b) -> do
          t <- call dv [(kI32, a), (kI32, b)] res 1
          case t of
            Nothing -> do
              q <- peekByteOff res valOf :: IO Int32
              putStrLn ("div(" ++ show a ++ ", " ++ show b ++ ") = " ++ show q)
            Just m -> putStrLn ("div(" ++ show a ++ ", " ++ show b ++ ") trapped: " ++ m)
        forM_ [16, 65536] $ \addr -> do
          t <- call peek' [(kI32, addr)] res 1
          case t of
            Nothing -> do
              q <- peekByteOff res valOf :: IO Int32
              putStrLn ("peek(" ++ show addr ++ ") = " ++ show q)
            Just m -> putStrLn ("peek(" ++ show addr ++ ") trapped: " ++ m)

      externVecDelete exports
    instanceDelete inst
    funcDelete logFunc
    freeHaskellFunPtr callback
    moduleDelete modul
    byteVecDelete binary
  storeDelete store
  engineDelete engine
