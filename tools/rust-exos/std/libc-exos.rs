//! Definitions for EX-OS (i686-unknown-exos).
//!
//! tools/rust-exos/std/libc-exos.rs — copiato da tools/rust-exos/std/prepara.sh
//! in `src/unix/exos/mod.rs` del crate `libc`. Copyright (C) 2026 Graziano
//! Falcone <exagonx@hotmail.com>, SPDX-License-Identifier: GPL-2.0-or-later
//! OR MIT OR Apache-2.0 (come il crate in cui entra).
//!
//! ! OGNI NUMERO QUI DENTRO VIENE DA lib/include DI EX-OS, e se non combacia un
//! programma Rust legge i campi spostati senza che nessun collegamento se ne
//! accorga. Le misure si controllano con tools/rust-exos/std/verifica-abi.sh,
//! che confronta questo file col compilatore C del bersaglio.
//!
//! ! LE COSE CHE EX-OS NON HA sono dichiarate col valore che la libc da' davvero
//! e non con uno inventato: O_EXCL e O_CLOEXEC valgono 0 (la libc li ignora,
//! quindi `File::create_new` non e' atomico). I socket sono solo IPv4 (vedi
//! lib/include/libc.h): le strutture IPv6 servono perche' il crate le nomina.
use crate::prelude::*;
use crate::{in6_addr, in_addr_t, timespec, DIR};

pub type pid_t = c_int;
pub type off_t = c_long;
pub type mode_t = c_uint;
pub type dev_t = c_uint;
pub type ino_t = c_uint;
pub type nlink_t = c_uint;
pub type blksize_t = c_uint;
pub type blkcnt_t = c_uint;
pub type useconds_t = c_uint;
pub type suseconds_t = c_long;
pub type time_t = i64;
pub type clock_t = c_long;
pub type clockid_t = c_int;
pub type locale_t = *mut c_void;
pub type nfds_t = c_uint;
pub type wchar_t = c_int;
pub type sigset_t = c_uint;
pub type sa_family_t = u16;
pub type socklen_t = c_uint;
pub type rlim_t = c_ulong;
pub type fsblkcnt_t = c_ulong;
pub type fsfilcnt_t = c_ulong;
pub type speed_t = c_uint;
pub type tcflag_t = c_uint;
pub type pthread_t = c_ulong;
pub type pthread_key_t = c_uint;
pub type pthread_once_t = c_int;

s! {
    // lib/include/libc.h: 4 x 7 + off_t + 3 time_t = 60 byte
    pub struct stat {
        pub st_dev: dev_t,
        pub st_ino: ino_t,
        pub st_mode: mode_t,
        pub st_nlink: nlink_t,
        pub st_uid: crate::uid_t,
        pub st_gid: crate::gid_t,
        pub st_size: off_t,
        pub st_blksize: blksize_t,
        pub st_blocks: blkcnt_t,
        pub st_atime: time_t,
        pub st_mtime: time_t,
        pub st_ctime: time_t,
    }

    pub struct dirent {
        pub d_ino: c_uint,
        pub d_type: c_uchar,
        pub d_name: [c_char; 256],
    }

    pub struct tm {
        pub tm_sec: c_int,
        pub tm_min: c_int,
        pub tm_hour: c_int,
        pub tm_mday: c_int,
        pub tm_mon: c_int,
        pub tm_year: c_int,
        pub tm_wday: c_int,
        pub tm_yday: c_int,
        pub tm_isdst: c_int,
        pub tm_gmtoff: c_long,
        pub tm_zone: *const c_char,
    }

    pub struct pollfd {
        pub fd: c_int,
        pub events: c_short,
        pub revents: c_short,
    }

    pub struct fd_set {
        bit: c_uint,
    }

    // lib/include/pthread.h: le forme vere stanno la', qui solo la misura.
    pub struct pthread_attr_t {
        __val: [u32; 4],
    }
    pub struct pthread_mutex_t {
        __val: [u32; 4],
    }
    pub struct pthread_mutexattr_t {
        __val: [u32; 1],
    }
    pub struct pthread_cond_t {
        __val: [u32; 2],
    }
    pub struct pthread_condattr_t {
        __val: [u32; 1],
    }
    pub struct pthread_rwlock_t {
        __val: [u32; 9],
    }
    pub struct pthread_rwlockattr_t {
        __val: [u32; 1],
    }
    pub struct sem_t {
        __val: [u32; 1],
    }

    pub struct passwd {
        pub pw_name: *mut c_char,
        pub pw_passwd: *mut c_char,
        pub pw_uid: crate::uid_t,
        pub pw_gid: crate::gid_t,
        pub pw_gecos: *mut c_char,
        pub pw_dir: *mut c_char,
        pub pw_shell: *mut c_char,
    }

    pub struct sigaction {
        pub sa_sigaction: crate::sighandler_t,
        pub sa_mask: sigset_t,
        pub sa_flags: c_int,
    }

    pub struct termios {
        pub c_iflag: tcflag_t,
        pub c_oflag: tcflag_t,
        pub c_cflag: tcflag_t,
        pub c_lflag: tcflag_t,
        pub c_cc: [crate::cc_t; 20],
    }

    // ! EX-OS NON HA statvfs(): la forma e' quella di POSIX perche' il crate
    // la nomina nelle dichiarazioni comuni, e nessuno la puo' riempire.
    pub struct statvfs {
        pub f_bsize: c_ulong,
        pub f_frsize: c_ulong,
        pub f_blocks: fsblkcnt_t,
        pub f_bfree: fsblkcnt_t,
        pub f_bavail: fsblkcnt_t,
        pub f_files: fsfilcnt_t,
        pub f_ffree: fsfilcnt_t,
        pub f_favail: fsfilcnt_t,
        pub f_fsid: c_ulong,
        pub f_flag: c_ulong,
        pub f_namemax: c_ulong,
    }

    pub struct Dl_info {
        pub dli_fname: *const c_char,
        pub dli_fbase: *mut c_void,
        pub dli_sname: *const c_char,
        pub dli_saddr: *mut c_void,
    }

    pub struct lconv {
        pub decimal_point: *mut c_char,
        pub thousands_sep: *mut c_char,
        pub grouping: *mut c_char,
        pub int_curr_symbol: *mut c_char,
        pub currency_symbol: *mut c_char,
        pub mon_decimal_point: *mut c_char,
        pub mon_thousands_sep: *mut c_char,
        pub mon_grouping: *mut c_char,
        pub positive_sign: *mut c_char,
        pub negative_sign: *mut c_char,
        pub int_frac_digits: c_char,
        pub frac_digits: c_char,
        pub p_cs_precedes: c_char,
        pub p_sep_by_space: c_char,
        pub n_cs_precedes: c_char,
        pub n_sep_by_space: c_char,
        pub p_sign_posn: c_char,
        pub n_sign_posn: c_char,
    }

    // I socket: nessuna funzione li usa ancora (vedi in testa).
    pub struct sockaddr {
        pub sa_family: sa_family_t,
        pub sa_data: [c_char; 14],
    }
    pub struct in_addr {
        pub s_addr: in_addr_t,
    }
    pub struct sockaddr_in {
        pub sin_family: sa_family_t,
        pub sin_port: crate::in_port_t,
        pub sin_addr: in_addr,
        pub sin_zero: [u8; 8],
    }
    pub struct sockaddr_in6 {
        pub sin6_family: sa_family_t,
        pub sin6_port: crate::in_port_t,
        pub sin6_flowinfo: u32,
        pub sin6_addr: in6_addr,
        pub sin6_scope_id: u32,
    }
    pub struct sockaddr_un {
        pub sun_family: sa_family_t,
        pub sun_path: [c_char; 108],
    }
    pub struct sockaddr_storage {
        pub ss_family: sa_family_t,
        __ss_pad: [u8; 126],
    }
    pub struct ip_mreq {
        pub imr_multiaddr: in_addr,
        pub imr_interface: in_addr,
    }
    pub struct ipv6_mreq {
        pub ipv6mr_multiaddr: in6_addr,
        pub ipv6mr_interface: c_uint,
    }
    pub struct msghdr {
        pub msg_name: *mut c_void,
        pub msg_namelen: socklen_t,
        pub msg_iov: *mut crate::iovec,
        pub msg_iovlen: c_int,
        pub msg_control: *mut c_void,
        pub msg_controllen: socklen_t,
        pub msg_flags: c_int,
    }
    pub struct addrinfo {
        pub ai_flags: c_int,
        pub ai_family: c_int,
        pub ai_socktype: c_int,
        pub ai_protocol: c_int,
        pub ai_addrlen: socklen_t,
        pub ai_addr: *mut sockaddr,
        pub ai_canonname: *mut c_char,
        pub ai_next: *mut addrinfo,
    }
}

pub const PTHREAD_MUTEX_INITIALIZER: pthread_mutex_t = pthread_mutex_t { __val: [0; 4] };
pub const PTHREAD_COND_INITIALIZER: pthread_cond_t = pthread_cond_t { __val: [0; 2] };
pub const PTHREAD_RWLOCK_INITIALIZER: pthread_rwlock_t = pthread_rwlock_t { __val: [0; 9] };
pub const PTHREAD_ONCE_INIT: pthread_once_t = 0;
pub const PTHREAD_MUTEX_NORMAL: c_int = 0;
pub const PTHREAD_MUTEX_RECURSIVE: c_int = 1;
pub const PTHREAD_MUTEX_ERRORCHECK: c_int = 2;
pub const PTHREAD_MUTEX_DEFAULT: c_int = PTHREAD_MUTEX_NORMAL;
pub const PTHREAD_CREATE_JOINABLE: c_int = 0;
pub const PTHREAD_CREATE_DETACHED: c_int = 1;
pub const PTHREAD_STACK_MIN: size_t = 16384;

pub const RTLD_DEFAULT: *mut c_void = core::ptr::null_mut();

pub const EXIT_SUCCESS: c_int = 0;
pub const EXIT_FAILURE: c_int = 1;
pub const STDIN_FILENO: c_int = 0;
pub const STDOUT_FILENO: c_int = 1;
pub const STDERR_FILENO: c_int = 2;

// time.h
pub const CLOCK_REALTIME: clockid_t = 0;
pub const CLOCK_MONOTONIC: clockid_t = 1;
pub const CLOCK_PROCESS_CPUTIME_ID: clockid_t = 2;
pub const CLOCK_THREAD_CPUTIME_ID: clockid_t = 3;
pub const CLOCK_MONOTONIC_RAW: clockid_t = 4;
pub const CLOCK_REALTIME_COARSE: clockid_t = 5;
pub const CLOCK_MONOTONIC_COARSE: clockid_t = 6;
pub const CLOCK_BOOTTIME: clockid_t = 7;

// errno.h (lib/include/libc.h: i numeri sono quelli di Linux)
pub const EPERM: c_int = 1;
pub const ENOENT: c_int = 2;
pub const ESRCH: c_int = 3;
pub const EINTR: c_int = 4;
pub const EIO: c_int = 5;
pub const ENXIO: c_int = 6;
pub const E2BIG: c_int = 7;
pub const ENOEXEC: c_int = 8;
pub const EBADF: c_int = 9;
pub const ECHILD: c_int = 10;
pub const EAGAIN: c_int = 11;
pub const ENOMEM: c_int = 12;
pub const EACCES: c_int = 13;
pub const EFAULT: c_int = 14;
pub const EBUSY: c_int = 16;
pub const EEXIST: c_int = 17;
pub const EXDEV: c_int = 18;
pub const ENODEV: c_int = 19;
pub const ENOTDIR: c_int = 20;
pub const EISDIR: c_int = 21;
pub const EINVAL: c_int = 22;
pub const ENFILE: c_int = 23;
pub const EMFILE: c_int = 24;
pub const ENOTTY: c_int = 25;
pub const ETXTBSY: c_int = 26;
pub const EFBIG: c_int = 27;
pub const ENOSPC: c_int = 28;
pub const ESPIPE: c_int = 29;
pub const EROFS: c_int = 30;
pub const EMLINK: c_int = 31;
pub const EPIPE: c_int = 32;
pub const EDOM: c_int = 33;
pub const ERANGE: c_int = 34;
pub const EDEADLK: c_int = 35;
pub const ENAMETOOLONG: c_int = 36;
pub const ENOLCK: c_int = 37;
pub const ENOSYS: c_int = 38;
pub const ENOTEMPTY: c_int = 39;
pub const ELOOP: c_int = 40;
pub const EWOULDBLOCK: c_int = EAGAIN;
pub const ENOMSG: c_int = 42;
pub const EIDRM: c_int = 43;
pub const ENOSTR: c_int = 60;
pub const ENODATA: c_int = 61;
pub const ETIME: c_int = 62;
pub const ENOSR: c_int = 63;
pub const ENOLINK: c_int = 67;
pub const EPROTO: c_int = 71;
pub const EBADMSG: c_int = 74;
pub const EOVERFLOW: c_int = 75;
pub const EILSEQ: c_int = 84;
pub const ENOTSOCK: c_int = 88;
pub const EDESTADDRREQ: c_int = 89;
pub const EMSGSIZE: c_int = 90;
pub const EPROTOTYPE: c_int = 91;
pub const ENOPROTOOPT: c_int = 92;
pub const EPROTONOSUPPORT: c_int = 93;
pub const EOPNOTSUPP: c_int = 95;
pub const ENOTSUP: c_int = EOPNOTSUPP;
pub const EAFNOSUPPORT: c_int = 97;
pub const EADDRINUSE: c_int = 98;
pub const EADDRNOTAVAIL: c_int = 99;
pub const ENETDOWN: c_int = 100;
pub const ENETUNREACH: c_int = 101;
pub const ENETRESET: c_int = 102;
pub const ECONNABORTED: c_int = 103;
pub const ECONNRESET: c_int = 104;
pub const ENOBUFS: c_int = 105;
pub const EISCONN: c_int = 106;
pub const ENOTCONN: c_int = 107;
pub const ETIMEDOUT: c_int = 110;
pub const ECONNREFUSED: c_int = 111;
pub const EHOSTUNREACH: c_int = 113;
pub const EALREADY: c_int = 114;
pub const EINPROGRESS: c_int = 115;
pub const ENOMEDIUM: c_int = 123;
pub const ECANCELED: c_int = 125;
pub const EOWNERDEAD: c_int = 130;
pub const ENOTRECOVERABLE: c_int = 131;
// Non li produce nessuno in EX-OS; la std li nomina, e il numero e' quello di
// Linux come tutti gli altri.
pub const ESTALE: c_int = 116;
pub const EDQUOT: c_int = 122;

// fcntl.h
pub const O_RDONLY: c_int = 0;
pub const O_WRONLY: c_int = 1;
pub const O_RDWR: c_int = 2;
pub const O_ACCMODE: c_int = 3;
pub const O_CREAT: c_int = 0x40;
pub const O_EXCL: c_int = 0;
pub const O_NOCTTY: c_int = 0;
pub const O_TRUNC: c_int = 0x200;
pub const O_APPEND: c_int = 0x400;
pub const O_NONBLOCK: c_int = 0x800;
pub const O_SYNC: c_int = 0;
pub const O_DSYNC: c_int = 0;
pub const O_CLOEXEC: c_int = 0;
pub const O_DIRECTORY: c_int = 0;
pub const O_NOFOLLOW: c_int = 0;
pub const F_DUPFD: c_int = 0;
pub const F_DUPFD_CLOEXEC: c_int = 1030;
pub const F_GETFD: c_int = 1;
pub const F_SETFD: c_int = 2;
pub const F_GETFL: c_int = 3;
pub const F_SETFL: c_int = 4;
pub const FD_CLOEXEC: c_int = 1;
pub const AT_FDCWD: c_int = -100;
pub const AT_REMOVEDIR: c_int = 0x200;
pub const SEEK_SET: c_int = 0;
pub const SEEK_CUR: c_int = 1;
pub const SEEK_END: c_int = 2;
pub const F_OK: c_int = 0;
pub const X_OK: c_int = 1;
pub const W_OK: c_int = 2;
pub const R_OK: c_int = 4;

// sys/stat.h
pub const S_IFMT: mode_t = 0o170000;
pub const S_IFDIR: mode_t = 0o040000;
pub const S_IFREG: mode_t = 0o100000;
pub const S_IFIFO: mode_t = 0o010000;
pub const S_IFCHR: mode_t = 0o020000;
pub const S_IFBLK: mode_t = 0o060000;
pub const S_IFLNK: mode_t = 0o120000;
pub const S_IFSOCK: mode_t = 0o140000;
pub const S_IRWXU: mode_t = 0o700;
pub const S_IRUSR: mode_t = 0o400;
pub const S_IWUSR: mode_t = 0o200;
pub const S_IXUSR: mode_t = 0o100;
pub const S_IRWXG: mode_t = 0o070;
pub const S_IRGRP: mode_t = 0o040;
pub const S_IWGRP: mode_t = 0o020;
pub const S_IXGRP: mode_t = 0o010;
pub const S_IRWXO: mode_t = 0o007;
pub const S_IROTH: mode_t = 0o004;
pub const S_IWOTH: mode_t = 0o002;
pub const S_IXOTH: mode_t = 0o001;
pub const S_ISUID: mode_t = 0o4000;
pub const S_ISGID: mode_t = 0o2000;
pub const S_ISVTX: mode_t = 0o1000;

// dirent.h
pub const DT_UNKNOWN: u8 = 0;
pub const DT_FIFO: u8 = 1;
pub const DT_CHR: u8 = 2;
pub const DT_DIR: u8 = 4;
pub const DT_BLK: u8 = 6;
pub const DT_REG: u8 = 8;
pub const DT_LNK: u8 = 10;
pub const DT_SOCK: u8 = 12;

// poll.h
pub const POLLIN: c_short = 0x1;
pub const POLLPRI: c_short = 0x2;
pub const POLLOUT: c_short = 0x4;
pub const POLLERR: c_short = 0x8;
pub const POLLHUP: c_short = 0x10;
pub const POLLNVAL: c_short = 0x20;

// sys/mman.h
pub const PROT_NONE: c_int = 0;
pub const PROT_READ: c_int = 1;
pub const PROT_WRITE: c_int = 2;
pub const PROT_EXEC: c_int = 4;
pub const MAP_SHARED: c_int = 0x01;
pub const MAP_PRIVATE: c_int = 0x02;
pub const MAP_FIXED: c_int = 0x10;
pub const MAP_ANONYMOUS: c_int = 0x20;
pub const MAP_ANON: c_int = MAP_ANONYMOUS;
pub const MAP_FAILED: *mut c_void = !0 as *mut c_void;

// unistd.h
pub const _SC_ARG_MAX: c_int = 0;
pub const _SC_CLK_TCK: c_int = 2;
pub const _SC_OPEN_MAX: c_int = 4;
pub const _SC_PAGESIZE: c_int = 30;
pub const _SC_PAGE_SIZE: c_int = _SC_PAGESIZE;
pub const _SC_NPROCESSORS_ONLN: c_int = 84;
pub const _SC_THREAD_STACK_MIN: c_int = 75;
pub const _SC_HOST_NAME_MAX: c_int = 180;

// limits.h
pub const PATH_MAX: c_int = 320;

// signal.h: i numeri di Linux, quelli che EX-OS ha.
pub const SIGHUP: c_int = 1;
pub const SIGINT: c_int = 2;
pub const SIGQUIT: c_int = 3;
pub const SIGILL: c_int = 4;
pub const SIGTRAP: c_int = 5;
pub const SIGABRT: c_int = 6;
pub const SIGBUS: c_int = 7;
pub const SIGFPE: c_int = 8;
pub const SIGKILL: c_int = 9;
pub const SIGUSR1: c_int = 10;
pub const SIGSEGV: c_int = 11;
pub const SIGUSR2: c_int = 12;
pub const SIGPIPE: c_int = 13;
pub const SIGALRM: c_int = 14;
pub const SIGTERM: c_int = 15;
pub const SIGCHLD: c_int = 17;
pub const SIG_DFL: crate::sighandler_t = 0;
pub const SIG_IGN: crate::sighandler_t = 1;
pub const SIG_ERR: crate::sighandler_t = !0;

// sys/socket.h — i numeri di Linux, come in lib/include/libc.h (sezione «I
// SOCKET BSD», dal 29 settembre 2026). ! I socket stanno nella libc.a, non nella
// libc.so: un programma Rust si collega alla prima.
pub const AF_UNSPEC: c_int = 0;
pub const AF_UNIX: c_int = 1;
pub const AF_INET: c_int = 2;
pub const AF_INET6: c_int = 10;
pub const SOCK_STREAM: c_int = 1;
pub const SOCK_DGRAM: c_int = 2;
pub const SOCK_RAW: c_int = 3;
pub const SOCK_SEQPACKET: c_int = 5;
pub const SOCK_NONBLOCK: c_int = 0o4000;
pub const SOCK_CLOEXEC: c_int = 0o2000000;
pub const SOL_SOCKET: c_int = 1;
pub const SO_REUSEADDR: c_int = 2;
pub const SO_TYPE: c_int = 3;
pub const SO_ERROR: c_int = 4;
pub const SO_BROADCAST: c_int = 6;
pub const SO_SNDBUF: c_int = 7;
pub const SO_RCVBUF: c_int = 8;
pub const SO_KEEPALIVE: c_int = 9;
pub const SO_LINGER: c_int = 13;
pub const SO_RCVTIMEO: c_int = 20;
pub const SO_SNDTIMEO: c_int = 21;
pub const SOMAXCONN: c_int = 128;
pub const IPPROTO_IP: c_int = 0;
pub const IPPROTO_TCP: c_int = 6;
pub const IPPROTO_UDP: c_int = 17;
pub const IPPROTO_IPV6: c_int = 41;
pub const TCP_NODELAY: c_int = 1;
pub const IP_TTL: c_int = 2;
pub const IP_MULTICAST_IF: c_int = 32;
pub const IP_MULTICAST_TTL: c_int = 33;
pub const IP_MULTICAST_LOOP: c_int = 34;
pub const IP_ADD_MEMBERSHIP: c_int = 35;
pub const IP_DROP_MEMBERSHIP: c_int = 36;
pub const IPV6_UNICAST_HOPS: c_int = 16;
pub const IPV6_MULTICAST_IF: c_int = 17;
pub const IPV6_MULTICAST_HOPS: c_int = 18;
pub const IPV6_MULTICAST_LOOP: c_int = 19;
pub const IPV6_JOIN_GROUP: c_int = 20;
pub const IPV6_LEAVE_GROUP: c_int = 21;
pub const IPV6_V6ONLY: c_int = 26;
pub const SHUT_RD: c_int = 0;
pub const SHUT_WR: c_int = 1;
pub const SHUT_RDWR: c_int = 2;
pub const MSG_PEEK: c_int = 2;
pub const MSG_NOSIGNAL: c_int = 0x4000;
pub const MSG_OOB: c_int = 0x1;
pub const MSG_DONTWAIT: c_int = 0x40;
pub const MSG_WAITALL: c_int = 0x100;
pub const SO_REUSEPORT: c_int = 15;
pub const IPPROTO_ICMP: c_int = 1;
pub const TCP_KEEPIDLE: c_int = 4;
pub const TCP_KEEPINTVL: c_int = 5;
pub const TCP_KEEPCNT: c_int = 6;
pub const FIONREAD: c_int = 0x541B;
pub const FIONBIO: c_int = 0x5421;
pub const EAI_SYSTEM: c_int = -11;

extern "C" {
    // ! errno e' per filo: lib/libc.c, __errno_dove.
    pub fn __errno_dove() -> *mut c_int;

    pub fn pthread_create(
        thread: *mut pthread_t,
        attr: *const pthread_attr_t,
        start_routine: extern "C" fn(*mut c_void) -> *mut c_void,
        arg: *mut c_void,
    ) -> c_int;
    pub fn pthread_getattr_np(thread: pthread_t, attr: *mut pthread_attr_t) -> c_int;
    pub fn pthread_attr_getstack(
        attr: *const pthread_attr_t,
        stackaddr: *mut *mut c_void,
        stacksize: *mut size_t,
    ) -> c_int;
    pub fn pthread_condattr_setclock(attr: *mut pthread_condattr_t, clock_id: clockid_t) -> c_int;
    pub fn pthread_setname_np(thread: pthread_t, name: *const c_char) -> c_int;
    pub fn pthread_getname_np(thread: pthread_t, name: *mut c_char, len: size_t) -> c_int;
    pub fn clock_gettime(clockid: clockid_t, tp: *mut timespec) -> c_int;
    pub fn clock_getres(clockid: clockid_t, tp: *mut timespec) -> c_int;
    pub fn getrandom(buf: *mut c_void, buflen: size_t, flags: c_uint) -> ssize_t;
    pub fn strerror_r(errnum: c_int, buf: *mut c_char, buflen: size_t) -> c_int;
    pub fn dirfd(dirp: *mut DIR) -> c_int;
    pub fn memalign(align: size_t, size: size_t) -> *mut c_void;
    pub fn getentropy(buf: *mut c_void, buflen: size_t) -> c_int;
    pub fn ioctl(fd: c_int, request: c_int, ...) -> c_int;
    pub fn futimens(fd: c_int, times: *const timespec) -> c_int;
    pub fn pipe2(fds: *mut c_int, flags: c_int) -> c_int;

    // I socket: lib/libc.c, sezione «I SOCKET BSD».
    pub fn bind(sockfd: c_int, addr: *const sockaddr, addrlen: socklen_t) -> c_int;
    pub fn recvfrom(
        sockfd: c_int,
        buf: *mut c_void,
        len: size_t,
        flags: c_int,
        src_addr: *mut sockaddr,
        addrlen: *mut socklen_t,
    ) -> ssize_t;
    pub fn accept4(
        sockfd: c_int,
        addr: *mut sockaddr,
        addrlen: *mut socklen_t,
        flags: c_int,
    ) -> c_int;
    pub fn recvmsg(sockfd: c_int, msg: *mut msghdr, flags: c_int) -> ssize_t;
    pub fn sendmsg(sockfd: c_int, msg: *const msghdr, flags: c_int) -> ssize_t;
}
