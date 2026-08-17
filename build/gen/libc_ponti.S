/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * I ponti verso libc.so: 324 salti indiretti, uno per funzione.
 *
 * __libc_ponti_nomi e' un BLOCCO di stringhe una dopo l'altra, nello stesso
 * ordine della tabella: si scorre avanzando di strlen+1.
 *
 * Ogni ponte e' un `jmp *tabella+N`, e non tocca ne' gli argomenti ne' il
 * valore di ritorno: e' il motivo per cui questo file puo' esistere senza
 * conoscere una sola firma. La tabella la riempie __libc_ponti_avvia() prima
 * che il programma esegua qualunque altra cosa.
 */

    .section .bss
    .align 4
    .globl __libc_ponti_tabella
__libc_ponti_tabella:
    .space 1296

    .section .rodata
    .align 4
    .globl __libc_ponti_nomi
__libc_ponti_nomi:
    .asciz "_Exit"
    .asciz "__environ_dove"
    .asciz "__errno_dove"
    .asciz "__libc_distruttori_registra"
    .asciz "__stderr_dove"
    .asciz "__stdin_dove"
    .asciz "__stdout_dove"
    .asciz "_assert_fallita"
    .asciz "_exit"
    .asciz "abort"
    .asciz "abs"
    .asciz "access"
    .asciz "aligned_alloc"
    .asciz "asctime"
    .asciz "at_quick_exit"
    .asciz "atexit"
    .asciz "atof"
    .asciz "atoi"
    .asciz "atol"
    .asciz "atoll"
    .asciz "blkinfo"
    .asciz "blkread"
    .asciz "blkwrite"
    .asciz "bootinstall"
    .asciz "bootverify"
    .asciz "bsearch"
    .asciz "calloc"
    .asciz "chdir"
    .asciz "chmod"
    .asciz "chown"
    .asciz "clearerr"
    .asciz "clock"
    .asciz "close"
    .asciz "closedir"
    .asciz "console_info"
    .asciz "console_setfg"
    .asciz "console_switch"
    .asciz "console_write"
    .asciz "ctime"
    .asciz "difftime"
    .asciz "diskinfo"
    .asciz "div"
    .asciz "dma_alloc"
    .asciz "dup"
    .asciz "dup2"
    .asciz "execv"
    .asciz "execvp"
    .asciz "exit"
    .asciz "fabs"
    .asciz "fchmod"
    .asciz "fclose"
    .asciz "fcntl"
    .asciz "fdopen"
    .asciz "feof"
    .asciz "ferror"
    .asciz "fflush"
    .asciz "fgetc"
    .asciz "fgetpos"
    .asciz "fgets"
    .asciz "fileno"
    .asciz "fopen"
    .asciz "fpathconf"
    .asciz "fprintf"
    .asciz "fputc"
    .asciz "fputs"
    .asciz "fread"
    .asciz "free"
    .asciz "freopen"
    .asciz "frexp"
    .asciz "fscanf"
    .asciz "fseek"
    .asciz "fsetpos"
    .asciz "fsize"
    .asciz "fstat"
    .asciz "ftell"
    .asciz "fwrite"
    .asciz "getc"
    .asciz "getchar"
    .asciz "getconf"
    .asciz "getcwd"
    .asciz "getegid"
    .asciz "getentropy"
    .asciz "getenv"
    .asciz "geteuid"
    .asciz "getgid"
    .asciz "getpagesize"
    .asciz "getpid"
    .asciz "getrandom"
    .asciz "getrusage"
    .asciz "gets"
    .asciz "gettimeofday"
    .asciz "getuid"
    .asciz "gmtime"
    .asciz "imaxabs"
    .asciz "imaxdiv"
    .asciz "ioctl"
    .asciz "ioport_bind"
    .asciz "ioport_in"
    .asciz "ioport_in16"
    .asciz "ioport_in32"
    .asciz "ioport_out"
    .asciz "ioport_out16"
    .asciz "ioport_out32"
    .asciz "ipc_lookup"
    .asciz "ipc_recv"
    .asciz "ipc_recv_timeout"
    .asciz "ipc_register"
    .asciz "ipc_send"
    .asciz "irq_bind"
    .asciz "irq_done"
    .asciz "isalnum"
    .asciz "isalpha"
    .asciz "isascii"
    .asciz "isatty"
    .asciz "isblank"
    .asciz "iscntrl"
    .asciz "isdigit"
    .asciz "isgraph"
    .asciz "islower"
    .asciz "isprint"
    .asciz "ispunct"
    .asciz "isspace"
    .asciz "isupper"
    .asciz "iswalnum"
    .asciz "iswalpha"
    .asciz "iswblank"
    .asciz "iswcntrl"
    .asciz "iswdigit"
    .asciz "iswgraph"
    .asciz "iswlower"
    .asciz "iswprint"
    .asciz "iswpunct"
    .asciz "iswspace"
    .asciz "iswupper"
    .asciz "iswxdigit"
    .asciz "isxdigit"
    .asciz "labs"
    .asciz "ldexp"
    .asciz "ldiv"
    .asciz "listdir"
    .asciz "listdir_from"
    .asciz "llabs"
    .asciz "lldiv"
    .asciz "localeconv"
    .asciz "localtime"
    .asciz "log_seriale"
    .asciz "longjmp"
    .asciz "lseek"
    .asciz "lstat"
    .asciz "malloc"
    .asciz "mblen"
    .asciz "mbrtowc"
    .asciz "mbstowcs"
    .asciz "mbtowc"
    .asciz "memalign"
    .asciz "memchr"
    .asciz "memcmp"
    .asciz "memcpy"
    .asciz "meminfo"
    .asciz "memmove"
    .asciz "memset"
    .asciz "mkdir"
    .asciz "mkstemp"
    .asciz "mktemp"
    .asciz "mktime"
    .asciz "mmap"
    .asciz "mmio_map"
    .asciz "modo_testo"
    .asciz "mount"
    .asciz "mountinfo"
    .asciz "munmap"
    .asciz "nanosleep"
    .asciz "open"
    .asciz "opendir"
    .asciz "osversion"
    .asciz "partwrite"
    .asciz "pathconf"
    .asciz "pclose"
    .asciz "perror"
    .asciz "pipe"
    .asciz "poll"
    .asciz "popen"
    .asciz "posix_memalign"
    .asciz "printf"
    .asciz "procinfo"
    .asciz "putc"
    .asciz "putchar"
    .asciz "putenv"
    .asciz "puts"
    .asciz "qsort"
    .asciz "quick_exit"
    .asciz "raise"
    .asciz "rand"
    .asciz "read"
    .asciz "readdir"
    .asciz "realloc"
    .asciz "realpath"
    .asciz "reboot"
    .asciz "remove"
    .asciz "rename"
    .asciz "rewind"
    .asciz "rewinddir"
    .asciz "rmdir"
    .asciz "sbrk"
    .asciz "scanf"
    .asciz "sched_yield"
    .asciz "select"
    .asciz "setbuf"
    .asciz "setenv"
    .asciz "setjmp"
    .asciz "setlocale"
    .asciz "setuid"
    .asciz "setvbuf"
    .asciz "sha256"
    .asciz "sha256_esa"
    .asciz "shm_apri"
    .asciz "shm_chiudi"
    .asciz "signal"
    .asciz "sleep"
    .asciz "snprintf"
    .asciz "spawn"
    .asciz "spawn_ex"
    .asciz "spawn_su_console"
    .asciz "sprintf"
    .asciz "sqrt"
    .asciz "srand"
    .asciz "sscanf"
    .asciz "stat"
    .asciz "statraw"
    .asciz "strcasecmp"
    .asciz "strcat"
    .asciz "strchr"
    .asciz "strcmp"
    .asciz "strcoll"
    .asciz "strcpy"
    .asciz "strcspn"
    .asciz "strdup"
    .asciz "strerror"
    .asciz "strftime"
    .asciz "strlen"
    .asciz "strncasecmp"
    .asciz "strncat"
    .asciz "strncmp"
    .asciz "strncpy"
    .asciz "strpbrk"
    .asciz "strrchr"
    .asciz "strsignal"
    .asciz "strspn"
    .asciz "strstr"
    .asciz "strtod"
    .asciz "strtof"
    .asciz "strtoimax"
    .asciz "strtok"
    .asciz "strtol"
    .asciz "strtold"
    .asciz "strtoll"
    .asciz "strtoul"
    .asciz "strtoull"
    .asciz "strtoumax"
    .asciz "strxfrm"
    .asciz "swprintf"
    .asciz "sysconf"
    .asciz "system"
    .asciz "time"
    .asciz "time_now"
    .asciz "times"
    .asciz "timespec_get"
    .asciz "tmpfile"
    .asciz "tmpnam"
    .asciz "toascii"
    .asciz "tolower"
    .asciz "toupper"
    .asciz "towlower"
    .asciz "towupper"
    .asciz "truncate"
    .asciz "tty_clear"
    .asciz "tty_getsize"
    .asciz "tty_raw"
    .asciz "umask"
    .asciz "umount"
    .asciz "ungetc"
    .asciz "unlink"
    .asciz "unsetenv"
    .asciz "uptime_ms"
    .asciz "usleep"
    .asciz "utime"
    .asciz "verboseboot"
    .asciz "vfprintf"
    .asciz "vfscanf"
    .asciz "video_info"
    .asciz "vprintf"
    .asciz "vscanf"
    .asciz "vsnprintf"
    .asciz "vsprintf"
    .asciz "vsscanf"
    .asciz "vswprintf"
    .asciz "wait"
    .asciz "waitpid"
    .asciz "wcscat"
    .asciz "wcschr"
    .asciz "wcscmp"
    .asciz "wcscpy"
    .asciz "wcscspn"
    .asciz "wcslen"
    .asciz "wcsncat"
    .asciz "wcsncmp"
    .asciz "wcsncpy"
    .asciz "wcspbrk"
    .asciz "wcsrchr"
    .asciz "wcsspn"
    .asciz "wcsstr"
    .asciz "wcstod"
    .asciz "wcstol"
    .asciz "wcstoll"
    .asciz "wcstombs"
    .asciz "wcstoul"
    .asciz "wcstoull"
    .asciz "wctomb"
    .asciz "wmemchr"
    .asciz "wmemcmp"
    .asciz "wmemcpy"
    .asciz "wmemmove"
    .asciz "wmemset"
    .asciz "write"

    .align 4
    .globl __libc_ponti_quanti
__libc_ponti_quanti:
    .long 324
    .section .text._Exit,"ax",@progbits
    .globl _Exit
    .type _Exit, @function
_Exit:
    jmp *__libc_ponti_tabella+0
    .size _Exit, .-_Exit

    .section .text.__environ_dove,"ax",@progbits
    .globl __environ_dove
    .type __environ_dove, @function
__environ_dove:
    jmp *__libc_ponti_tabella+4
    .size __environ_dove, .-__environ_dove

    .section .text.__errno_dove,"ax",@progbits
    .globl __errno_dove
    .type __errno_dove, @function
__errno_dove:
    jmp *__libc_ponti_tabella+8
    .size __errno_dove, .-__errno_dove

    .section .text.__libc_distruttori_registra,"ax",@progbits
    .globl __libc_distruttori_registra
    .type __libc_distruttori_registra, @function
__libc_distruttori_registra:
    jmp *__libc_ponti_tabella+12
    .size __libc_distruttori_registra, .-__libc_distruttori_registra

    .section .text.__stderr_dove,"ax",@progbits
    .globl __stderr_dove
    .type __stderr_dove, @function
__stderr_dove:
    jmp *__libc_ponti_tabella+16
    .size __stderr_dove, .-__stderr_dove

    .section .text.__stdin_dove,"ax",@progbits
    .globl __stdin_dove
    .type __stdin_dove, @function
__stdin_dove:
    jmp *__libc_ponti_tabella+20
    .size __stdin_dove, .-__stdin_dove

    .section .text.__stdout_dove,"ax",@progbits
    .globl __stdout_dove
    .type __stdout_dove, @function
__stdout_dove:
    jmp *__libc_ponti_tabella+24
    .size __stdout_dove, .-__stdout_dove

    .section .text._assert_fallita,"ax",@progbits
    .globl _assert_fallita
    .type _assert_fallita, @function
_assert_fallita:
    jmp *__libc_ponti_tabella+28
    .size _assert_fallita, .-_assert_fallita

    .section .text._exit,"ax",@progbits
    .globl _exit
    .type _exit, @function
_exit:
    jmp *__libc_ponti_tabella+32
    .size _exit, .-_exit

    .section .text.abort,"ax",@progbits
    .globl abort
    .type abort, @function
abort:
    jmp *__libc_ponti_tabella+36
    .size abort, .-abort

    .section .text.abs,"ax",@progbits
    .globl abs
    .type abs, @function
abs:
    jmp *__libc_ponti_tabella+40
    .size abs, .-abs

    .section .text.access,"ax",@progbits
    .globl access
    .type access, @function
access:
    jmp *__libc_ponti_tabella+44
    .size access, .-access

    .section .text.aligned_alloc,"ax",@progbits
    .globl aligned_alloc
    .type aligned_alloc, @function
aligned_alloc:
    jmp *__libc_ponti_tabella+48
    .size aligned_alloc, .-aligned_alloc

    .section .text.asctime,"ax",@progbits
    .globl asctime
    .type asctime, @function
asctime:
    jmp *__libc_ponti_tabella+52
    .size asctime, .-asctime

    .section .text.at_quick_exit,"ax",@progbits
    .globl at_quick_exit
    .type at_quick_exit, @function
at_quick_exit:
    jmp *__libc_ponti_tabella+56
    .size at_quick_exit, .-at_quick_exit

    .section .text.atexit,"ax",@progbits
    .globl atexit
    .type atexit, @function
atexit:
    jmp *__libc_ponti_tabella+60
    .size atexit, .-atexit

    .section .text.atof,"ax",@progbits
    .globl atof
    .type atof, @function
atof:
    jmp *__libc_ponti_tabella+64
    .size atof, .-atof

    .section .text.atoi,"ax",@progbits
    .globl atoi
    .type atoi, @function
atoi:
    jmp *__libc_ponti_tabella+68
    .size atoi, .-atoi

    .section .text.atol,"ax",@progbits
    .globl atol
    .type atol, @function
atol:
    jmp *__libc_ponti_tabella+72
    .size atol, .-atol

    .section .text.atoll,"ax",@progbits
    .globl atoll
    .type atoll, @function
atoll:
    jmp *__libc_ponti_tabella+76
    .size atoll, .-atoll

    .section .text.blkinfo,"ax",@progbits
    .globl blkinfo
    .type blkinfo, @function
blkinfo:
    jmp *__libc_ponti_tabella+80
    .size blkinfo, .-blkinfo

    .section .text.blkread,"ax",@progbits
    .globl blkread
    .type blkread, @function
blkread:
    jmp *__libc_ponti_tabella+84
    .size blkread, .-blkread

    .section .text.blkwrite,"ax",@progbits
    .globl blkwrite
    .type blkwrite, @function
blkwrite:
    jmp *__libc_ponti_tabella+88
    .size blkwrite, .-blkwrite

    .section .text.bootinstall,"ax",@progbits
    .globl bootinstall
    .type bootinstall, @function
bootinstall:
    jmp *__libc_ponti_tabella+92
    .size bootinstall, .-bootinstall

    .section .text.bootverify,"ax",@progbits
    .globl bootverify
    .type bootverify, @function
bootverify:
    jmp *__libc_ponti_tabella+96
    .size bootverify, .-bootverify

    .section .text.bsearch,"ax",@progbits
    .globl bsearch
    .type bsearch, @function
bsearch:
    jmp *__libc_ponti_tabella+100
    .size bsearch, .-bsearch

    .section .text.calloc,"ax",@progbits
    .globl calloc
    .type calloc, @function
calloc:
    jmp *__libc_ponti_tabella+104
    .size calloc, .-calloc

    .section .text.chdir,"ax",@progbits
    .globl chdir
    .type chdir, @function
chdir:
    jmp *__libc_ponti_tabella+108
    .size chdir, .-chdir

    .section .text.chmod,"ax",@progbits
    .globl chmod
    .type chmod, @function
chmod:
    jmp *__libc_ponti_tabella+112
    .size chmod, .-chmod

    .section .text.chown,"ax",@progbits
    .globl chown
    .type chown, @function
chown:
    jmp *__libc_ponti_tabella+116
    .size chown, .-chown

    .section .text.clearerr,"ax",@progbits
    .globl clearerr
    .type clearerr, @function
clearerr:
    jmp *__libc_ponti_tabella+120
    .size clearerr, .-clearerr

    .section .text.clock,"ax",@progbits
    .globl clock
    .type clock, @function
clock:
    jmp *__libc_ponti_tabella+124
    .size clock, .-clock

    .section .text.close,"ax",@progbits
    .globl close
    .type close, @function
close:
    jmp *__libc_ponti_tabella+128
    .size close, .-close

    .section .text.closedir,"ax",@progbits
    .globl closedir
    .type closedir, @function
closedir:
    jmp *__libc_ponti_tabella+132
    .size closedir, .-closedir

    .section .text.console_info,"ax",@progbits
    .globl console_info
    .type console_info, @function
console_info:
    jmp *__libc_ponti_tabella+136
    .size console_info, .-console_info

    .section .text.console_setfg,"ax",@progbits
    .globl console_setfg
    .type console_setfg, @function
console_setfg:
    jmp *__libc_ponti_tabella+140
    .size console_setfg, .-console_setfg

    .section .text.console_switch,"ax",@progbits
    .globl console_switch
    .type console_switch, @function
console_switch:
    jmp *__libc_ponti_tabella+144
    .size console_switch, .-console_switch

    .section .text.console_write,"ax",@progbits
    .globl console_write
    .type console_write, @function
console_write:
    jmp *__libc_ponti_tabella+148
    .size console_write, .-console_write

    .section .text.ctime,"ax",@progbits
    .globl ctime
    .type ctime, @function
ctime:
    jmp *__libc_ponti_tabella+152
    .size ctime, .-ctime

    .section .text.difftime,"ax",@progbits
    .globl difftime
    .type difftime, @function
difftime:
    jmp *__libc_ponti_tabella+156
    .size difftime, .-difftime

    .section .text.diskinfo,"ax",@progbits
    .globl diskinfo
    .type diskinfo, @function
diskinfo:
    jmp *__libc_ponti_tabella+160
    .size diskinfo, .-diskinfo

    .section .text.div,"ax",@progbits
    .globl div
    .type div, @function
div:
    jmp *__libc_ponti_tabella+164
    .size div, .-div

    .section .text.dma_alloc,"ax",@progbits
    .globl dma_alloc
    .type dma_alloc, @function
dma_alloc:
    jmp *__libc_ponti_tabella+168
    .size dma_alloc, .-dma_alloc

    .section .text.dup,"ax",@progbits
    .globl dup
    .type dup, @function
dup:
    jmp *__libc_ponti_tabella+172
    .size dup, .-dup

    .section .text.dup2,"ax",@progbits
    .globl dup2
    .type dup2, @function
dup2:
    jmp *__libc_ponti_tabella+176
    .size dup2, .-dup2

    .section .text.execv,"ax",@progbits
    .globl execv
    .type execv, @function
execv:
    jmp *__libc_ponti_tabella+180
    .size execv, .-execv

    .section .text.execvp,"ax",@progbits
    .globl execvp
    .type execvp, @function
execvp:
    jmp *__libc_ponti_tabella+184
    .size execvp, .-execvp

    .section .text.exit,"ax",@progbits
    .globl exit
    .type exit, @function
exit:
    jmp *__libc_ponti_tabella+188
    .size exit, .-exit

    .section .text.fabs,"ax",@progbits
    .globl fabs
    .type fabs, @function
fabs:
    jmp *__libc_ponti_tabella+192
    .size fabs, .-fabs

    .section .text.fchmod,"ax",@progbits
    .globl fchmod
    .type fchmod, @function
fchmod:
    jmp *__libc_ponti_tabella+196
    .size fchmod, .-fchmod

    .section .text.fclose,"ax",@progbits
    .globl fclose
    .type fclose, @function
fclose:
    jmp *__libc_ponti_tabella+200
    .size fclose, .-fclose

    .section .text.fcntl,"ax",@progbits
    .globl fcntl
    .type fcntl, @function
fcntl:
    jmp *__libc_ponti_tabella+204
    .size fcntl, .-fcntl

    .section .text.fdopen,"ax",@progbits
    .globl fdopen
    .type fdopen, @function
fdopen:
    jmp *__libc_ponti_tabella+208
    .size fdopen, .-fdopen

    .section .text.feof,"ax",@progbits
    .globl feof
    .type feof, @function
feof:
    jmp *__libc_ponti_tabella+212
    .size feof, .-feof

    .section .text.ferror,"ax",@progbits
    .globl ferror
    .type ferror, @function
ferror:
    jmp *__libc_ponti_tabella+216
    .size ferror, .-ferror

    .section .text.fflush,"ax",@progbits
    .globl fflush
    .type fflush, @function
fflush:
    jmp *__libc_ponti_tabella+220
    .size fflush, .-fflush

    .section .text.fgetc,"ax",@progbits
    .globl fgetc
    .type fgetc, @function
fgetc:
    jmp *__libc_ponti_tabella+224
    .size fgetc, .-fgetc

    .section .text.fgetpos,"ax",@progbits
    .globl fgetpos
    .type fgetpos, @function
fgetpos:
    jmp *__libc_ponti_tabella+228
    .size fgetpos, .-fgetpos

    .section .text.fgets,"ax",@progbits
    .globl fgets
    .type fgets, @function
fgets:
    jmp *__libc_ponti_tabella+232
    .size fgets, .-fgets

    .section .text.fileno,"ax",@progbits
    .globl fileno
    .type fileno, @function
fileno:
    jmp *__libc_ponti_tabella+236
    .size fileno, .-fileno

    .section .text.fopen,"ax",@progbits
    .globl fopen
    .type fopen, @function
fopen:
    jmp *__libc_ponti_tabella+240
    .size fopen, .-fopen

    .section .text.fpathconf,"ax",@progbits
    .globl fpathconf
    .type fpathconf, @function
fpathconf:
    jmp *__libc_ponti_tabella+244
    .size fpathconf, .-fpathconf

    .section .text.fprintf,"ax",@progbits
    .globl fprintf
    .type fprintf, @function
fprintf:
    jmp *__libc_ponti_tabella+248
    .size fprintf, .-fprintf

    .section .text.fputc,"ax",@progbits
    .globl fputc
    .type fputc, @function
fputc:
    jmp *__libc_ponti_tabella+252
    .size fputc, .-fputc

    .section .text.fputs,"ax",@progbits
    .globl fputs
    .type fputs, @function
fputs:
    jmp *__libc_ponti_tabella+256
    .size fputs, .-fputs

    .section .text.fread,"ax",@progbits
    .globl fread
    .type fread, @function
fread:
    jmp *__libc_ponti_tabella+260
    .size fread, .-fread

    .section .text.free,"ax",@progbits
    .globl free
    .type free, @function
free:
    jmp *__libc_ponti_tabella+264
    .size free, .-free

    .section .text.freopen,"ax",@progbits
    .globl freopen
    .type freopen, @function
freopen:
    jmp *__libc_ponti_tabella+268
    .size freopen, .-freopen

    .section .text.frexp,"ax",@progbits
    .globl frexp
    .type frexp, @function
frexp:
    jmp *__libc_ponti_tabella+272
    .size frexp, .-frexp

    .section .text.fscanf,"ax",@progbits
    .globl fscanf
    .type fscanf, @function
fscanf:
    jmp *__libc_ponti_tabella+276
    .size fscanf, .-fscanf

    .section .text.fseek,"ax",@progbits
    .globl fseek
    .type fseek, @function
fseek:
    jmp *__libc_ponti_tabella+280
    .size fseek, .-fseek

    .section .text.fsetpos,"ax",@progbits
    .globl fsetpos
    .type fsetpos, @function
fsetpos:
    jmp *__libc_ponti_tabella+284
    .size fsetpos, .-fsetpos

    .section .text.fsize,"ax",@progbits
    .globl fsize
    .type fsize, @function
fsize:
    jmp *__libc_ponti_tabella+288
    .size fsize, .-fsize

    .section .text.fstat,"ax",@progbits
    .globl fstat
    .type fstat, @function
fstat:
    jmp *__libc_ponti_tabella+292
    .size fstat, .-fstat

    .section .text.ftell,"ax",@progbits
    .globl ftell
    .type ftell, @function
ftell:
    jmp *__libc_ponti_tabella+296
    .size ftell, .-ftell

    .section .text.fwrite,"ax",@progbits
    .globl fwrite
    .type fwrite, @function
fwrite:
    jmp *__libc_ponti_tabella+300
    .size fwrite, .-fwrite

    .section .text.getc,"ax",@progbits
    .globl getc
    .type getc, @function
getc:
    jmp *__libc_ponti_tabella+304
    .size getc, .-getc

    .section .text.getchar,"ax",@progbits
    .globl getchar
    .type getchar, @function
getchar:
    jmp *__libc_ponti_tabella+308
    .size getchar, .-getchar

    .section .text.getconf,"ax",@progbits
    .globl getconf
    .type getconf, @function
getconf:
    jmp *__libc_ponti_tabella+312
    .size getconf, .-getconf

    .section .text.getcwd,"ax",@progbits
    .globl getcwd
    .type getcwd, @function
getcwd:
    jmp *__libc_ponti_tabella+316
    .size getcwd, .-getcwd

    .section .text.getegid,"ax",@progbits
    .globl getegid
    .type getegid, @function
getegid:
    jmp *__libc_ponti_tabella+320
    .size getegid, .-getegid

    .section .text.getentropy,"ax",@progbits
    .globl getentropy
    .type getentropy, @function
getentropy:
    jmp *__libc_ponti_tabella+324
    .size getentropy, .-getentropy

    .section .text.getenv,"ax",@progbits
    .globl getenv
    .type getenv, @function
getenv:
    jmp *__libc_ponti_tabella+328
    .size getenv, .-getenv

    .section .text.geteuid,"ax",@progbits
    .globl geteuid
    .type geteuid, @function
geteuid:
    jmp *__libc_ponti_tabella+332
    .size geteuid, .-geteuid

    .section .text.getgid,"ax",@progbits
    .globl getgid
    .type getgid, @function
getgid:
    jmp *__libc_ponti_tabella+336
    .size getgid, .-getgid

    .section .text.getpagesize,"ax",@progbits
    .globl getpagesize
    .type getpagesize, @function
getpagesize:
    jmp *__libc_ponti_tabella+340
    .size getpagesize, .-getpagesize

    .section .text.getpid,"ax",@progbits
    .globl getpid
    .type getpid, @function
getpid:
    jmp *__libc_ponti_tabella+344
    .size getpid, .-getpid

    .section .text.getrandom,"ax",@progbits
    .globl getrandom
    .type getrandom, @function
getrandom:
    jmp *__libc_ponti_tabella+348
    .size getrandom, .-getrandom

    .section .text.getrusage,"ax",@progbits
    .globl getrusage
    .type getrusage, @function
getrusage:
    jmp *__libc_ponti_tabella+352
    .size getrusage, .-getrusage

    .section .text.gets,"ax",@progbits
    .globl gets
    .type gets, @function
gets:
    jmp *__libc_ponti_tabella+356
    .size gets, .-gets

    .section .text.gettimeofday,"ax",@progbits
    .globl gettimeofday
    .type gettimeofday, @function
gettimeofday:
    jmp *__libc_ponti_tabella+360
    .size gettimeofday, .-gettimeofday

    .section .text.getuid,"ax",@progbits
    .globl getuid
    .type getuid, @function
getuid:
    jmp *__libc_ponti_tabella+364
    .size getuid, .-getuid

    .section .text.gmtime,"ax",@progbits
    .globl gmtime
    .type gmtime, @function
gmtime:
    jmp *__libc_ponti_tabella+368
    .size gmtime, .-gmtime

    .section .text.imaxabs,"ax",@progbits
    .globl imaxabs
    .type imaxabs, @function
imaxabs:
    jmp *__libc_ponti_tabella+372
    .size imaxabs, .-imaxabs

    .section .text.imaxdiv,"ax",@progbits
    .globl imaxdiv
    .type imaxdiv, @function
imaxdiv:
    jmp *__libc_ponti_tabella+376
    .size imaxdiv, .-imaxdiv

    .section .text.ioctl,"ax",@progbits
    .globl ioctl
    .type ioctl, @function
ioctl:
    jmp *__libc_ponti_tabella+380
    .size ioctl, .-ioctl

    .section .text.ioport_bind,"ax",@progbits
    .globl ioport_bind
    .type ioport_bind, @function
ioport_bind:
    jmp *__libc_ponti_tabella+384
    .size ioport_bind, .-ioport_bind

    .section .text.ioport_in,"ax",@progbits
    .globl ioport_in
    .type ioport_in, @function
ioport_in:
    jmp *__libc_ponti_tabella+388
    .size ioport_in, .-ioport_in

    .section .text.ioport_in16,"ax",@progbits
    .globl ioport_in16
    .type ioport_in16, @function
ioport_in16:
    jmp *__libc_ponti_tabella+392
    .size ioport_in16, .-ioport_in16

    .section .text.ioport_in32,"ax",@progbits
    .globl ioport_in32
    .type ioport_in32, @function
ioport_in32:
    jmp *__libc_ponti_tabella+396
    .size ioport_in32, .-ioport_in32

    .section .text.ioport_out,"ax",@progbits
    .globl ioport_out
    .type ioport_out, @function
ioport_out:
    jmp *__libc_ponti_tabella+400
    .size ioport_out, .-ioport_out

    .section .text.ioport_out16,"ax",@progbits
    .globl ioport_out16
    .type ioport_out16, @function
ioport_out16:
    jmp *__libc_ponti_tabella+404
    .size ioport_out16, .-ioport_out16

    .section .text.ioport_out32,"ax",@progbits
    .globl ioport_out32
    .type ioport_out32, @function
ioport_out32:
    jmp *__libc_ponti_tabella+408
    .size ioport_out32, .-ioport_out32

    .section .text.ipc_lookup,"ax",@progbits
    .globl ipc_lookup
    .type ipc_lookup, @function
ipc_lookup:
    jmp *__libc_ponti_tabella+412
    .size ipc_lookup, .-ipc_lookup

    .section .text.ipc_recv,"ax",@progbits
    .globl ipc_recv
    .type ipc_recv, @function
ipc_recv:
    jmp *__libc_ponti_tabella+416
    .size ipc_recv, .-ipc_recv

    .section .text.ipc_recv_timeout,"ax",@progbits
    .globl ipc_recv_timeout
    .type ipc_recv_timeout, @function
ipc_recv_timeout:
    jmp *__libc_ponti_tabella+420
    .size ipc_recv_timeout, .-ipc_recv_timeout

    .section .text.ipc_register,"ax",@progbits
    .globl ipc_register
    .type ipc_register, @function
ipc_register:
    jmp *__libc_ponti_tabella+424
    .size ipc_register, .-ipc_register

    .section .text.ipc_send,"ax",@progbits
    .globl ipc_send
    .type ipc_send, @function
ipc_send:
    jmp *__libc_ponti_tabella+428
    .size ipc_send, .-ipc_send

    .section .text.irq_bind,"ax",@progbits
    .globl irq_bind
    .type irq_bind, @function
irq_bind:
    jmp *__libc_ponti_tabella+432
    .size irq_bind, .-irq_bind

    .section .text.irq_done,"ax",@progbits
    .globl irq_done
    .type irq_done, @function
irq_done:
    jmp *__libc_ponti_tabella+436
    .size irq_done, .-irq_done

    .section .text.isalnum,"ax",@progbits
    .globl isalnum
    .type isalnum, @function
isalnum:
    jmp *__libc_ponti_tabella+440
    .size isalnum, .-isalnum

    .section .text.isalpha,"ax",@progbits
    .globl isalpha
    .type isalpha, @function
isalpha:
    jmp *__libc_ponti_tabella+444
    .size isalpha, .-isalpha

    .section .text.isascii,"ax",@progbits
    .globl isascii
    .type isascii, @function
isascii:
    jmp *__libc_ponti_tabella+448
    .size isascii, .-isascii

    .section .text.isatty,"ax",@progbits
    .globl isatty
    .type isatty, @function
isatty:
    jmp *__libc_ponti_tabella+452
    .size isatty, .-isatty

    .section .text.isblank,"ax",@progbits
    .globl isblank
    .type isblank, @function
isblank:
    jmp *__libc_ponti_tabella+456
    .size isblank, .-isblank

    .section .text.iscntrl,"ax",@progbits
    .globl iscntrl
    .type iscntrl, @function
iscntrl:
    jmp *__libc_ponti_tabella+460
    .size iscntrl, .-iscntrl

    .section .text.isdigit,"ax",@progbits
    .globl isdigit
    .type isdigit, @function
isdigit:
    jmp *__libc_ponti_tabella+464
    .size isdigit, .-isdigit

    .section .text.isgraph,"ax",@progbits
    .globl isgraph
    .type isgraph, @function
isgraph:
    jmp *__libc_ponti_tabella+468
    .size isgraph, .-isgraph

    .section .text.islower,"ax",@progbits
    .globl islower
    .type islower, @function
islower:
    jmp *__libc_ponti_tabella+472
    .size islower, .-islower

    .section .text.isprint,"ax",@progbits
    .globl isprint
    .type isprint, @function
isprint:
    jmp *__libc_ponti_tabella+476
    .size isprint, .-isprint

    .section .text.ispunct,"ax",@progbits
    .globl ispunct
    .type ispunct, @function
ispunct:
    jmp *__libc_ponti_tabella+480
    .size ispunct, .-ispunct

    .section .text.isspace,"ax",@progbits
    .globl isspace
    .type isspace, @function
isspace:
    jmp *__libc_ponti_tabella+484
    .size isspace, .-isspace

    .section .text.isupper,"ax",@progbits
    .globl isupper
    .type isupper, @function
isupper:
    jmp *__libc_ponti_tabella+488
    .size isupper, .-isupper

    .section .text.iswalnum,"ax",@progbits
    .globl iswalnum
    .type iswalnum, @function
iswalnum:
    jmp *__libc_ponti_tabella+492
    .size iswalnum, .-iswalnum

    .section .text.iswalpha,"ax",@progbits
    .globl iswalpha
    .type iswalpha, @function
iswalpha:
    jmp *__libc_ponti_tabella+496
    .size iswalpha, .-iswalpha

    .section .text.iswblank,"ax",@progbits
    .globl iswblank
    .type iswblank, @function
iswblank:
    jmp *__libc_ponti_tabella+500
    .size iswblank, .-iswblank

    .section .text.iswcntrl,"ax",@progbits
    .globl iswcntrl
    .type iswcntrl, @function
iswcntrl:
    jmp *__libc_ponti_tabella+504
    .size iswcntrl, .-iswcntrl

    .section .text.iswdigit,"ax",@progbits
    .globl iswdigit
    .type iswdigit, @function
iswdigit:
    jmp *__libc_ponti_tabella+508
    .size iswdigit, .-iswdigit

    .section .text.iswgraph,"ax",@progbits
    .globl iswgraph
    .type iswgraph, @function
iswgraph:
    jmp *__libc_ponti_tabella+512
    .size iswgraph, .-iswgraph

    .section .text.iswlower,"ax",@progbits
    .globl iswlower
    .type iswlower, @function
iswlower:
    jmp *__libc_ponti_tabella+516
    .size iswlower, .-iswlower

    .section .text.iswprint,"ax",@progbits
    .globl iswprint
    .type iswprint, @function
iswprint:
    jmp *__libc_ponti_tabella+520
    .size iswprint, .-iswprint

    .section .text.iswpunct,"ax",@progbits
    .globl iswpunct
    .type iswpunct, @function
iswpunct:
    jmp *__libc_ponti_tabella+524
    .size iswpunct, .-iswpunct

    .section .text.iswspace,"ax",@progbits
    .globl iswspace
    .type iswspace, @function
iswspace:
    jmp *__libc_ponti_tabella+528
    .size iswspace, .-iswspace

    .section .text.iswupper,"ax",@progbits
    .globl iswupper
    .type iswupper, @function
iswupper:
    jmp *__libc_ponti_tabella+532
    .size iswupper, .-iswupper

    .section .text.iswxdigit,"ax",@progbits
    .globl iswxdigit
    .type iswxdigit, @function
iswxdigit:
    jmp *__libc_ponti_tabella+536
    .size iswxdigit, .-iswxdigit

    .section .text.isxdigit,"ax",@progbits
    .globl isxdigit
    .type isxdigit, @function
isxdigit:
    jmp *__libc_ponti_tabella+540
    .size isxdigit, .-isxdigit

    .section .text.labs,"ax",@progbits
    .globl labs
    .type labs, @function
labs:
    jmp *__libc_ponti_tabella+544
    .size labs, .-labs

    .section .text.ldexp,"ax",@progbits
    .globl ldexp
    .type ldexp, @function
ldexp:
    jmp *__libc_ponti_tabella+548
    .size ldexp, .-ldexp

    .section .text.ldiv,"ax",@progbits
    .globl ldiv
    .type ldiv, @function
ldiv:
    jmp *__libc_ponti_tabella+552
    .size ldiv, .-ldiv

    .section .text.listdir,"ax",@progbits
    .globl listdir
    .type listdir, @function
listdir:
    jmp *__libc_ponti_tabella+556
    .size listdir, .-listdir

    .section .text.listdir_from,"ax",@progbits
    .globl listdir_from
    .type listdir_from, @function
listdir_from:
    jmp *__libc_ponti_tabella+560
    .size listdir_from, .-listdir_from

    .section .text.llabs,"ax",@progbits
    .globl llabs
    .type llabs, @function
llabs:
    jmp *__libc_ponti_tabella+564
    .size llabs, .-llabs

    .section .text.lldiv,"ax",@progbits
    .globl lldiv
    .type lldiv, @function
lldiv:
    jmp *__libc_ponti_tabella+568
    .size lldiv, .-lldiv

    .section .text.localeconv,"ax",@progbits
    .globl localeconv
    .type localeconv, @function
localeconv:
    jmp *__libc_ponti_tabella+572
    .size localeconv, .-localeconv

    .section .text.localtime,"ax",@progbits
    .globl localtime
    .type localtime, @function
localtime:
    jmp *__libc_ponti_tabella+576
    .size localtime, .-localtime

    .section .text.log_seriale,"ax",@progbits
    .globl log_seriale
    .type log_seriale, @function
log_seriale:
    jmp *__libc_ponti_tabella+580
    .size log_seriale, .-log_seriale

    .section .text.longjmp,"ax",@progbits
    .globl longjmp
    .type longjmp, @function
longjmp:
    jmp *__libc_ponti_tabella+584
    .size longjmp, .-longjmp

    .section .text.lseek,"ax",@progbits
    .globl lseek
    .type lseek, @function
lseek:
    jmp *__libc_ponti_tabella+588
    .size lseek, .-lseek

    .section .text.lstat,"ax",@progbits
    .globl lstat
    .type lstat, @function
lstat:
    jmp *__libc_ponti_tabella+592
    .size lstat, .-lstat

    .section .text.malloc,"ax",@progbits
    .globl malloc
    .type malloc, @function
malloc:
    jmp *__libc_ponti_tabella+596
    .size malloc, .-malloc

    .section .text.mblen,"ax",@progbits
    .globl mblen
    .type mblen, @function
mblen:
    jmp *__libc_ponti_tabella+600
    .size mblen, .-mblen

    .section .text.mbrtowc,"ax",@progbits
    .globl mbrtowc
    .type mbrtowc, @function
mbrtowc:
    jmp *__libc_ponti_tabella+604
    .size mbrtowc, .-mbrtowc

    .section .text.mbstowcs,"ax",@progbits
    .globl mbstowcs
    .type mbstowcs, @function
mbstowcs:
    jmp *__libc_ponti_tabella+608
    .size mbstowcs, .-mbstowcs

    .section .text.mbtowc,"ax",@progbits
    .globl mbtowc
    .type mbtowc, @function
mbtowc:
    jmp *__libc_ponti_tabella+612
    .size mbtowc, .-mbtowc

    .section .text.memalign,"ax",@progbits
    .globl memalign
    .type memalign, @function
memalign:
    jmp *__libc_ponti_tabella+616
    .size memalign, .-memalign

    .section .text.memchr,"ax",@progbits
    .globl memchr
    .type memchr, @function
memchr:
    jmp *__libc_ponti_tabella+620
    .size memchr, .-memchr

    .section .text.memcmp,"ax",@progbits
    .globl memcmp
    .type memcmp, @function
memcmp:
    jmp *__libc_ponti_tabella+624
    .size memcmp, .-memcmp

    .section .text.memcpy,"ax",@progbits
    .globl memcpy
    .type memcpy, @function
memcpy:
    jmp *__libc_ponti_tabella+628
    .size memcpy, .-memcpy

    .section .text.meminfo,"ax",@progbits
    .globl meminfo
    .type meminfo, @function
meminfo:
    jmp *__libc_ponti_tabella+632
    .size meminfo, .-meminfo

    .section .text.memmove,"ax",@progbits
    .globl memmove
    .type memmove, @function
memmove:
    jmp *__libc_ponti_tabella+636
    .size memmove, .-memmove

    .section .text.memset,"ax",@progbits
    .globl memset
    .type memset, @function
memset:
    jmp *__libc_ponti_tabella+640
    .size memset, .-memset

    .section .text.mkdir,"ax",@progbits
    .globl mkdir
    .type mkdir, @function
mkdir:
    jmp *__libc_ponti_tabella+644
    .size mkdir, .-mkdir

    .section .text.mkstemp,"ax",@progbits
    .globl mkstemp
    .type mkstemp, @function
mkstemp:
    jmp *__libc_ponti_tabella+648
    .size mkstemp, .-mkstemp

    .section .text.mktemp,"ax",@progbits
    .globl mktemp
    .type mktemp, @function
mktemp:
    jmp *__libc_ponti_tabella+652
    .size mktemp, .-mktemp

    .section .text.mktime,"ax",@progbits
    .globl mktime
    .type mktime, @function
mktime:
    jmp *__libc_ponti_tabella+656
    .size mktime, .-mktime

    .section .text.mmap,"ax",@progbits
    .globl mmap
    .type mmap, @function
mmap:
    jmp *__libc_ponti_tabella+660
    .size mmap, .-mmap

    .section .text.mmio_map,"ax",@progbits
    .globl mmio_map
    .type mmio_map, @function
mmio_map:
    jmp *__libc_ponti_tabella+664
    .size mmio_map, .-mmio_map

    .section .text.modo_testo,"ax",@progbits
    .globl modo_testo
    .type modo_testo, @function
modo_testo:
    jmp *__libc_ponti_tabella+668
    .size modo_testo, .-modo_testo

    .section .text.mount,"ax",@progbits
    .globl mount
    .type mount, @function
mount:
    jmp *__libc_ponti_tabella+672
    .size mount, .-mount

    .section .text.mountinfo,"ax",@progbits
    .globl mountinfo
    .type mountinfo, @function
mountinfo:
    jmp *__libc_ponti_tabella+676
    .size mountinfo, .-mountinfo

    .section .text.munmap,"ax",@progbits
    .globl munmap
    .type munmap, @function
munmap:
    jmp *__libc_ponti_tabella+680
    .size munmap, .-munmap

    .section .text.nanosleep,"ax",@progbits
    .globl nanosleep
    .type nanosleep, @function
nanosleep:
    jmp *__libc_ponti_tabella+684
    .size nanosleep, .-nanosleep

    .section .text.open,"ax",@progbits
    .globl open
    .type open, @function
open:
    jmp *__libc_ponti_tabella+688
    .size open, .-open

    .section .text.opendir,"ax",@progbits
    .globl opendir
    .type opendir, @function
opendir:
    jmp *__libc_ponti_tabella+692
    .size opendir, .-opendir

    .section .text.osversion,"ax",@progbits
    .globl osversion
    .type osversion, @function
osversion:
    jmp *__libc_ponti_tabella+696
    .size osversion, .-osversion

    .section .text.partwrite,"ax",@progbits
    .globl partwrite
    .type partwrite, @function
partwrite:
    jmp *__libc_ponti_tabella+700
    .size partwrite, .-partwrite

    .section .text.pathconf,"ax",@progbits
    .globl pathconf
    .type pathconf, @function
pathconf:
    jmp *__libc_ponti_tabella+704
    .size pathconf, .-pathconf

    .section .text.pclose,"ax",@progbits
    .globl pclose
    .type pclose, @function
pclose:
    jmp *__libc_ponti_tabella+708
    .size pclose, .-pclose

    .section .text.perror,"ax",@progbits
    .globl perror
    .type perror, @function
perror:
    jmp *__libc_ponti_tabella+712
    .size perror, .-perror

    .section .text.pipe,"ax",@progbits
    .globl pipe
    .type pipe, @function
pipe:
    jmp *__libc_ponti_tabella+716
    .size pipe, .-pipe

    .section .text.poll,"ax",@progbits
    .globl poll
    .type poll, @function
poll:
    jmp *__libc_ponti_tabella+720
    .size poll, .-poll

    .section .text.popen,"ax",@progbits
    .globl popen
    .type popen, @function
popen:
    jmp *__libc_ponti_tabella+724
    .size popen, .-popen

    .section .text.posix_memalign,"ax",@progbits
    .globl posix_memalign
    .type posix_memalign, @function
posix_memalign:
    jmp *__libc_ponti_tabella+728
    .size posix_memalign, .-posix_memalign

    .section .text.printf,"ax",@progbits
    .globl printf
    .type printf, @function
printf:
    jmp *__libc_ponti_tabella+732
    .size printf, .-printf

    .section .text.procinfo,"ax",@progbits
    .globl procinfo
    .type procinfo, @function
procinfo:
    jmp *__libc_ponti_tabella+736
    .size procinfo, .-procinfo

    .section .text.putc,"ax",@progbits
    .globl putc
    .type putc, @function
putc:
    jmp *__libc_ponti_tabella+740
    .size putc, .-putc

    .section .text.putchar,"ax",@progbits
    .globl putchar
    .type putchar, @function
putchar:
    jmp *__libc_ponti_tabella+744
    .size putchar, .-putchar

    .section .text.putenv,"ax",@progbits
    .globl putenv
    .type putenv, @function
putenv:
    jmp *__libc_ponti_tabella+748
    .size putenv, .-putenv

    .section .text.puts,"ax",@progbits
    .globl puts
    .type puts, @function
puts:
    jmp *__libc_ponti_tabella+752
    .size puts, .-puts

    .section .text.qsort,"ax",@progbits
    .globl qsort
    .type qsort, @function
qsort:
    jmp *__libc_ponti_tabella+756
    .size qsort, .-qsort

    .section .text.quick_exit,"ax",@progbits
    .globl quick_exit
    .type quick_exit, @function
quick_exit:
    jmp *__libc_ponti_tabella+760
    .size quick_exit, .-quick_exit

    .section .text.raise,"ax",@progbits
    .globl raise
    .type raise, @function
raise:
    jmp *__libc_ponti_tabella+764
    .size raise, .-raise

    .section .text.rand,"ax",@progbits
    .globl rand
    .type rand, @function
rand:
    jmp *__libc_ponti_tabella+768
    .size rand, .-rand

    .section .text.read,"ax",@progbits
    .globl read
    .type read, @function
read:
    jmp *__libc_ponti_tabella+772
    .size read, .-read

    .section .text.readdir,"ax",@progbits
    .globl readdir
    .type readdir, @function
readdir:
    jmp *__libc_ponti_tabella+776
    .size readdir, .-readdir

    .section .text.realloc,"ax",@progbits
    .globl realloc
    .type realloc, @function
realloc:
    jmp *__libc_ponti_tabella+780
    .size realloc, .-realloc

    .section .text.realpath,"ax",@progbits
    .globl realpath
    .type realpath, @function
realpath:
    jmp *__libc_ponti_tabella+784
    .size realpath, .-realpath

    .section .text.reboot,"ax",@progbits
    .globl reboot
    .type reboot, @function
reboot:
    jmp *__libc_ponti_tabella+788
    .size reboot, .-reboot

    .section .text.remove,"ax",@progbits
    .globl remove
    .type remove, @function
remove:
    jmp *__libc_ponti_tabella+792
    .size remove, .-remove

    .section .text.rename,"ax",@progbits
    .globl rename
    .type rename, @function
rename:
    jmp *__libc_ponti_tabella+796
    .size rename, .-rename

    .section .text.rewind,"ax",@progbits
    .globl rewind
    .type rewind, @function
rewind:
    jmp *__libc_ponti_tabella+800
    .size rewind, .-rewind

    .section .text.rewinddir,"ax",@progbits
    .globl rewinddir
    .type rewinddir, @function
rewinddir:
    jmp *__libc_ponti_tabella+804
    .size rewinddir, .-rewinddir

    .section .text.rmdir,"ax",@progbits
    .globl rmdir
    .type rmdir, @function
rmdir:
    jmp *__libc_ponti_tabella+808
    .size rmdir, .-rmdir

    .section .text.sbrk,"ax",@progbits
    .globl sbrk
    .type sbrk, @function
sbrk:
    jmp *__libc_ponti_tabella+812
    .size sbrk, .-sbrk

    .section .text.scanf,"ax",@progbits
    .globl scanf
    .type scanf, @function
scanf:
    jmp *__libc_ponti_tabella+816
    .size scanf, .-scanf

    .section .text.sched_yield,"ax",@progbits
    .globl sched_yield
    .type sched_yield, @function
sched_yield:
    jmp *__libc_ponti_tabella+820
    .size sched_yield, .-sched_yield

    .section .text.select,"ax",@progbits
    .globl select
    .type select, @function
select:
    jmp *__libc_ponti_tabella+824
    .size select, .-select

    .section .text.setbuf,"ax",@progbits
    .globl setbuf
    .type setbuf, @function
setbuf:
    jmp *__libc_ponti_tabella+828
    .size setbuf, .-setbuf

    .section .text.setenv,"ax",@progbits
    .globl setenv
    .type setenv, @function
setenv:
    jmp *__libc_ponti_tabella+832
    .size setenv, .-setenv

    .section .text.setjmp,"ax",@progbits
    .globl setjmp
    .type setjmp, @function
setjmp:
    jmp *__libc_ponti_tabella+836
    .size setjmp, .-setjmp

    .section .text.setlocale,"ax",@progbits
    .globl setlocale
    .type setlocale, @function
setlocale:
    jmp *__libc_ponti_tabella+840
    .size setlocale, .-setlocale

    .section .text.setuid,"ax",@progbits
    .globl setuid
    .type setuid, @function
setuid:
    jmp *__libc_ponti_tabella+844
    .size setuid, .-setuid

    .section .text.setvbuf,"ax",@progbits
    .globl setvbuf
    .type setvbuf, @function
setvbuf:
    jmp *__libc_ponti_tabella+848
    .size setvbuf, .-setvbuf

    .section .text.sha256,"ax",@progbits
    .globl sha256
    .type sha256, @function
sha256:
    jmp *__libc_ponti_tabella+852
    .size sha256, .-sha256

    .section .text.sha256_esa,"ax",@progbits
    .globl sha256_esa
    .type sha256_esa, @function
sha256_esa:
    jmp *__libc_ponti_tabella+856
    .size sha256_esa, .-sha256_esa

    .section .text.shm_apri,"ax",@progbits
    .globl shm_apri
    .type shm_apri, @function
shm_apri:
    jmp *__libc_ponti_tabella+860
    .size shm_apri, .-shm_apri

    .section .text.shm_chiudi,"ax",@progbits
    .globl shm_chiudi
    .type shm_chiudi, @function
shm_chiudi:
    jmp *__libc_ponti_tabella+864
    .size shm_chiudi, .-shm_chiudi

    .section .text.signal,"ax",@progbits
    .globl signal
    .type signal, @function
signal:
    jmp *__libc_ponti_tabella+868
    .size signal, .-signal

    .section .text.sleep,"ax",@progbits
    .globl sleep
    .type sleep, @function
sleep:
    jmp *__libc_ponti_tabella+872
    .size sleep, .-sleep

    .section .text.snprintf,"ax",@progbits
    .globl snprintf
    .type snprintf, @function
snprintf:
    jmp *__libc_ponti_tabella+876
    .size snprintf, .-snprintf

    .section .text.spawn,"ax",@progbits
    .globl spawn
    .type spawn, @function
spawn:
    jmp *__libc_ponti_tabella+880
    .size spawn, .-spawn

    .section .text.spawn_ex,"ax",@progbits
    .globl spawn_ex
    .type spawn_ex, @function
spawn_ex:
    jmp *__libc_ponti_tabella+884
    .size spawn_ex, .-spawn_ex

    .section .text.spawn_su_console,"ax",@progbits
    .globl spawn_su_console
    .type spawn_su_console, @function
spawn_su_console:
    jmp *__libc_ponti_tabella+888
    .size spawn_su_console, .-spawn_su_console

    .section .text.sprintf,"ax",@progbits
    .globl sprintf
    .type sprintf, @function
sprintf:
    jmp *__libc_ponti_tabella+892
    .size sprintf, .-sprintf

    .section .text.sqrt,"ax",@progbits
    .globl sqrt
    .type sqrt, @function
sqrt:
    jmp *__libc_ponti_tabella+896
    .size sqrt, .-sqrt

    .section .text.srand,"ax",@progbits
    .globl srand
    .type srand, @function
srand:
    jmp *__libc_ponti_tabella+900
    .size srand, .-srand

    .section .text.sscanf,"ax",@progbits
    .globl sscanf
    .type sscanf, @function
sscanf:
    jmp *__libc_ponti_tabella+904
    .size sscanf, .-sscanf

    .section .text.stat,"ax",@progbits
    .globl stat
    .type stat, @function
stat:
    jmp *__libc_ponti_tabella+908
    .size stat, .-stat

    .section .text.statraw,"ax",@progbits
    .globl statraw
    .type statraw, @function
statraw:
    jmp *__libc_ponti_tabella+912
    .size statraw, .-statraw

    .section .text.strcasecmp,"ax",@progbits
    .globl strcasecmp
    .type strcasecmp, @function
strcasecmp:
    jmp *__libc_ponti_tabella+916
    .size strcasecmp, .-strcasecmp

    .section .text.strcat,"ax",@progbits
    .globl strcat
    .type strcat, @function
strcat:
    jmp *__libc_ponti_tabella+920
    .size strcat, .-strcat

    .section .text.strchr,"ax",@progbits
    .globl strchr
    .type strchr, @function
strchr:
    jmp *__libc_ponti_tabella+924
    .size strchr, .-strchr

    .section .text.strcmp,"ax",@progbits
    .globl strcmp
    .type strcmp, @function
strcmp:
    jmp *__libc_ponti_tabella+928
    .size strcmp, .-strcmp

    .section .text.strcoll,"ax",@progbits
    .globl strcoll
    .type strcoll, @function
strcoll:
    jmp *__libc_ponti_tabella+932
    .size strcoll, .-strcoll

    .section .text.strcpy,"ax",@progbits
    .globl strcpy
    .type strcpy, @function
strcpy:
    jmp *__libc_ponti_tabella+936
    .size strcpy, .-strcpy

    .section .text.strcspn,"ax",@progbits
    .globl strcspn
    .type strcspn, @function
strcspn:
    jmp *__libc_ponti_tabella+940
    .size strcspn, .-strcspn

    .section .text.strdup,"ax",@progbits
    .globl strdup
    .type strdup, @function
strdup:
    jmp *__libc_ponti_tabella+944
    .size strdup, .-strdup

    .section .text.strerror,"ax",@progbits
    .globl strerror
    .type strerror, @function
strerror:
    jmp *__libc_ponti_tabella+948
    .size strerror, .-strerror

    .section .text.strftime,"ax",@progbits
    .globl strftime
    .type strftime, @function
strftime:
    jmp *__libc_ponti_tabella+952
    .size strftime, .-strftime

    .section .text.strlen,"ax",@progbits
    .globl strlen
    .type strlen, @function
strlen:
    jmp *__libc_ponti_tabella+956
    .size strlen, .-strlen

    .section .text.strncasecmp,"ax",@progbits
    .globl strncasecmp
    .type strncasecmp, @function
strncasecmp:
    jmp *__libc_ponti_tabella+960
    .size strncasecmp, .-strncasecmp

    .section .text.strncat,"ax",@progbits
    .globl strncat
    .type strncat, @function
strncat:
    jmp *__libc_ponti_tabella+964
    .size strncat, .-strncat

    .section .text.strncmp,"ax",@progbits
    .globl strncmp
    .type strncmp, @function
strncmp:
    jmp *__libc_ponti_tabella+968
    .size strncmp, .-strncmp

    .section .text.strncpy,"ax",@progbits
    .globl strncpy
    .type strncpy, @function
strncpy:
    jmp *__libc_ponti_tabella+972
    .size strncpy, .-strncpy

    .section .text.strpbrk,"ax",@progbits
    .globl strpbrk
    .type strpbrk, @function
strpbrk:
    jmp *__libc_ponti_tabella+976
    .size strpbrk, .-strpbrk

    .section .text.strrchr,"ax",@progbits
    .globl strrchr
    .type strrchr, @function
strrchr:
    jmp *__libc_ponti_tabella+980
    .size strrchr, .-strrchr

    .section .text.strsignal,"ax",@progbits
    .globl strsignal
    .type strsignal, @function
strsignal:
    jmp *__libc_ponti_tabella+984
    .size strsignal, .-strsignal

    .section .text.strspn,"ax",@progbits
    .globl strspn
    .type strspn, @function
strspn:
    jmp *__libc_ponti_tabella+988
    .size strspn, .-strspn

    .section .text.strstr,"ax",@progbits
    .globl strstr
    .type strstr, @function
strstr:
    jmp *__libc_ponti_tabella+992
    .size strstr, .-strstr

    .section .text.strtod,"ax",@progbits
    .globl strtod
    .type strtod, @function
strtod:
    jmp *__libc_ponti_tabella+996
    .size strtod, .-strtod

    .section .text.strtof,"ax",@progbits
    .globl strtof
    .type strtof, @function
strtof:
    jmp *__libc_ponti_tabella+1000
    .size strtof, .-strtof

    .section .text.strtoimax,"ax",@progbits
    .globl strtoimax
    .type strtoimax, @function
strtoimax:
    jmp *__libc_ponti_tabella+1004
    .size strtoimax, .-strtoimax

    .section .text.strtok,"ax",@progbits
    .globl strtok
    .type strtok, @function
strtok:
    jmp *__libc_ponti_tabella+1008
    .size strtok, .-strtok

    .section .text.strtol,"ax",@progbits
    .globl strtol
    .type strtol, @function
strtol:
    jmp *__libc_ponti_tabella+1012
    .size strtol, .-strtol

    .section .text.strtold,"ax",@progbits
    .globl strtold
    .type strtold, @function
strtold:
    jmp *__libc_ponti_tabella+1016
    .size strtold, .-strtold

    .section .text.strtoll,"ax",@progbits
    .globl strtoll
    .type strtoll, @function
strtoll:
    jmp *__libc_ponti_tabella+1020
    .size strtoll, .-strtoll

    .section .text.strtoul,"ax",@progbits
    .globl strtoul
    .type strtoul, @function
strtoul:
    jmp *__libc_ponti_tabella+1024
    .size strtoul, .-strtoul

    .section .text.strtoull,"ax",@progbits
    .globl strtoull
    .type strtoull, @function
strtoull:
    jmp *__libc_ponti_tabella+1028
    .size strtoull, .-strtoull

    .section .text.strtoumax,"ax",@progbits
    .globl strtoumax
    .type strtoumax, @function
strtoumax:
    jmp *__libc_ponti_tabella+1032
    .size strtoumax, .-strtoumax

    .section .text.strxfrm,"ax",@progbits
    .globl strxfrm
    .type strxfrm, @function
strxfrm:
    jmp *__libc_ponti_tabella+1036
    .size strxfrm, .-strxfrm

    .section .text.swprintf,"ax",@progbits
    .globl swprintf
    .type swprintf, @function
swprintf:
    jmp *__libc_ponti_tabella+1040
    .size swprintf, .-swprintf

    .section .text.sysconf,"ax",@progbits
    .globl sysconf
    .type sysconf, @function
sysconf:
    jmp *__libc_ponti_tabella+1044
    .size sysconf, .-sysconf

    .section .text.system,"ax",@progbits
    .globl system
    .type system, @function
system:
    jmp *__libc_ponti_tabella+1048
    .size system, .-system

    .section .text.time,"ax",@progbits
    .globl time
    .type time, @function
time:
    jmp *__libc_ponti_tabella+1052
    .size time, .-time

    .section .text.time_now,"ax",@progbits
    .globl time_now
    .type time_now, @function
time_now:
    jmp *__libc_ponti_tabella+1056
    .size time_now, .-time_now

    .section .text.times,"ax",@progbits
    .globl times
    .type times, @function
times:
    jmp *__libc_ponti_tabella+1060
    .size times, .-times

    .section .text.timespec_get,"ax",@progbits
    .globl timespec_get
    .type timespec_get, @function
timespec_get:
    jmp *__libc_ponti_tabella+1064
    .size timespec_get, .-timespec_get

    .section .text.tmpfile,"ax",@progbits
    .globl tmpfile
    .type tmpfile, @function
tmpfile:
    jmp *__libc_ponti_tabella+1068
    .size tmpfile, .-tmpfile

    .section .text.tmpnam,"ax",@progbits
    .globl tmpnam
    .type tmpnam, @function
tmpnam:
    jmp *__libc_ponti_tabella+1072
    .size tmpnam, .-tmpnam

    .section .text.toascii,"ax",@progbits
    .globl toascii
    .type toascii, @function
toascii:
    jmp *__libc_ponti_tabella+1076
    .size toascii, .-toascii

    .section .text.tolower,"ax",@progbits
    .globl tolower
    .type tolower, @function
tolower:
    jmp *__libc_ponti_tabella+1080
    .size tolower, .-tolower

    .section .text.toupper,"ax",@progbits
    .globl toupper
    .type toupper, @function
toupper:
    jmp *__libc_ponti_tabella+1084
    .size toupper, .-toupper

    .section .text.towlower,"ax",@progbits
    .globl towlower
    .type towlower, @function
towlower:
    jmp *__libc_ponti_tabella+1088
    .size towlower, .-towlower

    .section .text.towupper,"ax",@progbits
    .globl towupper
    .type towupper, @function
towupper:
    jmp *__libc_ponti_tabella+1092
    .size towupper, .-towupper

    .section .text.truncate,"ax",@progbits
    .globl truncate
    .type truncate, @function
truncate:
    jmp *__libc_ponti_tabella+1096
    .size truncate, .-truncate

    .section .text.tty_clear,"ax",@progbits
    .globl tty_clear
    .type tty_clear, @function
tty_clear:
    jmp *__libc_ponti_tabella+1100
    .size tty_clear, .-tty_clear

    .section .text.tty_getsize,"ax",@progbits
    .globl tty_getsize
    .type tty_getsize, @function
tty_getsize:
    jmp *__libc_ponti_tabella+1104
    .size tty_getsize, .-tty_getsize

    .section .text.tty_raw,"ax",@progbits
    .globl tty_raw
    .type tty_raw, @function
tty_raw:
    jmp *__libc_ponti_tabella+1108
    .size tty_raw, .-tty_raw

    .section .text.umask,"ax",@progbits
    .globl umask
    .type umask, @function
umask:
    jmp *__libc_ponti_tabella+1112
    .size umask, .-umask

    .section .text.umount,"ax",@progbits
    .globl umount
    .type umount, @function
umount:
    jmp *__libc_ponti_tabella+1116
    .size umount, .-umount

    .section .text.ungetc,"ax",@progbits
    .globl ungetc
    .type ungetc, @function
ungetc:
    jmp *__libc_ponti_tabella+1120
    .size ungetc, .-ungetc

    .section .text.unlink,"ax",@progbits
    .globl unlink
    .type unlink, @function
unlink:
    jmp *__libc_ponti_tabella+1124
    .size unlink, .-unlink

    .section .text.unsetenv,"ax",@progbits
    .globl unsetenv
    .type unsetenv, @function
unsetenv:
    jmp *__libc_ponti_tabella+1128
    .size unsetenv, .-unsetenv

    .section .text.uptime_ms,"ax",@progbits
    .globl uptime_ms
    .type uptime_ms, @function
uptime_ms:
    jmp *__libc_ponti_tabella+1132
    .size uptime_ms, .-uptime_ms

    .section .text.usleep,"ax",@progbits
    .globl usleep
    .type usleep, @function
usleep:
    jmp *__libc_ponti_tabella+1136
    .size usleep, .-usleep

    .section .text.utime,"ax",@progbits
    .globl utime
    .type utime, @function
utime:
    jmp *__libc_ponti_tabella+1140
    .size utime, .-utime

    .section .text.verboseboot,"ax",@progbits
    .globl verboseboot
    .type verboseboot, @function
verboseboot:
    jmp *__libc_ponti_tabella+1144
    .size verboseboot, .-verboseboot

    .section .text.vfprintf,"ax",@progbits
    .globl vfprintf
    .type vfprintf, @function
vfprintf:
    jmp *__libc_ponti_tabella+1148
    .size vfprintf, .-vfprintf

    .section .text.vfscanf,"ax",@progbits
    .globl vfscanf
    .type vfscanf, @function
vfscanf:
    jmp *__libc_ponti_tabella+1152
    .size vfscanf, .-vfscanf

    .section .text.video_info,"ax",@progbits
    .globl video_info
    .type video_info, @function
video_info:
    jmp *__libc_ponti_tabella+1156
    .size video_info, .-video_info

    .section .text.vprintf,"ax",@progbits
    .globl vprintf
    .type vprintf, @function
vprintf:
    jmp *__libc_ponti_tabella+1160
    .size vprintf, .-vprintf

    .section .text.vscanf,"ax",@progbits
    .globl vscanf
    .type vscanf, @function
vscanf:
    jmp *__libc_ponti_tabella+1164
    .size vscanf, .-vscanf

    .section .text.vsnprintf,"ax",@progbits
    .globl vsnprintf
    .type vsnprintf, @function
vsnprintf:
    jmp *__libc_ponti_tabella+1168
    .size vsnprintf, .-vsnprintf

    .section .text.vsprintf,"ax",@progbits
    .globl vsprintf
    .type vsprintf, @function
vsprintf:
    jmp *__libc_ponti_tabella+1172
    .size vsprintf, .-vsprintf

    .section .text.vsscanf,"ax",@progbits
    .globl vsscanf
    .type vsscanf, @function
vsscanf:
    jmp *__libc_ponti_tabella+1176
    .size vsscanf, .-vsscanf

    .section .text.vswprintf,"ax",@progbits
    .globl vswprintf
    .type vswprintf, @function
vswprintf:
    jmp *__libc_ponti_tabella+1180
    .size vswprintf, .-vswprintf

    .section .text.wait,"ax",@progbits
    .globl wait
    .type wait, @function
wait:
    jmp *__libc_ponti_tabella+1184
    .size wait, .-wait

    .section .text.waitpid,"ax",@progbits
    .globl waitpid
    .type waitpid, @function
waitpid:
    jmp *__libc_ponti_tabella+1188
    .size waitpid, .-waitpid

    .section .text.wcscat,"ax",@progbits
    .globl wcscat
    .type wcscat, @function
wcscat:
    jmp *__libc_ponti_tabella+1192
    .size wcscat, .-wcscat

    .section .text.wcschr,"ax",@progbits
    .globl wcschr
    .type wcschr, @function
wcschr:
    jmp *__libc_ponti_tabella+1196
    .size wcschr, .-wcschr

    .section .text.wcscmp,"ax",@progbits
    .globl wcscmp
    .type wcscmp, @function
wcscmp:
    jmp *__libc_ponti_tabella+1200
    .size wcscmp, .-wcscmp

    .section .text.wcscpy,"ax",@progbits
    .globl wcscpy
    .type wcscpy, @function
wcscpy:
    jmp *__libc_ponti_tabella+1204
    .size wcscpy, .-wcscpy

    .section .text.wcscspn,"ax",@progbits
    .globl wcscspn
    .type wcscspn, @function
wcscspn:
    jmp *__libc_ponti_tabella+1208
    .size wcscspn, .-wcscspn

    .section .text.wcslen,"ax",@progbits
    .globl wcslen
    .type wcslen, @function
wcslen:
    jmp *__libc_ponti_tabella+1212
    .size wcslen, .-wcslen

    .section .text.wcsncat,"ax",@progbits
    .globl wcsncat
    .type wcsncat, @function
wcsncat:
    jmp *__libc_ponti_tabella+1216
    .size wcsncat, .-wcsncat

    .section .text.wcsncmp,"ax",@progbits
    .globl wcsncmp
    .type wcsncmp, @function
wcsncmp:
    jmp *__libc_ponti_tabella+1220
    .size wcsncmp, .-wcsncmp

    .section .text.wcsncpy,"ax",@progbits
    .globl wcsncpy
    .type wcsncpy, @function
wcsncpy:
    jmp *__libc_ponti_tabella+1224
    .size wcsncpy, .-wcsncpy

    .section .text.wcspbrk,"ax",@progbits
    .globl wcspbrk
    .type wcspbrk, @function
wcspbrk:
    jmp *__libc_ponti_tabella+1228
    .size wcspbrk, .-wcspbrk

    .section .text.wcsrchr,"ax",@progbits
    .globl wcsrchr
    .type wcsrchr, @function
wcsrchr:
    jmp *__libc_ponti_tabella+1232
    .size wcsrchr, .-wcsrchr

    .section .text.wcsspn,"ax",@progbits
    .globl wcsspn
    .type wcsspn, @function
wcsspn:
    jmp *__libc_ponti_tabella+1236
    .size wcsspn, .-wcsspn

    .section .text.wcsstr,"ax",@progbits
    .globl wcsstr
    .type wcsstr, @function
wcsstr:
    jmp *__libc_ponti_tabella+1240
    .size wcsstr, .-wcsstr

    .section .text.wcstod,"ax",@progbits
    .globl wcstod
    .type wcstod, @function
wcstod:
    jmp *__libc_ponti_tabella+1244
    .size wcstod, .-wcstod

    .section .text.wcstol,"ax",@progbits
    .globl wcstol
    .type wcstol, @function
wcstol:
    jmp *__libc_ponti_tabella+1248
    .size wcstol, .-wcstol

    .section .text.wcstoll,"ax",@progbits
    .globl wcstoll
    .type wcstoll, @function
wcstoll:
    jmp *__libc_ponti_tabella+1252
    .size wcstoll, .-wcstoll

    .section .text.wcstombs,"ax",@progbits
    .globl wcstombs
    .type wcstombs, @function
wcstombs:
    jmp *__libc_ponti_tabella+1256
    .size wcstombs, .-wcstombs

    .section .text.wcstoul,"ax",@progbits
    .globl wcstoul
    .type wcstoul, @function
wcstoul:
    jmp *__libc_ponti_tabella+1260
    .size wcstoul, .-wcstoul

    .section .text.wcstoull,"ax",@progbits
    .globl wcstoull
    .type wcstoull, @function
wcstoull:
    jmp *__libc_ponti_tabella+1264
    .size wcstoull, .-wcstoull

    .section .text.wctomb,"ax",@progbits
    .globl wctomb
    .type wctomb, @function
wctomb:
    jmp *__libc_ponti_tabella+1268
    .size wctomb, .-wctomb

    .section .text.wmemchr,"ax",@progbits
    .globl wmemchr
    .type wmemchr, @function
wmemchr:
    jmp *__libc_ponti_tabella+1272
    .size wmemchr, .-wmemchr

    .section .text.wmemcmp,"ax",@progbits
    .globl wmemcmp
    .type wmemcmp, @function
wmemcmp:
    jmp *__libc_ponti_tabella+1276
    .size wmemcmp, .-wmemcmp

    .section .text.wmemcpy,"ax",@progbits
    .globl wmemcpy
    .type wmemcpy, @function
wmemcpy:
    jmp *__libc_ponti_tabella+1280
    .size wmemcpy, .-wmemcpy

    .section .text.wmemmove,"ax",@progbits
    .globl wmemmove
    .type wmemmove, @function
wmemmove:
    jmp *__libc_ponti_tabella+1284
    .size wmemmove, .-wmemmove

    .section .text.wmemset,"ax",@progbits
    .globl wmemset
    .type wmemset, @function
wmemset:
    jmp *__libc_ponti_tabella+1288
    .size wmemset, .-wmemset

    .section .text.write,"ax",@progbits
    .globl write
    .type write, @function
write:
    jmp *__libc_ponti_tabella+1292
    .size write, .-write

    .section .note.GNU-stack,"",@progbits
