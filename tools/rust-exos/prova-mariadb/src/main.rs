//! The `mariadb` crate inside EX-OS (tools/prova_mariadb.sh): connect, a
//! table, rows in with an escaped value, rows back, an error from a bad
//! query. One line per check: CONNRS ok/NO <what>, then "CONNRS fine <n>".
use std::env;

fn main() {
    let a: Vec<String> = env::args().collect();
    let host = a.get(1).map(|s| s.as_str()).unwrap_or("127.0.0.1");
    let port: u16 = a.get(2).and_then(|s| s.parse().ok()).unwrap_or(3306);
    let mut no = 0;
    let mut esito = |ok: bool, cosa: &str| {
        println!("CONNRS {} {}", if ok { "ok" } else { "NO" }, cosa);
        if !ok { no += 1; }
    };
    match mariadb::Conn::connect(host, "root", "", Some("prova"), port) {
        Err(e) => esito(false, &format!("connessione: {}", e)),
        Ok(c) => {
            println!("CONNRS versione {} (client {})", c.server_version(), mariadb::client_version());
            esito(true, "connessione");
            esito(c.query("create table if not exists rs (id int auto_increment primary key, nome varchar(40)) engine=innodb").is_ok(),
                  "create table");
            let mut n = 0;
            for nome in ["RS1", "RS'2", "RS3"] {
                if c.query(&format!("insert into rs (nome) values ('{}')", c.escape(nome))).ok() == Some(1) {
                    n += 1;
                }
            }
            esito(n == 3 && c.insert_id() == 3, "3 insert, uno con l'apice");
            match c.rows("select nome, null from rs order by id") {
                Ok(r) => {
                    let t: Vec<String> = r.iter().map(|x| x[0].clone().unwrap_or_default()).collect();
                    println!("CONNRS righe {}", t.join(","));
                    esito(t.join(",") == "RS1,RS'2,RS3" && r[0][1].is_none(), "righe lette, NULL");
                }
                Err(e) => esito(false, &format!("select: {}", e)),
            }
            match c.rows("select * from tabella_che_non_esiste") {
                Err(e) => esito(e.code == 1146, "errore da una query sbagliata"),
                Ok(_) => esito(false, "errore da una query sbagliata"),
            }
        }
    }
    println!("CONNRS fine {}", no);
    std::process::exit(if no == 0 { 0 } else { 1 });
}
