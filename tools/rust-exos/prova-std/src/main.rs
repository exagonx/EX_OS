//! @RUST-STD: la std di Rust dentro EX-OS (tools/rust-exos/prova-std.sh).
//!
//! Ogni controllo stampa [OK] o [NO]; l'ultima riga dice quanti NO. Sono le
//! parti della std che servono a Firefox e a cargo: memoria, testo, file,
//! directory, ambiente, tempo, fili con i loro lucchetti, variabili per filo.
use std::cell::Cell;
use std::collections::HashMap;
use std::io::{Read, Write};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{mpsc, Arc, Condvar, Mutex, RwLock};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};
use std::{env, fs, thread};

static NO: AtomicUsize = AtomicUsize::new(0);

fn ok(cosa: &str, va: bool) {
    println!("  {}  {}", if va { "[OK]" } else { "[NO]" }, cosa);
    if !va {
        NO.fetch_add(1, Ordering::SeqCst);
    }
}

thread_local! {
    static MIO: Cell<u32> = const { Cell::new(7) };
}

fn main() {
    println!("rust-std: la std di Rust dentro EX-OS");

    // argomenti e ambiente
    let args: Vec<String> = env::args().collect();
    ok(&format!("argomenti: {:?}", args), args.len() == 3 && args[1] == "uno");
    env::set_var("PROVA_RUST", "si'");
    ok("ambiente: set_var e var", env::var("PROVA_RUST").as_deref() == Ok("si'"));
    ok(&format!("sistema: {} / {}", env::consts::OS, env::consts::FAMILY),
       env::consts::OS == "exos" && cfg!(unix));

    // memoria e testo
    let mut m = HashMap::new();
    for i in 0..1000u32 {
        m.insert(format!("chiave{}", i), i * i);
    }
    ok("HashMap con 1000 voci", m.len() == 1000 && m["chiave31"] == 961);
    let v: Vec<u64> = (1..=100).collect();
    ok("Vec e iteratori (somma 5050)", v.iter().sum::<u64>() == 5050);
    ok("f64 (radice di 2)", ((2.0f64).sqrt() - 1.41421356).abs() < 1e-6);

    // file e directory
    let dir = "/disk/rustdir";
    let _ = fs::remove_dir_all(dir);
    ok("create_dir", fs::create_dir(dir).is_ok());
    let p = format!("{}/testo.txt", dir);
    let scritto = fs::File::create(&p)
        .and_then(|mut f| f.write_all(b"ciao da Rust\nseconda riga\n"));
    ok("File::create e write_all", scritto.is_ok());
    let mut s = String::new();
    let letto = fs::File::open(&p).and_then(|mut f| f.read_to_string(&mut s));
    ok("File::open e read_to_string", letto.is_ok() && s.lines().count() == 2);
    let md = fs::metadata(&p);
    ok("metadata: misura 26, e' un file",
       md.as_ref().map(|m| m.len() == 26 && m.is_file()).unwrap_or(false));
    let voci: Vec<String> = fs::read_dir(dir)
        .map(|r| r.filter_map(|e| e.ok()).map(|e| e.file_name().to_string_lossy().into_owned()).collect())
        .unwrap_or_default();
    ok(&format!("read_dir: {:?}", voci), voci == vec!["testo.txt".to_string()]);
    ok("rename", fs::rename(&p, format!("{}/altro.txt", dir)).is_ok());
    ok("un file che non c'e' da' NotFound",
       fs::File::open("/disk/non-esiste").map_err(|e| e.kind()) .err()
           == Some(std::io::ErrorKind::NotFound));
    let altro = format!("{}/altro.txt", dir);
    ok("remove_file", fs::remove_file(&altro).is_ok() && fs::metadata(&altro).is_err());
    let r = fs::remove_dir(dir);
    let resta = fs::metadata(dir).is_ok();
    println!("        (remove_dir: {:?}, la directory c'e' ancora: {})", r.as_ref().err(), resta);
    ok("remove_dir", r.is_ok() && !resta);

    let albero = "/disk/rustalbero";
    let tutte = fs::create_dir_all(format!("{}/a/b/c", albero));
    if let Err(e) = &tutte { println!("        (create_dir_all: {:?})", e); }
    let orfana = fs::create_dir("/disk/non-c-e/figlia");
    println!("        (mkdir con il genitore che manca: {:?})", orfana.as_ref().err().map(|e| e.kind()));
    ok("mkdir con il genitore che manca da' NotFound",
       orfana.as_ref().err().map(|e| e.kind()) == Some(std::io::ErrorKind::NotFound));
    let fatto = tutte.is_ok()
        && fs::write(format!("{}/a/uno.txt", albero), b"1").is_ok()
        && fs::write(format!("{}/a/b/c/due.txt", albero), b"22").is_ok();
    ok("create_dir_all e fs::write in un albero", fatto);
    let via = fs::remove_dir_all(albero);
    let resta = fs::metadata(albero).is_ok();
    println!("        (remove_dir_all: {:?}, l'albero c'e' ancora: {})", via.as_ref().err(), resta);
    ok("remove_dir_all", via.is_ok() && !resta);

    // tempo
    let t0 = Instant::now();
    thread::sleep(Duration::from_millis(200));
    let dt = t0.elapsed();
    println!("        (sleep di 200 ms: {:?})", dt);
    ok("Instant e sleep", dt >= Duration::from_millis(190) && dt < Duration::from_secs(2));
    let adesso = SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0);
    ok(&format!("SystemTime: {} secondi dal 1970", adesso), adesso > 1_700_000_000);

    // fili
    let fatti: Vec<_> = (0..8u64).map(|i| thread::spawn(move || i * i)).collect();
    let somma: u64 = fatti.into_iter().map(|h| h.join().unwrap()).sum();
    ok("thread::spawn e join, otto fili (somma 140)", somma == 140);

    let conto = Arc::new(Mutex::new(0u32));
    let mani: Vec<_> = (0..6).map(|_| {
        let c = Arc::clone(&conto);
        thread::spawn(move || for _ in 0..10000 { *c.lock().unwrap() += 1; })
    }).collect();
    for h in mani { h.join().unwrap(); }
    ok("Mutex conteso: 6 x 10000", *conto.lock().unwrap() == 60000);

    let coppia = Arc::new((Mutex::new(false), Condvar::new()));
    let c2 = Arc::clone(&coppia);
    let h = thread::spawn(move || {
        thread::sleep(Duration::from_millis(50));
        *c2.0.lock().unwrap() = true;
        c2.1.notify_one();
    });
    let mut pronto = coppia.0.lock().unwrap();
    while !*pronto { pronto = coppia.1.wait(pronto).unwrap(); }
    drop(pronto);
    h.join().unwrap();
    ok("Condvar: un filo sveglia l'altro", true);

    let (tx, rx) = mpsc::channel();
    for i in 0..4u32 {
        let tx = tx.clone();
        thread::spawn(move || { for k in 0..25 { tx.send(i * 100 + k).unwrap(); } });
    }
    drop(tx);
    let ricevuti: Vec<u32> = rx.iter().collect();
    ok("mpsc: quattro mittenti, 100 messaggi", ricevuti.len() == 100);

    let rw = RwLock::new(5);
    { let a = rw.read().unwrap(); let b = rw.read().unwrap(); ok("RwLock: due lettori", *a + *b == 10); }
    *rw.write().unwrap() = 6;
    ok("RwLock: lo scrittore", *rw.read().unwrap() == 6);

    MIO.with(|m| m.set(1));
    let figlio = thread::spawn(|| MIO.with(|m| { let prima = m.get(); m.set(99); prima })).join().unwrap();
    ok("thread_local!: il figlio parte da 7, il principale ha ancora 1",
       figlio == 7 && MIO.with(|m| m.get()) == 1);

    let nome = thread::Builder::new().name("operaio".into())
        .spawn(|| thread::current().name().map(|s| s.to_string())).unwrap().join().unwrap();
    ok("thread::Builder con un nome", nome.as_deref() == Some("operaio"));

    let n = NO.load(Ordering::SeqCst);
    println!("rust-std: {} NO", n);
    std::process::exit(n as i32);
}
