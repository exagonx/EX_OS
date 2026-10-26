//! The `mariadb` crate — MariaDB and MySQL from Rust on EX-OS, over the C API
//! of Connector/C (libmariadbclient.a, tools/mariadb-connector-c-exos).
//!
//! Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
//! SPDX-License-Identifier: GPL-2.0-or-later
//!
//! ```ignore
//! let c = mariadb::Conn::connect("127.0.0.1", "root", "", Some("prova"), 3306)?;
//! c.query("insert into t values (1, 'EX-OS')")?;
//! for row in c.rows("select id, nome from t")? {
//!     println!("{:?}", row);          // [Some("1"), Some("EX-OS")]
//! }
//! ```
//!
//! ! NO CARGO NEEDED, NO DEPENDENCIES: EX-OS has rustc but not cargo, and
//! the crates.io drivers (mysql, sqlx) pull dozens of crates. This one is a
//! single file of `extern "C"` and a thin safe layer, built with
//!     rustc -O --crate-type rlib --edition 2021 lib.rs -o libmariadb.rlib
//! and used with `--extern mariadb=libmariadb.rlib`. The libraries come from
//! the #[link] lines below: the gcc that links Rust programs finds them in
//! /exos/lib like any -l.
//!
//! Values come back as text (`Option<String>`, None for NULL), as the C API
//! gives them; a parameter goes into a statement through `escape`.
use std::ffi::{CStr, CString};
use std::fmt;
use std::os::raw::{c_char, c_int, c_uint, c_ulong};
use std::ptr;

#[repr(C)]
struct Mysql {
    _p: [u8; 0],
}
#[repr(C)]
struct MysqlRes {
    _p: [u8; 0],
}

// ! THE ORDER IS THE LINK ORDER: libmariadbclient wants OpenSSL, zlib and
// zstd after it, and m for floor/ceil. No kind = "static": rustc would look
// for the .a itself and put it inside the rlib; the gcc that links finds it
#[link(name = "mariadbclient")]
#[link(name = "ssl")]
#[link(name = "crypto")]
#[link(name = "z")]
#[link(name = "zstd")]
#[link(name = "pthread")]
#[link(name = "m")]
extern "C" {
    fn mysql_init(m: *mut Mysql) -> *mut Mysql;
    fn mysql_real_connect(m: *mut Mysql, host: *const c_char, user: *const c_char,
                          passwd: *const c_char, db: *const c_char, port: c_uint,
                          unix_socket: *const c_char, flags: c_ulong) -> *mut Mysql;
    fn mysql_close(m: *mut Mysql);
    fn mysql_error(m: *mut Mysql) -> *const c_char;
    fn mysql_errno(m: *mut Mysql) -> c_uint;
    fn mysql_real_query(m: *mut Mysql, q: *const c_char, len: c_ulong) -> c_int;
    fn mysql_store_result(m: *mut Mysql) -> *mut MysqlRes;
    fn mysql_field_count(m: *mut Mysql) -> c_uint;
    fn mysql_num_fields(r: *mut MysqlRes) -> c_uint;
    fn mysql_fetch_row(r: *mut MysqlRes) -> *mut *mut c_char;
    fn mysql_fetch_lengths(r: *mut MysqlRes) -> *mut c_ulong;
    fn mysql_free_result(r: *mut MysqlRes);
    fn mysql_affected_rows(m: *mut Mysql) -> u64;
    fn mysql_insert_id(m: *mut Mysql) -> u64;
    fn mysql_get_server_info(m: *mut Mysql) -> *const c_char;
    fn mysql_get_client_info() -> *const c_char;
    fn mysql_real_escape_string(m: *mut Mysql, to: *mut c_char, from: *const c_char,
                                len: c_ulong) -> c_ulong;
}

/// An error from the server or the library: its number (1146 = no such
/// table, 2003 = cannot connect...) and its message.
#[derive(Debug, Clone)]
pub struct Error {
    pub code: u32,
    pub message: String,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "MariaDB {}: {}", self.code, self.message)
    }
}

impl std::error::Error for Error {}

/// A row: one `Option<String>` per column, None for NULL.
pub type Row = Vec<Option<String>>;

/// One connection. Not shared between threads (the C handle is not).
pub struct Conn {
    m: *mut Mysql,
}

fn testo(p: *const c_char) -> String {
    if p.is_null() {
        String::new()
    } else {
        unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned()
    }
}

fn c(s: &str) -> Result<CString, Error> {
    CString::new(s).map_err(|_| Error { code: 0, message: "a NUL byte in the text".into() })
}

impl Conn {
    /// Connects over TCP (EX-OS has no UNIX sockets). `db` None: no default
    /// database.
    pub fn connect(host: &str, user: &str, password: &str, db: Option<&str>, port: u16)
        -> Result<Conn, Error> {
        let (h, u, p) = (c(host)?, c(user)?, c(password)?);
        let d = match db { Some(d) => Some(c(d)?), None => None };
        unsafe {
            let m = mysql_init(ptr::null_mut());
            if m.is_null() {
                return Err(Error { code: 2008, message: "out of memory".into() });
            }
            let conn = Conn { m };
            if mysql_real_connect(m, h.as_ptr(), u.as_ptr(), p.as_ptr(),
                                  d.as_ref().map_or(ptr::null(), |d| d.as_ptr()),
                                  port as c_uint, ptr::null(), 0).is_null() {
                return Err(conn.error());
            }
            Ok(conn)
        }
    }

    fn error(&self) -> Error {
        unsafe { Error { code: mysql_errno(self.m), message: testo(mysql_error(self.m)) } }
    }

    /// A statement with no rows back (insert, update, create...): the rows
    /// it changed. A select's rows are read and thrown away.
    pub fn query(&self, sql: &str) -> Result<u64, Error> {
        unsafe {
            if mysql_real_query(self.m, sql.as_ptr() as *const c_char, sql.len() as c_ulong) != 0 {
                return Err(self.error());
            }
            let r = mysql_store_result(self.m);
            if !r.is_null() {
                mysql_free_result(r);
            } else if mysql_field_count(self.m) != 0 {
                return Err(self.error());
            }
            Ok(mysql_affected_rows(self.m))
        }
    }

    /// A statement with rows back, all of them in memory.
    pub fn rows(&self, sql: &str) -> Result<Vec<Row>, Error> {
        unsafe {
            if mysql_real_query(self.m, sql.as_ptr() as *const c_char, sql.len() as c_ulong) != 0 {
                return Err(self.error());
            }
            let r = mysql_store_result(self.m);
            if r.is_null() {
                if mysql_field_count(self.m) != 0 {
                    return Err(self.error());
                }
                return Ok(Vec::new());
            }
            let n = mysql_num_fields(r) as usize;
            let mut tutte = Vec::new();
            loop {
                let riga = mysql_fetch_row(r);
                if riga.is_null() {
                    break;
                }
                let lung = mysql_fetch_lengths(r);
                let mut v = Vec::with_capacity(n);
                for i in 0..n {
                    let p = *riga.add(i);
                    if p.is_null() {
                        v.push(None);
                    } else {
                        let b = std::slice::from_raw_parts(p as *const u8, *lung.add(i) as usize);
                        v.push(Some(String::from_utf8_lossy(b).into_owned()));
                    }
                }
                tutte.push(v);
            }
            mysql_free_result(r);
            Ok(tutte)
        }
    }

    /// The text made safe to put between quotes in a statement.
    pub fn escape(&self, s: &str) -> String {
        let mut buf = vec![0u8; s.len() * 2 + 1];
        unsafe {
            let n = mysql_real_escape_string(self.m, buf.as_mut_ptr() as *mut c_char,
                                             s.as_ptr() as *const c_char, s.len() as c_ulong);
            buf.truncate(n as usize);
        }
        String::from_utf8_lossy(&buf).into_owned()
    }

    /// The AUTO_INCREMENT value of the last insert.
    pub fn insert_id(&self) -> u64 {
        unsafe { mysql_insert_id(self.m) }
    }

    /// The server's version, e.g. "11.8.9-MariaDB".
    pub fn server_version(&self) -> String {
        unsafe { testo(mysql_get_server_info(self.m)) }
    }
}

impl Drop for Conn {
    fn drop(&mut self) {
        unsafe { mysql_close(self.m) }
    }
}

/// The version of Connector/C linked in.
pub fn client_version() -> String {
    unsafe { testo(mysql_get_client_info()) }
}
