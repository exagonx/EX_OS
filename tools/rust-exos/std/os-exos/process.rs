// =============================================================================
// tools/rust-exos/std/os-exos/process.rs -> library/std/src/sys/process/unix/exos.rs
//
// Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later (this file; the std it joins is
// MIT/Apache-2.0)
//
// std::process for EX-OS (@RUST-64 R64-2, 16 October 2026). A native rustc
// must run the linker, so Command::spawn cannot stay "unsupported" as NuttX
// has it. EX-OS has no fork/exec: a child is made in one call, spawn_ex
// (lib/libc.c), which takes argv, envp and a list of redirections - a child
// descriptor given one of the parent's. The model is std's vxworks.rs (also a
// spawn system); the differences:
//   - stdin/stdout/stderr go through spawn_ex's SpawnRedir, the parent's own
//     descriptors are not touched;
//   - spawn_ex wants a path: a program name without '/' is looked up in PATH
//     (the command's own PATH if it sets one), as execvp would;
//   - the working directory is changed around the call with chdir, as on
//     VxWorks, and put back;
//   - an EX-OS process always ends by exiting: WIFEXITED is always true and
//     the code is the low byte (lib/include/libc.h).
// =============================================================================
#![forbid(unsafe_op_in_unsafe_fn)]
use libc::{self, c_char, c_int, pid_t};

use super::common::*;
use crate::ffi::{CStr, CString, OsStr};
use crate::io::{self, ErrorKind};
use crate::num::NonZero;
use crate::os::unix::ffi::OsStrExt;
use crate::process::StdioPipes;
use crate::sys::cvt;
use crate::{fmt, sys};

#[repr(C)]
struct SpawnRedir {
    fd: c_int,
    flags: c_int,
    percorso: *const c_char,
    fd_padre: c_int,
}

unsafe extern "C" {
    fn spawn_ex(
        path: *const c_char,
        argv: *const *const c_char,
        envp: *const *const c_char,
        redir: *const SpawnRedir,
        n_redir: c_int,
    ) -> c_int;
}

/// The program to start: as given if it has a '/', else the first match in
/// PATH (the command's environment first, then the process's).
fn trova(programma: &CStr, envp: Option<&CStringArray>) -> io::Result<CString> {
    let nome = programma.to_bytes();
    if nome.contains(&b'/') {
        return Ok(programma.to_owned());
    }
    let mut path: Option<Vec<u8>> = None;
    if let Some(env) = envp {
        for p in env.iter() {
            let b = p.to_bytes();
            if let Some(v) = b.strip_prefix(b"PATH=") {
                path = Some(v.to_vec());
            }
        }
    }
    let path = match path {
        Some(p) => p,
        None => crate::env::var_os("PATH")
            .map(|p| p.as_bytes().to_vec())
            .unwrap_or_else(|| b"/bin:/exos/bin".to_vec()),
    };
    for dir in path.split(|&c| c == b':') {
        if dir.is_empty() {
            continue;
        }
        let mut cand = dir.to_vec();
        cand.push(b'/');
        cand.extend_from_slice(nome);
        if crate::fs::metadata(OsStr::from_bytes(&cand)).map(|m| m.is_file()).unwrap_or(false) {
            return CString::new(cand).map_err(|_| io::Error::from(ErrorKind::InvalidInput));
        }
    }
    Err(io::const_error!(ErrorKind::NotFound, "program not found in PATH"))
}

////////////////////////////////////////////////////////////////////////////////
// Command
////////////////////////////////////////////////////////////////////////////////

impl Command {
    pub fn spawn(
        &mut self,
        default: Stdio,
        needs_stdin: bool,
    ) -> io::Result<(Process, StdioPipes)> {
        let envp = self.capture_env();

        if self.saw_nul() {
            return Err(io::const_error!(
                ErrorKind::InvalidInput,
                "nul byte found in provided data",
            ));
        }
        if self.get_chroot().is_some() {
            return Err(io::const_error!(ErrorKind::Unsupported, "chroot not supported by EX-OS"));
        }
        // pre_exec closures cannot run: there is no fork
        let _ = self.get_closures();

        let programma = trova(self.get_program_cstr(), envp.as_ref())?;
        let (ours, theirs) = self.setup_io(default, needs_stdin)?;

        let mut redir: [SpawnRedir; 3] = [
            SpawnRedir { fd: 0, flags: 0, percorso: crate::ptr::null(), fd_padre: -1 },
            SpawnRedir { fd: 1, flags: 0, percorso: crate::ptr::null(), fd_padre: -1 },
            SpawnRedir { fd: 2, flags: 0, percorso: crate::ptr::null(), fd_padre: -1 },
        ];
        let mut n = 0usize;
        for (i, fd) in [theirs.stdin.fd(), theirs.stdout.fd(), theirs.stderr.fd()].into_iter().enumerate() {
            if let Some(fd) = fd {
                redir[n] = SpawnRedir { fd: i as c_int, flags: 0, percorso: crate::ptr::null(), fd_padre: fd };
                n += 1;
            }
        }

        // the working directory, around the call (the parent's is put back)
        let mut prima: Option<crate::path::PathBuf> = None;
        if let Some(cwd) = self.get_cwd() {
            prima = crate::env::current_dir().ok();
            cvt(unsafe { libc::chdir(cwd.as_ptr()) })?;
        }

        let c_envp = envp
            .as_ref()
            .map(|c| c.as_ptr())
            .unwrap_or_else(|| unsafe { *sys::env::environ() as *const _ });

        let ret = {
            // ensure that access to the environment is synchronized
            let _lock = sys::env::env_read_lock();
            unsafe {
                spawn_ex(
                    programma.as_ptr(),
                    self.get_argv().as_ptr(),
                    c_envp,
                    redir.as_ptr(),
                    n as c_int,
                )
            }
        };

        if let Some(p) = prima {
            let _ = crate::env::set_current_dir(p);
        }
        // `theirs` (the child's ends of the pipes) is dropped here, as it must:
        // a pipe with a writer still open in the parent never reads EOF.
        drop(theirs);

        if ret >= 0 {
            Ok((Process { pid: ret as pid_t, status: None }, ours))
        } else {
            Err(io::Error::from_raw_os_error(-ret))
        }
    }

    pub fn exec(&mut self, default: Stdio) -> io::Error {
        match Command::spawn(self, default, false) {
            Ok((mut p, _)) => match p.wait() {
                Ok(st) => crate::process::exit(st.code().unwrap_or(1)),
                Err(e) => e,
            },
            Err(e) => e,
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// Processes
////////////////////////////////////////////////////////////////////////////////

pub struct Process {
    pid: pid_t,
    status: Option<ExitStatus>,
}

impl Process {
    pub fn id(&self) -> u32 {
        self.pid as u32
    }

    pub fn kill(&self) -> io::Result<()> {
        self.send_signal(libc::SIGKILL)
    }

    pub fn send_signal(&self, signal: i32) -> io::Result<()> {
        if self.status.is_some() {
            Ok(())
        } else {
            cvt(unsafe { libc::kill(self.pid, signal) }).map(drop)
        }
    }

    pub fn send_process_group_signal(&self, _signal: i32) -> io::Result<()> {
        Err(io::const_error!(ErrorKind::Unsupported, "EX-OS has no process groups"))
    }

    pub fn wait(&mut self) -> io::Result<ExitStatus> {
        use crate::sys::cvt_r;
        if let Some(status) = self.status {
            return Ok(status);
        }
        let mut status = 0 as c_int;
        cvt_r(|| unsafe { libc::waitpid(self.pid, &mut status, 0) })?;
        self.status = Some(ExitStatus::new(status));
        Ok(ExitStatus::new(status))
    }

    pub fn try_wait(&mut self) -> io::Result<Option<ExitStatus>> {
        if let Some(status) = self.status {
            return Ok(Some(status));
        }
        let mut status = 0 as c_int;
        let pid = cvt(unsafe { libc::waitpid(self.pid, &mut status, 1 /* WNOHANG */) })?;
        if pid == 0 {
            Ok(None)
        } else {
            self.status = Some(ExitStatus::new(status));
            Ok(Some(ExitStatus::new(status)))
        }
    }
}

/// The status waitpid gives on EX-OS: always an exit, the code in the low byte.
#[derive(PartialEq, Eq, Clone, Copy, Debug, Default)]
pub struct ExitStatus(c_int);

impl ExitStatus {
    pub fn new(status: c_int) -> ExitStatus {
        ExitStatus(status)
    }

    pub fn exit_ok(&self) -> Result<(), ExitStatusError> {
        match NonZero::try_from(self.0 & 0xFF) {
            Ok(failure) => Err(ExitStatusError(failure)),
            Err(_) => Ok(()),
        }
    }

    pub fn code(&self) -> Option<i32> {
        Some(self.0 & 0xFF)
    }

    pub fn signal(&self) -> Option<i32> {
        None
    }

    pub fn core_dumped(&self) -> bool {
        false
    }

    pub fn stopped_signal(&self) -> Option<i32> {
        None
    }

    pub fn continued(&self) -> bool {
        false
    }

    pub fn into_raw(&self) -> c_int {
        self.0
    }
}

impl From<c_int> for ExitStatus {
    fn from(a: c_int) -> ExitStatus {
        ExitStatus(a)
    }
}

impl fmt::Display for ExitStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "exit code: {}", self.0 & 0xFF)
    }
}

#[derive(PartialEq, Eq, Clone, Copy, Debug)]
pub struct ExitStatusError(NonZero<c_int>);

impl Into<ExitStatus> for ExitStatusError {
    fn into(self) -> ExitStatus {
        ExitStatus(self.0.into())
    }
}

impl ExitStatusError {
    pub fn code(self) -> Option<NonZero<i32>> {
        ExitStatus(self.0.into()).code().map(|st| st.try_into().unwrap())
    }
}
