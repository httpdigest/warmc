// A small language implemented twice: a lexer and recursive-descent parser
// produce an AST for a language with integers, booleans, variables, `let`,
// assignment, `if`/`else`, `while`, functions with recursion, and `print`;
// a tree-walking interpreter evaluates it, and a compiler lowers the same AST
// to bytecode for a stack VM with locals, jumps and calls.  Both run the same
// programs (factorial, fibonacci, primes, gcd, collatz, a closure-free
// accumulator) and their outputs are printed side by side.
//
// This is the shape of a real interpreter: an enum-dispatched `match` in a
// loop (one `br_table` per instruction), a Vec used as an operand stack, a
// HashMap-free environment of Vec<(String, Value)> frames, and deep
// recursion in the parser and tree walker.

use std::collections::BTreeMap;
use std::fmt;

#[derive(Debug, Clone, PartialEq)]
enum Tok {
    Num(i64),
    Ident(String),
    Kw(&'static str),
    Op(&'static str),
    LParen,
    RParen,
    LBrace,
    RBrace,
    Comma,
    Semi,
}

fn lex(src: &str) -> Result<Vec<Tok>, String> {
    let kws = ["let", "fn", "if", "else", "while", "return", "print", "true", "false"];
    let ops = ["==", "!=", "<=", ">=", "&&", "||", "+", "-", "*", "/", "%", "<", ">", "=", "!"];
    let b = src.as_bytes();
    let mut i = 0;
    let mut out = vec![];
    'outer: while i < b.len() {
        let c = b[i];
        if c.is_ascii_whitespace() {
            i += 1;
            continue;
        }
        if c == b'#' {
            while i < b.len() && b[i] != b'\n' {
                i += 1;
            }
            continue;
        }
        if c.is_ascii_digit() {
            let s = i;
            while i < b.len() && b[i].is_ascii_digit() {
                i += 1;
            }
            out.push(Tok::Num(src[s..i].parse().map_err(|e| format!("{:?}", e))?));
            continue;
        }
        if c.is_ascii_alphabetic() || c == b'_' {
            let s = i;
            while i < b.len() && (b[i].is_ascii_alphanumeric() || b[i] == b'_') {
                i += 1;
            }
            let w = &src[s..i];
            match kws.iter().find(|k| **k == w) {
                Some(k) => out.push(Tok::Kw(k)),
                None => out.push(Tok::Ident(w.to_string())),
            }
            continue;
        }
        let single = match c {
            b'(' => Some(Tok::LParen),
            b')' => Some(Tok::RParen),
            b'{' => Some(Tok::LBrace),
            b'}' => Some(Tok::RBrace),
            b',' => Some(Tok::Comma),
            b';' => Some(Tok::Semi),
            _ => None,
        };
        if let Some(t) = single {
            out.push(t);
            i += 1;
            continue;
        }
        for op in ops.iter() {
            if src[i..].starts_with(op) {
                out.push(Tok::Op(op));
                i += op.len();
                continue 'outer;
            }
        }
        return Err(format!("bad char {:?} at {}", c as char, i));
    }
    Ok(out)
}

#[derive(Debug, Clone)]
enum Expr {
    Num(i64),
    Bool(bool),
    Var(String),
    Un(&'static str, Box<Expr>),
    Bin(&'static str, Box<Expr>, Box<Expr>),
    Call(String, Vec<Expr>),
}

#[derive(Debug, Clone)]
enum Stmt {
    Let(String, Expr),
    Assign(String, Expr),
    If(Expr, Vec<Stmt>, Vec<Stmt>),
    While(Expr, Vec<Stmt>),
    Return(Expr),
    Print(Vec<Expr>),
    Expr(Expr),
    Fn(String, Vec<String>, Vec<Stmt>),
}

struct Parser {
    toks: Vec<Tok>,
    pos: usize,
}

impl Parser {
    fn peek(&self) -> Option<&Tok> {
        self.toks.get(self.pos)
    }
    fn next(&mut self) -> Option<Tok> {
        let t = self.toks.get(self.pos).cloned();
        self.pos += 1;
        t
    }
    fn expect(&mut self, t: Tok) -> Result<(), String> {
        match self.next() {
            Some(ref x) if *x == t => Ok(()),
            other => Err(format!("expected {:?}, got {:?}", t, other)),
        }
    }
    fn ident(&mut self) -> Result<String, String> {
        match self.next() {
            Some(Tok::Ident(s)) => Ok(s),
            other => Err(format!("expected identifier, got {:?}", other)),
        }
    }
    fn program(&mut self) -> Result<Vec<Stmt>, String> {
        let mut v = vec![];
        while self.peek().is_some() {
            v.push(self.stmt()?);
        }
        Ok(v)
    }
    fn block(&mut self) -> Result<Vec<Stmt>, String> {
        self.expect(Tok::LBrace)?;
        let mut v = vec![];
        while self.peek() != Some(&Tok::RBrace) {
            if self.peek().is_none() {
                return Err("unterminated block".into());
            }
            v.push(self.stmt()?);
        }
        self.expect(Tok::RBrace)?;
        Ok(v)
    }
    fn stmt(&mut self) -> Result<Stmt, String> {
        match self.peek().cloned() {
            Some(Tok::Kw("let")) => {
                self.next();
                let name = self.ident()?;
                self.expect(Tok::Op("="))?;
                let e = self.expr()?;
                self.expect(Tok::Semi)?;
                Ok(Stmt::Let(name, e))
            }
            Some(Tok::Kw("fn")) => {
                self.next();
                let name = self.ident()?;
                self.expect(Tok::LParen)?;
                let mut params = vec![];
                while self.peek() != Some(&Tok::RParen) {
                    params.push(self.ident()?);
                    if self.peek() == Some(&Tok::Comma) {
                        self.next();
                    }
                }
                self.expect(Tok::RParen)?;
                Ok(Stmt::Fn(name, params, self.block()?))
            }
            Some(Tok::Kw("if")) => {
                self.next();
                let c = self.expr()?;
                let t = self.block()?;
                let e = if self.peek() == Some(&Tok::Kw("else")) {
                    self.next();
                    if self.peek() == Some(&Tok::Kw("if")) {
                        vec![self.stmt()?]
                    } else {
                        self.block()?
                    }
                } else {
                    vec![]
                };
                Ok(Stmt::If(c, t, e))
            }
            Some(Tok::Kw("while")) => {
                self.next();
                let c = self.expr()?;
                Ok(Stmt::While(c, self.block()?))
            }
            Some(Tok::Kw("return")) => {
                self.next();
                let e = self.expr()?;
                self.expect(Tok::Semi)?;
                Ok(Stmt::Return(e))
            }
            Some(Tok::Kw("print")) => {
                self.next();
                self.expect(Tok::LParen)?;
                let mut args = vec![];
                while self.peek() != Some(&Tok::RParen) {
                    args.push(self.expr()?);
                    if self.peek() == Some(&Tok::Comma) {
                        self.next();
                    }
                }
                self.expect(Tok::RParen)?;
                self.expect(Tok::Semi)?;
                Ok(Stmt::Print(args))
            }
            Some(Tok::Ident(name)) if self.toks.get(self.pos + 1) == Some(&Tok::Op("=")) => {
                self.next();
                self.next();
                let e = self.expr()?;
                self.expect(Tok::Semi)?;
                Ok(Stmt::Assign(name, e))
            }
            _ => {
                let e = self.expr()?;
                self.expect(Tok::Semi)?;
                Ok(Stmt::Expr(e))
            }
        }
    }
    fn expr(&mut self) -> Result<Expr, String> {
        self.binary(0)
    }
    fn binary(&mut self, min_prec: u8) -> Result<Expr, String> {
        let mut lhs = self.unary()?;
        loop {
            let op = match self.peek() {
                Some(Tok::Op(op)) => *op,
                _ => break,
            };
            let prec = match op {
                "||" => 1,
                "&&" => 2,
                "==" | "!=" => 3,
                "<" | ">" | "<=" | ">=" => 4,
                "+" | "-" => 5,
                "*" | "/" | "%" => 6,
                _ => break,
            };
            if prec < min_prec {
                break;
            }
            self.next();
            let rhs = self.binary(prec + 1)?;
            lhs = Expr::Bin(op, Box::new(lhs), Box::new(rhs));
        }
        Ok(lhs)
    }
    fn unary(&mut self) -> Result<Expr, String> {
        match self.peek() {
            Some(Tok::Op("-")) => {
                self.next();
                Ok(Expr::Un("-", Box::new(self.unary()?)))
            }
            Some(Tok::Op("!")) => {
                self.next();
                Ok(Expr::Un("!", Box::new(self.unary()?)))
            }
            _ => self.primary(),
        }
    }
    fn primary(&mut self) -> Result<Expr, String> {
        match self.next() {
            Some(Tok::Num(n)) => Ok(Expr::Num(n)),
            Some(Tok::Kw("true")) => Ok(Expr::Bool(true)),
            Some(Tok::Kw("false")) => Ok(Expr::Bool(false)),
            Some(Tok::LParen) => {
                let e = self.expr()?;
                self.expect(Tok::RParen)?;
                Ok(e)
            }
            Some(Tok::Ident(name)) => {
                if self.peek() == Some(&Tok::LParen) {
                    self.next();
                    let mut args = vec![];
                    while self.peek() != Some(&Tok::RParen) {
                        args.push(self.expr()?);
                        if self.peek() == Some(&Tok::Comma) {
                            self.next();
                        }
                    }
                    self.expect(Tok::RParen)?;
                    Ok(Expr::Call(name, args))
                } else {
                    Ok(Expr::Var(name))
                }
            }
            other => Err(format!("unexpected {:?}", other)),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
enum Val {
    Int(i64),
    Bool(bool),
}

impl fmt::Display for Val {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Val::Int(n) => write!(f, "{}", n),
            Val::Bool(b) => write!(f, "{}", b),
        }
    }
}

fn binop(op: &str, a: Val, b: Val) -> Result<Val, String> {
    use Val::*;
    Ok(match (op, a, b) {
        ("+", Int(x), Int(y)) => Int(x.wrapping_add(y)),
        ("-", Int(x), Int(y)) => Int(x.wrapping_sub(y)),
        ("*", Int(x), Int(y)) => Int(x.wrapping_mul(y)),
        ("/", Int(_), Int(0)) | ("%", Int(_), Int(0)) => return Err("division by zero".into()),
        ("/", Int(x), Int(y)) => Int(x.wrapping_div(y)),
        ("%", Int(x), Int(y)) => Int(x.wrapping_rem(y)),
        ("<", Int(x), Int(y)) => Bool(x < y),
        (">", Int(x), Int(y)) => Bool(x > y),
        ("<=", Int(x), Int(y)) => Bool(x <= y),
        (">=", Int(x), Int(y)) => Bool(x >= y),
        ("==", x, y) => Bool(x == y),
        ("!=", x, y) => Bool(x != y),
        ("&&", Bool(x), Bool(y)) => Bool(x && y),
        ("||", Bool(x), Bool(y)) => Bool(x || y),
        (op, a, b) => return Err(format!("type error: {} {} {}", a, op, b)),
    })
}

// ---- tree-walking interpreter
struct Interp {
    funcs: BTreeMap<String, (Vec<String>, Vec<Stmt>)>,
    frames: Vec<Vec<(String, Val)>>,
    out: Vec<String>,
    steps: u64,
}

enum Flow {
    Next,
    Ret(Val),
}

impl Interp {
    fn lookup(&self, n: &str) -> Result<Val, String> {
        self.frames.last().and_then(|f| f.iter().rev().find(|(k, _)| k == n)).map(|(_, v)| *v).ok_or(format!("undefined {}", n))
    }
    fn eval(&mut self, e: &Expr) -> Result<Val, String> {
        self.steps += 1;
        match e {
            Expr::Num(n) => Ok(Val::Int(*n)),
            Expr::Bool(b) => Ok(Val::Bool(*b)),
            Expr::Var(n) => self.lookup(n),
            Expr::Un("-", x) => match self.eval(x)? {
                Val::Int(n) => Ok(Val::Int(n.wrapping_neg())),
                v => Err(format!("cannot negate {}", v)),
            },
            Expr::Un(_, x) => match self.eval(x)? {
                Val::Bool(b) => Ok(Val::Bool(!b)),
                v => Err(format!("cannot not {}", v)),
            },
            Expr::Bin("&&", a, b) => {
                if self.eval(a)? == Val::Bool(false) {
                    Ok(Val::Bool(false))
                } else {
                    self.eval(b)
                }
            }
            Expr::Bin(op, a, b) => {
                let x = self.eval(a)?;
                let y = self.eval(b)?;
                binop(op, x, y)
            }
            Expr::Call(name, args) => {
                let (params, body) = self.funcs.get(name).cloned().ok_or(format!("no function {}", name))?;
                if params.len() != args.len() {
                    return Err(format!("{} takes {} args", name, params.len()));
                }
                let mut frame = vec![];
                for (p, a) in params.iter().zip(args) {
                    frame.push((p.clone(), self.eval(a)?));
                }
                if self.frames.len() > 200 {
                    return Err("stack overflow".into());
                }
                self.frames.push(frame);
                let r = self.exec_block(&body);
                self.frames.pop();
                match r? {
                    Flow::Ret(v) => Ok(v),
                    Flow::Next => Ok(Val::Int(0)),
                }
            }
        }
    }
    fn exec_block(&mut self, b: &[Stmt]) -> Result<Flow, String> {
        let depth = self.frames.last().map(|f| f.len()).unwrap_or(0);
        for s in b {
            if let Flow::Ret(v) = self.exec(s)? {
                return Ok(Flow::Ret(v));
            }
        }
        if let Some(f) = self.frames.last_mut() {
            f.truncate(depth);
        }
        Ok(Flow::Next)
    }
    fn exec(&mut self, s: &Stmt) -> Result<Flow, String> {
        match s {
            Stmt::Let(n, e) => {
                let v = self.eval(e)?;
                if let Some(f) = self.frames.last_mut() {
                    f.push((n.clone(), v));
                }
            }
            Stmt::Assign(n, e) => {
                let v = self.eval(e)?;
                let slot = self.frames.last_mut().and_then(|f| f.iter_mut().rev().find(|(k, _)| k == n)).ok_or(format!("assign to undefined {}", n))?;
                slot.1 = v;
            }
            Stmt::If(c, t, e) => {
                let branch = if self.eval(c)? == Val::Bool(true) { t } else { e };
                return self.exec_block(branch);
            }
            Stmt::While(c, body) => {
                while self.eval(c)? == Val::Bool(true) {
                    if let Flow::Ret(v) = self.exec_block(body)? {
                        return Ok(Flow::Ret(v));
                    }
                }
            }
            Stmt::Return(e) => return Ok(Flow::Ret(self.eval(e)?)),
            Stmt::Print(args) => {
                let mut parts = vec![];
                for a in args {
                    parts.push(self.eval(a)?.to_string());
                }
                self.out.push(parts.join(" "));
            }
            Stmt::Expr(e) => {
                self.eval(e)?;
            }
            Stmt::Fn(n, p, b) => {
                self.funcs.insert(n.clone(), (p.clone(), b.clone()));
            }
        }
        Ok(Flow::Next)
    }
}

// ---- bytecode compiler and VM
#[derive(Debug, Clone, Copy)]
enum Op {
    Push(i64),
    PushBool(bool),
    Load(usize),
    Store(usize),
    Bin(u8),
    Neg,
    Not,
    Jump(usize),
    JumpIfFalse(usize),
    Call(usize, usize),
    Ret,
    Print(usize),
    Pop,
    Halt,
}

const BINOPS: [&str; 13] = ["+", "-", "*", "/", "%", "<", ">", "<=", ">=", "==", "!=", "&&", "||"];

struct Compiler {
    code: Vec<Op>,
    funcs: BTreeMap<String, (usize, usize, usize)>, // entry, nparams, nlocals
    patches: Vec<(usize, String)>,
    locals: Vec<String>,
    max_locals: usize,
}

impl Compiler {
    fn local(&mut self, n: &str) -> Result<usize, String> {
        self.locals.iter().rposition(|l| l == n).ok_or(format!("undefined {}", n))
    }
    fn expr(&mut self, e: &Expr) -> Result<(), String> {
        match e {
            Expr::Num(n) => self.code.push(Op::Push(*n)),
            Expr::Bool(b) => self.code.push(Op::PushBool(*b)),
            Expr::Var(n) => {
                let i = self.local(n)?;
                self.code.push(Op::Load(i));
            }
            Expr::Un(op, x) => {
                self.expr(x)?;
                self.code.push(if *op == "-" { Op::Neg } else { Op::Not });
            }
            Expr::Bin(op, a, b) => {
                self.expr(a)?;
                self.expr(b)?;
                let k = BINOPS.iter().position(|o| o == op).ok_or("bad op")?;
                self.code.push(Op::Bin(k as u8));
            }
            Expr::Call(n, args) => {
                for a in args {
                    self.expr(a)?;
                }
                self.patches.push((self.code.len(), n.clone()));
                self.code.push(Op::Call(0, args.len()));
            }
        }
        Ok(())
    }
    fn block(&mut self, b: &[Stmt]) -> Result<(), String> {
        let mark = self.locals.len();
        for s in b {
            self.stmt(s)?;
        }
        self.locals.truncate(mark);
        Ok(())
    }
    fn stmt(&mut self, s: &Stmt) -> Result<(), String> {
        match s {
            Stmt::Let(n, e) => {
                self.expr(e)?;
                self.locals.push(n.clone());
                self.max_locals = self.max_locals.max(self.locals.len());
                self.code.push(Op::Store(self.locals.len() - 1));
            }
            Stmt::Assign(n, e) => {
                self.expr(e)?;
                let i = self.local(n)?;
                self.code.push(Op::Store(i));
            }
            Stmt::If(c, t, e) => {
                self.expr(c)?;
                let jf = self.code.len();
                self.code.push(Op::JumpIfFalse(0));
                self.block(t)?;
                let j = self.code.len();
                self.code.push(Op::Jump(0));
                let else_at = self.code.len();
                self.code[jf] = Op::JumpIfFalse(else_at);
                self.block(e)?;
                let end = self.code.len();
                self.code[j] = Op::Jump(end);
            }
            Stmt::While(c, body) => {
                let top = self.code.len();
                self.expr(c)?;
                let jf = self.code.len();
                self.code.push(Op::JumpIfFalse(0));
                self.block(body)?;
                self.code.push(Op::Jump(top));
                let end = self.code.len();
                self.code[jf] = Op::JumpIfFalse(end);
            }
            Stmt::Return(e) => {
                self.expr(e)?;
                self.code.push(Op::Ret);
            }
            Stmt::Print(args) => {
                for a in args {
                    self.expr(a)?;
                }
                self.code.push(Op::Print(args.len()));
            }
            Stmt::Expr(e) => {
                self.expr(e)?;
                self.code.push(Op::Pop);
            }
            Stmt::Fn(..) => {}
        }
        Ok(())
    }
    fn compile(prog: &[Stmt]) -> Result<(Vec<Op>, usize, BTreeMap<String, (usize, usize, usize)>), String> {
        let mut c = Compiler { code: vec![], funcs: BTreeMap::new(), patches: vec![], locals: vec![], max_locals: 0 };
        let mut main_body = vec![];
        let mut fns = vec![];
        for s in prog {
            match s {
                Stmt::Fn(n, p, b) => fns.push((n.clone(), p.clone(), b.clone())),
                other => main_body.push(other.clone()),
            }
        }
        c.block(&main_body)?;
        c.code.push(Op::Halt);
        let main_locals = c.max_locals;
        for (n, p, b) in fns {
            let entry = c.code.len();
            c.locals = p.clone();
            c.max_locals = p.len();
            c.block(&b)?;
            c.code.push(Op::Push(0));
            c.code.push(Op::Ret);
            c.funcs.insert(n, (entry, p.len(), c.max_locals));
        }
        for (at, name) in c.patches.clone() {
            let (entry, np, _) = *c.funcs.get(&name).ok_or(format!("no function {}", name))?;
            if let Op::Call(_, argc) = c.code[at] {
                if argc != np {
                    return Err(format!("{} takes {} args", name, np));
                }
            }
            c.code[at] = Op::Call(entry, np);
        }
        Ok((c.code, main_locals, c.funcs))
    }
}

fn run_vm(code: &[Op], main_locals: usize, funcs: &BTreeMap<String, (usize, usize, usize)>) -> Result<(Vec<String>, u64), String> {
    let nlocals_at: BTreeMap<usize, usize> = funcs.values().map(|&(e, _, nl)| (e, nl)).collect();
    let mut stack: Vec<Val> = vec![];
    let mut locals: Vec<Val> = vec![Val::Int(0); main_locals];
    let mut frames: Vec<(usize, usize)> = vec![]; // return pc, locals base
    let mut base = 0;
    let mut pc = 0;
    let mut out = vec![];
    let mut steps = 0u64;
    loop {
        steps += 1;
        let op = *code.get(pc).ok_or("pc out of range")?;
        pc += 1;
        match op {
            Op::Push(n) => stack.push(Val::Int(n)),
            Op::PushBool(b) => stack.push(Val::Bool(b)),
            Op::Load(i) => stack.push(locals[base + i]),
            Op::Store(i) => locals[base + i] = stack.pop().ok_or("underflow")?,
            Op::Bin(k) => {
                let b = stack.pop().ok_or("underflow")?;
                let a = stack.pop().ok_or("underflow")?;
                stack.push(binop(BINOPS[k as usize], a, b)?);
            }
            Op::Neg => match stack.pop() {
                Some(Val::Int(n)) => stack.push(Val::Int(n.wrapping_neg())),
                v => return Err(format!("cannot negate {:?}", v)),
            },
            Op::Not => match stack.pop() {
                Some(Val::Bool(b)) => stack.push(Val::Bool(!b)),
                v => return Err(format!("cannot not {:?}", v)),
            },
            Op::Jump(t) => pc = t,
            Op::JumpIfFalse(t) => {
                if stack.pop() == Some(Val::Bool(false)) {
                    pc = t;
                }
            }
            Op::Call(entry, np) => {
                if frames.len() > 200 {
                    return Err("stack overflow".into());
                }
                let nl = *nlocals_at.get(&entry).unwrap_or(&np);
                frames.push((pc, base));
                base = locals.len();
                locals.resize(base + nl.max(np), Val::Int(0));
                for i in (0..np).rev() {
                    locals[base + i] = stack.pop().ok_or("underflow")?;
                }
                pc = entry;
            }
            Op::Ret => {
                let (rpc, rbase) = frames.pop().ok_or("return from main")?;
                locals.truncate(base);
                base = rbase;
                pc = rpc;
            }
            Op::Print(n) => {
                let at = stack.len() - n;
                let parts: Vec<String> = stack.drain(at..).map(|v| v.to_string()).collect();
                out.push(parts.join(" "));
            }
            Op::Pop => {
                stack.pop();
            }
            Op::Halt => return Ok((out, steps)),
        }
    }
}

const PROGRAMS: &[(&str, &str)] = &[
    (
        "factorial",
        "fn fact(n) { if n <= 1 { return 1; } return n * fact(n - 1); }
         let i = 0;
         while i <= 20 { print(i, fact(i)); i = i + 1; }",
    ),
    (
        "fibonacci",
        "fn fib(n) { if n < 2 { return n; } return fib(n - 1) + fib(n - 2); }
         fn fib_iter(n) { let a = 0; let b = 1; while n > 0 { let t = a + b; a = b; b = t; n = n - 1; } return a; }
         let k = 0;
         while k < 18 { print(k, fib(k), fib_iter(k * 5)); k = k + 1; }",
    ),
    (
        "primes",
        "fn is_prime(n) {
           if n < 2 { return false; }
           let d = 2;
           while d * d <= n { if n % d == 0 { return false; } d = d + 1; }
           return true;
         }
         let n = 0; let count = 0;
         while n < 200 { if is_prime(n) { count = count + 1; if count % 5 == 0 { print(count, n); } } n = n + 1; }
         print(count);",
    ),
    (
        "gcd_collatz",
        "fn gcd(a, b) { while b != 0 { let t = a % b; a = b; b = t; } return a; }
         fn steps(n) { let s = 0; while n != 1 { if n % 2 == 0 { n = n / 2; } else { n = 3 * n + 1; } s = s + 1; } return s; }
         print(gcd(1071, 462), gcd(270, 192), gcd(17, 5), gcd(0, 9));
         let i = 1;
         while i < 30 { print(i, steps(i), gcd(i * 6, 84)); i = i + 3; }",
    ),
    (
        "logic",
        "let a = 5; let b = -3;
         print(a + b * 2, (a + b) * 2, -a - -b, a / b, a % b, -a % 3);
         print(a > b && b < 0, !(a == 5) || false, a != b, true == !false);
         if a < b { print(1); } else if a == 5 { print(2); } else { print(3); }
         let x = 0;
         while x < 3 { let y = x * x; if y == 1 { print(y, x); } x = x + 1; }",
    ),
    ("lex_error", "let s = \"strings are not supported\";"),
    (
        "nested_loops",
        "let i = 0; let total = 0;
         while i < 6 { let j = 0; while j < i { if (i + j) % 3 == 0 { total = total + i * j; print(i, j, total); } j = j + 1; } i = i + 1; }
         print(total);",
    ),
    (
        "accumulate",
        "fn ack(m, n) { if m == 0 { return n + 1; } if n == 0 { return ack(m - 1, 1); } return ack(m - 1, ack(m, n - 1)); }
         fn sum_to(n) { let s = 0; let i = 1; while i <= n { s = s + i * i; i = i + 1; } return s; }
         print(ack(2, 3), ack(3, 2), sum_to(100), sum_to(1000));
         fn deep(n) { if n == 0 { return 0; } return 1 + deep(n - 1); }
         print(deep(150));",
    ),
    (
        "too_deep",
        "fn deep(n) { if n == 0 { return 0; } return 1 + deep(n - 1); }
         print(deep(10));
         print(deep(500));",
    ),
    ("errors", "let z = 0; print(10 / z);"),
    ("type_error", "print(1 + true);"),
    ("parse_error", "let = 5;"),
];

fn main() {
    for (name, src) in PROGRAMS {
        println!("=== {}", name);
        let toks = match lex(src) {
            Ok(t) => t,
            Err(e) => {
                println!("lex error: {}", e);
                continue;
            }
        };
        println!("tokens {}", toks.len());
        let prog = match (Parser { toks, pos: 0 }).program() {
            Ok(p) => p,
            Err(e) => {
                println!("parse error: {}", e);
                continue;
            }
        };
        let mut it = Interp { funcs: BTreeMap::new(), frames: vec![vec![]], out: vec![], steps: 0 };
        let mut tree_err = None;
        for s in &prog {
            if let Err(e) = it.exec(s) {
                tree_err = Some(e);
                break;
            }
        }
        let vm = Compiler::compile(&prog).and_then(|(code, nl, funcs)| run_vm(&code, nl, &funcs).map(|r| (r, code.len())));
        match &vm {
            Ok(((out, steps), ncode)) => {
                println!("tree steps {} vm steps {} code {} same output {}", it.steps, steps, ncode, *out == it.out);
                for (a, b) in it.out.iter().zip(out.iter()) {
                    println!("  {:<24} | {}", a, b);
                }
            }
            Err(e) => {
                println!("vm error: {}", e);
                for a in &it.out {
                    println!("  {}", a);
                }
            }
        }
        if let Some(e) = tree_err {
            println!("tree error: {}", e);
        }
    }
}
