// Integer formatting and parsing: format! with width, fill, alignment, sign,
// zero padding, `#` alternate forms for hex/octal/binary, upper-case hex,
// width and precision taken from arguments, Debug hex (`{:x?}`), named and
// positional arguments; then to_string / parse::<T> for every width with all
// the error kinds, from_str_radix in bases 2 to 36, and a hand-written
// to-radix routine checked against the formatter.
//
// core's integer Display divides by 100 at a time and indexes a two-digit
// lookup table, and the hex/binary forms shift and mask; the padding logic is
// shared by every type and works on character counts, not bytes.  Parsing is
// a multiply-add loop with an overflow check per digit whose shape depends on
// the type's width and signedness.

use std::fmt::Write as _;
use std::hint::black_box as bb;

fn to_radix(mut n: u64, radix: u32) -> String {
    if n == 0 {
        return "0".into();
    }
    let mut out = Vec::new();
    while n > 0 {
        let d = (n % radix as u64) as u32;
        out.push(std::char::from_digit(d, radix).unwrap_or('?'));
        n /= radix as u64;
    }
    out.iter().rev().collect()
}

fn widths() {
    for v in [0i32, 7, -7, 42, -42, 1234567, i32::MIN, i32::MAX] {
        let v = bb(v);
        println!(
            "[{}] [{:5}] [{:<5}] [{:^7}] [{:>8}] [{:+}] [{:05}] [{:+06}] [{:*^9}] [{:_<6}] [{:#>4}]",
            v, v, v, v, v, v, v, v, v, v, v
        );
    }
    for v in [0u32, 1, 255, 256, 0xdead_beef, u32::MAX] {
        let v = bb(v);
        println!(
            "[{:x}] [{:X}] [{:#x}] [{:#X}] [{:o}] [{:#o}] [{:b}] [{:#b}] [{:08x}] [{:#010x}] [{:#34b}] [{:>10o}] [{:e}]",
            v, v, v, v, v, v, v, v, v, v, v, v, v
        );
    }
    for v in [-1i8, -128, 127, 0] {
        let v = bb(v);
        println!("i8 [{:x}] [{:#b}] [{:o}] [{:4}] [{:+04}]", v, v, v, v, v);
    }
    for v in [-1i64, i64::MIN, 0x0123_4567_89ab_cdef] {
        let v = bb(v);
        println!("i64 [{:x}] [{:#X}] [{:#o}] [{:25}] [{:<+25}]", v, v, v, v, v);
    }
    let w = bb(9usize);
    let p = bb(3usize);
    println!("[{:w$}] [{:>w$}] [{:^w$.p$}] [{:.*}] [{:>w2$}]", 42, 6, 1.0 / 3.0, 2, 2.5f64, "s", w = w, p = p, w2 = bb(5usize));
    println!("[{0}-{1}-{0}] [{a}{b}{a}] [{0:?}] [{x:#06x}]", "p", "q", a = 1, b = 2, x = 255);
    let name = "inline";
    let count = 3;
    println!("[{name}] [{count:>4}] [{name:?}] [{count:#b}]");
    println!("debug hex {:x?} {:X?} {:#x?}", vec![10u8, 255, 16], [0xabcu16, 1], Some(255u32));
    println!("debug pretty {:#?}", (1, "two", [3u8, 4]));
    println!("chars [{:5}] [{:>3}] [{:^5}] [{:?}]", 'x', 'é', '日', '\'');
    println!("strings [{:8}] [{:>8}] [{:^8}] [{:.3}] [{:>8.2}] [{:-^12}]", "ab", "日本", "mid", "truncate", "xyz", "é");
    println!("bool [{}] [{:>6}] [{:<6}] [{:?}]", true, false, true, false);
    println!("ptr-free unit {:?} tuple {:?} nested {:?}", (), ((1,), [(2, 3)]), Some(Ok::<i32, ()>(4)));
}

fn writes() {
    let mut s = String::new();
    for i in 0..12u32 {
        let _ = write!(s, "{:>3}|", i * i * i % 997);
        if i % 4 == 3 {
            let _ = writeln!(s);
        }
    }
    print!("{}", s);
    let table: Vec<String> = (1..=8u32).map(|i| format!("{:>2} {:>6b} {:>3o} {:>2x} {:>4}", i * 3, i * 3, i * 3, i * 3, (i * 3).pow(2))).collect();
    for row in table {
        println!("table {}", row);
    }
}

macro_rules! roundtrip {
    ($t:ty, $vals:expr) => {{
        for &v in $vals.iter() {
            let v: $t = bb(v);
            let s = v.to_string();
            let back = s.parse::<$t>();
            let hex = format!("{:x}", v);
            let bin = format!("{:b}", v);
            println!(
                "{} {}: back {:?} hex {} bin_len {} radix16 {:?}",
                stringify!($t),
                s,
                back,
                hex,
                bin.len(),
                <$t>::from_str_radix(&hex, 16).ok()
            );
        }
    }};
}

fn parsing() {
    roundtrip!(u8, [0u8, 9, 10, 99, 100, 255]);
    roundtrip!(u16, [0u16, 999, 1000, 65535]);
    roundtrip!(u32, [0u32, 4_294_967_295, 1_000_000_000]);
    roundtrip!(u64, [0u64, 18_446_744_073_709_551_615, 10_000_000_000_000_000_000]);
    roundtrip!(i32, [0i32, 1, 2_147_483_647]);
    let inputs = ["0", "-0", "+0", "127", "128", "-128", "-129", "255", "256", "65535", "-32768", "4294967295", "4294967296", "-2147483648", "9223372036854775807", "9223372036854775808", "-9223372036854775808", "18446744073709551615", "18446744073709551616", "", "+", "-", " 1", "1 ", "1_000", "0x1f", "١٢"];
    for s in inputs {
        println!(
            "{:?}: i8 {:?} u8 {:?} i16 {:?} u16 {:?} i32 {:?} u32 {:?} i64 {:?} u64 {:?}",
            s,
            s.parse::<i8>().ok(),
            s.parse::<u8>().ok(),
            s.parse::<i16>().ok(),
            s.parse::<u16>().ok(),
            s.parse::<i32>().ok(),
            s.parse::<u32>().ok(),
            s.parse::<i64>().ok(),
            s.parse::<u64>().ok()
        );
        if let Err(e) = s.parse::<i16>() {
            println!("   i16 error {:?}: {}", e.kind(), e);
        }
    }
    for radix in [2u32, 3, 7, 8, 10, 16, 20, 36] {
        let n = bb(1_234_567_890_123u64);
        let s = to_radix(n, radix);
        let back = u64::from_str_radix(&s, radix);
        let upper = u64::from_str_radix(&s.to_uppercase(), radix);
        println!("radix {:2}: {} back {:?} upper {:?}", radix, s, back, upper);
    }
    let cases = [("zz", 36), ("ZZ", 36), ("-ff", 16), ("777", 8), ("888", 8), ("1010", 2), ("102", 2), ("-80", 16), ("7fffffff", 16), ("80000000", 16)];
    for (s, r) in cases {
        println!(
            "from_str_radix {:?} {}: i32 {:?} u32 {:?} i8 {:?}",
            s,
            r,
            i32::from_str_radix(s, r).map_err(|e| e.to_string()),
            u32::from_str_radix(s, r).ok(),
            i8::from_str_radix(s, r).ok()
        );
    }
    let check = (0..2000u64).map(|i| i * 7919 + i * i * 31).all(|v| to_radix(v, 16) == format!("{:x}", v) && to_radix(v, 2) == format!("{:b}", v) && to_radix(v, 8) == format!("{:o}", v) && to_radix(v, 10) == v.to_string());
    println!("to_radix agrees with formatter: {}", check);
    let digits_sum: u64 = (0..3000u64).map(|i| (i * 1_000_003).to_string().bytes().map(|b| (b - b'0') as u64).sum::<u64>()).sum();
    println!("digit sum over 3000 numbers {}", digits_sum);
    let parsed_sum: i64 = (0..3000i64).map(|i| format!("{}", i * -77_777).parse::<i64>().unwrap_or(0)).sum();
    println!("parse sum {}", parsed_sum);
}

fn thousands(n: i64) -> String {
    let s = n.unsigned_abs().to_string();
    let mut out = String::new();
    for (i, c) in s.chars().enumerate() {
        if i > 0 && (s.len() - i) % 3 == 0 {
            out.push(',');
        }
        out.push(c);
    }
    if n < 0 {
        format!("-{}", out)
    } else {
        out
    }
}

fn main() {
    widths();
    writes();
    parsing();
    for n in [0i64, 999, 1000, -1234567, 1_000_000_000_000, i64::MIN] {
        println!("thousands {} -> {}", n, thousands(bb(n)));
    }
}
