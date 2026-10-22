// Strings and UTF-8: chars, bytes, char_indices, len vs char count on
// multi-byte text, split/split_whitespace/splitn/rsplit/split_terminator/
// lines/split_once, trim and its _start/_end/_matches forms, replace/replacen,
// find/rfind/contains/starts_with with &str, char and closure patterns,
// matches/match_indices, to_uppercase/to_lowercase including characters whose
// case mapping changes length, char methods (is_alphabetic, to_digit,
// from_u32, from_digit, len_utf8, encode_utf8, escape_*), String building with
// push/insert/remove/retain/drain/truncate, byte-level slicing on char
// boundaries, from_utf8 errors, from_utf8_lossy, and UTF-16 round trips.
//
// The decoders are byte-at-a-time state machines with branches on the high
// bits of each byte, and the searchers (`str::find` with a &str needle is
// Two-Way, with a char it is memchr) use word-at-a-time tricks over u32/u64
// loads.  Inputs mix ASCII with 2-, 3- and 4-byte sequences so every decode
// arm runs.

use std::hint::black_box as bb;

fn chars_and_bytes() {
    let samples = ["hello", "héllo", "naïve café", "日本語テキスト", "emoji 🦀🎉!", "ß→SS", "Ǆ ǅ ǆ", "", "a\u{301}"];
    for s in samples {
        let s = bb(s);
        println!(
            "{:?}: len {} chars {} upper {:?} lower {:?} rev {:?}",
            s,
            s.len(),
            s.chars().count(),
            s.to_uppercase(),
            s.to_lowercase(),
            s.chars().rev().collect::<String>()
        );
        println!("   bytes {:?}", s.bytes().take(16).collect::<Vec<u8>>());
        println!("   char_indices {:?}", s.char_indices().map(|(i, c)| (i, c as u32)).collect::<Vec<_>>());
        let lens: Vec<usize> = s.chars().map(|c| c.len_utf8()).collect();
        let u16s: Vec<u16> = s.encode_utf16().collect();
        println!("   utf8 lens {:?} utf16 units {} roundtrip {}", lens, u16s.len(), String::from_utf16(&u16s).map(|t| t == s).unwrap_or(false));
        println!("   is_char_boundary {:?}", (0..=s.len()).filter(|&i| s.is_char_boundary(i)).collect::<Vec<_>>());
    }
}

fn char_methods() {
    for c in ['a', 'Z', '7', ' ', '\n', 'é', 'ß', 'Σ', 'ς', '日', '🦀', '\u{0}', '_', 'Ⅻ', '٣'] {
        let c = bb(c);
        let mut buf = [0u8; 4];
        let enc = c.encode_utf8(&mut buf);
        println!(
            "{:?} U+{:04X}: alpha {} num {} alnum {} ws {} upper {} lower {} ctrl {} ascii {} digit10 {:?} digit16 {:?} up {:?} low {:?} utf8 {:?} esc {} dbg {}",
            c,
            c as u32,
            c.is_alphabetic(),
            c.is_numeric(),
            c.is_alphanumeric(),
            c.is_whitespace(),
            c.is_uppercase(),
            c.is_lowercase(),
            c.is_control(),
            c.is_ascii(),
            c.to_digit(10),
            c.to_digit(16),
            c.to_uppercase().collect::<String>(),
            c.to_lowercase().collect::<String>(),
            enc.as_bytes(),
            c.escape_unicode(),
            c.escape_debug()
        );
    }
    for d in 0..20u32 {
        print!("{:?}", std::char::from_digit(d, 16));
        print!(" ");
    }
    println!();
    for v in [0x41u32, 0xe9, 0xd800, 0xdfff, 0x10ffff, 0x110000, 0x1f980] {
        println!("from_u32 {:#x} -> {:?}", v, std::char::from_u32(bb(v)));
    }
    let ascii: String = (b'!'..=b'~').map(|b| b as char).filter(|c| c.is_ascii_punctuation()).collect();
    println!("ascii punct {}", ascii);
    println!("ascii ops {} {} {} {}", 'q'.to_ascii_uppercase(), 'Q'.to_ascii_lowercase(), 'é'.to_ascii_uppercase(), 'a'.eq_ignore_ascii_case(&'A'));
}

fn splitting() {
    let csv = bb("name,age,,city\nann,31,,paris\nbob,,x,berlin\n");
    for (i, line) in csv.lines().enumerate() {
        println!("line {}: {:?} fields {}", i, line.split(',').collect::<Vec<_>>(), line.split(',').filter(|f| !f.is_empty()).count());
    }
    let s = bb("  the  quick\tbrown \n fox  ");
    println!("split_whitespace {:?}", s.split_whitespace().collect::<Vec<_>>());
    println!("split ' ' {:?}", s.split(' ').collect::<Vec<_>>());
    println!("splitn 3 {:?}", s.trim().splitn(3, ' ').collect::<Vec<_>>());
    println!("rsplitn 2 {:?}", "a/b/c/d".rsplitn(2, '/').collect::<Vec<_>>());
    println!("rsplit {:?}", "a/b/c/d".rsplit('/').collect::<Vec<_>>());
    println!("split_terminator {:?}", "a;b;c;".split_terminator(';').collect::<Vec<_>>());
    println!("split closure {:?}", "a1b22c333d".split(|c: char| c.is_ascii_digit()).collect::<Vec<_>>());
    println!("split str {:?}", "one<>two<><>three".split("<>").collect::<Vec<_>>());
    println!("split chars {:?}", "a-b_c d".split(&['-', '_', ' '][..]).collect::<Vec<_>>());
    println!("split_inclusive {:?}", "line1\nline2\nend".split_inclusive('\n').collect::<Vec<_>>());
    println!("split_once {:?} {:?} rsplit_once {:?}", "k=v=w".split_once('='), "none".split_once('='), "k=v=w".rsplit_once('='));
    println!("split_at {:?}", "héllo".split_at(3));
    println!("lines crlf {:?}", "a\r\nb\n\nc\r\n".lines().collect::<Vec<_>>());
    println!("unicode split {:?}", "日本-語-テ".split('-').map(|p| p.chars().count()).collect::<Vec<_>>());
}

fn trimming_and_search() {
    let s = bb("xx--Hello, World!--xx");
    println!("trim_matches {:?}", s.trim_matches('x'));
    println!("trim_start_matches {:?}", s.trim_start_matches("xx-"));
    println!("trim_end_matches {:?}", s.trim_end_matches(|c| c == 'x' || c == '-'));
    println!("strip_prefix {:?} {:?} strip_suffix {:?}", s.strip_prefix("xx"), s.strip_prefix("yy"), s.strip_suffix("-xx"));
    println!("trim {:?} {:?} {:?}", "  \t pad \n".trim(), "  \t pad \n".trim_start(), "  \t pad \n".trim_end());
    let hay = bb("the cat sat on the mat with the hat");
    println!("find {:?} rfind {:?} find char {:?} find closure {:?} find missing {:?}", hay.find("the"), hay.rfind("the"), hay.find('s'), hay.find(|c: char| c == 'w'), hay.find("dog"));
    println!("contains {} {} starts {} ends {}", hay.contains("sat on"), hay.contains('z'), hay.starts_with("the c"), hay.ends_with(char::is_alphabetic));
    println!("matches {:?}", hay.matches("at").count());
    println!("match_indices {:?}", hay.match_indices("he").collect::<Vec<_>>());
    println!("rmatch_indices {:?}", hay.rmatch_indices('t').map(|(i, _)| i).collect::<Vec<_>>());
    println!("replace {:?}", hay.replace("the", "a"));
    println!("replacen {:?}", hay.replacen('a', "4", 3));
    println!("replace char-closure {:?}", "a1b2c3".replace(|c: char| c.is_numeric(), "#"));
    let uni = bb("Grüße aus Köln, schöne Grüße");
    println!("unicode find {:?} rfind {:?} replace {:?}", uni.find("Grüße"), uni.rfind("Grüße"), uni.replace("ü", "ue"));
    let long = "ab".repeat(500) + "abc" + &"ab".repeat(100);
    println!("long find {:?} rfind {:?} count ab {}", long.find("abc"), long.rfind("ba"), long.matches("ab").count());
    println!("cmp {:?} {:?} {:?} eq_ignore_case {}", "apple".cmp("apricot"), "Zebra".cmp("apple"), "abc".cmp("ab"), "HeLLo".eq_ignore_ascii_case("hello"));
}

fn building() {
    let mut s = String::with_capacity(8);
    for (i, w) in ["alpha", "beta", "gamma", "délta"].iter().enumerate() {
        if i > 0 {
            s.push_str(", ");
        }
        s.push_str(w);
        s.push(if i % 2 == 0 { '!' } else { '?' });
    }
    println!("built {:?} len {}", s, s.len());
    s.insert(0, '[');
    s.insert_str(1, ">> ");
    s.push(']');
    println!("insert {:?}", s);
    let r = s.remove(1);
    println!("remove {:?} -> {:?}", r, s);
    s.retain(|c| c != ' ' && c != ',');
    println!("retain {:?}", s);
    let dr: String = s.drain(..3).collect();
    println!("drain {:?} rest {:?}", dr, s);
    s.truncate(10);
    println!("truncate {:?} pop {:?} {:?}", s.clone(), s.clone().pop(), s.split_off(5));
    s.replace_range(1..3, "XYZ");
    println!("replace_range {:?}", s);
    s.clear();
    println!("clear {:?} empty {}", s, s.is_empty());
    let mut acc = String::new();
    for i in 0..30 {
        acc += &format!("{:x}", i * i);
        if acc.len() > 20 {
            println!("acc {} {:?}", i, acc);
            acc = acc.chars().rev().take(5).collect();
        }
    }
    let parts = vec!["x".to_string(), "y".to_string(), "z".to_string()];
    let joined = parts.join("::");
    let concat: String = parts.concat();
    println!("join {} concat {} repeat {}", joined, concat, "ab".repeat(3));
    let t: String = "Hello World".chars().map(|c| if c.is_uppercase() { c.to_ascii_lowercase() } else { c.to_ascii_uppercase() }).collect();
    println!("swapcase {}", t);
    let mut ascii = String::from("MiXeD cAsE");
    ascii.make_ascii_lowercase();
    println!("make_ascii_lowercase {}", ascii);
    let words: Vec<String> = "the rain in spain".split(' ').map(|w| {
        let mut c = w.chars();
        match c.next() {
            Some(f) => f.to_uppercase().collect::<String>() + c.as_str(),
            None => String::new(),
        }
    }).collect();
    println!("title {}", words.join(" "));
    println!("char_count vs bytes {:?}", ["ü", "€", "𝄞"].iter().map(|s| (s.len(), s.chars().count())).collect::<Vec<_>>());
    let s = "héllo wörld";
    println!("slices {:?} {:?} {:?} get bad {:?} get ok {:?}", &s[0..1], &s[1..3], &s[7..], s.get(2..4), s.get(1..3));
}

fn utf8_errors() {
    let inputs: [&[u8]; 7] = [b"plain", b"caf\xc3\xa9", b"bad\xffbyte", b"trunc\xe6\x97", b"\xf0\x9f\xa6\x80crab", b"\xed\xa0\x80surrogate", b"over\xc0\xaflong"];
    for b in inputs.iter() {
        match std::str::from_utf8(bb(b)) {
            Ok(s) => println!("utf8 ok {:?}", s),
            Err(e) => println!("utf8 err valid_up_to {} error_len {:?} lossy {:?}", e.valid_up_to(), e.error_len(), String::from_utf8_lossy(b)),
        }
    }
    let v = vec![0xe2u8, 0x82, 0xac, 0x41];
    println!("from_utf8 vec {:?}", String::from_utf8(v));
    let bad = vec![0x41u8, 0x80];
    println!("from_utf8 bad {:?}", String::from_utf8(bad).map_err(|e| e.into_bytes()));
    let u = [0xd83eu16, 0xdd80, 0x0041, 0xd800];
    println!("utf16 lossy {:?} strict {:?}", String::from_utf16_lossy(&u), String::from_utf16(&u).is_ok());
    let bytes = "Ünïcödé".as_bytes();
    println!("bytes {:x?}", bytes);
    println!("escape_default {} escape_debug {}", "tab\there \"q\" é\n".escape_default(), "tab\there é\n".escape_debug());
}

fn main() {
    chars_and_bytes();
    char_methods();
    splitting();
    trimming_and_search();
    building();
    utf8_errors();
}
