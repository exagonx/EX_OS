//! @SOCKET-BSD: std::net di Rust dentro EX-OS (tools/rust-exos/prova-rete.sh).
//!
//! L'altra parte e' tools/exilla/prova-socket-host.py: eco TCP a
//! 10.0.0.2:7801, eco UDP (in maiuscolo) a 10.0.0.2:7802, e un cliente che
//! bussa alla nostra 7000 quando stampiamo «IN ASCOLTO 7000».
use std::io::{ErrorKind, Read, Write};
use std::net::{TcpListener, TcpStream, ToSocketAddrs, UdpSocket};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::{Duration, Instant};

static NO: AtomicUsize = AtomicUsize::new(0);

fn ok(cosa: &str, va: bool) {
    println!("  {}  {}", if va { "[OK]" } else { "[NO]" }, cosa);
    if !va {
        NO.fetch_add(1, Ordering::SeqCst);
    }
}

fn main() {
    println!("prova-rete: std::net di Rust dentro EX-OS");

    let eco = TcpStream::connect("10.0.0.2:7801");
    ok(&format!("TcpStream::connect: {:?}", eco.as_ref().err()), eco.is_ok());
    if let Ok(mut s) = eco {
        let via: Vec<u8> = (0..60000u32).map(|i| (i * 13 + i / 97) as u8).collect();
        let mut torna = vec![0u8; via.len()];
        let scritto = s.write_all(&via);
        let letto = s.read_exact(&mut torna);
        ok("write_all e read_exact di 60000 byte, uguali",
           scritto.is_ok() && letto.is_ok() && via == torna);
        ok(&format!("peer_addr {:?}", s.peer_addr().ok()),
           s.peer_addr().map(|a| a.to_string() == "10.0.0.2:7801").unwrap_or(false));

        s.set_nonblocking(true).unwrap();
        let mut b = [0u8; 16];
        ok("set_nonblocking: read rende WouldBlock",
           s.read(&mut b).err().map(|e| e.kind()) == Some(ErrorKind::WouldBlock));
        s.set_nonblocking(false).unwrap();

        s.set_read_timeout(Some(Duration::from_millis(300))).unwrap();
        let t0 = Instant::now();
        let r = s.read(&mut b);
        let dt = t0.elapsed();
        println!("        (scaduta dopo {:?})", dt);
        ok("set_read_timeout: scade dopo ~300 ms",
           r.is_err() && dt >= Duration::from_millis(280) && dt < Duration::from_millis(1500));
        s.set_read_timeout(None).unwrap();

        // Il socket in comune fra due fili: uno legge, l'altro scrive.
        let s = Arc::new(s);
        let s2 = Arc::clone(&s);
        let lettore = thread::spawn(move || {
            let mut b = [0u8; 8];
            (&*s2).read_exact(&mut b).map(|_| b)
        });
        thread::sleep(Duration::from_millis(200));
        (&*s).write_all(b"dal filo").unwrap();
        let r = lettore.join().unwrap();
        ok("un filo legge cio' che l'altro ha scritto sullo stesso TcpStream",
           r.map(|b| &b == b"dal filo").unwrap_or(false));
    }

    let u = UdpSocket::bind("0.0.0.0:0").unwrap();
    u.set_read_timeout(Some(Duration::from_secs(3))).unwrap();
    let mandati = u.send_to(b"rust udp", "10.0.0.2:7802");
    let mut b = [0u8; 32];
    let r = u.recv_from(&mut b);
    ok(&format!("UdpSocket: send_to e recv_from ({:?})", r.as_ref().map(|x| x.1)),
       mandati.ok() == Some(8) && r.map(|(n, da)| &b[..n] == b"RUST UDP" && da.port() == 7802)
           .unwrap_or(false));

    let ascolta = TcpListener::bind("0.0.0.0:7000");
    ok("TcpListener::bind sulla 7000", ascolta.is_ok());
    if let Ok(l) = ascolta {
        println!("prova-rete: IN ASCOLTO 7000");
        match l.accept() {
            Ok((mut c, da)) => {
                let mut b = [0u8; 4];
                let letto = c.read_exact(&mut b);
                let su = b.to_ascii_uppercase();
                ok(&format!("accept da {} e risposta in maiuscolo", da),
                   letto.is_ok() && c.write_all(&su).is_ok());
            }
            Err(e) => ok(&format!("accept: {}", e), false),
        }
    }

    match ("example.com", 443).to_socket_addrs() {
        Ok(mut a) => println!("        (example.com = {:?})", a.next()),
        Err(e) => println!("        (example.com: {} — senza internet dall'host e' normale)", e),
    }

    let n = NO.load(Ordering::SeqCst);
    println!("prova-rete: {} NO", n);
    std::process::exit(n as i32);
}
