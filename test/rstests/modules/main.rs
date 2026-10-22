// Modules and visibility inside one file: nested `mod` blocks with pub,
// pub(crate), pub(super) and private items; `use` with self/super/crate
// paths, renaming with `as`, glob imports and re-exports; structs with private
// fields reachable only through constructors; impl blocks for one type spread
// across modules; module-level consts and statics, including a `static mut`
// counter behind unsafe and an AtomicUsize; trait defined in one module and
// implemented in another; and functions with the same name in different
// modules.
//
// None of this exists at run time -- it is name resolution -- but it decides
// which functions are distinct symbols, which get inlined across module
// boundaries, and which statics land in the data segment, so it shapes the
// module warmc receives: many small functions and a few globals touched from
// several of them.

use std::hint::black_box as bb;
use std::sync::atomic::{AtomicUsize, Ordering};

static CALLS: AtomicUsize = AtomicUsize::new(0);

fn tick() -> usize {
    CALLS.fetch_add(1, Ordering::Relaxed) + 1
}

mod geometry {
    pub mod shapes {
        use super::consts::PI;

        #[derive(Debug, Clone)]
        pub struct Circle {
            pub r: f64,
            id: u32,
        }

        impl Circle {
            pub fn new(r: f64) -> Circle {
                Circle { r, id: crate::tick() as u32 }
            }
            pub fn area(&self) -> f64 {
                PI * self.r * self.r
            }
            pub fn id(&self) -> u32 {
                self.id
            }
            pub(crate) fn scale(&mut self, k: f64) {
                self.r *= k;
            }
        }

        pub(super) fn secret_ratio() -> f64 {
            2.0 * PI
        }

        pub fn describe() -> String {
            format!("shapes module, ratio {:.5}", secret_ratio())
        }
    }

    pub mod consts {
        pub const PI: f64 = 3.141592653589793;
        pub const E: f64 = 2.718281828459045;
        pub(crate) static NAMES: [&str; 3] = ["pi", "e", "tau"];
    }

    pub mod transform {
        use super::shapes::Circle;

        // A second impl block for Circle, in another module.
        impl Circle {
            pub fn grown(&self, by: f64) -> Circle {
                let mut c = self.clone();
                c.scale(1.0 + by);
                c
            }
        }

        pub fn ratio_twice() -> f64 {
            super::shapes::secret_ratio() * 2.0
        }
    }

    pub use self::consts::E as EULER;
    pub use self::shapes::Circle;

    pub fn describe() -> String {
        format!("geometry module, {}", shapes::describe())
    }
}

mod bank {
    #[derive(Debug)]
    pub struct Account {
        owner: String,
        balance: i64,
        history: Vec<i64>,
    }

    #[derive(Debug, PartialEq)]
    pub enum TxError {
        Insufficient { needed: i64, available: i64 },
        Invalid(i64),
    }

    impl Account {
        pub fn open(owner: &str, initial: i64) -> Account {
            super::tick();
            Account { owner: owner.to_string(), balance: initial, history: vec![initial] }
        }
        pub fn deposit(&mut self, amt: i64) -> Result<i64, TxError> {
            validate(amt)?;
            self.balance += amt;
            self.history.push(amt);
            Ok(self.balance)
        }
        pub fn withdraw(&mut self, amt: i64) -> Result<i64, TxError> {
            validate(amt)?;
            if amt > self.balance {
                return Err(TxError::Insufficient { needed: amt, available: self.balance });
            }
            self.balance -= amt;
            self.history.push(-amt);
            Ok(self.balance)
        }
        pub fn balance(&self) -> i64 {
            self.balance
        }
        pub fn owner(&self) -> &str {
            &self.owner
        }
        pub fn history(&self) -> &[i64] {
            &self.history
        }
    }

    fn validate(amt: i64) -> Result<(), TxError> {
        if amt <= 0 {
            Err(TxError::Invalid(amt))
        } else {
            Ok(())
        }
    }

    pub mod transfer {
        use super::{Account, TxError};

        pub fn between(a: &mut Account, b: &mut Account, amt: i64) -> Result<(i64, i64), TxError> {
            a.withdraw(amt)?;
            let nb = b.deposit(amt)?;
            Ok((a.balance(), nb))
        }
    }
}

mod util {
    pub trait Summary {
        fn summary(&self) -> String;
        fn short(&self) -> String {
            let s = self.summary();
            if s.len() > 24 {
                format!("{}...", &s[..21])
            } else {
                s
            }
        }
    }

    pub mod text {
        pub fn describe() -> &'static str {
            "util::text::describe"
        }
        pub fn caesar(s: &str, k: u8) -> String {
            s.chars()
                .map(|c| match c {
                    'a'..='z' => (((c as u8 - b'a' + k) % 26) + b'a') as char,
                    'A'..='Z' => (((c as u8 - b'A' + k) % 26) + b'A') as char,
                    _ => c,
                })
                .collect()
        }
    }

    pub mod numbers {
        pub fn describe() -> &'static str {
            "util::numbers::describe"
        }
        pub fn digits(mut n: u64) -> Vec<u8> {
            let mut d = vec![];
            loop {
                d.push((n % 10) as u8);
                n /= 10;
                if n == 0 {
                    break;
                }
            }
            d.reverse();
            d
        }
        pub(in crate::util) fn internal() -> u32 {
            42
        }
    }

    pub fn use_internal() -> u32 {
        numbers::internal() + 1
    }
}

// Implementing a trait from one module for types from others.
mod impls {
    use crate::bank::Account;
    use crate::geometry::Circle;
    use crate::util::Summary;

    impl Summary for Account {
        fn summary(&self) -> String {
            format!("{} has {} after {} tx", self.owner(), self.balance(), self.history().len())
        }
    }
    impl Summary for Circle {
        fn summary(&self) -> String {
            format!("circle #{} r={:.2}", self.id(), self.r)
        }
    }
}

mod counter {
    static mut COUNT: u64 = 0;
    pub fn bump(by: u64) -> u64 {
        unsafe {
            COUNT += by;
            COUNT
        }
    }
}

use bank::transfer;
use bank::{Account, TxError};
use geometry::consts::*;
use geometry::Circle as Round;
use util::numbers::describe as num_describe;
use util::text::{self, caesar};
use util::Summary;

fn main() {
    println!("{}", geometry::describe());
    println!("{} / {} / {}", text::describe(), num_describe(), util::numbers::describe());
    println!("consts PI {} E {} EULER {} names {:?}", PI, E, geometry::EULER, geometry::consts::NAMES);
    println!("ratio twice {:.6} internal {}", geometry::transform::ratio_twice(), util::use_internal());

    let mut circles: Vec<Round> = (1..=4).map(|i| Round::new(bb(i as f64) * 0.5)).collect();
    for c in &circles {
        println!("{} area {:.4} grown {:.4}", c.summary(), c.area(), c.grown(0.5).area());
    }
    circles[1].scale(3.0);
    println!("scaled {}", circles[1].short());

    let mut alice = Account::open("alice", 100);
    let mut bob = Account::open("bob", 20);
    let steps: Vec<(&str, i64)> = vec![("dep", 50), ("wd", 30), ("wd", 500), ("dep", -5), ("xfer", 70), ("xfer", 100), ("wd", 0)];
    for (kind, amt) in steps {
        let r = match kind {
            "dep" => alice.deposit(amt).map(|b| format!("alice now {}", b)),
            "wd" => alice.withdraw(amt).map(|b| format!("alice now {}", b)),
            _ => transfer::between(&mut alice, &mut bob, amt).map(|(a, b)| format!("alice {} bob {}", a, b)),
        };
        match r {
            Ok(s) => println!("{} {} -> {}", kind, amt, s),
            Err(TxError::Insufficient { needed, available }) => println!("{} {} -> insufficient: need {} have {}", kind, amt, needed, available),
            Err(e) => println!("{} {} -> error {:?}", kind, amt, e),
        }
    }
    println!("{}", alice.summary());
    println!("{}", bob.short());
    println!("history {:?} {:?}", alice.history(), bob.history());

    for k in [1u8, 3, 13, 25] {
        let enc = caesar("Hello, World! xyz", k);
        let dec = caesar(&enc, 26 - k);
        println!("caesar {} {:?} -> {:?}", k, enc, dec);
    }
    println!("digits {:?} {:?}", util::numbers::digits(bb(9071203)), util::numbers::digits(0));
    let mut last = 0;
    for i in 1..=5 {
        last = counter::bump(i * i);
    }
    println!("static mut counter {}", last);
    println!("atomic calls {}", CALLS.load(Ordering::Relaxed));
}
