// A recursive `Box<enum>` AST: arithmetic expressions generated as text, parsed
// by recursive descent into boxed nodes, constant-folded by value into a new
// tree, evaluated forty times under different variable bindings, and dropped.
//
// Each pass is a recursive function whose body is a `match` on the node: `eval`,
// `fold` and `size` each dispatch with a `br_table` on the tag and call
// themselves for the children.  `fold` consumes its argument, so every node is
// moved out of its box and most are boxed again, which puts the module's
// `malloc` and `free` -- dlmalloc, as in bintree -- at nearly every node, and
// dropping a tree is a recursive drop glue that frees it bottom-up.
//
// enummatch is a match over a flat sequence of enum values and bintree a tree of
// one node type.  This is the interpreter shape between them: the tag, the
// payload's offset and the depth of recursion all come from the data, and the
// cost is calls and allocation rather than loops.

enum Expr {
    Num(i64),
    Var(usize),
    Neg(Box<Expr>),
    Add(Box<Expr>, Box<Expr>),
    Sub(Box<Expr>, Box<Expr>),
    Mul(Box<Expr>, Box<Expr>),
    If(Box<Expr>, Box<Expr>, Box<Expr>),
}

struct Rng(u64);
impl Rng {
    fn next(&mut self) -> u64 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        self.0
    }
}

// The source text is generated, so the parser has real text to read.
fn gen(r: &mut Rng, depth: u32, out: &mut String) {
    let k = r.next();
    if depth == 0 || k % 8 < 2 {
        if k & 0x100 != 0 {
            out.push((b'a' + (k >> 10) as u8 % 4) as char);
        } else {
            out.push_str(&((k >> 16) % 1000).to_string());
        }
        return;
    }
    match (k >> 4) % 6 {
        0 => {
            out.push('-');
            gen(r, depth - 1, out);
        }
        1 => {
            out.push('?');
            out.push('(');
            gen(r, depth - 1, out);
            out.push(',');
            gen(r, depth - 1, out);
            out.push(',');
            gen(r, depth - 1, out);
            out.push(')');
        }
        op => {
            out.push('(');
            gen(r, depth - 1, out);
            out.push(match op {
                2 | 5 => '+',
                3 => '-',
                _ => '*',
            });
            gen(r, depth - 1, out);
            out.push(')');
        }
    }
}

struct Parser<'a> {
    s: &'a [u8],
    i: usize,
}

impl<'a> Parser<'a> {
    fn eat(&mut self, c: u8) {
        if self.s.get(self.i) == Some(&c) {
            self.i += 1;
        }
    }
    fn parse(&mut self) -> Box<Expr> {
        let c = self.s[self.i];
        self.i += 1;
        match c {
            b'-' => Box::new(Expr::Neg(self.parse())),
            b'?' => {
                self.eat(b'(');
                let a = self.parse();
                self.eat(b',');
                let b = self.parse();
                self.eat(b',');
                let c = self.parse();
                self.eat(b')');
                Box::new(Expr::If(a, b, c))
            }
            b'(' => {
                let a = self.parse();
                let op = self.s[self.i];
                self.i += 1;
                let b = self.parse();
                self.eat(b')');
                Box::new(match op {
                    b'+' => Expr::Add(a, b),
                    b'-' => Expr::Sub(a, b),
                    _ => Expr::Mul(a, b),
                })
            }
            b'a'..=b'z' => Box::new(Expr::Var((c - b'a') as usize)),
            _ => {
                let mut v = (c - b'0') as i64;
                while let Some(&d) = self.s.get(self.i) {
                    if !d.is_ascii_digit() {
                        break;
                    }
                    v = v * 10 + (d - b'0') as i64;
                    self.i += 1;
                }
                Box::new(Expr::Num(v))
            }
        }
    }
}

fn eval(e: &Expr, env: &[i64; 4]) -> i64 {
    match e {
        Expr::Num(v) => *v,
        Expr::Var(i) => env[*i],
        Expr::Neg(a) => eval(a, env).wrapping_neg(),
        Expr::Add(a, b) => eval(a, env).wrapping_add(eval(b, env)),
        Expr::Sub(a, b) => eval(a, env).wrapping_sub(eval(b, env)),
        Expr::Mul(a, b) => eval(a, env).wrapping_mul(eval(b, env)),
        Expr::If(c, a, b) => {
            if eval(c, env) & 1 != 0 {
                eval(a, env)
            } else {
                eval(b, env)
            }
        }
    }
}

// Constant folding by value: the tree is consumed and a new one returned, so
// every node is moved out of its box and most are re-boxed.
fn fold(e: Box<Expr>) -> Box<Expr> {
    match *e {
        Expr::Neg(a) => match *fold(a) {
            Expr::Num(v) => Box::new(Expr::Num(v.wrapping_neg())),
            a => Box::new(Expr::Neg(Box::new(a))),
        },
        Expr::Add(a, b) => bin(fold(a), fold(b), 0),
        Expr::Sub(a, b) => bin(fold(a), fold(b), 1),
        Expr::Mul(a, b) => bin(fold(a), fold(b), 2),
        Expr::If(c, a, b) => {
            let c = fold(c);
            if let Expr::Num(v) = *c {
                if v & 1 != 0 { fold(a) } else { fold(b) }
            } else {
                Box::new(Expr::If(c, fold(a), fold(b)))
            }
        }
        other => Box::new(other),
    }
}

fn bin(a: Box<Expr>, b: Box<Expr>, op: u32) -> Box<Expr> {
    if let (Expr::Num(x), Expr::Num(y)) = (&*a, &*b) {
        return Box::new(Expr::Num(match op {
            0 => x.wrapping_add(*y),
            1 => x.wrapping_sub(*y),
            _ => x.wrapping_mul(*y),
        }));
    }
    Box::new(match op {
        0 => Expr::Add(a, b),
        1 => Expr::Sub(a, b),
        _ => Expr::Mul(a, b),
    })
}

fn size(e: &Expr) -> u64 {
    match e {
        Expr::Num(_) | Expr::Var(_) => 1,
        Expr::Neg(a) => 1 + size(a),
        Expr::Add(a, b) | Expr::Sub(a, b) | Expr::Mul(a, b) => 1 + size(a) + size(b),
        Expr::If(c, a, b) => 1 + size(c) + size(a) + size(b),
    }
}

#[inline(never)]
fn round(src: &str, evals: u32) -> u64 {
    let tree = Parser { s: src.as_bytes(), i: 0 }.parse();
    let mut s = size(&tree);
    let tree = fold(tree);
    s = s.wrapping_mul(7).wrapping_add(size(&tree));
    for k in 0..evals as i64 {
        let env = [k, k * 3 - 7, 11 - k, k ^ 5];
        s = s.wrapping_mul(31).wrapping_add(eval(&tree, &env) as u64);
    }
    s
}

fn main() {
    let mut r = Rng(0x9e3779b97f4a7c15);
    let mut srcs = Vec::new();
    for _ in 0..16 {
        let mut s = String::new();
        gen(&mut r, std::hint::black_box(14), &mut s);
        srcs.push(s);
    }
    let mut t = 0u64;
    for i in 0..2000usize {
        t = t.rotate_left(3) ^ round(std::hint::black_box(&srcs[i % srcs.len()]), 40);
    }
    let len: usize = srcs.iter().map(|s| s.len()).sum();
    println!("t = {} len = {}", t, len);
}
