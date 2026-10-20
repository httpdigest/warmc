// An exit status other than 0 or 1, which wasi:cli/exit's `exit` cannot say.
//
// `exit` takes a result, so through it a component can only succeed or fail;
// rustc's std reaches for `exit-with-code` for any other std::process::exit,
// and a host that bound only `exit` traps there instead of exiting.  tinygo
// builds against WIT that predates the function, so this suite is the only
// one that asks for it.  The line printed first has to arrive as well: exit
// runs no destructors, and stdout is flushed per line or not at all.
fn main() {
    println!("leaving with 42");
    std::process::exit(42);
}
