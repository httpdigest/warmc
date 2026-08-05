// The error kinds Rust maps the host's error-code onto.
//
// A Go guest prints an errno; a Rust guest branches on io::ErrorKind, which
// is a second, independent reading of the same 37-case enum.  A mapping that
// is wrong but plausible survives the first and not the second: NotFound and
// PermissionDenied are different words for codes that are adjacent numbers.
use std::fs;

fn kind(r: std::io::Result<impl Sized>) -> String {
    match r {
        Ok(_) => "ok".to_string(),
        Err(e) => format!("{:?}", e.kind()),
    }
}

fn main() {
    let d = "/d";
    println!("open missing {}", kind(fs::File::open(format!("{d}/nope"))));
    println!("stat missing {}", kind(fs::metadata(format!("{d}/nope"))));
    println!("read dir as file {}", kind(fs::read(format!("{d}/sub"))));
    println!("mkdir existing {}", kind(fs::create_dir(format!("{d}/sub"))));
    println!("rmdir a file {}", kind(fs::remove_dir(format!("{d}/seed.txt"))));
    println!("unlink a dir {}", kind(fs::remove_file(format!("{d}/sub"))));
    println!("through a file {}", kind(fs::read(format!("{d}/seed.txt/inner"))));
    println!("rename missing {}", kind(fs::rename(format!("{d}/nope"), format!("{d}/other"))));
    println!("create in missing {}", kind(fs::write(format!("{d}/nodir/x"), b"x")));
    println!("readlink a file {}", kind(fs::read_link(format!("{d}/seed.txt"))));

    fs::create_dir(format!("{d}/full")).unwrap();
    fs::write(format!("{d}/full/x"), b"x").unwrap();
    println!("rmdir non-empty {}", kind(fs::remove_dir(format!("{d}/full"))));
    fs::remove_file(format!("{d}/full/x")).unwrap();
    println!("rmdir emptied {}", kind(fs::remove_dir(format!("{d}/full"))));
}
