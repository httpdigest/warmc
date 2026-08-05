// Files through streams, which is the way Rust reaches them.
//
// tinygo calls descriptor.read and descriptor.write with an explicit offset.
// Rust's std opens a stream over the descriptor instead -- read-via-stream,
// write-via-stream, append-via-stream -- and those carry their own position,
// because preview2 has no seek.  A host that kept one position for the
// descriptor rather than one per stream answers the first read and then
// starts every later one in the wrong place, which is what reading the file
// twice below is for.
use std::fs;
use std::io::{Read, Seek, SeekFrom, Write};

fn main() {
    let d = "/d";

    fs::write(format!("{d}/w.txt"), b"first\n").unwrap();
    println!("read {:?}", fs::read_to_string(format!("{d}/w.txt")));

    let mut f = fs::OpenOptions::new().append(true).open(format!("{d}/w.txt")).unwrap();
    f.write_all(b"second\n").unwrap();
    drop(f);
    println!("appended {:?}", fs::read_to_string(format!("{d}/w.txt")));

    // Twice through the same file, so a position kept in the wrong place is
    // visible on the second pass.
    for pass in 0..2 {
        let mut f = fs::File::open(format!("{d}/w.txt")).unwrap();
        let mut s = String::new();
        f.read_to_string(&mut s).unwrap();
        println!("pass {pass} {:?}", s);
    }

    // Seeking is the guest keeping the position, since preview2 has none.
    let mut f = fs::File::open(format!("{d}/w.txt")).unwrap();
    f.seek(SeekFrom::Start(6)).unwrap();
    let mut rest = String::new();
    f.read_to_string(&mut rest).unwrap();
    println!("from 6 {:?}", rest);

    let m = fs::metadata(format!("{d}/w.txt")).unwrap();
    println!("meta len {} dir {} file {}", m.len(), m.is_dir(), m.is_file());
    let sub = fs::metadata(format!("{d}/sub")).unwrap();
    println!("sub dir {} file {}", sub.is_dir(), sub.is_file());

    fs::create_dir(format!("{d}/made")).unwrap();
    fs::write(format!("{d}/made/a.txt"), b"a").unwrap();
    let mut names: Vec<String> = fs::read_dir(format!("{d}/made")).unwrap()
        .map(|e| e.unwrap().file_name().into_string().unwrap()).collect();
    names.sort();
    println!("made {:?}", names);

    fs::rename(format!("{d}/made/a.txt"), format!("{d}/made/b.txt")).unwrap();
    fs::remove_file(format!("{d}/made/b.txt")).unwrap();
    fs::remove_dir(format!("{d}/made")).unwrap();
    println!("cleaned {}", !std::path::Path::new(&format!("{d}/made")).exists());

    // Something bigger than any one write, to push the stream loop.
    let big: Vec<u8> = (0..300000u32).map(|i| (i % 251) as u8).collect();
    fs::write(format!("{d}/big.bin"), &big).unwrap();
    let back = fs::read(format!("{d}/big.bin")).unwrap();
    println!("big {} {}", back.len(), back == big);
}
