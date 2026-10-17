// tools/rust-exos/rustc-nativo/ciao.rs - compiled BY rustc INSIDE EX-OS
// (prova-nativo.sh): collections, a thread, a caught panic, a process.
use std::collections::HashMap;

fn main() {
    let mut m = HashMap::new();
    for w in "uno due due tre tre tre".split(' ') {
        *m.entry(w).or_insert(0) += 1;
    }
    let t = std::thread::spawn(|| 6 * 7).join().unwrap();
    let r = std::panic::catch_unwind(|| panic!("preso"));
    let p = std::process::Command::new("uname").output().map(|o| o.status.success()).unwrap_or(false);
    println!("ciao da Rust compilato su EX-OS: tre={} filo={} panic={} processo={}", m["tre"], t, r.is_err(), p);
    println!("NATIVO-{}", if m["tre"] == 3 && t == 42 && r.is_err() && p { "OK" } else { "NO" });
}
