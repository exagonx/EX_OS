// tools/rust-exos/prova-processi/src/main.rs - std::process inside EX-OS.
use std::io::Write;
use std::process::{Command, Stdio};

static mut NO: u32 = 0;
fn ok(cosa: &str, va: bool) {
    println!("  {}  {}", if va { "[OK]" } else { "[NO]" }, cosa);
    if !va {
        unsafe { NO += 1 };
    }
}

fn main() {
    println!("rust-processi: std::process dentro EX-OS");

    let o = Command::new("uname").output();
    let testo = o.as_ref().map(|o| String::from_utf8_lossy(&o.stdout).into_owned()).unwrap_or_default();
    ok(&format!("uname dal PATH, uscita letta: {:?}", testo.trim()),
       o.as_ref().map(|o| o.status.success()).unwrap_or(false) && testo.contains("EX-OS"));

    let o = Command::new("/bin/ls").arg("/").output();
    ok("/bin/ls / con un argomento",
       o.map(|o| o.status.success() && String::from_utf8_lossy(&o.stdout).contains("bin")).unwrap_or(false));

    let st = Command::new("/bin/ls").arg("/non-c-e").stdout(Stdio::null()).stderr(Stdio::null()).status();
    ok(&format!("un comando che fallisce: {:?}", st.as_ref().map(|s| s.code())),
       st.map(|s| !s.success()).unwrap_or(false));

    ok("un programma che non c'e' da' NotFound",
       Command::new("non-esiste-xyz").spawn().err().map(|e| e.kind()) == Some(std::io::ErrorKind::NotFound));

    let mut figlio = Command::new("/bin/sh").stdin(Stdio::piped()).stdout(Stdio::piped()).spawn();
    let letto = match figlio.as_mut() {
        Ok(f) => {
            let _ = f.stdin.take().unwrap().write_all(b"echo dalla-pipe\nexit\n");
            let o = figlio.unwrap().wait_with_output();
            o.map(|o| String::from_utf8_lossy(&o.stdout).contains("dalla-pipe")).unwrap_or(false)
        }
        Err(_) => false,
    };
    ok("stdin e stdout su pipe (sh legge un comando e risponde)", letto);

    // `sh -c env`: one line, no banner, no autoexec; env prints what it got
    let o = Command::new("/bin/sh").arg("-c").arg("env").env("PROVA_RUST", "ambiente-ok").output();
    let testo = o.map(|o| String::from_utf8_lossy(&o.stdout).into_owned()).unwrap_or_default();
    if !testo.contains("PROVA_RUST=ambiente-ok") {
        println!("     (il figlio ha scritto {} byte: {:?})", testo.len(), testo.chars().take(300).collect::<String>());
    }
    ok("una variabile d'ambiente al figlio", testo.contains("PROVA_RUST=ambiente-ok"));

    let o = Command::new("/bin/ls").current_dir("/bin").output();
    ok("current_dir per il figlio",
       o.map(|o| String::from_utf8_lossy(&o.stdout).contains("sh")).unwrap_or(false)
       && std::env::current_dir().map(|d| d == std::path::Path::new("/")).unwrap_or(false));

    println!("rust-processi: {} NO", unsafe { NO });
}
