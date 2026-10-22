// Byte- and text-processing routines of the kind every program carries:
// base64 encode/decode (standard alphabet with padding, and error cases),
// hex dump with an ASCII column, a CSV parser with quoted fields and escaped
// quotes, run-length encoding and decoding, a word-wrap to a column width,
// a template expander with {name} placeholders, string similarity by the
// bigram Dice coefficient, Adler-32 and FNV-1a checksums, ROT13 and Atbash
// ciphers, and an INI-style parser into sorted sections.
//
// These are loops over &[u8] with shifts, masks and table lookups, plus
// String building through push and format!, so they exercise byte loads and
// stores at every offset, small-table indexing, and the allocator growing
// buffers one push at a time.

use std::collections::BTreeMap;
use std::hint::black_box as bb;

const B64: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

fn b64_encode(data: &[u8]) -> String {
    let mut out = String::with_capacity((data.len() + 2) / 3 * 4);
    for chunk in data.chunks(3) {
        let b = [chunk[0], *chunk.get(1).unwrap_or(&0), *chunk.get(2).unwrap_or(&0)];
        let n = (b[0] as u32) << 16 | (b[1] as u32) << 8 | b[2] as u32;
        out.push(B64[(n >> 18) as usize & 63] as char);
        out.push(B64[(n >> 12) as usize & 63] as char);
        out.push(if chunk.len() > 1 { B64[(n >> 6) as usize & 63] as char } else { '=' });
        out.push(if chunk.len() > 2 { B64[n as usize & 63] as char } else { '=' });
    }
    out
}

fn b64_decode(s: &str) -> Result<Vec<u8>, String> {
    let mut rev = [255u8; 256];
    for (i, &c) in B64.iter().enumerate() {
        rev[c as usize] = i as u8;
    }
    let bytes = s.as_bytes();
    if bytes.len() % 4 != 0 {
        return Err(format!("length {} not a multiple of 4", bytes.len()));
    }
    let mut out = Vec::with_capacity(bytes.len() / 4 * 3);
    for (ci, chunk) in bytes.chunks(4).enumerate() {
        let mut n = 0u32;
        let mut pad = 0;
        for (i, &c) in chunk.iter().enumerate() {
            let v = if c == b'=' {
                if i < 2 || ci != bytes.len() / 4 - 1 {
                    return Err(format!("misplaced padding at {}", ci * 4 + i));
                }
                pad += 1;
                0
            } else if pad > 0 {
                return Err("data after padding".into());
            } else {
                match rev[c as usize] {
                    255 => return Err(format!("invalid byte {:?} at {}", c as char, ci * 4 + i)),
                    v => v as u32,
                }
            };
            n = n << 6 | v;
        }
        out.push((n >> 16) as u8);
        if pad < 2 {
            out.push((n >> 8) as u8);
        }
        if pad < 1 {
            out.push(n as u8);
        }
    }
    Ok(out)
}

fn hexdump(data: &[u8]) {
    for (i, row) in data.chunks(16).enumerate() {
        let hex: Vec<String> = row.iter().map(|b| format!("{:02x}", b)).collect();
        let ascii: String = row.iter().map(|&b| if (0x20..0x7f).contains(&b) { b as char } else { '.' }).collect();
        println!("  {:08x}  {:<47}  |{}|", i * 16, hex.join(" "), ascii);
    }
}

fn csv_parse(src: &str) -> Vec<Vec<String>> {
    let mut rows = vec![];
    let mut row = vec![];
    let mut field = String::new();
    let mut in_q = false;
    let mut chars = src.chars().peekable();
    while let Some(c) = chars.next() {
        match (in_q, c) {
            (true, '"') => {
                if chars.peek() == Some(&'"') {
                    field.push('"');
                    chars.next();
                } else {
                    in_q = false;
                }
            }
            (true, c) => field.push(c),
            (false, '"') => in_q = true,
            (false, ',') => row.push(std::mem::take(&mut field)),
            (false, '\n') => {
                row.push(std::mem::take(&mut field));
                rows.push(std::mem::take(&mut row));
            }
            (false, '\r') => {}
            (false, c) => field.push(c),
        }
    }
    if !field.is_empty() || !row.is_empty() {
        row.push(field);
        rows.push(row);
    }
    rows
}

fn rle_encode(s: &str) -> String {
    let mut out = String::new();
    let mut it = s.chars().peekable();
    while let Some(c) = it.next() {
        let mut n = 1;
        while it.peek() == Some(&c) {
            it.next();
            n += 1;
        }
        if n > 1 {
            out.push_str(&n.to_string());
        }
        out.push(c);
    }
    out
}

fn rle_decode(s: &str) -> String {
    let mut out = String::new();
    let mut n = 0usize;
    for c in s.chars() {
        if let Some(d) = c.to_digit(10) {
            n = n * 10 + d as usize;
        } else {
            out.extend(std::iter::repeat(c).take(n.max(1)));
            n = 0;
        }
    }
    out
}

fn wrap(text: &str, width: usize) -> Vec<String> {
    let mut lines = vec![];
    let mut cur = String::new();
    for w in text.split_whitespace() {
        let wl = w.chars().count();
        let cl = cur.chars().count();
        if cl > 0 && cl + 1 + wl > width {
            lines.push(std::mem::take(&mut cur));
        }
        if !cur.is_empty() {
            cur.push(' ');
        }
        cur.push_str(w);
    }
    if !cur.is_empty() {
        lines.push(cur);
    }
    lines
}

fn expand(tpl: &str, vars: &BTreeMap<&str, String>) -> Result<String, String> {
    let mut out = String::new();
    let mut rest = tpl;
    while let Some(open) = rest.find('{') {
        out.push_str(&rest[..open]);
        let after = &rest[open + 1..];
        if after.starts_with('{') {
            out.push('{');
            rest = &after[1..];
            continue;
        }
        let close = after.find('}').ok_or_else(|| format!("unclosed placeholder at {}", tpl.len() - rest.len() + open))?;
        let key = &after[..close];
        let (name, fmt) = key.split_once(':').unwrap_or((key, ""));
        let val = vars.get(name).ok_or_else(|| format!("unknown variable {:?}", name))?;
        match fmt {
            "" => out.push_str(val),
            "upper" => out.push_str(&val.to_uppercase()),
            "len" => out.push_str(&val.chars().count().to_string()),
            f if f.starts_with('>') => {
                let w: usize = f[1..].parse().map_err(|_| format!("bad width {:?}", f))?;
                out.push_str(&format!("{:>w$}", val, w = w));
            }
            f => return Err(format!("unknown format {:?}", f)),
        }
        rest = &after[close + 1..];
    }
    out.push_str(rest);
    Ok(out)
}

fn dice(a: &str, b: &str) -> f64 {
    let bigrams = |s: &str| -> Vec<(char, char)> {
        let cs: Vec<char> = s.to_lowercase().chars().filter(|c| !c.is_whitespace()).collect();
        let mut v: Vec<(char, char)> = cs.windows(2).map(|w| (w[0], w[1])).collect();
        v.sort();
        v
    };
    let (x, y) = (bigrams(a), bigrams(b));
    if x.is_empty() && y.is_empty() {
        return 1.0;
    }
    let (mut i, mut j, mut common) = (0, 0, 0);
    while i < x.len() && j < y.len() {
        if x[i] == y[j] {
            common += 1;
            i += 1;
            j += 1;
        } else if x[i] < y[j] {
            i += 1;
        } else {
            j += 1;
        }
    }
    2.0 * common as f64 / (x.len() + y.len()) as f64
}

fn adler32(data: &[u8]) -> u32 {
    let (mut a, mut b) = (1u32, 0u32);
    for &x in data {
        a = (a + x as u32) % 65521;
        b = (b + a) % 65521;
    }
    b << 16 | a
}

fn fnv1a(data: &[u8]) -> u64 {
    data.iter().fold(0xcbf2_9ce4_8422_2325u64, |h, &b| (h ^ b as u64).wrapping_mul(0x0100_0000_01b3))
}

fn ini(src: &str) -> BTreeMap<String, BTreeMap<String, String>> {
    let mut out: BTreeMap<String, BTreeMap<String, String>> = BTreeMap::new();
    let mut section = String::from("default");
    for line in src.lines() {
        let line = line.split(|c| c == ';' || c == '#').next().unwrap_or("").trim();
        if line.is_empty() {
            continue;
        }
        if line.starts_with('[') && line.ends_with(']') {
            section = line[1..line.len() - 1].trim().to_lowercase();
            continue;
        }
        if let Some((k, v)) = line.split_once('=') {
            out.entry(section.clone()).or_default().insert(k.trim().to_lowercase(), v.trim().trim_matches('"').to_string());
        }
    }
    out
}

fn main() {
    let samples: [&[u8]; 8] = [b"", b"f", b"fo", b"foo", b"foob", b"fooba", b"foobar", b"Many hands make light work."];
    for s in samples.iter() {
        let e = b64_encode(bb(s));
        let d = b64_decode(&e);
        println!("b64 {:?} -> {:?} -> ok {}", String::from_utf8_lossy(s), e, d.as_deref() == Ok(*s));
    }
    let binary: Vec<u8> = (0..=255u8).step_by(7).chain([0, 255, 128]).collect();
    let enc = b64_encode(&binary);
    println!("binary b64 {}", enc);
    println!("binary roundtrip {}", b64_decode(&enc).map(|d| d == binary).unwrap_or(false));
    for bad in ["abc", "ab=c", "a===", "ab!d", "Zm9v=Zm9v", "Zg==", "Zm8="] {
        println!("b64 decode {:?} -> {:?}", bad, b64_decode(bad).map(|v| String::from_utf8_lossy(&v).into_owned()));
    }

    println!("hexdump:");
    let mut blob: Vec<u8> = b"warmc compiles wasm \x00\x01\x02 ahead of time\n\ttabs and \xff bytes".to_vec();
    blob.extend((0..20u8).map(|i| i.wrapping_mul(13)));
    hexdump(&blob);

    let csv = "name,age,quote\n\"Smith, Ann\",31,\"She said \"\"hi\"\"\"\nBob,,plain\r\n\"multi\nline\",7,\"\"\nlast,1,no-newline";
    for (i, row) in csv_parse(bb(csv)).iter().enumerate() {
        println!("csv row {} ({} fields): {:?}", i, row.len(), row);
    }

    for s in ["aaabccddddde", "abc", "", "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz", "ééé ü"] {
        let e = rle_encode(s);
        println!("rle {:?} -> {:?} -> {:?} ok {}", s, e, rle_decode(&e), rle_decode(&e) == s);
    }

    let para = "The quick brown fox jumps over the lazy dog. Pack my box with five dozen liquor jugs! How vexingly quick daft zebras jump; sphinx of black quartz, judge my vow. Ünïcödé wörds count by chars.";
    for width in [20, 33, 60] {
        println!("wrap {}:", width);
        for l in wrap(para, width) {
            println!("  |{:<w$}| {}", l, l.chars().count(), w = width);
        }
    }

    let mut vars = BTreeMap::new();
    vars.insert("name", "warmc".to_string());
    vars.insert("lang", "rust".to_string());
    vars.insert("n", "42".to_string());
    for tpl in ["Hello {name}!", "{lang:upper} has {name:len} letters", "[{n:>6}] {{literal}}", "{missing}", "{name:weird}", "open {name", "no placeholders", "{name}{lang}{n}"] {
        println!("template {:?} -> {:?}", tpl, expand(tpl, &vars));
    }

    let pairs = [("night", "nacht"), ("context", "contact"), ("healed", "sealed"), ("Rust", "rust"), ("abc", "xyz"), ("", ""), ("warmc", "wasmtime")];
    for (a, b) in pairs {
        println!("dice {:?} {:?} = {:.6}", a, b, dice(a, b));
    }

    for s in ["", "a", "abc", "Wikipedia", "The quick brown fox jumps over the lazy dog"] {
        println!("adler32 {:?} = {:08x} fnv1a = {:016x}", s, adler32(bb(s.as_bytes())), fnv1a(s.as_bytes()));
    }
    let big: Vec<u8> = (0..20000u32).map(|i| (i * 31 % 251) as u8).collect();
    println!("adler32 20000 bytes {:08x} fnv {:016x}", adler32(&big), fnv1a(&big));

    let rot13: String = "Hello, World! Why did the chicken cross the road?".chars().map(|c| match c {
        'a'..='z' => (((c as u8 - b'a') + 13) % 26 + b'a') as char,
        'A'..='Z' => (((c as u8 - b'A') + 13) % 26 + b'A') as char,
        _ => c,
    }).collect();
    let atbash: String = rot13.chars().map(|c| match c {
        'a'..='z' => (b'z' - (c as u8 - b'a')) as char,
        'A'..='Z' => (b'Z' - (c as u8 - b'A')) as char,
        _ => c,
    }).collect();
    println!("rot13 {}", rot13);
    println!("atbash {}", atbash);

    let conf = "; global\nname = \"demo\"\n[Server]\nHost = example.org\nport=8080 # inline\n\n[client]\nretries = 3\ntimeout= 2.5\n[SERVER]\nport = 9090\n";
    for (sec, kv) in ini(conf) {
        println!("ini [{}] {:?}", sec, kv);
    }
}
