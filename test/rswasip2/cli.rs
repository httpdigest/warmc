// What the command line and the terminal look like to a Rust guest.
//
// Rust's std writes through check-write/write/flush where tinygo calls
// blocking-write-and-flush, so every line printed here goes down a path the
// Go suites never take.  It also asks whether the three standard descriptors
// are terminals -- wasi:cli/terminal-stdin and its two siblings -- which
// nothing else does at all; under the harness they are files, and both
// engines have to say so.
use std::io::{IsTerminal, Write};

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    println!("args {:?}", args);

    let mut env: Vec<(String, String)> = std::env::vars().collect();
    env.sort();
    println!("env {:?}", env);
    println!("lookup {:?} {:?}", std::env::var("PROBE"), std::env::var("ABSENT").is_err());

    println!("stdin tty {}", std::io::stdin().is_terminal());
    println!("stdout tty {}", std::io::stdout().is_terminal());
    println!("stderr tty {}", std::io::stderr().is_terminal());

    // A write large enough that the guest has to consult check-write more
    // than once, whatever permit it is given.
    let big = "x".repeat(10000);
    let mut out = std::io::stdout();
    out.write_all(big.as_bytes()).unwrap();
    out.write_all(b"\n").unwrap();
    out.flush().unwrap();

    eprintln!("on stderr");
}
