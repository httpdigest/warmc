// Text processing with `str` methods: a small stack-machine program generated as
// text, then split with `lines` and `split_whitespace` and executed by a `match`
// on each word, with identifiers hashed through `char_indices` and
// `to_ascii_uppercase`, and literals read by `parse::<i64>`.
//
// A `match` on `&str` is not a jump table on strings.  LLVM switches on the
// length first, a `br_table` over the possible lengths, and then compares the
// bytes as `i32`, `i16` and `i8` loads against constants -- "push" is one
// `i32.load` and a compare against 0x68737570 -- so matching a keyword costs a
// few integer compares and no `memcmp`.  The variable table, keyed by name, does
// call `memcmp`.  `split_whitespace` decodes UTF-8 and tests every character
// against Unicode's White_Space, a `br_table` on the character's high byte, and
// two of the words here ("größe", "naïve") are not ASCII, so that path runs.
//
// gobench/tokenize is a hand-written lexer; this is the one std hands you, and
// the shape of every line-oriented config reader and assembler.  One digit of
// the program changes each round, so no round repeats the one before.

const WORDS: [&str; 22] = [
    "push", "add", "sub", "mul", "xor", "dup", "swap", "drop", "neg", "over", "shl", "shr",
    "store", "load", "max", "min", "Alpha", "beta_2", "gamma", "größe", "naïve", "z",
];

#[inline(never)]
fn make_program(lines: usize, seed: u64) -> String {
    let mut x = seed;
    let mut s = String::new();
    for l in 0..lines {
        let words = 3 + l % 6;
        for w in 0..words {
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            if w > 0 {
                s.push_str(if x & 0x300 == 0 { "\t" } else { " " });
            }
            let k = (x % WORDS.len() as u64) as usize;
            let word = WORDS[k];
            s.push_str(word);
            if word == "push" {
                s.push(' ');
                s.push_str(&((x >> 24) as i32 % 100000).to_string());
            } else if word == "store" || word == "load" {
                s.push(' ');
                s.push_str(WORDS[16 + (x >> 40) as usize % 6]);
            }
        }
        if l % 7 == 3 {
            s.push_str("  # a comment, with words: add mul push 99");
        }
        s.push('\n');
    }
    s
}

#[inline(never)]
fn ident_hash(w: &str) -> i64 {
    // Case-folded, position-weighted, by character rather than by byte.
    let mut h = 0i64;
    for (i, c) in w.char_indices() {
        h = h.wrapping_mul(31).wrapping_add(c.to_ascii_uppercase() as i64 * (i as i64 + 1));
    }
    h
}

#[inline(never)]
fn run(src: &str, stack: &mut Vec<i64>) -> i64 {
    stack.clear();
    let mut vars: Vec<(&str, i64)> = Vec::new();
    let mut steps = 0i64;
    for line in src.lines() {
        let code = match line.find('#') {
            Some(i) => &line[..i],
            None => line,
        };
        let mut it = code.split_whitespace();
        while let Some(w) = it.next() {
            steps += 1;
            match w {
                "push" => {
                    let v = it.next().and_then(|t| t.parse::<i64>().ok()).unwrap_or(0);
                    stack.push(v);
                }
                "add" | "sub" | "mul" | "xor" | "shl" | "shr" | "max" | "min" => {
                    let b = stack.pop().unwrap_or(1);
                    let a = stack.pop().unwrap_or(2);
                    stack.push(match w {
                        "add" => a.wrapping_add(b),
                        "sub" => a.wrapping_sub(b),
                        "mul" => a.wrapping_mul(b),
                        "xor" => a ^ b,
                        "shl" => a.wrapping_shl(b as u32 & 15),
                        "shr" => a.wrapping_shr(b as u32 & 15),
                        "max" => a.max(b),
                        _ => a.min(b),
                    });
                }
                "dup" => {
                    let a = stack.last().copied().unwrap_or(3);
                    stack.push(a);
                }
                "swap" => {
                    let n = stack.len();
                    if n >= 2 {
                        stack.swap(n - 1, n - 2);
                    }
                }
                "over" => {
                    let n = stack.len();
                    let a = if n >= 2 { stack[n - 2] } else { 4 };
                    stack.push(a);
                }
                "drop" => {
                    stack.pop();
                }
                "neg" => {
                    if let Some(a) = stack.last_mut() {
                        *a = a.wrapping_neg();
                    }
                }
                "store" | "load" => {
                    let name = it.next().unwrap_or("");
                    let slot = match vars.iter().position(|(n, _)| *n == name) {
                        Some(p) => p,
                        None => {
                            vars.push((name, 0));
                            vars.len() - 1
                        }
                    };
                    if w == "store" {
                        vars[slot].1 = stack.pop().unwrap_or(5);
                    } else {
                        stack.push(vars[slot].1);
                    }
                }
                _ => stack.push(ident_hash(w)),
            }
        }
    }
    let mut h = steps;
    for &v in stack.iter() {
        h = h.wrapping_mul(1_000_003).wrapping_add(v);
    }
    for &(n, v) in vars.iter() {
        h ^= v.wrapping_add(n.len() as i64);
    }
    h
}

fn main() {
    let mut src = make_program(std::hint::black_box(2000), 0x2545f4914f6cdd1d).into_bytes();
    let digits: Vec<usize> = (0..src.len()).filter(|&i| src[i].is_ascii_digit()).collect();
    let mut stack = Vec::new();
    let mut h = 0i64;
    for r in 0..360u32 {
        // One digit of one literal changed per round, so every round parses a
        // slightly different program.
        let d = digits[(r as usize).wrapping_mul(2654435761) % digits.len()];
        src[d] = b'0' + (src[d] - b'0' + 1) % 10;
        let text = std::str::from_utf8(&src).unwrap();
        h = h.rotate_left(7) ^ run(std::hint::black_box(text), &mut stack);
    }
    println!("h = {} bytes = {}", h, src.len());
}
