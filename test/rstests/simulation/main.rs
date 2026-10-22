// Small numeric simulations printed frame by frame: Conway's Life on a torus,
// the rule-110 and rule-30 elementary automata on a bit-packed u64 row, an
// ASCII Mandelbrot set (f64 escape-time), a falling-and-bouncing particle
// system in f32 with drag and elastic collisions against walls, Gaussian
// elimination with partial pivoting solving a 5x5 system and inverting a
// matrix, a 1-D heat equation stepped explicitly, and a predator-prey
// (Lotka-Volterra) model integrated with RK4.
//
// The float paths are sequences of dependent multiply-adds where the exact
// order of operations decides the last bits, so a back end that fuses a
// multiply and an add the wasm kept separate, or reassociates, changes the
// printed digits -- wasm semantics forbid both, and this test shows it.  The
// integer paths are dense neighbour-counting loops over 2-D grids.

use std::hint::black_box as bb;

fn life() {
    let (h, w) = (12usize, 24usize);
    let mut g = vec![vec![false; w]; h];
    // A glider, a blinker, and an R-pentomino.
    for &(r, c) in &[(1, 2), (2, 3), (3, 1), (3, 2), (3, 3), (8, 4), (8, 5), (8, 6), (5, 15), (5, 16), (6, 14), (6, 15), (7, 15)] {
        g[r][c] = true;
    }
    for gen in 0..=24 {
        if gen % 6 == 0 {
            println!("life gen {} alive {}", gen, g.iter().flatten().filter(|&&x| x).count());
            for row in &g {
                println!("  {}", row.iter().map(|&x| if x { '#' } else { '.' }).collect::<String>());
            }
        }
        let mut n = vec![vec![false; w]; h];
        for r in 0..h {
            for c in 0..w {
                let mut k = 0;
                for dr in [h - 1, 0, 1] {
                    for dc in [w - 1, 0, 1] {
                        if (dr, dc) != (0, 0) && g[(r + dr) % h][(c + dc) % w] {
                            k += 1;
                        }
                    }
                }
                n[r][c] = matches!((g[r][c], k), (true, 2) | (_, 3));
            }
        }
        g = n;
    }
}

fn automaton(rule: u8, steps: usize) {
    let mut row: u64 = bb(1u64 << 32);
    println!("rule {}", rule);
    for _ in 0..steps {
        println!("  {}", (0..64).rev().map(|i| if row >> i & 1 == 1 { '#' } else { ' ' }).collect::<String>().trim_end());
        let l = row.rotate_left(1);
        let r = row.rotate_right(1);
        let mut next = 0u64;
        for i in 0..64 {
            let pat = ((l >> i & 1) << 2) | ((row >> i & 1) << 1) | (r >> i & 1);
            next |= (((rule >> pat) & 1) as u64) << i;
        }
        row = next;
    }
    println!("  final popcount {}", row.count_ones());
}

fn mandelbrot() {
    let chars = b" .:-=+*#%@";
    let (w, h) = (60, 22);
    let mut total = 0u64;
    for py in 0..h {
        let mut line = String::new();
        for px in 0..w {
            let x0 = bb(-2.2) + px as f64 * (3.0 / w as f64);
            let y0 = -1.2 + py as f64 * (2.4 / h as f64);
            let (mut x, mut y, mut i) = (0.0f64, 0.0f64, 0);
            while x * x + y * y <= 4.0 && i < 200 {
                let xt = x * x - y * y + x0;
                y = 2.0 * x * y + y0;
                x = xt;
                i += 1;
            }
            total += i as u64;
            line.push(chars[(i * (chars.len() - 1)) / 200] as char);
        }
        println!("  {}", line.trim_end());
    }
    println!("mandelbrot total iterations {}", total);
}

#[derive(Clone, Copy, Debug)]
struct P {
    x: f32,
    y: f32,
    vx: f32,
    vy: f32,
    m: f32,
}

fn particles() {
    let mut ps: Vec<P> = (0..6)
        .map(|i| {
            let f = i as f32;
            P { x: 1.0 + f * 1.5, y: 8.0 - f * 0.7, vx: 0.9 - f * 0.35, vy: f * 0.2 - 0.3, m: 1.0 + f * 0.25 }
        })
        .collect();
    let dt = bb(0.05f32);
    let (gx, gy) = (0.0f32, -9.81f32);
    let drag = 0.02f32;
    for step in 0..=200 {
        if step % 25 == 0 {
            let ke: f32 = ps.iter().map(|p| 0.5 * p.m * (p.vx * p.vx + p.vy * p.vy)).sum();
            let pe: f32 = ps.iter().map(|p| p.m * -gy * p.y).sum();
            println!("step {:3}: KE {:.5} PE {:.5} p0 ({:.5}, {:.5}) v0 ({:.5}, {:.5}) bits {:08x}", step, ke, pe, ps[0].x, ps[0].y, ps[0].vx, ps[0].vy, ps[0].x.to_bits());
        }
        for p in ps.iter_mut() {
            p.vx += (gx - drag * p.vx) * dt;
            p.vy += (gy - drag * p.vy) * dt;
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            if p.y < 0.0 {
                p.y = -p.y;
                p.vy = -p.vy * 0.9;
            }
            if p.x < 0.0 || p.x > 10.0 {
                p.x = p.x.clamp(0.0, 10.0);
                p.vx = -p.vx;
            }
        }
        // Pairwise elastic collisions in 1-D along x when close.
        for i in 0..ps.len() {
            for j in i + 1..ps.len() {
                let (a, b) = (ps[i], ps[j]);
                let d2 = (a.x - b.x).powi(2) + (a.y - b.y).powi(2);
                if d2 < 0.09 {
                    let (m1, m2) = (a.m, b.m);
                    ps[i].vx = ((m1 - m2) * a.vx + 2.0 * m2 * b.vx) / (m1 + m2);
                    ps[j].vx = ((m2 - m1) * b.vx + 2.0 * m1 * a.vx) / (m1 + m2);
                }
            }
        }
    }
}

fn gauss(a: &mut Vec<Vec<f64>>, b: &mut Vec<f64>) -> Option<Vec<f64>> {
    let n = b.len();
    for col in 0..n {
        let piv = (col..n).max_by(|&i, &j| a[i][col].abs().total_cmp(&a[j][col].abs()))?;
        if a[piv][col].abs() < 1e-12 {
            return None;
        }
        a.swap(col, piv);
        b.swap(col, piv);
        for r in col + 1..n {
            let f = a[r][col] / a[col][col];
            for c in col..n {
                a[r][c] -= f * a[col][c];
            }
            b[r] -= f * b[col];
        }
    }
    let mut x = vec![0.0; n];
    for r in (0..n).rev() {
        let s: f64 = (r + 1..n).map(|c| a[r][c] * x[c]).sum();
        x[r] = (b[r] - s) / a[r][r];
    }
    Some(x)
}

fn linear_algebra() {
    let a0: Vec<Vec<f64>> = (0..5).map(|r| (0..5).map(|c| if r == c { 10.0 + r as f64 } else { 1.0 / (1.0 + r as f64 + c as f64) }).collect()).collect();
    let b0: Vec<f64> = (0..5).map(|i| (i as f64 + 1.0).sqrt()).collect();
    let x = gauss(&mut a0.clone(), &mut b0.clone());
    println!("solve x {:?}", x.as_ref().map(|v| v.iter().map(|z| format!("{:.12}", z)).collect::<Vec<_>>()));
    if let Some(x) = &x {
        let resid: f64 = (0..5).map(|r| (0..5).map(|c| a0[r][c] * x[c]).sum::<f64>() - b0[r]).map(|e| e.abs()).fold(0.0, f64::max);
        println!("residual below 1e-12: {}", resid < 1e-12);
    }
    // Inverse by solving against each unit vector.
    let mut inv = vec![vec![0.0; 5]; 5];
    for k in 0..5 {
        let mut e = vec![0.0; 5];
        e[k] = 1.0;
        if let Some(col) = gauss(&mut a0.clone(), &mut e) {
            for r in 0..5 {
                inv[r][k] = col[r];
            }
        }
    }
    for row in &inv {
        println!("inv {}", row.iter().map(|v| format!("{:+.10}", v)).collect::<Vec<_>>().join(" "));
    }
    let mut max_off = 0.0f64;
    for r in 0..5 {
        for c in 0..5 {
            let p: f64 = (0..5).map(|k| a0[r][k] * inv[k][c]).sum();
            let want = if r == c { 1.0 } else { 0.0 };
            max_off = max_off.max((p - want).abs());
        }
    }
    println!("A*inv(A) max error below 1e-14: {}", max_off < 1e-14);
    let singular = vec![vec![1.0, 2.0], vec![2.0, 4.0]];
    println!("singular -> {:?}", gauss(&mut singular.clone(), &mut vec![1.0, 2.0]));
}

fn heat() {
    let n = 21;
    let mut u = vec![0.0f64; n];
    u[n / 2] = 100.0;
    let alpha = bb(0.4);
    for t in 0..=60 {
        if t % 15 == 0 {
            println!("heat t={:2} {}", t, u.iter().map(|v| format!("{:5.2}", v)).collect::<Vec<_>>().join(" "));
        }
        let mut next = u.clone();
        for i in 1..n - 1 {
            next[i] = u[i] + alpha * (u[i - 1] - 2.0 * u[i] + u[i + 1]);
        }
        u = next;
    }
    println!("heat total {:.12}", u.iter().sum::<f64>());
}

fn lotka_volterra() {
    let (a, b, c, d) = (1.1f64, 0.4, 0.4, 0.1);
    let f = |x: f64, y: f64| (a * x - b * x * y, d * x * y - c * y);
    let (mut x, mut y) = (bb(10.0f64), 10.0f64);
    let h = 0.01;
    for step in 0..=3000 {
        if step % 300 == 0 {
            println!("lv t={:5.2} prey {:.9} pred {:.9}", step as f64 * h, x, y);
        }
        let (k1x, k1y) = f(x, y);
        let (k2x, k2y) = f(x + h / 2.0 * k1x, y + h / 2.0 * k1y);
        let (k3x, k3y) = f(x + h / 2.0 * k2x, y + h / 2.0 * k2y);
        let (k4x, k4y) = f(x + h * k3x, y + h * k3y);
        x += h / 6.0 * (k1x + 2.0 * k2x + 2.0 * k3x + k4x);
        y += h / 6.0 * (k1y + 2.0 * k2y + 2.0 * k3y + k4y);
    }
}

fn main() {
    life();
    automaton(110, 24);
    automaton(30, 16);
    mandelbrot();
    particles();
    linear_algebra();
    heat();
    lotka_volterra();
}
