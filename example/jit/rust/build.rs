// Links the tree's libwarmc (make jit), and finds it where it is at run time.
use std::path::PathBuf;

fn main() {
    let lib = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap()).join("../../../build");
    let lib = lib.canonicalize().unwrap_or(lib);
    println!("cargo:rustc-link-search=native={}", lib.display());
    println!("cargo:rustc-link-lib=dylib=warmc");
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() != Ok("windows") {
        println!("cargo:rustc-link-arg=-Wl,-rpath,{}", lib.display());
    }
}
