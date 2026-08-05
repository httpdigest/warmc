// The preopen, from a second guest.
//
// test/wasip2/escape.go asks the same questions through tinygo.  Asking them
// again through Rust is not repetition: the two resolve a path differently
// before the host ever sees it -- Rust's std keeps its own preopen table and
// splits a path against it -- so a walk that refuses one and not the other is
// a walk that is being told what to do by the guest rather than by the
// sandbox.  The links are made by run_rswasip2.sh, since neither guest can.
use std::fs;

fn try_read(what: &str, path: &str) {
    match fs::read(path) {
        Ok(b) => println!("{what} READ {} bytes", b.len()),
        Err(_) => println!("{what} refused"),
    }
}

fn main() {
    let d = "/d";
    try_read("absolute:", "/etc/hosts");
    try_read("dotdot past the root:", &format!("{d}/../../../etc/hosts"));
    try_read("through an absolute link:", &format!("{d}/abs-link"));
    try_read("through a climbing link:", &format!("{d}/rel-link"));
    try_read("through a link mid-path:", &format!("{d}/up/etc/hosts"));

    try_read("dotdot that stays in:", &format!("{d}/sub/../seed.txt"));
    try_read("through a link that stays in:", &format!("{d}/inside"));

    println!("write past the root {}",
             fs::write(format!("{d}/../escaped.txt"), b"x").is_err());
    println!("mkdir past the root {}",
             fs::create_dir(format!("{d}/../escaped-dir")).is_err());
    println!("remove past the root {}",
             fs::remove_file(format!("{d}/../seed.txt")).is_err());
}
