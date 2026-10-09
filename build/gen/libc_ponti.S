/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * I ponti verso libc.so: 453 salti indiretti, uno per funzione.
 *
 * __libc_ponti_impronte ha, nello stesso ordine della tabella, l'IMPRONTA di
 * ogni nome (FNV-1a a 32 bit): quattro byte invece del nome intero. Il nome
 * lo tiene libc.so; chi si avvia ne calcola l'impronta e confronta.
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
    .space 1812

    .section .rodata
    .align 4
    .globl __libc_ponti_impronte
__libc_ponti_impronte:
    .long 0xb3002016        /* _Exit */
    .long 0x6bda62a9        /* __environ_dove */
    .long 0x484f619e        /* __errno_dove */
    .long 0xf2fd114f        /* __libc_distruttori_registra */
    .long 0xc76e08a0        /* __stderr_dove */
    .long 0xfef08850        /* __stdin_dove */
    .long 0x08c5495d        /* __stdout_dove */
    .long 0x0d663828        /* _assert_fallita */
    .long 0x35a6d536        /* _exit */
    .long 0xa52bcaf9        /* abort */
    .long 0x2a48023b        /* abs */
    .long 0x1126caeb        /* access */
    .long 0xe8fe2ed9        /* ahci_guarda */
    .long 0x3c718937        /* ahci_passa */
    .long 0x4ef4831c        /* ahci_stato */
    .long 0x4f171a15        /* aligned_alloc */
    .long 0x69dc410b        /* asctime */
    .long 0x95fe5f09        /* at_quick_exit */
    .long 0xd41e7bd8        /* atexit */
    .long 0xfe7440f9        /* atof */
    .long 0x07744f24        /* atoi */
    .long 0x04744a6b        /* atol */
    .long 0x0a108905        /* atoll */
    .long 0xf32b240b        /* attesa_dormi */
    .long 0xd82e8229        /* attesa_sveglia */
    .long 0x675ada58        /* blk_attendi */
    .long 0x7be5afe1        /* blk_espelli */
    .long 0xbc22a80b        /* blk_offri */
    .long 0xa5fb06da        /* blk_risposta */
    .long 0x7fe86e78        /* blk_scansiona */
    .long 0xde21b6dc        /* blkinfo */
    .long 0x7d00df74        /* blkread */
    .long 0x83b4b18f        /* blkwrite */
    .long 0x9f505210        /* bootinstall */
    .long 0x4b238194        /* bootverify */
    .long 0xe039b2c9        /* bsearch */
    .long 0x73e20203        /* calloc */
    .long 0x30144565        /* chdir */
    .long 0x21cf6452        /* chmod */
    .long 0x4c085c06        /* chown */
    .long 0xe6aecf25        /* clearerr */
    .long 0x15a40f5d        /* clock */
    .long 0x40d5ffd4        /* clock_getres */
    .long 0xd1446b63        /* clock_gettime */
    .long 0x27cb3b23        /* close */
    .long 0x7288f882        /* closedir */
    .long 0x92b9b306        /* condizione_aspetta */
    .long 0xf6a62ea3        /* condizione_aspetta_ms */
    .long 0xf7181767        /* condizione_segnala */
    .long 0xf24f153e        /* condizione_segnala_tutti */
    .long 0x45d5a3a7        /* console_ctrlc */
    .long 0x6ab0d6d0        /* console_grafica */
    .long 0x3d7be71b        /* console_info */
    .long 0xc5941b4a        /* console_registro */
    .long 0xe154158a        /* console_setfg */
    .long 0x2c266d6f        /* console_switch */
    .long 0xc6ce027c        /* console_testo */
    .long 0x5b30a692        /* console_write */
    .long 0x6409a1da        /* cpu_info */
    .long 0x13a6ec13        /* ctime */
    .long 0xedbea49b        /* difftime */
    .long 0x47293bf8        /* diskinfo */
    .long 0xe562ab48        /* div */
    .long 0xe57ba779        /* diventa_root */
    .long 0x5ba87b4b        /* dma_alloc */
    .long 0xd330f226        /* dup */
    .long 0x8a0d157c        /* dup2 */
    .long 0x48c5c01e        /* execv */
    .long 0xfd4ded2a        /* execvp */
    .long 0xcded1a85        /* exit */
    .long 0x176942b7        /* fabs */
    .long 0xf98a6204        /* fb_map */
    .long 0x51d5d066        /* fchmod */
    .long 0xc3debaf7        /* fclose */
    .long 0x03039870        /* fcntl */
    .long 0x3c14a90b        /* fdopen */
    .long 0x1b54cd7b        /* fdprova */
    .long 0xa94d67e5        /* feof */
    .long 0x6e4cc81d        /* ferror */
    .long 0x9798bbc1        /* fflush */
    .long 0x9466dd68        /* fgetc */
    .long 0x98ffac6b        /* fgetpos */
    .long 0x8466c438        /* fgets */
    .long 0x050302a8        /* fileno */
    .long 0x4dc909c5        /* fopen */
    .long 0x7d287af8        /* fpathconf */
    .long 0x937d1ef6        /* fprintf */
    .long 0x23c63deb        /* fputc */
    .long 0x33c6571b        /* fputs */
    .long 0x854b1879        /* fread */
    .long 0x99b3eedb        /* free */
    .long 0xfd13491c        /* freopen */
    .long 0xa30cdef4        /* frexp */
    .long 0x33e99396        /* fscanf */
    .long 0x806806e5        /* fseek */
    .long 0x84eba0cf        /* fsetpos */
    .long 0xf0931f88        /* fsize */
    .long 0x59ecaedb        /* fstat */
    .long 0x457b0da6        /* ftell */
    .long 0x8af06c60        /* fwrite */
    .long 0x83eb36dc        /* getc */
    .long 0xae94bd2f        /* getchar */
    .long 0x2da55339        /* getconf */
    .long 0x26dd22cf        /* getcwd */
    .long 0xd7e858ce        /* getegid */
    .long 0x9165c9a4        /* getentropy */
    .long 0x4ddded5a        /* getenv */
    .long 0xcd4786bc        /* geteuid */
    .long 0x88bf2f4d        /* getgid */
    .long 0x16b19d15        /* getpagesize */
    .long 0x7cc21180        /* getpid */
    .long 0xa80c7298        /* getrandom */
    .long 0x3e71e10e        /* getrusage */
    .long 0x73eb1dac        /* gets */
    .long 0xc55666d3        /* gettimeofday */
    .long 0x90221e1b        /* getuid */
    .long 0x4426e788        /* gmtime */
    .long 0xca1ee005        /* gmtime_r */
    .long 0x39ef2266        /* imaxabs */
    .long 0x86f10805        /* imaxdiv */
    .long 0xdb8a6a72        /* interrompi */
    .long 0x9fe1ed1e        /* ioctl */
    .long 0x50f9c4d0        /* ioport_bind */
    .long 0x84edbd9c        /* ioport_in */
    .long 0x05414db3        /* ioport_in16 */
    .long 0x1945ea5d        /* ioport_in32 */
    .long 0x927d1369        /* ioport_out */
    .long 0x1d35341a        /* ioport_out16 */
    .long 0x2139b794        /* ioport_out32 */
    .long 0xd4f00899        /* ipc_attendi */
    .long 0x4f483ec8        /* ipc_lookup */
    .long 0xb5e0d998        /* ipc_pronto */
    .long 0x450a99ca        /* ipc_recv */
    .long 0x14d59f82        /* ipc_recv_timeout */
    .long 0xdf31fc3f        /* ipc_register */
    .long 0xf90998e0        /* ipc_rimetti */
    .long 0x44cc508b        /* ipc_scegli */
    .long 0x021047cc        /* ipc_send */
    .long 0xd64881c1        /* irq_bind */
    .long 0x3fca998a        /* irq_done */
    .long 0x0167fe5a        /* irq_unbind */
    .long 0x25a94a70        /* isalnum */
    .long 0x64f4097b        /* isalpha */
    .long 0xf7a01b00        /* isascii */
    .long 0xe2ae36e7        /* isatty */
    .long 0xd5c59331        /* isblank */
    .long 0x61d09d1e        /* iscntrl */
    .long 0xf75e7926        /* isdigit */
    .long 0x3a2097f7        /* isgraph */
    .long 0x70e42dea        /* islower */
    .long 0xc41553d8        /* isprint */
    .long 0x1ad615a7        /* ispunct */
    .long 0xae5c87b5        /* isspace */
    .long 0x1ccfd077        /* isupper */
    .long 0xb7e47849        /* iswalnum */
    .long 0xf6c3635e        /* iswalpha */
    .long 0x5e5dcea4        /* iswblank */
    .long 0x9287e1ab        /* iswcntrl */
    .long 0x6fe4ef23        /* iswdigit */
    .long 0x2f89ffee        /* iswgraph */
    .long 0xb0caa40b        /* iswlower */
    .long 0xe4f9d4e1        /* iswprint */
    .long 0xa24ffd96        /* iswpunct */
    .long 0x2ceff180        /* iswspace */
    .long 0x6d17cc2a        /* iswupper */
    .long 0x2f1c7037        /* iswxdigit */
    .long 0x00a52190        /* isxdigit */
    .long 0x739b6f0a        /* kbprova */
    .long 0x873b159f        /* kbstato */
    .long 0x042ddf93        /* klog_leggi */
    .long 0x6506aa85        /* labs */
    .long 0x8c63f160        /* ldexp */
    .long 0x91162dfe        /* ldiv */
    .long 0x77853d70        /* listdir */
    .long 0x4fd17a6d        /* listdir_from */
    .long 0x818e7d1b        /* llabs */
    .long 0x3ca92628        /* lldiv */
    .long 0xa28515a9        /* localeconv */
    .long 0xe14dcfc1        /* localtime */
    .long 0x9f1789d8        /* localtime_r */
    .long 0xe11a1741        /* log_seriale */
    .long 0xc8f2abd6        /* longjmp */
    .long 0x069fa287        /* lseek */
    .long 0x610f3dc1        /* lstat */
    .long 0x558c274d        /* malloc */
    .long 0x68257d2e        /* malloc_usable_size */
    .long 0x03d226b5        /* mblen */
    .long 0x3558c047        /* mbrtowc */
    .long 0xc0176fc5        /* mbstowcs */
    .long 0x8b57371d        /* mbtowc */
    .long 0x2a65949f        /* memalign */
    .long 0xaf4975fd        /* memchr */
    .long 0xaf3caa0a        /* memcmp */
    .long 0xa45cec64        /* memcpy */
    .long 0x2bfedd3e        /* meminfo */
    .long 0x01081b8b        /* memmove */
    .long 0xcb80cc06        /* memset */
    .long 0xabe3e5d8        /* mkdir */
    .long 0xcd0436cc        /* mkstemp */
    .long 0x2c0b8173        /* mktemp */
    .long 0x3a665c80        /* mktime */
    .long 0x2e4732ae        /* mmap */
    .long 0xec9b04be        /* mmio_map */
    .long 0x2e9026a2        /* modo_testo */
    .long 0x6c166aba        /* mount */
    .long 0xadc05842        /* mountinfo */
    .long 0x1128760b        /* munmap */
    .long 0x643de85e        /* mutex_lascia */
    .long 0x8d25571b        /* mutex_prendi */
    .long 0xa2a5d7f5        /* mutex_prova */
    .long 0x71044eb2        /* nanosleep */
    .long 0xee2266e4        /* nome_utente */
    .long 0xd35ec4c9        /* open */
    .long 0x61791c38        /* opendir */
    .long 0x8d3c73d9        /* osversion */
    .long 0x368d8303        /* partwrite */
    .long 0x9c7a8784        /* pathconf */
    .long 0xd9a88295        /* pclose */
    .long 0x74bfd75f        /* perror */
    .long 0xf81d19f9        /* pipe */
    .long 0x6fd68838        /* poll */
    .long 0xf917d613        /* popen */
    .long 0x6dc01a8f        /* posix_memalign */
    .long 0xe76fb4aa        /* printf */
    .long 0xb610f3c3        /* proc_gruppo */
    .long 0x649f4179        /* procinfo */
    .long 0x5e0a8986        /* pthread_attr_destroy */
    .long 0x330c05c8        /* pthread_attr_getdetachstate */
    .long 0xcd66e6e0        /* pthread_attr_getschedparam */
    .long 0xfad76c25        /* pthread_attr_getschedpolicy */
    .long 0xb9a4b128        /* pthread_attr_getstack */
    .long 0x3a8d6af9        /* pthread_attr_getstacksize */
    .long 0xda610296        /* pthread_attr_init */
    .long 0x478ab3dc        /* pthread_attr_setdetachstate */
    .long 0x094582a0        /* pthread_attr_setguardsize */
    .long 0xba7a1dfe        /* pthread_attr_setinheritsched */
    .long 0xf3fc53c4        /* pthread_attr_setschedparam */
    .long 0xcd5e33a9        /* pthread_attr_setschedpolicy */
    .long 0x4f004a38        /* pthread_attr_setscope */
    .long 0xc5f35f3d        /* pthread_attr_setstacksize */
    .long 0x43bc5e3c        /* pthread_cond_broadcast */
    .long 0xeb205d1d        /* pthread_cond_destroy */
    .long 0xdc0a0c0b        /* pthread_cond_init */
    .long 0x975861e1        /* pthread_cond_signal */
    .long 0x5268ace9        /* pthread_cond_timedwait */
    .long 0x349b6d58        /* pthread_cond_wait */
    .long 0x9207ab66        /* pthread_condattr_destroy */
    .long 0xc0771366        /* pthread_condattr_getclock */
    .long 0x0c0abff6        /* pthread_condattr_init */
    .long 0xb5a5853a        /* pthread_condattr_setclock */
    .long 0x30a388c4        /* pthread_create */
    .long 0xe2bfe017        /* pthread_detach */
    .long 0x4a71d9f8        /* pthread_equal */
    .long 0x7c82f030        /* pthread_exit */
    .long 0x2444f3be        /* pthread_getattr_np */
    .long 0xb95139d6        /* pthread_getname_np */
    .long 0x5bd2b344        /* pthread_getschedparam */
    .long 0x0f89a364        /* pthread_getspecific */
    .long 0x22d2bd10        /* pthread_join */
    .long 0x35d05704        /* pthread_key_create */
    .long 0xfe95407b        /* pthread_key_delete */
    .long 0x029f1b20        /* pthread_kill */
    .long 0x3d0ffd86        /* pthread_mutex_destroy */
    .long 0x6994de96        /* pthread_mutex_init */
    .long 0x596ead5b        /* pthread_mutex_lock */
    .long 0xdfd65dec        /* pthread_mutex_timedlock */
    .long 0x9021b62a        /* pthread_mutex_trylock */
    .long 0x5138dd10        /* pthread_mutex_unlock */
    .long 0x14fbb151        /* pthread_mutexattr_destroy */
    .long 0x6e283b23        /* pthread_mutexattr_gettype */
    .long 0xa06c69e7        /* pthread_mutexattr_init */
    .long 0xd513b517        /* pthread_mutexattr_settype */
    .long 0x510217e1        /* pthread_once */
    .long 0xc40b5231        /* pthread_rwlock_destroy */
    .long 0xdf7f4347        /* pthread_rwlock_init */
    .long 0x9cc28288        /* pthread_rwlock_rdlock */
    .long 0x5e5696c3        /* pthread_rwlock_tryrdlock */
    .long 0xbb9a3fe0        /* pthread_rwlock_trywrlock */
    .long 0x6acc8c39        /* pthread_rwlock_unlock */
    .long 0xb3a4501f        /* pthread_rwlock_wrlock */
    .long 0x0f902eda        /* pthread_self */
    .long 0xd9a3d26a        /* pthread_setname_np */
    .long 0x353d4660        /* pthread_setschedparam */
    .long 0x4715d658        /* pthread_setspecific */
    .long 0x70dbf749        /* pthread_sigmask */
    .long 0x65362c21        /* pthread_yield */
    .long 0x9e5852b1        /* pty_apri */
    .long 0x2d3674aa        /* pty_ctl */
    .long 0x8c67b8f7        /* putc */
    .long 0x15026602        /* putchar */
    .long 0x24a4d9c1        /* putenv */
    .long 0x9c67d227        /* puts */
    .long 0xbe456288        /* qsort */
    .long 0x2fd4df39        /* quick_exit */
    .long 0x5ef9ed23        /* raise */
    .long 0xa19b8cd6        /* rand */
    .long 0xcedfa3c5        /* read */
    .long 0x72b77394        /* readdir */
    .long 0xbf26b345        /* realloc */
    .long 0xae53fb1e        /* realpath */
    .long 0x87729fb8        /* reboot */
    .long 0xdb9215fd        /* remove */
    .long 0x81f2b7d3        /* rename */
    .long 0xda112eae        /* rewind */
    .long 0xa33ccf45        /* rewinddir */
    .long 0x3e468adf        /* rmdir */
    .long 0x8530a347        /* sbrk */
    .long 0x7f91078a        /* scanf */
    .long 0x4dbaca6e        /* sched_yield */
    .long 0x11c2662d        /* select */
    .long 0x6d152e05        /* sem_destroy */
    .long 0xaa271f0c        /* sem_getvalue */
    .long 0xc1066133        /* sem_init */
    .long 0x343af507        /* sem_post */
    .long 0x9f500c41        /* sem_timedwait */
    .long 0x7f8bc723        /* sem_trywait */
    .long 0x4f486560        /* sem_wait */
    .long 0xcf29e72b        /* semaforo_lascia */
    .long 0xa892cd4a        /* semaforo_prendi */
    .long 0xa113596f        /* semaforo_prendi_ms */
    .long 0x3773ab4e        /* semaforo_prova */
    .long 0x6aed47bc        /* setbuf */
    .long 0x7c464266        /* setenv */
    .long 0xbf979a06        /* setjmp */
    .long 0xd6b7f315        /* setlocale */
    .long 0x5971eacf        /* setuid */
    .long 0x36acee80        /* setvbuf */
    .long 0x9cd435fa        /* sha256 */
    .long 0x3013bb24        /* sha256_avvia */
    .long 0x0f9076ab        /* sha256_dai */
    .long 0xa76ff472        /* sha256_esa */
    .long 0xe224d8d7        /* sha256_fine */
    .long 0xacf14f07        /* sha256_fine_esa */
    .long 0xec4ee9f6        /* shm_apri */
    .long 0x6b5fcb20        /* shm_chiudi */
    .long 0xbd7bba09        /* signal */
    .long 0x89eabb08        /* sleep */
    .long 0x3fd5eb6f        /* snprintf */
    .long 0x3a224d98        /* spawn */
    .long 0x50dccbc8        /* spawn_ex */
    .long 0xaeee9b07        /* spawn_su_console */
    .long 0x5c8c917c        /* spawn_utente */
    .long 0x70e2d349        /* sprintf */
    .long 0x7dee3bcf        /* sqrt */
    .long 0x1bbf4029        /* srand */
    .long 0x03ab1e13        /* sscanf */
    .long 0xd33b3297        /* stat */
    .long 0x3b5babc1        /* statperm */
    .long 0x22bbc437        /* statraw */
    .long 0x4910d150        /* strcasecmp */
    .long 0x9940f7a4        /* strcat */
    .long 0x892f9553        /* strchr */
    .long 0x9d367094        /* strcmp */
    .long 0x54524c02        /* strcoll */
    .long 0x926a3aa6        /* strcpy */
    .long 0xd462345c        /* strcspn */
    .long 0x07fad6e1        /* strdup */
    .long 0x76a5d196        /* strerror */
    .long 0xd78e545b        /* strerror_r */
    .long 0x0b30aae3        /* strftime */
    .long 0x58ba3d97        /* strlen */
    .long 0xad575f72        /* strncasecmp */
    .long 0xb1ee6f2e        /* strncat */
    .long 0x9de3c256        /* strncmp */
    .long 0xa2c986b8        /* strncpy */
    .long 0x6b070b35        /* strpbrk */
    .long 0x49053e05        /* strrchr */
    .long 0xda451f04        /* strsignal */
    .long 0xb573d07f        /* strspn */
    .long 0xc17cddbf        /* strstr */
    .long 0x47cced67        /* strtod */
    .long 0x49ccf08d        /* strtof */
    .long 0xcad4357e        /* strtoimax */
    .long 0x3cccdc16        /* strtok */
    .long 0xcd92a2db        /* strtok_r */
    .long 0x3fcce0cf        /* strtol */
    .long 0x1a85ad31        /* strtold */
    .long 0x1285a099        /* strtoll */
    .long 0x0c6fd0d8        /* strtoul */
    .long 0x48058b5c        /* strtoull */
    .long 0xf509f6b2        /* strtoumax */
    .long 0xe9b5b52f        /* strxfrm */
    .long 0xdb2dd230        /* swprintf */
    .long 0xe3611a64        /* sysconf */
    .long 0x491e0a9c        /* system */
    .long 0xebb024ad        /* thread_attendi */
    .long 0xced20ee1        /* thread_crea */
    .long 0xdddb9b42        /* thread_devo_fermarmi */
    .long 0xe6bd1426        /* thread_esci */
    .long 0x555da281        /* thread_ferma */
    .long 0x5d3c9be4        /* time */
    .long 0x5369db13        /* time_now */
    .long 0xa7f53907        /* time_set */
    .long 0xe3fe613c        /* timegm */
    .long 0x5d68eeb5        /* times */
    .long 0x8befe1c4        /* timespec_get */
    .long 0x94e5b448        /* tmpfile */
    .long 0xa6b648da        /* tmpnam */
    .long 0x18bb6437        /* toascii */
    .long 0x3e2397f1        /* tolower */
    .long 0xdc0f32d8        /* toupper */
    .long 0x7e19ec76        /* towlower */
    .long 0xd7ad1083        /* towupper */
    .long 0x423c87b7        /* truncate */
    .long 0x4e0f8a96        /* tty_clear */
    .long 0xaf7bbd12        /* tty_getsize */
    .long 0x650894bd        /* tty_raw */
    .long 0xabf7ad48        /* umask */
    .long 0xc0f1f2fd        /* umount */
    .long 0x96183d4f        /* ungetc */
    .long 0x5ae63e52        /* unlink */
    .long 0x7c62673d        /* unsetenv */
    .long 0x7028e59a        /* uptime_ms */
    .long 0xdbb09e23        /* usleep */
    .long 0xb49b24a9        /* utime */
    .long 0x534f26cd        /* verboseboot */
    .long 0x01be7c82        /* vfprintf */
    .long 0x0c5ffd02        /* vfscanf */
    .long 0xf10ba371        /* video_info */
    .long 0x7e011206        /* vprintf */
    .long 0x4734e946        /* vscanf */
    .long 0xa4383ef3        /* vsnprintf */
    .long 0xca35402d        /* vsprintf */
    .long 0xd391e587        /* vsscanf */
    .long 0x5ea48be4        /* vswprintf */
    .long 0x892e4ca0        /* wait */
    .long 0xec4e2b2d        /* waitpid */
    .long 0x2c8ce5d8        /* wcscat */
    .long 0x24a29f8f        /* wcschr */
    .long 0x28976ce8        /* wcscmp */
    .long 0x2d681692        /* wcscpy */
    .long 0xc3395a98        /* wcscspn */
    .long 0xa4e39b3b        /* wcslen */
    .long 0x7beb1de2        /* wcsncat */
    .long 0x8ff437ba        /* wcsncmp */
    .long 0x84c65b34        /* wcsncpy */
    .long 0xefa33f19        /* wcspbrk */
    .long 0xed2ae2f9        /* wcsrchr */
    .long 0x4aea75d3        /* wcsspn */
    .long 0x4edfeec3        /* wcsstr */
    .long 0x7699241b        /* wcstod */
    .long 0x6e991783        /* wcstol */
    .long 0x0a00ad3d        /* wcstoll */
    .long 0x8e351fbf        /* wcstombs */
    .long 0x23e97cdc        /* wcstoul */
    .long 0x388f4910        /* wcstoull */
    .long 0x9bfd6fc5        /* wctomb */
    .long 0xf866346e        /* wmemchr */
    .long 0xf472fa15        /* wmemcmp */
    .long 0xef2b8f1b        /* wmemcpy */
    .long 0xdbc2de5a        /* wmemmove */
    .long 0x3017eab1        /* wmemset */
    .long 0xbe269f5c        /* write */

    .align 4
    .globl __libc_ponti_quanti
__libc_ponti_quanti:
    .long 453
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

    .section .text.ahci_guarda,"ax",@progbits
    .globl ahci_guarda
    .type ahci_guarda, @function
ahci_guarda:
    jmp *__libc_ponti_tabella+48
    .size ahci_guarda, .-ahci_guarda

    .section .text.ahci_passa,"ax",@progbits
    .globl ahci_passa
    .type ahci_passa, @function
ahci_passa:
    jmp *__libc_ponti_tabella+52
    .size ahci_passa, .-ahci_passa

    .section .text.ahci_stato,"ax",@progbits
    .globl ahci_stato
    .type ahci_stato, @function
ahci_stato:
    jmp *__libc_ponti_tabella+56
    .size ahci_stato, .-ahci_stato

    .section .text.aligned_alloc,"ax",@progbits
    .globl aligned_alloc
    .type aligned_alloc, @function
aligned_alloc:
    jmp *__libc_ponti_tabella+60
    .size aligned_alloc, .-aligned_alloc

    .section .text.asctime,"ax",@progbits
    .globl asctime
    .type asctime, @function
asctime:
    jmp *__libc_ponti_tabella+64
    .size asctime, .-asctime

    .section .text.at_quick_exit,"ax",@progbits
    .globl at_quick_exit
    .type at_quick_exit, @function
at_quick_exit:
    jmp *__libc_ponti_tabella+68
    .size at_quick_exit, .-at_quick_exit

    .section .text.atexit,"ax",@progbits
    .globl atexit
    .type atexit, @function
atexit:
    jmp *__libc_ponti_tabella+72
    .size atexit, .-atexit

    .section .text.atof,"ax",@progbits
    .globl atof
    .type atof, @function
atof:
    jmp *__libc_ponti_tabella+76
    .size atof, .-atof

    .section .text.atoi,"ax",@progbits
    .globl atoi
    .type atoi, @function
atoi:
    jmp *__libc_ponti_tabella+80
    .size atoi, .-atoi

    .section .text.atol,"ax",@progbits
    .globl atol
    .type atol, @function
atol:
    jmp *__libc_ponti_tabella+84
    .size atol, .-atol

    .section .text.atoll,"ax",@progbits
    .globl atoll
    .type atoll, @function
atoll:
    jmp *__libc_ponti_tabella+88
    .size atoll, .-atoll

    .section .text.attesa_dormi,"ax",@progbits
    .globl attesa_dormi
    .type attesa_dormi, @function
attesa_dormi:
    jmp *__libc_ponti_tabella+92
    .size attesa_dormi, .-attesa_dormi

    .section .text.attesa_sveglia,"ax",@progbits
    .globl attesa_sveglia
    .type attesa_sveglia, @function
attesa_sveglia:
    jmp *__libc_ponti_tabella+96
    .size attesa_sveglia, .-attesa_sveglia

    .section .text.blk_attendi,"ax",@progbits
    .globl blk_attendi
    .type blk_attendi, @function
blk_attendi:
    jmp *__libc_ponti_tabella+100
    .size blk_attendi, .-blk_attendi

    .section .text.blk_espelli,"ax",@progbits
    .globl blk_espelli
    .type blk_espelli, @function
blk_espelli:
    jmp *__libc_ponti_tabella+104
    .size blk_espelli, .-blk_espelli

    .section .text.blk_offri,"ax",@progbits
    .globl blk_offri
    .type blk_offri, @function
blk_offri:
    jmp *__libc_ponti_tabella+108
    .size blk_offri, .-blk_offri

    .section .text.blk_risposta,"ax",@progbits
    .globl blk_risposta
    .type blk_risposta, @function
blk_risposta:
    jmp *__libc_ponti_tabella+112
    .size blk_risposta, .-blk_risposta

    .section .text.blk_scansiona,"ax",@progbits
    .globl blk_scansiona
    .type blk_scansiona, @function
blk_scansiona:
    jmp *__libc_ponti_tabella+116
    .size blk_scansiona, .-blk_scansiona

    .section .text.blkinfo,"ax",@progbits
    .globl blkinfo
    .type blkinfo, @function
blkinfo:
    jmp *__libc_ponti_tabella+120
    .size blkinfo, .-blkinfo

    .section .text.blkread,"ax",@progbits
    .globl blkread
    .type blkread, @function
blkread:
    jmp *__libc_ponti_tabella+124
    .size blkread, .-blkread

    .section .text.blkwrite,"ax",@progbits
    .globl blkwrite
    .type blkwrite, @function
blkwrite:
    jmp *__libc_ponti_tabella+128
    .size blkwrite, .-blkwrite

    .section .text.bootinstall,"ax",@progbits
    .globl bootinstall
    .type bootinstall, @function
bootinstall:
    jmp *__libc_ponti_tabella+132
    .size bootinstall, .-bootinstall

    .section .text.bootverify,"ax",@progbits
    .globl bootverify
    .type bootverify, @function
bootverify:
    jmp *__libc_ponti_tabella+136
    .size bootverify, .-bootverify

    .section .text.bsearch,"ax",@progbits
    .globl bsearch
    .type bsearch, @function
bsearch:
    jmp *__libc_ponti_tabella+140
    .size bsearch, .-bsearch

    .section .text.calloc,"ax",@progbits
    .globl calloc
    .type calloc, @function
calloc:
    jmp *__libc_ponti_tabella+144
    .size calloc, .-calloc

    .section .text.chdir,"ax",@progbits
    .globl chdir
    .type chdir, @function
chdir:
    jmp *__libc_ponti_tabella+148
    .size chdir, .-chdir

    .section .text.chmod,"ax",@progbits
    .globl chmod
    .type chmod, @function
chmod:
    jmp *__libc_ponti_tabella+152
    .size chmod, .-chmod

    .section .text.chown,"ax",@progbits
    .globl chown
    .type chown, @function
chown:
    jmp *__libc_ponti_tabella+156
    .size chown, .-chown

    .section .text.clearerr,"ax",@progbits
    .globl clearerr
    .type clearerr, @function
clearerr:
    jmp *__libc_ponti_tabella+160
    .size clearerr, .-clearerr

    .section .text.clock,"ax",@progbits
    .globl clock
    .type clock, @function
clock:
    jmp *__libc_ponti_tabella+164
    .size clock, .-clock

    .section .text.clock_getres,"ax",@progbits
    .globl clock_getres
    .type clock_getres, @function
clock_getres:
    jmp *__libc_ponti_tabella+168
    .size clock_getres, .-clock_getres

    .section .text.clock_gettime,"ax",@progbits
    .globl clock_gettime
    .type clock_gettime, @function
clock_gettime:
    jmp *__libc_ponti_tabella+172
    .size clock_gettime, .-clock_gettime

    .section .text.close,"ax",@progbits
    .globl close
    .type close, @function
close:
    jmp *__libc_ponti_tabella+176
    .size close, .-close

    .section .text.closedir,"ax",@progbits
    .globl closedir
    .type closedir, @function
closedir:
    jmp *__libc_ponti_tabella+180
    .size closedir, .-closedir

    .section .text.condizione_aspetta,"ax",@progbits
    .globl condizione_aspetta
    .type condizione_aspetta, @function
condizione_aspetta:
    jmp *__libc_ponti_tabella+184
    .size condizione_aspetta, .-condizione_aspetta

    .section .text.condizione_aspetta_ms,"ax",@progbits
    .globl condizione_aspetta_ms
    .type condizione_aspetta_ms, @function
condizione_aspetta_ms:
    jmp *__libc_ponti_tabella+188
    .size condizione_aspetta_ms, .-condizione_aspetta_ms

    .section .text.condizione_segnala,"ax",@progbits
    .globl condizione_segnala
    .type condizione_segnala, @function
condizione_segnala:
    jmp *__libc_ponti_tabella+192
    .size condizione_segnala, .-condizione_segnala

    .section .text.condizione_segnala_tutti,"ax",@progbits
    .globl condizione_segnala_tutti
    .type condizione_segnala_tutti, @function
condizione_segnala_tutti:
    jmp *__libc_ponti_tabella+196
    .size condizione_segnala_tutti, .-condizione_segnala_tutti

    .section .text.console_ctrlc,"ax",@progbits
    .globl console_ctrlc
    .type console_ctrlc, @function
console_ctrlc:
    jmp *__libc_ponti_tabella+200
    .size console_ctrlc, .-console_ctrlc

    .section .text.console_grafica,"ax",@progbits
    .globl console_grafica
    .type console_grafica, @function
console_grafica:
    jmp *__libc_ponti_tabella+204
    .size console_grafica, .-console_grafica

    .section .text.console_info,"ax",@progbits
    .globl console_info
    .type console_info, @function
console_info:
    jmp *__libc_ponti_tabella+208
    .size console_info, .-console_info

    .section .text.console_registro,"ax",@progbits
    .globl console_registro
    .type console_registro, @function
console_registro:
    jmp *__libc_ponti_tabella+212
    .size console_registro, .-console_registro

    .section .text.console_setfg,"ax",@progbits
    .globl console_setfg
    .type console_setfg, @function
console_setfg:
    jmp *__libc_ponti_tabella+216
    .size console_setfg, .-console_setfg

    .section .text.console_switch,"ax",@progbits
    .globl console_switch
    .type console_switch, @function
console_switch:
    jmp *__libc_ponti_tabella+220
    .size console_switch, .-console_switch

    .section .text.console_testo,"ax",@progbits
    .globl console_testo
    .type console_testo, @function
console_testo:
    jmp *__libc_ponti_tabella+224
    .size console_testo, .-console_testo

    .section .text.console_write,"ax",@progbits
    .globl console_write
    .type console_write, @function
console_write:
    jmp *__libc_ponti_tabella+228
    .size console_write, .-console_write

    .section .text.cpu_info,"ax",@progbits
    .globl cpu_info
    .type cpu_info, @function
cpu_info:
    jmp *__libc_ponti_tabella+232
    .size cpu_info, .-cpu_info

    .section .text.ctime,"ax",@progbits
    .globl ctime
    .type ctime, @function
ctime:
    jmp *__libc_ponti_tabella+236
    .size ctime, .-ctime

    .section .text.difftime,"ax",@progbits
    .globl difftime
    .type difftime, @function
difftime:
    jmp *__libc_ponti_tabella+240
    .size difftime, .-difftime

    .section .text.diskinfo,"ax",@progbits
    .globl diskinfo
    .type diskinfo, @function
diskinfo:
    jmp *__libc_ponti_tabella+244
    .size diskinfo, .-diskinfo

    .section .text.div,"ax",@progbits
    .globl div
    .type div, @function
div:
    jmp *__libc_ponti_tabella+248
    .size div, .-div

    .section .text.diventa_root,"ax",@progbits
    .globl diventa_root
    .type diventa_root, @function
diventa_root:
    jmp *__libc_ponti_tabella+252
    .size diventa_root, .-diventa_root

    .section .text.dma_alloc,"ax",@progbits
    .globl dma_alloc
    .type dma_alloc, @function
dma_alloc:
    jmp *__libc_ponti_tabella+256
    .size dma_alloc, .-dma_alloc

    .section .text.dup,"ax",@progbits
    .globl dup
    .type dup, @function
dup:
    jmp *__libc_ponti_tabella+260
    .size dup, .-dup

    .section .text.dup2,"ax",@progbits
    .globl dup2
    .type dup2, @function
dup2:
    jmp *__libc_ponti_tabella+264
    .size dup2, .-dup2

    .section .text.execv,"ax",@progbits
    .globl execv
    .type execv, @function
execv:
    jmp *__libc_ponti_tabella+268
    .size execv, .-execv

    .section .text.execvp,"ax",@progbits
    .globl execvp
    .type execvp, @function
execvp:
    jmp *__libc_ponti_tabella+272
    .size execvp, .-execvp

    .section .text.exit,"ax",@progbits
    .globl exit
    .type exit, @function
exit:
    jmp *__libc_ponti_tabella+276
    .size exit, .-exit

    .section .text.fabs,"ax",@progbits
    .globl fabs
    .type fabs, @function
fabs:
    jmp *__libc_ponti_tabella+280
    .size fabs, .-fabs

    .section .text.fb_map,"ax",@progbits
    .globl fb_map
    .type fb_map, @function
fb_map:
    jmp *__libc_ponti_tabella+284
    .size fb_map, .-fb_map

    .section .text.fchmod,"ax",@progbits
    .globl fchmod
    .type fchmod, @function
fchmod:
    jmp *__libc_ponti_tabella+288
    .size fchmod, .-fchmod

    .section .text.fclose,"ax",@progbits
    .globl fclose
    .type fclose, @function
fclose:
    jmp *__libc_ponti_tabella+292
    .size fclose, .-fclose

    .section .text.fcntl,"ax",@progbits
    .globl fcntl
    .type fcntl, @function
fcntl:
    jmp *__libc_ponti_tabella+296
    .size fcntl, .-fcntl

    .section .text.fdopen,"ax",@progbits
    .globl fdopen
    .type fdopen, @function
fdopen:
    jmp *__libc_ponti_tabella+300
    .size fdopen, .-fdopen

    .section .text.fdprova,"ax",@progbits
    .globl fdprova
    .type fdprova, @function
fdprova:
    jmp *__libc_ponti_tabella+304
    .size fdprova, .-fdprova

    .section .text.feof,"ax",@progbits
    .globl feof
    .type feof, @function
feof:
    jmp *__libc_ponti_tabella+308
    .size feof, .-feof

    .section .text.ferror,"ax",@progbits
    .globl ferror
    .type ferror, @function
ferror:
    jmp *__libc_ponti_tabella+312
    .size ferror, .-ferror

    .section .text.fflush,"ax",@progbits
    .globl fflush
    .type fflush, @function
fflush:
    jmp *__libc_ponti_tabella+316
    .size fflush, .-fflush

    .section .text.fgetc,"ax",@progbits
    .globl fgetc
    .type fgetc, @function
fgetc:
    jmp *__libc_ponti_tabella+320
    .size fgetc, .-fgetc

    .section .text.fgetpos,"ax",@progbits
    .globl fgetpos
    .type fgetpos, @function
fgetpos:
    jmp *__libc_ponti_tabella+324
    .size fgetpos, .-fgetpos

    .section .text.fgets,"ax",@progbits
    .globl fgets
    .type fgets, @function
fgets:
    jmp *__libc_ponti_tabella+328
    .size fgets, .-fgets

    .section .text.fileno,"ax",@progbits
    .globl fileno
    .type fileno, @function
fileno:
    jmp *__libc_ponti_tabella+332
    .size fileno, .-fileno

    .section .text.fopen,"ax",@progbits
    .globl fopen
    .type fopen, @function
fopen:
    jmp *__libc_ponti_tabella+336
    .size fopen, .-fopen

    .section .text.fpathconf,"ax",@progbits
    .globl fpathconf
    .type fpathconf, @function
fpathconf:
    jmp *__libc_ponti_tabella+340
    .size fpathconf, .-fpathconf

    .section .text.fprintf,"ax",@progbits
    .globl fprintf
    .type fprintf, @function
fprintf:
    jmp *__libc_ponti_tabella+344
    .size fprintf, .-fprintf

    .section .text.fputc,"ax",@progbits
    .globl fputc
    .type fputc, @function
fputc:
    jmp *__libc_ponti_tabella+348
    .size fputc, .-fputc

    .section .text.fputs,"ax",@progbits
    .globl fputs
    .type fputs, @function
fputs:
    jmp *__libc_ponti_tabella+352
    .size fputs, .-fputs

    .section .text.fread,"ax",@progbits
    .globl fread
    .type fread, @function
fread:
    jmp *__libc_ponti_tabella+356
    .size fread, .-fread

    .section .text.free,"ax",@progbits
    .globl free
    .type free, @function
free:
    jmp *__libc_ponti_tabella+360
    .size free, .-free

    .section .text.freopen,"ax",@progbits
    .globl freopen
    .type freopen, @function
freopen:
    jmp *__libc_ponti_tabella+364
    .size freopen, .-freopen

    .section .text.frexp,"ax",@progbits
    .globl frexp
    .type frexp, @function
frexp:
    jmp *__libc_ponti_tabella+368
    .size frexp, .-frexp

    .section .text.fscanf,"ax",@progbits
    .globl fscanf
    .type fscanf, @function
fscanf:
    jmp *__libc_ponti_tabella+372
    .size fscanf, .-fscanf

    .section .text.fseek,"ax",@progbits
    .globl fseek
    .type fseek, @function
fseek:
    jmp *__libc_ponti_tabella+376
    .size fseek, .-fseek

    .section .text.fsetpos,"ax",@progbits
    .globl fsetpos
    .type fsetpos, @function
fsetpos:
    jmp *__libc_ponti_tabella+380
    .size fsetpos, .-fsetpos

    .section .text.fsize,"ax",@progbits
    .globl fsize
    .type fsize, @function
fsize:
    jmp *__libc_ponti_tabella+384
    .size fsize, .-fsize

    .section .text.fstat,"ax",@progbits
    .globl fstat
    .type fstat, @function
fstat:
    jmp *__libc_ponti_tabella+388
    .size fstat, .-fstat

    .section .text.ftell,"ax",@progbits
    .globl ftell
    .type ftell, @function
ftell:
    jmp *__libc_ponti_tabella+392
    .size ftell, .-ftell

    .section .text.fwrite,"ax",@progbits
    .globl fwrite
    .type fwrite, @function
fwrite:
    jmp *__libc_ponti_tabella+396
    .size fwrite, .-fwrite

    .section .text.getc,"ax",@progbits
    .globl getc
    .type getc, @function
getc:
    jmp *__libc_ponti_tabella+400
    .size getc, .-getc

    .section .text.getchar,"ax",@progbits
    .globl getchar
    .type getchar, @function
getchar:
    jmp *__libc_ponti_tabella+404
    .size getchar, .-getchar

    .section .text.getconf,"ax",@progbits
    .globl getconf
    .type getconf, @function
getconf:
    jmp *__libc_ponti_tabella+408
    .size getconf, .-getconf

    .section .text.getcwd,"ax",@progbits
    .globl getcwd
    .type getcwd, @function
getcwd:
    jmp *__libc_ponti_tabella+412
    .size getcwd, .-getcwd

    .section .text.getegid,"ax",@progbits
    .globl getegid
    .type getegid, @function
getegid:
    jmp *__libc_ponti_tabella+416
    .size getegid, .-getegid

    .section .text.getentropy,"ax",@progbits
    .globl getentropy
    .type getentropy, @function
getentropy:
    jmp *__libc_ponti_tabella+420
    .size getentropy, .-getentropy

    .section .text.getenv,"ax",@progbits
    .globl getenv
    .type getenv, @function
getenv:
    jmp *__libc_ponti_tabella+424
    .size getenv, .-getenv

    .section .text.geteuid,"ax",@progbits
    .globl geteuid
    .type geteuid, @function
geteuid:
    jmp *__libc_ponti_tabella+428
    .size geteuid, .-geteuid

    .section .text.getgid,"ax",@progbits
    .globl getgid
    .type getgid, @function
getgid:
    jmp *__libc_ponti_tabella+432
    .size getgid, .-getgid

    .section .text.getpagesize,"ax",@progbits
    .globl getpagesize
    .type getpagesize, @function
getpagesize:
    jmp *__libc_ponti_tabella+436
    .size getpagesize, .-getpagesize

    .section .text.getpid,"ax",@progbits
    .globl getpid
    .type getpid, @function
getpid:
    jmp *__libc_ponti_tabella+440
    .size getpid, .-getpid

    .section .text.getrandom,"ax",@progbits
    .globl getrandom
    .type getrandom, @function
getrandom:
    jmp *__libc_ponti_tabella+444
    .size getrandom, .-getrandom

    .section .text.getrusage,"ax",@progbits
    .globl getrusage
    .type getrusage, @function
getrusage:
    jmp *__libc_ponti_tabella+448
    .size getrusage, .-getrusage

    .section .text.gets,"ax",@progbits
    .globl gets
    .type gets, @function
gets:
    jmp *__libc_ponti_tabella+452
    .size gets, .-gets

    .section .text.gettimeofday,"ax",@progbits
    .globl gettimeofday
    .type gettimeofday, @function
gettimeofday:
    jmp *__libc_ponti_tabella+456
    .size gettimeofday, .-gettimeofday

    .section .text.getuid,"ax",@progbits
    .globl getuid
    .type getuid, @function
getuid:
    jmp *__libc_ponti_tabella+460
    .size getuid, .-getuid

    .section .text.gmtime,"ax",@progbits
    .globl gmtime
    .type gmtime, @function
gmtime:
    jmp *__libc_ponti_tabella+464
    .size gmtime, .-gmtime

    .section .text.gmtime_r,"ax",@progbits
    .globl gmtime_r
    .type gmtime_r, @function
gmtime_r:
    jmp *__libc_ponti_tabella+468
    .size gmtime_r, .-gmtime_r

    .section .text.imaxabs,"ax",@progbits
    .globl imaxabs
    .type imaxabs, @function
imaxabs:
    jmp *__libc_ponti_tabella+472
    .size imaxabs, .-imaxabs

    .section .text.imaxdiv,"ax",@progbits
    .globl imaxdiv
    .type imaxdiv, @function
imaxdiv:
    jmp *__libc_ponti_tabella+476
    .size imaxdiv, .-imaxdiv

    .section .text.interrompi,"ax",@progbits
    .globl interrompi
    .type interrompi, @function
interrompi:
    jmp *__libc_ponti_tabella+480
    .size interrompi, .-interrompi

    .section .text.ioctl,"ax",@progbits
    .globl ioctl
    .type ioctl, @function
ioctl:
    jmp *__libc_ponti_tabella+484
    .size ioctl, .-ioctl

    .section .text.ioport_bind,"ax",@progbits
    .globl ioport_bind
    .type ioport_bind, @function
ioport_bind:
    jmp *__libc_ponti_tabella+488
    .size ioport_bind, .-ioport_bind

    .section .text.ioport_in,"ax",@progbits
    .globl ioport_in
    .type ioport_in, @function
ioport_in:
    jmp *__libc_ponti_tabella+492
    .size ioport_in, .-ioport_in

    .section .text.ioport_in16,"ax",@progbits
    .globl ioport_in16
    .type ioport_in16, @function
ioport_in16:
    jmp *__libc_ponti_tabella+496
    .size ioport_in16, .-ioport_in16

    .section .text.ioport_in32,"ax",@progbits
    .globl ioport_in32
    .type ioport_in32, @function
ioport_in32:
    jmp *__libc_ponti_tabella+500
    .size ioport_in32, .-ioport_in32

    .section .text.ioport_out,"ax",@progbits
    .globl ioport_out
    .type ioport_out, @function
ioport_out:
    jmp *__libc_ponti_tabella+504
    .size ioport_out, .-ioport_out

    .section .text.ioport_out16,"ax",@progbits
    .globl ioport_out16
    .type ioport_out16, @function
ioport_out16:
    jmp *__libc_ponti_tabella+508
    .size ioport_out16, .-ioport_out16

    .section .text.ioport_out32,"ax",@progbits
    .globl ioport_out32
    .type ioport_out32, @function
ioport_out32:
    jmp *__libc_ponti_tabella+512
    .size ioport_out32, .-ioport_out32

    .section .text.ipc_attendi,"ax",@progbits
    .globl ipc_attendi
    .type ipc_attendi, @function
ipc_attendi:
    jmp *__libc_ponti_tabella+516
    .size ipc_attendi, .-ipc_attendi

    .section .text.ipc_lookup,"ax",@progbits
    .globl ipc_lookup
    .type ipc_lookup, @function
ipc_lookup:
    jmp *__libc_ponti_tabella+520
    .size ipc_lookup, .-ipc_lookup

    .section .text.ipc_pronto,"ax",@progbits
    .globl ipc_pronto
    .type ipc_pronto, @function
ipc_pronto:
    jmp *__libc_ponti_tabella+524
    .size ipc_pronto, .-ipc_pronto

    .section .text.ipc_recv,"ax",@progbits
    .globl ipc_recv
    .type ipc_recv, @function
ipc_recv:
    jmp *__libc_ponti_tabella+528
    .size ipc_recv, .-ipc_recv

    .section .text.ipc_recv_timeout,"ax",@progbits
    .globl ipc_recv_timeout
    .type ipc_recv_timeout, @function
ipc_recv_timeout:
    jmp *__libc_ponti_tabella+532
    .size ipc_recv_timeout, .-ipc_recv_timeout

    .section .text.ipc_register,"ax",@progbits
    .globl ipc_register
    .type ipc_register, @function
ipc_register:
    jmp *__libc_ponti_tabella+536
    .size ipc_register, .-ipc_register

    .section .text.ipc_rimetti,"ax",@progbits
    .globl ipc_rimetti
    .type ipc_rimetti, @function
ipc_rimetti:
    jmp *__libc_ponti_tabella+540
    .size ipc_rimetti, .-ipc_rimetti

    .section .text.ipc_scegli,"ax",@progbits
    .globl ipc_scegli
    .type ipc_scegli, @function
ipc_scegli:
    jmp *__libc_ponti_tabella+544
    .size ipc_scegli, .-ipc_scegli

    .section .text.ipc_send,"ax",@progbits
    .globl ipc_send
    .type ipc_send, @function
ipc_send:
    jmp *__libc_ponti_tabella+548
    .size ipc_send, .-ipc_send

    .section .text.irq_bind,"ax",@progbits
    .globl irq_bind
    .type irq_bind, @function
irq_bind:
    jmp *__libc_ponti_tabella+552
    .size irq_bind, .-irq_bind

    .section .text.irq_done,"ax",@progbits
    .globl irq_done
    .type irq_done, @function
irq_done:
    jmp *__libc_ponti_tabella+556
    .size irq_done, .-irq_done

    .section .text.irq_unbind,"ax",@progbits
    .globl irq_unbind
    .type irq_unbind, @function
irq_unbind:
    jmp *__libc_ponti_tabella+560
    .size irq_unbind, .-irq_unbind

    .section .text.isalnum,"ax",@progbits
    .globl isalnum
    .type isalnum, @function
isalnum:
    jmp *__libc_ponti_tabella+564
    .size isalnum, .-isalnum

    .section .text.isalpha,"ax",@progbits
    .globl isalpha
    .type isalpha, @function
isalpha:
    jmp *__libc_ponti_tabella+568
    .size isalpha, .-isalpha

    .section .text.isascii,"ax",@progbits
    .globl isascii
    .type isascii, @function
isascii:
    jmp *__libc_ponti_tabella+572
    .size isascii, .-isascii

    .section .text.isatty,"ax",@progbits
    .globl isatty
    .type isatty, @function
isatty:
    jmp *__libc_ponti_tabella+576
    .size isatty, .-isatty

    .section .text.isblank,"ax",@progbits
    .globl isblank
    .type isblank, @function
isblank:
    jmp *__libc_ponti_tabella+580
    .size isblank, .-isblank

    .section .text.iscntrl,"ax",@progbits
    .globl iscntrl
    .type iscntrl, @function
iscntrl:
    jmp *__libc_ponti_tabella+584
    .size iscntrl, .-iscntrl

    .section .text.isdigit,"ax",@progbits
    .globl isdigit
    .type isdigit, @function
isdigit:
    jmp *__libc_ponti_tabella+588
    .size isdigit, .-isdigit

    .section .text.isgraph,"ax",@progbits
    .globl isgraph
    .type isgraph, @function
isgraph:
    jmp *__libc_ponti_tabella+592
    .size isgraph, .-isgraph

    .section .text.islower,"ax",@progbits
    .globl islower
    .type islower, @function
islower:
    jmp *__libc_ponti_tabella+596
    .size islower, .-islower

    .section .text.isprint,"ax",@progbits
    .globl isprint
    .type isprint, @function
isprint:
    jmp *__libc_ponti_tabella+600
    .size isprint, .-isprint

    .section .text.ispunct,"ax",@progbits
    .globl ispunct
    .type ispunct, @function
ispunct:
    jmp *__libc_ponti_tabella+604
    .size ispunct, .-ispunct

    .section .text.isspace,"ax",@progbits
    .globl isspace
    .type isspace, @function
isspace:
    jmp *__libc_ponti_tabella+608
    .size isspace, .-isspace

    .section .text.isupper,"ax",@progbits
    .globl isupper
    .type isupper, @function
isupper:
    jmp *__libc_ponti_tabella+612
    .size isupper, .-isupper

    .section .text.iswalnum,"ax",@progbits
    .globl iswalnum
    .type iswalnum, @function
iswalnum:
    jmp *__libc_ponti_tabella+616
    .size iswalnum, .-iswalnum

    .section .text.iswalpha,"ax",@progbits
    .globl iswalpha
    .type iswalpha, @function
iswalpha:
    jmp *__libc_ponti_tabella+620
    .size iswalpha, .-iswalpha

    .section .text.iswblank,"ax",@progbits
    .globl iswblank
    .type iswblank, @function
iswblank:
    jmp *__libc_ponti_tabella+624
    .size iswblank, .-iswblank

    .section .text.iswcntrl,"ax",@progbits
    .globl iswcntrl
    .type iswcntrl, @function
iswcntrl:
    jmp *__libc_ponti_tabella+628
    .size iswcntrl, .-iswcntrl

    .section .text.iswdigit,"ax",@progbits
    .globl iswdigit
    .type iswdigit, @function
iswdigit:
    jmp *__libc_ponti_tabella+632
    .size iswdigit, .-iswdigit

    .section .text.iswgraph,"ax",@progbits
    .globl iswgraph
    .type iswgraph, @function
iswgraph:
    jmp *__libc_ponti_tabella+636
    .size iswgraph, .-iswgraph

    .section .text.iswlower,"ax",@progbits
    .globl iswlower
    .type iswlower, @function
iswlower:
    jmp *__libc_ponti_tabella+640
    .size iswlower, .-iswlower

    .section .text.iswprint,"ax",@progbits
    .globl iswprint
    .type iswprint, @function
iswprint:
    jmp *__libc_ponti_tabella+644
    .size iswprint, .-iswprint

    .section .text.iswpunct,"ax",@progbits
    .globl iswpunct
    .type iswpunct, @function
iswpunct:
    jmp *__libc_ponti_tabella+648
    .size iswpunct, .-iswpunct

    .section .text.iswspace,"ax",@progbits
    .globl iswspace
    .type iswspace, @function
iswspace:
    jmp *__libc_ponti_tabella+652
    .size iswspace, .-iswspace

    .section .text.iswupper,"ax",@progbits
    .globl iswupper
    .type iswupper, @function
iswupper:
    jmp *__libc_ponti_tabella+656
    .size iswupper, .-iswupper

    .section .text.iswxdigit,"ax",@progbits
    .globl iswxdigit
    .type iswxdigit, @function
iswxdigit:
    jmp *__libc_ponti_tabella+660
    .size iswxdigit, .-iswxdigit

    .section .text.isxdigit,"ax",@progbits
    .globl isxdigit
    .type isxdigit, @function
isxdigit:
    jmp *__libc_ponti_tabella+664
    .size isxdigit, .-isxdigit

    .section .text.kbprova,"ax",@progbits
    .globl kbprova
    .type kbprova, @function
kbprova:
    jmp *__libc_ponti_tabella+668
    .size kbprova, .-kbprova

    .section .text.kbstato,"ax",@progbits
    .globl kbstato
    .type kbstato, @function
kbstato:
    jmp *__libc_ponti_tabella+672
    .size kbstato, .-kbstato

    .section .text.klog_leggi,"ax",@progbits
    .globl klog_leggi
    .type klog_leggi, @function
klog_leggi:
    jmp *__libc_ponti_tabella+676
    .size klog_leggi, .-klog_leggi

    .section .text.labs,"ax",@progbits
    .globl labs
    .type labs, @function
labs:
    jmp *__libc_ponti_tabella+680
    .size labs, .-labs

    .section .text.ldexp,"ax",@progbits
    .globl ldexp
    .type ldexp, @function
ldexp:
    jmp *__libc_ponti_tabella+684
    .size ldexp, .-ldexp

    .section .text.ldiv,"ax",@progbits
    .globl ldiv
    .type ldiv, @function
ldiv:
    jmp *__libc_ponti_tabella+688
    .size ldiv, .-ldiv

    .section .text.listdir,"ax",@progbits
    .globl listdir
    .type listdir, @function
listdir:
    jmp *__libc_ponti_tabella+692
    .size listdir, .-listdir

    .section .text.listdir_from,"ax",@progbits
    .globl listdir_from
    .type listdir_from, @function
listdir_from:
    jmp *__libc_ponti_tabella+696
    .size listdir_from, .-listdir_from

    .section .text.llabs,"ax",@progbits
    .globl llabs
    .type llabs, @function
llabs:
    jmp *__libc_ponti_tabella+700
    .size llabs, .-llabs

    .section .text.lldiv,"ax",@progbits
    .globl lldiv
    .type lldiv, @function
lldiv:
    jmp *__libc_ponti_tabella+704
    .size lldiv, .-lldiv

    .section .text.localeconv,"ax",@progbits
    .globl localeconv
    .type localeconv, @function
localeconv:
    jmp *__libc_ponti_tabella+708
    .size localeconv, .-localeconv

    .section .text.localtime,"ax",@progbits
    .globl localtime
    .type localtime, @function
localtime:
    jmp *__libc_ponti_tabella+712
    .size localtime, .-localtime

    .section .text.localtime_r,"ax",@progbits
    .globl localtime_r
    .type localtime_r, @function
localtime_r:
    jmp *__libc_ponti_tabella+716
    .size localtime_r, .-localtime_r

    .section .text.log_seriale,"ax",@progbits
    .globl log_seriale
    .type log_seriale, @function
log_seriale:
    jmp *__libc_ponti_tabella+720
    .size log_seriale, .-log_seriale

    .section .text.longjmp,"ax",@progbits
    .globl longjmp
    .type longjmp, @function
longjmp:
    jmp *__libc_ponti_tabella+724
    .size longjmp, .-longjmp

    .section .text.lseek,"ax",@progbits
    .globl lseek
    .type lseek, @function
lseek:
    jmp *__libc_ponti_tabella+728
    .size lseek, .-lseek

    .section .text.lstat,"ax",@progbits
    .globl lstat
    .type lstat, @function
lstat:
    jmp *__libc_ponti_tabella+732
    .size lstat, .-lstat

    .section .text.malloc,"ax",@progbits
    .globl malloc
    .type malloc, @function
malloc:
    jmp *__libc_ponti_tabella+736
    .size malloc, .-malloc

    .section .text.malloc_usable_size,"ax",@progbits
    .globl malloc_usable_size
    .type malloc_usable_size, @function
malloc_usable_size:
    jmp *__libc_ponti_tabella+740
    .size malloc_usable_size, .-malloc_usable_size

    .section .text.mblen,"ax",@progbits
    .globl mblen
    .type mblen, @function
mblen:
    jmp *__libc_ponti_tabella+744
    .size mblen, .-mblen

    .section .text.mbrtowc,"ax",@progbits
    .globl mbrtowc
    .type mbrtowc, @function
mbrtowc:
    jmp *__libc_ponti_tabella+748
    .size mbrtowc, .-mbrtowc

    .section .text.mbstowcs,"ax",@progbits
    .globl mbstowcs
    .type mbstowcs, @function
mbstowcs:
    jmp *__libc_ponti_tabella+752
    .size mbstowcs, .-mbstowcs

    .section .text.mbtowc,"ax",@progbits
    .globl mbtowc
    .type mbtowc, @function
mbtowc:
    jmp *__libc_ponti_tabella+756
    .size mbtowc, .-mbtowc

    .section .text.memalign,"ax",@progbits
    .globl memalign
    .type memalign, @function
memalign:
    jmp *__libc_ponti_tabella+760
    .size memalign, .-memalign

    .section .text.memchr,"ax",@progbits
    .globl memchr
    .type memchr, @function
memchr:
    jmp *__libc_ponti_tabella+764
    .size memchr, .-memchr

    .section .text.memcmp,"ax",@progbits
    .globl memcmp
    .type memcmp, @function
memcmp:
    jmp *__libc_ponti_tabella+768
    .size memcmp, .-memcmp

    .section .text.memcpy,"ax",@progbits
    .globl memcpy
    .type memcpy, @function
memcpy:
    jmp *__libc_ponti_tabella+772
    .size memcpy, .-memcpy

    .section .text.meminfo,"ax",@progbits
    .globl meminfo
    .type meminfo, @function
meminfo:
    jmp *__libc_ponti_tabella+776
    .size meminfo, .-meminfo

    .section .text.memmove,"ax",@progbits
    .globl memmove
    .type memmove, @function
memmove:
    jmp *__libc_ponti_tabella+780
    .size memmove, .-memmove

    .section .text.memset,"ax",@progbits
    .globl memset
    .type memset, @function
memset:
    jmp *__libc_ponti_tabella+784
    .size memset, .-memset

    .section .text.mkdir,"ax",@progbits
    .globl mkdir
    .type mkdir, @function
mkdir:
    jmp *__libc_ponti_tabella+788
    .size mkdir, .-mkdir

    .section .text.mkstemp,"ax",@progbits
    .globl mkstemp
    .type mkstemp, @function
mkstemp:
    jmp *__libc_ponti_tabella+792
    .size mkstemp, .-mkstemp

    .section .text.mktemp,"ax",@progbits
    .globl mktemp
    .type mktemp, @function
mktemp:
    jmp *__libc_ponti_tabella+796
    .size mktemp, .-mktemp

    .section .text.mktime,"ax",@progbits
    .globl mktime
    .type mktime, @function
mktime:
    jmp *__libc_ponti_tabella+800
    .size mktime, .-mktime

    .section .text.mmap,"ax",@progbits
    .globl mmap
    .type mmap, @function
mmap:
    jmp *__libc_ponti_tabella+804
    .size mmap, .-mmap

    .section .text.mmio_map,"ax",@progbits
    .globl mmio_map
    .type mmio_map, @function
mmio_map:
    jmp *__libc_ponti_tabella+808
    .size mmio_map, .-mmio_map

    .section .text.modo_testo,"ax",@progbits
    .globl modo_testo
    .type modo_testo, @function
modo_testo:
    jmp *__libc_ponti_tabella+812
    .size modo_testo, .-modo_testo

    .section .text.mount,"ax",@progbits
    .globl mount
    .type mount, @function
mount:
    jmp *__libc_ponti_tabella+816
    .size mount, .-mount

    .section .text.mountinfo,"ax",@progbits
    .globl mountinfo
    .type mountinfo, @function
mountinfo:
    jmp *__libc_ponti_tabella+820
    .size mountinfo, .-mountinfo

    .section .text.munmap,"ax",@progbits
    .globl munmap
    .type munmap, @function
munmap:
    jmp *__libc_ponti_tabella+824
    .size munmap, .-munmap

    .section .text.mutex_lascia,"ax",@progbits
    .globl mutex_lascia
    .type mutex_lascia, @function
mutex_lascia:
    jmp *__libc_ponti_tabella+828
    .size mutex_lascia, .-mutex_lascia

    .section .text.mutex_prendi,"ax",@progbits
    .globl mutex_prendi
    .type mutex_prendi, @function
mutex_prendi:
    jmp *__libc_ponti_tabella+832
    .size mutex_prendi, .-mutex_prendi

    .section .text.mutex_prova,"ax",@progbits
    .globl mutex_prova
    .type mutex_prova, @function
mutex_prova:
    jmp *__libc_ponti_tabella+836
    .size mutex_prova, .-mutex_prova

    .section .text.nanosleep,"ax",@progbits
    .globl nanosleep
    .type nanosleep, @function
nanosleep:
    jmp *__libc_ponti_tabella+840
    .size nanosleep, .-nanosleep

    .section .text.nome_utente,"ax",@progbits
    .globl nome_utente
    .type nome_utente, @function
nome_utente:
    jmp *__libc_ponti_tabella+844
    .size nome_utente, .-nome_utente

    .section .text.open,"ax",@progbits
    .globl open
    .type open, @function
open:
    jmp *__libc_ponti_tabella+848
    .size open, .-open

    .section .text.opendir,"ax",@progbits
    .globl opendir
    .type opendir, @function
opendir:
    jmp *__libc_ponti_tabella+852
    .size opendir, .-opendir

    .section .text.osversion,"ax",@progbits
    .globl osversion
    .type osversion, @function
osversion:
    jmp *__libc_ponti_tabella+856
    .size osversion, .-osversion

    .section .text.partwrite,"ax",@progbits
    .globl partwrite
    .type partwrite, @function
partwrite:
    jmp *__libc_ponti_tabella+860
    .size partwrite, .-partwrite

    .section .text.pathconf,"ax",@progbits
    .globl pathconf
    .type pathconf, @function
pathconf:
    jmp *__libc_ponti_tabella+864
    .size pathconf, .-pathconf

    .section .text.pclose,"ax",@progbits
    .globl pclose
    .type pclose, @function
pclose:
    jmp *__libc_ponti_tabella+868
    .size pclose, .-pclose

    .section .text.perror,"ax",@progbits
    .globl perror
    .type perror, @function
perror:
    jmp *__libc_ponti_tabella+872
    .size perror, .-perror

    .section .text.pipe,"ax",@progbits
    .globl pipe
    .type pipe, @function
pipe:
    jmp *__libc_ponti_tabella+876
    .size pipe, .-pipe

    .section .text.poll,"ax",@progbits
    .globl poll
    .type poll, @function
poll:
    jmp *__libc_ponti_tabella+880
    .size poll, .-poll

    .section .text.popen,"ax",@progbits
    .globl popen
    .type popen, @function
popen:
    jmp *__libc_ponti_tabella+884
    .size popen, .-popen

    .section .text.posix_memalign,"ax",@progbits
    .globl posix_memalign
    .type posix_memalign, @function
posix_memalign:
    jmp *__libc_ponti_tabella+888
    .size posix_memalign, .-posix_memalign

    .section .text.printf,"ax",@progbits
    .globl printf
    .type printf, @function
printf:
    jmp *__libc_ponti_tabella+892
    .size printf, .-printf

    .section .text.proc_gruppo,"ax",@progbits
    .globl proc_gruppo
    .type proc_gruppo, @function
proc_gruppo:
    jmp *__libc_ponti_tabella+896
    .size proc_gruppo, .-proc_gruppo

    .section .text.procinfo,"ax",@progbits
    .globl procinfo
    .type procinfo, @function
procinfo:
    jmp *__libc_ponti_tabella+900
    .size procinfo, .-procinfo

    .section .text.pthread_attr_destroy,"ax",@progbits
    .globl pthread_attr_destroy
    .type pthread_attr_destroy, @function
pthread_attr_destroy:
    jmp *__libc_ponti_tabella+904
    .size pthread_attr_destroy, .-pthread_attr_destroy

    .section .text.pthread_attr_getdetachstate,"ax",@progbits
    .globl pthread_attr_getdetachstate
    .type pthread_attr_getdetachstate, @function
pthread_attr_getdetachstate:
    jmp *__libc_ponti_tabella+908
    .size pthread_attr_getdetachstate, .-pthread_attr_getdetachstate

    .section .text.pthread_attr_getschedparam,"ax",@progbits
    .globl pthread_attr_getschedparam
    .type pthread_attr_getschedparam, @function
pthread_attr_getschedparam:
    jmp *__libc_ponti_tabella+912
    .size pthread_attr_getschedparam, .-pthread_attr_getschedparam

    .section .text.pthread_attr_getschedpolicy,"ax",@progbits
    .globl pthread_attr_getschedpolicy
    .type pthread_attr_getschedpolicy, @function
pthread_attr_getschedpolicy:
    jmp *__libc_ponti_tabella+916
    .size pthread_attr_getschedpolicy, .-pthread_attr_getschedpolicy

    .section .text.pthread_attr_getstack,"ax",@progbits
    .globl pthread_attr_getstack
    .type pthread_attr_getstack, @function
pthread_attr_getstack:
    jmp *__libc_ponti_tabella+920
    .size pthread_attr_getstack, .-pthread_attr_getstack

    .section .text.pthread_attr_getstacksize,"ax",@progbits
    .globl pthread_attr_getstacksize
    .type pthread_attr_getstacksize, @function
pthread_attr_getstacksize:
    jmp *__libc_ponti_tabella+924
    .size pthread_attr_getstacksize, .-pthread_attr_getstacksize

    .section .text.pthread_attr_init,"ax",@progbits
    .globl pthread_attr_init
    .type pthread_attr_init, @function
pthread_attr_init:
    jmp *__libc_ponti_tabella+928
    .size pthread_attr_init, .-pthread_attr_init

    .section .text.pthread_attr_setdetachstate,"ax",@progbits
    .globl pthread_attr_setdetachstate
    .type pthread_attr_setdetachstate, @function
pthread_attr_setdetachstate:
    jmp *__libc_ponti_tabella+932
    .size pthread_attr_setdetachstate, .-pthread_attr_setdetachstate

    .section .text.pthread_attr_setguardsize,"ax",@progbits
    .globl pthread_attr_setguardsize
    .type pthread_attr_setguardsize, @function
pthread_attr_setguardsize:
    jmp *__libc_ponti_tabella+936
    .size pthread_attr_setguardsize, .-pthread_attr_setguardsize

    .section .text.pthread_attr_setinheritsched,"ax",@progbits
    .globl pthread_attr_setinheritsched
    .type pthread_attr_setinheritsched, @function
pthread_attr_setinheritsched:
    jmp *__libc_ponti_tabella+940
    .size pthread_attr_setinheritsched, .-pthread_attr_setinheritsched

    .section .text.pthread_attr_setschedparam,"ax",@progbits
    .globl pthread_attr_setschedparam
    .type pthread_attr_setschedparam, @function
pthread_attr_setschedparam:
    jmp *__libc_ponti_tabella+944
    .size pthread_attr_setschedparam, .-pthread_attr_setschedparam

    .section .text.pthread_attr_setschedpolicy,"ax",@progbits
    .globl pthread_attr_setschedpolicy
    .type pthread_attr_setschedpolicy, @function
pthread_attr_setschedpolicy:
    jmp *__libc_ponti_tabella+948
    .size pthread_attr_setschedpolicy, .-pthread_attr_setschedpolicy

    .section .text.pthread_attr_setscope,"ax",@progbits
    .globl pthread_attr_setscope
    .type pthread_attr_setscope, @function
pthread_attr_setscope:
    jmp *__libc_ponti_tabella+952
    .size pthread_attr_setscope, .-pthread_attr_setscope

    .section .text.pthread_attr_setstacksize,"ax",@progbits
    .globl pthread_attr_setstacksize
    .type pthread_attr_setstacksize, @function
pthread_attr_setstacksize:
    jmp *__libc_ponti_tabella+956
    .size pthread_attr_setstacksize, .-pthread_attr_setstacksize

    .section .text.pthread_cond_broadcast,"ax",@progbits
    .globl pthread_cond_broadcast
    .type pthread_cond_broadcast, @function
pthread_cond_broadcast:
    jmp *__libc_ponti_tabella+960
    .size pthread_cond_broadcast, .-pthread_cond_broadcast

    .section .text.pthread_cond_destroy,"ax",@progbits
    .globl pthread_cond_destroy
    .type pthread_cond_destroy, @function
pthread_cond_destroy:
    jmp *__libc_ponti_tabella+964
    .size pthread_cond_destroy, .-pthread_cond_destroy

    .section .text.pthread_cond_init,"ax",@progbits
    .globl pthread_cond_init
    .type pthread_cond_init, @function
pthread_cond_init:
    jmp *__libc_ponti_tabella+968
    .size pthread_cond_init, .-pthread_cond_init

    .section .text.pthread_cond_signal,"ax",@progbits
    .globl pthread_cond_signal
    .type pthread_cond_signal, @function
pthread_cond_signal:
    jmp *__libc_ponti_tabella+972
    .size pthread_cond_signal, .-pthread_cond_signal

    .section .text.pthread_cond_timedwait,"ax",@progbits
    .globl pthread_cond_timedwait
    .type pthread_cond_timedwait, @function
pthread_cond_timedwait:
    jmp *__libc_ponti_tabella+976
    .size pthread_cond_timedwait, .-pthread_cond_timedwait

    .section .text.pthread_cond_wait,"ax",@progbits
    .globl pthread_cond_wait
    .type pthread_cond_wait, @function
pthread_cond_wait:
    jmp *__libc_ponti_tabella+980
    .size pthread_cond_wait, .-pthread_cond_wait

    .section .text.pthread_condattr_destroy,"ax",@progbits
    .globl pthread_condattr_destroy
    .type pthread_condattr_destroy, @function
pthread_condattr_destroy:
    jmp *__libc_ponti_tabella+984
    .size pthread_condattr_destroy, .-pthread_condattr_destroy

    .section .text.pthread_condattr_getclock,"ax",@progbits
    .globl pthread_condattr_getclock
    .type pthread_condattr_getclock, @function
pthread_condattr_getclock:
    jmp *__libc_ponti_tabella+988
    .size pthread_condattr_getclock, .-pthread_condattr_getclock

    .section .text.pthread_condattr_init,"ax",@progbits
    .globl pthread_condattr_init
    .type pthread_condattr_init, @function
pthread_condattr_init:
    jmp *__libc_ponti_tabella+992
    .size pthread_condattr_init, .-pthread_condattr_init

    .section .text.pthread_condattr_setclock,"ax",@progbits
    .globl pthread_condattr_setclock
    .type pthread_condattr_setclock, @function
pthread_condattr_setclock:
    jmp *__libc_ponti_tabella+996
    .size pthread_condattr_setclock, .-pthread_condattr_setclock

    .section .text.pthread_create,"ax",@progbits
    .globl pthread_create
    .type pthread_create, @function
pthread_create:
    jmp *__libc_ponti_tabella+1000
    .size pthread_create, .-pthread_create

    .section .text.pthread_detach,"ax",@progbits
    .globl pthread_detach
    .type pthread_detach, @function
pthread_detach:
    jmp *__libc_ponti_tabella+1004
    .size pthread_detach, .-pthread_detach

    .section .text.pthread_equal,"ax",@progbits
    .globl pthread_equal
    .type pthread_equal, @function
pthread_equal:
    jmp *__libc_ponti_tabella+1008
    .size pthread_equal, .-pthread_equal

    .section .text.pthread_exit,"ax",@progbits
    .globl pthread_exit
    .type pthread_exit, @function
pthread_exit:
    jmp *__libc_ponti_tabella+1012
    .size pthread_exit, .-pthread_exit

    .section .text.pthread_getattr_np,"ax",@progbits
    .globl pthread_getattr_np
    .type pthread_getattr_np, @function
pthread_getattr_np:
    jmp *__libc_ponti_tabella+1016
    .size pthread_getattr_np, .-pthread_getattr_np

    .section .text.pthread_getname_np,"ax",@progbits
    .globl pthread_getname_np
    .type pthread_getname_np, @function
pthread_getname_np:
    jmp *__libc_ponti_tabella+1020
    .size pthread_getname_np, .-pthread_getname_np

    .section .text.pthread_getschedparam,"ax",@progbits
    .globl pthread_getschedparam
    .type pthread_getschedparam, @function
pthread_getschedparam:
    jmp *__libc_ponti_tabella+1024
    .size pthread_getschedparam, .-pthread_getschedparam

    .section .text.pthread_getspecific,"ax",@progbits
    .globl pthread_getspecific
    .type pthread_getspecific, @function
pthread_getspecific:
    jmp *__libc_ponti_tabella+1028
    .size pthread_getspecific, .-pthread_getspecific

    .section .text.pthread_join,"ax",@progbits
    .globl pthread_join
    .type pthread_join, @function
pthread_join:
    jmp *__libc_ponti_tabella+1032
    .size pthread_join, .-pthread_join

    .section .text.pthread_key_create,"ax",@progbits
    .globl pthread_key_create
    .type pthread_key_create, @function
pthread_key_create:
    jmp *__libc_ponti_tabella+1036
    .size pthread_key_create, .-pthread_key_create

    .section .text.pthread_key_delete,"ax",@progbits
    .globl pthread_key_delete
    .type pthread_key_delete, @function
pthread_key_delete:
    jmp *__libc_ponti_tabella+1040
    .size pthread_key_delete, .-pthread_key_delete

    .section .text.pthread_kill,"ax",@progbits
    .globl pthread_kill
    .type pthread_kill, @function
pthread_kill:
    jmp *__libc_ponti_tabella+1044
    .size pthread_kill, .-pthread_kill

    .section .text.pthread_mutex_destroy,"ax",@progbits
    .globl pthread_mutex_destroy
    .type pthread_mutex_destroy, @function
pthread_mutex_destroy:
    jmp *__libc_ponti_tabella+1048
    .size pthread_mutex_destroy, .-pthread_mutex_destroy

    .section .text.pthread_mutex_init,"ax",@progbits
    .globl pthread_mutex_init
    .type pthread_mutex_init, @function
pthread_mutex_init:
    jmp *__libc_ponti_tabella+1052
    .size pthread_mutex_init, .-pthread_mutex_init

    .section .text.pthread_mutex_lock,"ax",@progbits
    .globl pthread_mutex_lock
    .type pthread_mutex_lock, @function
pthread_mutex_lock:
    jmp *__libc_ponti_tabella+1056
    .size pthread_mutex_lock, .-pthread_mutex_lock

    .section .text.pthread_mutex_timedlock,"ax",@progbits
    .globl pthread_mutex_timedlock
    .type pthread_mutex_timedlock, @function
pthread_mutex_timedlock:
    jmp *__libc_ponti_tabella+1060
    .size pthread_mutex_timedlock, .-pthread_mutex_timedlock

    .section .text.pthread_mutex_trylock,"ax",@progbits
    .globl pthread_mutex_trylock
    .type pthread_mutex_trylock, @function
pthread_mutex_trylock:
    jmp *__libc_ponti_tabella+1064
    .size pthread_mutex_trylock, .-pthread_mutex_trylock

    .section .text.pthread_mutex_unlock,"ax",@progbits
    .globl pthread_mutex_unlock
    .type pthread_mutex_unlock, @function
pthread_mutex_unlock:
    jmp *__libc_ponti_tabella+1068
    .size pthread_mutex_unlock, .-pthread_mutex_unlock

    .section .text.pthread_mutexattr_destroy,"ax",@progbits
    .globl pthread_mutexattr_destroy
    .type pthread_mutexattr_destroy, @function
pthread_mutexattr_destroy:
    jmp *__libc_ponti_tabella+1072
    .size pthread_mutexattr_destroy, .-pthread_mutexattr_destroy

    .section .text.pthread_mutexattr_gettype,"ax",@progbits
    .globl pthread_mutexattr_gettype
    .type pthread_mutexattr_gettype, @function
pthread_mutexattr_gettype:
    jmp *__libc_ponti_tabella+1076
    .size pthread_mutexattr_gettype, .-pthread_mutexattr_gettype

    .section .text.pthread_mutexattr_init,"ax",@progbits
    .globl pthread_mutexattr_init
    .type pthread_mutexattr_init, @function
pthread_mutexattr_init:
    jmp *__libc_ponti_tabella+1080
    .size pthread_mutexattr_init, .-pthread_mutexattr_init

    .section .text.pthread_mutexattr_settype,"ax",@progbits
    .globl pthread_mutexattr_settype
    .type pthread_mutexattr_settype, @function
pthread_mutexattr_settype:
    jmp *__libc_ponti_tabella+1084
    .size pthread_mutexattr_settype, .-pthread_mutexattr_settype

    .section .text.pthread_once,"ax",@progbits
    .globl pthread_once
    .type pthread_once, @function
pthread_once:
    jmp *__libc_ponti_tabella+1088
    .size pthread_once, .-pthread_once

    .section .text.pthread_rwlock_destroy,"ax",@progbits
    .globl pthread_rwlock_destroy
    .type pthread_rwlock_destroy, @function
pthread_rwlock_destroy:
    jmp *__libc_ponti_tabella+1092
    .size pthread_rwlock_destroy, .-pthread_rwlock_destroy

    .section .text.pthread_rwlock_init,"ax",@progbits
    .globl pthread_rwlock_init
    .type pthread_rwlock_init, @function
pthread_rwlock_init:
    jmp *__libc_ponti_tabella+1096
    .size pthread_rwlock_init, .-pthread_rwlock_init

    .section .text.pthread_rwlock_rdlock,"ax",@progbits
    .globl pthread_rwlock_rdlock
    .type pthread_rwlock_rdlock, @function
pthread_rwlock_rdlock:
    jmp *__libc_ponti_tabella+1100
    .size pthread_rwlock_rdlock, .-pthread_rwlock_rdlock

    .section .text.pthread_rwlock_tryrdlock,"ax",@progbits
    .globl pthread_rwlock_tryrdlock
    .type pthread_rwlock_tryrdlock, @function
pthread_rwlock_tryrdlock:
    jmp *__libc_ponti_tabella+1104
    .size pthread_rwlock_tryrdlock, .-pthread_rwlock_tryrdlock

    .section .text.pthread_rwlock_trywrlock,"ax",@progbits
    .globl pthread_rwlock_trywrlock
    .type pthread_rwlock_trywrlock, @function
pthread_rwlock_trywrlock:
    jmp *__libc_ponti_tabella+1108
    .size pthread_rwlock_trywrlock, .-pthread_rwlock_trywrlock

    .section .text.pthread_rwlock_unlock,"ax",@progbits
    .globl pthread_rwlock_unlock
    .type pthread_rwlock_unlock, @function
pthread_rwlock_unlock:
    jmp *__libc_ponti_tabella+1112
    .size pthread_rwlock_unlock, .-pthread_rwlock_unlock

    .section .text.pthread_rwlock_wrlock,"ax",@progbits
    .globl pthread_rwlock_wrlock
    .type pthread_rwlock_wrlock, @function
pthread_rwlock_wrlock:
    jmp *__libc_ponti_tabella+1116
    .size pthread_rwlock_wrlock, .-pthread_rwlock_wrlock

    .section .text.pthread_self,"ax",@progbits
    .globl pthread_self
    .type pthread_self, @function
pthread_self:
    jmp *__libc_ponti_tabella+1120
    .size pthread_self, .-pthread_self

    .section .text.pthread_setname_np,"ax",@progbits
    .globl pthread_setname_np
    .type pthread_setname_np, @function
pthread_setname_np:
    jmp *__libc_ponti_tabella+1124
    .size pthread_setname_np, .-pthread_setname_np

    .section .text.pthread_setschedparam,"ax",@progbits
    .globl pthread_setschedparam
    .type pthread_setschedparam, @function
pthread_setschedparam:
    jmp *__libc_ponti_tabella+1128
    .size pthread_setschedparam, .-pthread_setschedparam

    .section .text.pthread_setspecific,"ax",@progbits
    .globl pthread_setspecific
    .type pthread_setspecific, @function
pthread_setspecific:
    jmp *__libc_ponti_tabella+1132
    .size pthread_setspecific, .-pthread_setspecific

    .section .text.pthread_sigmask,"ax",@progbits
    .globl pthread_sigmask
    .type pthread_sigmask, @function
pthread_sigmask:
    jmp *__libc_ponti_tabella+1136
    .size pthread_sigmask, .-pthread_sigmask

    .section .text.pthread_yield,"ax",@progbits
    .globl pthread_yield
    .type pthread_yield, @function
pthread_yield:
    jmp *__libc_ponti_tabella+1140
    .size pthread_yield, .-pthread_yield

    .section .text.pty_apri,"ax",@progbits
    .globl pty_apri
    .type pty_apri, @function
pty_apri:
    jmp *__libc_ponti_tabella+1144
    .size pty_apri, .-pty_apri

    .section .text.pty_ctl,"ax",@progbits
    .globl pty_ctl
    .type pty_ctl, @function
pty_ctl:
    jmp *__libc_ponti_tabella+1148
    .size pty_ctl, .-pty_ctl

    .section .text.putc,"ax",@progbits
    .globl putc
    .type putc, @function
putc:
    jmp *__libc_ponti_tabella+1152
    .size putc, .-putc

    .section .text.putchar,"ax",@progbits
    .globl putchar
    .type putchar, @function
putchar:
    jmp *__libc_ponti_tabella+1156
    .size putchar, .-putchar

    .section .text.putenv,"ax",@progbits
    .globl putenv
    .type putenv, @function
putenv:
    jmp *__libc_ponti_tabella+1160
    .size putenv, .-putenv

    .section .text.puts,"ax",@progbits
    .globl puts
    .type puts, @function
puts:
    jmp *__libc_ponti_tabella+1164
    .size puts, .-puts

    .section .text.qsort,"ax",@progbits
    .globl qsort
    .type qsort, @function
qsort:
    jmp *__libc_ponti_tabella+1168
    .size qsort, .-qsort

    .section .text.quick_exit,"ax",@progbits
    .globl quick_exit
    .type quick_exit, @function
quick_exit:
    jmp *__libc_ponti_tabella+1172
    .size quick_exit, .-quick_exit

    .section .text.raise,"ax",@progbits
    .globl raise
    .type raise, @function
raise:
    jmp *__libc_ponti_tabella+1176
    .size raise, .-raise

    .section .text.rand,"ax",@progbits
    .globl rand
    .type rand, @function
rand:
    jmp *__libc_ponti_tabella+1180
    .size rand, .-rand

    .section .text.read,"ax",@progbits
    .globl read
    .type read, @function
read:
    jmp *__libc_ponti_tabella+1184
    .size read, .-read

    .section .text.readdir,"ax",@progbits
    .globl readdir
    .type readdir, @function
readdir:
    jmp *__libc_ponti_tabella+1188
    .size readdir, .-readdir

    .section .text.realloc,"ax",@progbits
    .globl realloc
    .type realloc, @function
realloc:
    jmp *__libc_ponti_tabella+1192
    .size realloc, .-realloc

    .section .text.realpath,"ax",@progbits
    .globl realpath
    .type realpath, @function
realpath:
    jmp *__libc_ponti_tabella+1196
    .size realpath, .-realpath

    .section .text.reboot,"ax",@progbits
    .globl reboot
    .type reboot, @function
reboot:
    jmp *__libc_ponti_tabella+1200
    .size reboot, .-reboot

    .section .text.remove,"ax",@progbits
    .globl remove
    .type remove, @function
remove:
    jmp *__libc_ponti_tabella+1204
    .size remove, .-remove

    .section .text.rename,"ax",@progbits
    .globl rename
    .type rename, @function
rename:
    jmp *__libc_ponti_tabella+1208
    .size rename, .-rename

    .section .text.rewind,"ax",@progbits
    .globl rewind
    .type rewind, @function
rewind:
    jmp *__libc_ponti_tabella+1212
    .size rewind, .-rewind

    .section .text.rewinddir,"ax",@progbits
    .globl rewinddir
    .type rewinddir, @function
rewinddir:
    jmp *__libc_ponti_tabella+1216
    .size rewinddir, .-rewinddir

    .section .text.rmdir,"ax",@progbits
    .globl rmdir
    .type rmdir, @function
rmdir:
    jmp *__libc_ponti_tabella+1220
    .size rmdir, .-rmdir

    .section .text.sbrk,"ax",@progbits
    .globl sbrk
    .type sbrk, @function
sbrk:
    jmp *__libc_ponti_tabella+1224
    .size sbrk, .-sbrk

    .section .text.scanf,"ax",@progbits
    .globl scanf
    .type scanf, @function
scanf:
    jmp *__libc_ponti_tabella+1228
    .size scanf, .-scanf

    .section .text.sched_yield,"ax",@progbits
    .globl sched_yield
    .type sched_yield, @function
sched_yield:
    jmp *__libc_ponti_tabella+1232
    .size sched_yield, .-sched_yield

    .section .text.select,"ax",@progbits
    .globl select
    .type select, @function
select:
    jmp *__libc_ponti_tabella+1236
    .size select, .-select

    .section .text.sem_destroy,"ax",@progbits
    .globl sem_destroy
    .type sem_destroy, @function
sem_destroy:
    jmp *__libc_ponti_tabella+1240
    .size sem_destroy, .-sem_destroy

    .section .text.sem_getvalue,"ax",@progbits
    .globl sem_getvalue
    .type sem_getvalue, @function
sem_getvalue:
    jmp *__libc_ponti_tabella+1244
    .size sem_getvalue, .-sem_getvalue

    .section .text.sem_init,"ax",@progbits
    .globl sem_init
    .type sem_init, @function
sem_init:
    jmp *__libc_ponti_tabella+1248
    .size sem_init, .-sem_init

    .section .text.sem_post,"ax",@progbits
    .globl sem_post
    .type sem_post, @function
sem_post:
    jmp *__libc_ponti_tabella+1252
    .size sem_post, .-sem_post

    .section .text.sem_timedwait,"ax",@progbits
    .globl sem_timedwait
    .type sem_timedwait, @function
sem_timedwait:
    jmp *__libc_ponti_tabella+1256
    .size sem_timedwait, .-sem_timedwait

    .section .text.sem_trywait,"ax",@progbits
    .globl sem_trywait
    .type sem_trywait, @function
sem_trywait:
    jmp *__libc_ponti_tabella+1260
    .size sem_trywait, .-sem_trywait

    .section .text.sem_wait,"ax",@progbits
    .globl sem_wait
    .type sem_wait, @function
sem_wait:
    jmp *__libc_ponti_tabella+1264
    .size sem_wait, .-sem_wait

    .section .text.semaforo_lascia,"ax",@progbits
    .globl semaforo_lascia
    .type semaforo_lascia, @function
semaforo_lascia:
    jmp *__libc_ponti_tabella+1268
    .size semaforo_lascia, .-semaforo_lascia

    .section .text.semaforo_prendi,"ax",@progbits
    .globl semaforo_prendi
    .type semaforo_prendi, @function
semaforo_prendi:
    jmp *__libc_ponti_tabella+1272
    .size semaforo_prendi, .-semaforo_prendi

    .section .text.semaforo_prendi_ms,"ax",@progbits
    .globl semaforo_prendi_ms
    .type semaforo_prendi_ms, @function
semaforo_prendi_ms:
    jmp *__libc_ponti_tabella+1276
    .size semaforo_prendi_ms, .-semaforo_prendi_ms

    .section .text.semaforo_prova,"ax",@progbits
    .globl semaforo_prova
    .type semaforo_prova, @function
semaforo_prova:
    jmp *__libc_ponti_tabella+1280
    .size semaforo_prova, .-semaforo_prova

    .section .text.setbuf,"ax",@progbits
    .globl setbuf
    .type setbuf, @function
setbuf:
    jmp *__libc_ponti_tabella+1284
    .size setbuf, .-setbuf

    .section .text.setenv,"ax",@progbits
    .globl setenv
    .type setenv, @function
setenv:
    jmp *__libc_ponti_tabella+1288
    .size setenv, .-setenv

    .section .text.setjmp,"ax",@progbits
    .globl setjmp
    .type setjmp, @function
setjmp:
    jmp *__libc_ponti_tabella+1292
    .size setjmp, .-setjmp

    .section .text.setlocale,"ax",@progbits
    .globl setlocale
    .type setlocale, @function
setlocale:
    jmp *__libc_ponti_tabella+1296
    .size setlocale, .-setlocale

    .section .text.setuid,"ax",@progbits
    .globl setuid
    .type setuid, @function
setuid:
    jmp *__libc_ponti_tabella+1300
    .size setuid, .-setuid

    .section .text.setvbuf,"ax",@progbits
    .globl setvbuf
    .type setvbuf, @function
setvbuf:
    jmp *__libc_ponti_tabella+1304
    .size setvbuf, .-setvbuf

    .section .text.sha256,"ax",@progbits
    .globl sha256
    .type sha256, @function
sha256:
    jmp *__libc_ponti_tabella+1308
    .size sha256, .-sha256

    .section .text.sha256_avvia,"ax",@progbits
    .globl sha256_avvia
    .type sha256_avvia, @function
sha256_avvia:
    jmp *__libc_ponti_tabella+1312
    .size sha256_avvia, .-sha256_avvia

    .section .text.sha256_dai,"ax",@progbits
    .globl sha256_dai
    .type sha256_dai, @function
sha256_dai:
    jmp *__libc_ponti_tabella+1316
    .size sha256_dai, .-sha256_dai

    .section .text.sha256_esa,"ax",@progbits
    .globl sha256_esa
    .type sha256_esa, @function
sha256_esa:
    jmp *__libc_ponti_tabella+1320
    .size sha256_esa, .-sha256_esa

    .section .text.sha256_fine,"ax",@progbits
    .globl sha256_fine
    .type sha256_fine, @function
sha256_fine:
    jmp *__libc_ponti_tabella+1324
    .size sha256_fine, .-sha256_fine

    .section .text.sha256_fine_esa,"ax",@progbits
    .globl sha256_fine_esa
    .type sha256_fine_esa, @function
sha256_fine_esa:
    jmp *__libc_ponti_tabella+1328
    .size sha256_fine_esa, .-sha256_fine_esa

    .section .text.shm_apri,"ax",@progbits
    .globl shm_apri
    .type shm_apri, @function
shm_apri:
    jmp *__libc_ponti_tabella+1332
    .size shm_apri, .-shm_apri

    .section .text.shm_chiudi,"ax",@progbits
    .globl shm_chiudi
    .type shm_chiudi, @function
shm_chiudi:
    jmp *__libc_ponti_tabella+1336
    .size shm_chiudi, .-shm_chiudi

    .section .text.signal,"ax",@progbits
    .globl signal
    .type signal, @function
signal:
    jmp *__libc_ponti_tabella+1340
    .size signal, .-signal

    .section .text.sleep,"ax",@progbits
    .globl sleep
    .type sleep, @function
sleep:
    jmp *__libc_ponti_tabella+1344
    .size sleep, .-sleep

    .section .text.snprintf,"ax",@progbits
    .globl snprintf
    .type snprintf, @function
snprintf:
    jmp *__libc_ponti_tabella+1348
    .size snprintf, .-snprintf

    .section .text.spawn,"ax",@progbits
    .globl spawn
    .type spawn, @function
spawn:
    jmp *__libc_ponti_tabella+1352
    .size spawn, .-spawn

    .section .text.spawn_ex,"ax",@progbits
    .globl spawn_ex
    .type spawn_ex, @function
spawn_ex:
    jmp *__libc_ponti_tabella+1356
    .size spawn_ex, .-spawn_ex

    .section .text.spawn_su_console,"ax",@progbits
    .globl spawn_su_console
    .type spawn_su_console, @function
spawn_su_console:
    jmp *__libc_ponti_tabella+1360
    .size spawn_su_console, .-spawn_su_console

    .section .text.spawn_utente,"ax",@progbits
    .globl spawn_utente
    .type spawn_utente, @function
spawn_utente:
    jmp *__libc_ponti_tabella+1364
    .size spawn_utente, .-spawn_utente

    .section .text.sprintf,"ax",@progbits
    .globl sprintf
    .type sprintf, @function
sprintf:
    jmp *__libc_ponti_tabella+1368
    .size sprintf, .-sprintf

    .section .text.sqrt,"ax",@progbits
    .globl sqrt
    .type sqrt, @function
sqrt:
    jmp *__libc_ponti_tabella+1372
    .size sqrt, .-sqrt

    .section .text.srand,"ax",@progbits
    .globl srand
    .type srand, @function
srand:
    jmp *__libc_ponti_tabella+1376
    .size srand, .-srand

    .section .text.sscanf,"ax",@progbits
    .globl sscanf
    .type sscanf, @function
sscanf:
    jmp *__libc_ponti_tabella+1380
    .size sscanf, .-sscanf

    .section .text.stat,"ax",@progbits
    .globl stat
    .type stat, @function
stat:
    jmp *__libc_ponti_tabella+1384
    .size stat, .-stat

    .section .text.statperm,"ax",@progbits
    .globl statperm
    .type statperm, @function
statperm:
    jmp *__libc_ponti_tabella+1388
    .size statperm, .-statperm

    .section .text.statraw,"ax",@progbits
    .globl statraw
    .type statraw, @function
statraw:
    jmp *__libc_ponti_tabella+1392
    .size statraw, .-statraw

    .section .text.strcasecmp,"ax",@progbits
    .globl strcasecmp
    .type strcasecmp, @function
strcasecmp:
    jmp *__libc_ponti_tabella+1396
    .size strcasecmp, .-strcasecmp

    .section .text.strcat,"ax",@progbits
    .globl strcat
    .type strcat, @function
strcat:
    jmp *__libc_ponti_tabella+1400
    .size strcat, .-strcat

    .section .text.strchr,"ax",@progbits
    .globl strchr
    .type strchr, @function
strchr:
    jmp *__libc_ponti_tabella+1404
    .size strchr, .-strchr

    .section .text.strcmp,"ax",@progbits
    .globl strcmp
    .type strcmp, @function
strcmp:
    jmp *__libc_ponti_tabella+1408
    .size strcmp, .-strcmp

    .section .text.strcoll,"ax",@progbits
    .globl strcoll
    .type strcoll, @function
strcoll:
    jmp *__libc_ponti_tabella+1412
    .size strcoll, .-strcoll

    .section .text.strcpy,"ax",@progbits
    .globl strcpy
    .type strcpy, @function
strcpy:
    jmp *__libc_ponti_tabella+1416
    .size strcpy, .-strcpy

    .section .text.strcspn,"ax",@progbits
    .globl strcspn
    .type strcspn, @function
strcspn:
    jmp *__libc_ponti_tabella+1420
    .size strcspn, .-strcspn

    .section .text.strdup,"ax",@progbits
    .globl strdup
    .type strdup, @function
strdup:
    jmp *__libc_ponti_tabella+1424
    .size strdup, .-strdup

    .section .text.strerror,"ax",@progbits
    .globl strerror
    .type strerror, @function
strerror:
    jmp *__libc_ponti_tabella+1428
    .size strerror, .-strerror

    .section .text.strerror_r,"ax",@progbits
    .globl strerror_r
    .type strerror_r, @function
strerror_r:
    jmp *__libc_ponti_tabella+1432
    .size strerror_r, .-strerror_r

    .section .text.strftime,"ax",@progbits
    .globl strftime
    .type strftime, @function
strftime:
    jmp *__libc_ponti_tabella+1436
    .size strftime, .-strftime

    .section .text.strlen,"ax",@progbits
    .globl strlen
    .type strlen, @function
strlen:
    jmp *__libc_ponti_tabella+1440
    .size strlen, .-strlen

    .section .text.strncasecmp,"ax",@progbits
    .globl strncasecmp
    .type strncasecmp, @function
strncasecmp:
    jmp *__libc_ponti_tabella+1444
    .size strncasecmp, .-strncasecmp

    .section .text.strncat,"ax",@progbits
    .globl strncat
    .type strncat, @function
strncat:
    jmp *__libc_ponti_tabella+1448
    .size strncat, .-strncat

    .section .text.strncmp,"ax",@progbits
    .globl strncmp
    .type strncmp, @function
strncmp:
    jmp *__libc_ponti_tabella+1452
    .size strncmp, .-strncmp

    .section .text.strncpy,"ax",@progbits
    .globl strncpy
    .type strncpy, @function
strncpy:
    jmp *__libc_ponti_tabella+1456
    .size strncpy, .-strncpy

    .section .text.strpbrk,"ax",@progbits
    .globl strpbrk
    .type strpbrk, @function
strpbrk:
    jmp *__libc_ponti_tabella+1460
    .size strpbrk, .-strpbrk

    .section .text.strrchr,"ax",@progbits
    .globl strrchr
    .type strrchr, @function
strrchr:
    jmp *__libc_ponti_tabella+1464
    .size strrchr, .-strrchr

    .section .text.strsignal,"ax",@progbits
    .globl strsignal
    .type strsignal, @function
strsignal:
    jmp *__libc_ponti_tabella+1468
    .size strsignal, .-strsignal

    .section .text.strspn,"ax",@progbits
    .globl strspn
    .type strspn, @function
strspn:
    jmp *__libc_ponti_tabella+1472
    .size strspn, .-strspn

    .section .text.strstr,"ax",@progbits
    .globl strstr
    .type strstr, @function
strstr:
    jmp *__libc_ponti_tabella+1476
    .size strstr, .-strstr

    .section .text.strtod,"ax",@progbits
    .globl strtod
    .type strtod, @function
strtod:
    jmp *__libc_ponti_tabella+1480
    .size strtod, .-strtod

    .section .text.strtof,"ax",@progbits
    .globl strtof
    .type strtof, @function
strtof:
    jmp *__libc_ponti_tabella+1484
    .size strtof, .-strtof

    .section .text.strtoimax,"ax",@progbits
    .globl strtoimax
    .type strtoimax, @function
strtoimax:
    jmp *__libc_ponti_tabella+1488
    .size strtoimax, .-strtoimax

    .section .text.strtok,"ax",@progbits
    .globl strtok
    .type strtok, @function
strtok:
    jmp *__libc_ponti_tabella+1492
    .size strtok, .-strtok

    .section .text.strtok_r,"ax",@progbits
    .globl strtok_r
    .type strtok_r, @function
strtok_r:
    jmp *__libc_ponti_tabella+1496
    .size strtok_r, .-strtok_r

    .section .text.strtol,"ax",@progbits
    .globl strtol
    .type strtol, @function
strtol:
    jmp *__libc_ponti_tabella+1500
    .size strtol, .-strtol

    .section .text.strtold,"ax",@progbits
    .globl strtold
    .type strtold, @function
strtold:
    jmp *__libc_ponti_tabella+1504
    .size strtold, .-strtold

    .section .text.strtoll,"ax",@progbits
    .globl strtoll
    .type strtoll, @function
strtoll:
    jmp *__libc_ponti_tabella+1508
    .size strtoll, .-strtoll

    .section .text.strtoul,"ax",@progbits
    .globl strtoul
    .type strtoul, @function
strtoul:
    jmp *__libc_ponti_tabella+1512
    .size strtoul, .-strtoul

    .section .text.strtoull,"ax",@progbits
    .globl strtoull
    .type strtoull, @function
strtoull:
    jmp *__libc_ponti_tabella+1516
    .size strtoull, .-strtoull

    .section .text.strtoumax,"ax",@progbits
    .globl strtoumax
    .type strtoumax, @function
strtoumax:
    jmp *__libc_ponti_tabella+1520
    .size strtoumax, .-strtoumax

    .section .text.strxfrm,"ax",@progbits
    .globl strxfrm
    .type strxfrm, @function
strxfrm:
    jmp *__libc_ponti_tabella+1524
    .size strxfrm, .-strxfrm

    .section .text.swprintf,"ax",@progbits
    .globl swprintf
    .type swprintf, @function
swprintf:
    jmp *__libc_ponti_tabella+1528
    .size swprintf, .-swprintf

    .section .text.sysconf,"ax",@progbits
    .globl sysconf
    .type sysconf, @function
sysconf:
    jmp *__libc_ponti_tabella+1532
    .size sysconf, .-sysconf

    .section .text.system,"ax",@progbits
    .globl system
    .type system, @function
system:
    jmp *__libc_ponti_tabella+1536
    .size system, .-system

    .section .text.thread_attendi,"ax",@progbits
    .globl thread_attendi
    .type thread_attendi, @function
thread_attendi:
    jmp *__libc_ponti_tabella+1540
    .size thread_attendi, .-thread_attendi

    .section .text.thread_crea,"ax",@progbits
    .globl thread_crea
    .type thread_crea, @function
thread_crea:
    jmp *__libc_ponti_tabella+1544
    .size thread_crea, .-thread_crea

    .section .text.thread_devo_fermarmi,"ax",@progbits
    .globl thread_devo_fermarmi
    .type thread_devo_fermarmi, @function
thread_devo_fermarmi:
    jmp *__libc_ponti_tabella+1548
    .size thread_devo_fermarmi, .-thread_devo_fermarmi

    .section .text.thread_esci,"ax",@progbits
    .globl thread_esci
    .type thread_esci, @function
thread_esci:
    jmp *__libc_ponti_tabella+1552
    .size thread_esci, .-thread_esci

    .section .text.thread_ferma,"ax",@progbits
    .globl thread_ferma
    .type thread_ferma, @function
thread_ferma:
    jmp *__libc_ponti_tabella+1556
    .size thread_ferma, .-thread_ferma

    .section .text.time,"ax",@progbits
    .globl time
    .type time, @function
time:
    jmp *__libc_ponti_tabella+1560
    .size time, .-time

    .section .text.time_now,"ax",@progbits
    .globl time_now
    .type time_now, @function
time_now:
    jmp *__libc_ponti_tabella+1564
    .size time_now, .-time_now

    .section .text.time_set,"ax",@progbits
    .globl time_set
    .type time_set, @function
time_set:
    jmp *__libc_ponti_tabella+1568
    .size time_set, .-time_set

    .section .text.timegm,"ax",@progbits
    .globl timegm
    .type timegm, @function
timegm:
    jmp *__libc_ponti_tabella+1572
    .size timegm, .-timegm

    .section .text.times,"ax",@progbits
    .globl times
    .type times, @function
times:
    jmp *__libc_ponti_tabella+1576
    .size times, .-times

    .section .text.timespec_get,"ax",@progbits
    .globl timespec_get
    .type timespec_get, @function
timespec_get:
    jmp *__libc_ponti_tabella+1580
    .size timespec_get, .-timespec_get

    .section .text.tmpfile,"ax",@progbits
    .globl tmpfile
    .type tmpfile, @function
tmpfile:
    jmp *__libc_ponti_tabella+1584
    .size tmpfile, .-tmpfile

    .section .text.tmpnam,"ax",@progbits
    .globl tmpnam
    .type tmpnam, @function
tmpnam:
    jmp *__libc_ponti_tabella+1588
    .size tmpnam, .-tmpnam

    .section .text.toascii,"ax",@progbits
    .globl toascii
    .type toascii, @function
toascii:
    jmp *__libc_ponti_tabella+1592
    .size toascii, .-toascii

    .section .text.tolower,"ax",@progbits
    .globl tolower
    .type tolower, @function
tolower:
    jmp *__libc_ponti_tabella+1596
    .size tolower, .-tolower

    .section .text.toupper,"ax",@progbits
    .globl toupper
    .type toupper, @function
toupper:
    jmp *__libc_ponti_tabella+1600
    .size toupper, .-toupper

    .section .text.towlower,"ax",@progbits
    .globl towlower
    .type towlower, @function
towlower:
    jmp *__libc_ponti_tabella+1604
    .size towlower, .-towlower

    .section .text.towupper,"ax",@progbits
    .globl towupper
    .type towupper, @function
towupper:
    jmp *__libc_ponti_tabella+1608
    .size towupper, .-towupper

    .section .text.truncate,"ax",@progbits
    .globl truncate
    .type truncate, @function
truncate:
    jmp *__libc_ponti_tabella+1612
    .size truncate, .-truncate

    .section .text.tty_clear,"ax",@progbits
    .globl tty_clear
    .type tty_clear, @function
tty_clear:
    jmp *__libc_ponti_tabella+1616
    .size tty_clear, .-tty_clear

    .section .text.tty_getsize,"ax",@progbits
    .globl tty_getsize
    .type tty_getsize, @function
tty_getsize:
    jmp *__libc_ponti_tabella+1620
    .size tty_getsize, .-tty_getsize

    .section .text.tty_raw,"ax",@progbits
    .globl tty_raw
    .type tty_raw, @function
tty_raw:
    jmp *__libc_ponti_tabella+1624
    .size tty_raw, .-tty_raw

    .section .text.umask,"ax",@progbits
    .globl umask
    .type umask, @function
umask:
    jmp *__libc_ponti_tabella+1628
    .size umask, .-umask

    .section .text.umount,"ax",@progbits
    .globl umount
    .type umount, @function
umount:
    jmp *__libc_ponti_tabella+1632
    .size umount, .-umount

    .section .text.ungetc,"ax",@progbits
    .globl ungetc
    .type ungetc, @function
ungetc:
    jmp *__libc_ponti_tabella+1636
    .size ungetc, .-ungetc

    .section .text.unlink,"ax",@progbits
    .globl unlink
    .type unlink, @function
unlink:
    jmp *__libc_ponti_tabella+1640
    .size unlink, .-unlink

    .section .text.unsetenv,"ax",@progbits
    .globl unsetenv
    .type unsetenv, @function
unsetenv:
    jmp *__libc_ponti_tabella+1644
    .size unsetenv, .-unsetenv

    .section .text.uptime_ms,"ax",@progbits
    .globl uptime_ms
    .type uptime_ms, @function
uptime_ms:
    jmp *__libc_ponti_tabella+1648
    .size uptime_ms, .-uptime_ms

    .section .text.usleep,"ax",@progbits
    .globl usleep
    .type usleep, @function
usleep:
    jmp *__libc_ponti_tabella+1652
    .size usleep, .-usleep

    .section .text.utime,"ax",@progbits
    .globl utime
    .type utime, @function
utime:
    jmp *__libc_ponti_tabella+1656
    .size utime, .-utime

    .section .text.verboseboot,"ax",@progbits
    .globl verboseboot
    .type verboseboot, @function
verboseboot:
    jmp *__libc_ponti_tabella+1660
    .size verboseboot, .-verboseboot

    .section .text.vfprintf,"ax",@progbits
    .globl vfprintf
    .type vfprintf, @function
vfprintf:
    jmp *__libc_ponti_tabella+1664
    .size vfprintf, .-vfprintf

    .section .text.vfscanf,"ax",@progbits
    .globl vfscanf
    .type vfscanf, @function
vfscanf:
    jmp *__libc_ponti_tabella+1668
    .size vfscanf, .-vfscanf

    .section .text.video_info,"ax",@progbits
    .globl video_info
    .type video_info, @function
video_info:
    jmp *__libc_ponti_tabella+1672
    .size video_info, .-video_info

    .section .text.vprintf,"ax",@progbits
    .globl vprintf
    .type vprintf, @function
vprintf:
    jmp *__libc_ponti_tabella+1676
    .size vprintf, .-vprintf

    .section .text.vscanf,"ax",@progbits
    .globl vscanf
    .type vscanf, @function
vscanf:
    jmp *__libc_ponti_tabella+1680
    .size vscanf, .-vscanf

    .section .text.vsnprintf,"ax",@progbits
    .globl vsnprintf
    .type vsnprintf, @function
vsnprintf:
    jmp *__libc_ponti_tabella+1684
    .size vsnprintf, .-vsnprintf

    .section .text.vsprintf,"ax",@progbits
    .globl vsprintf
    .type vsprintf, @function
vsprintf:
    jmp *__libc_ponti_tabella+1688
    .size vsprintf, .-vsprintf

    .section .text.vsscanf,"ax",@progbits
    .globl vsscanf
    .type vsscanf, @function
vsscanf:
    jmp *__libc_ponti_tabella+1692
    .size vsscanf, .-vsscanf

    .section .text.vswprintf,"ax",@progbits
    .globl vswprintf
    .type vswprintf, @function
vswprintf:
    jmp *__libc_ponti_tabella+1696
    .size vswprintf, .-vswprintf

    .section .text.wait,"ax",@progbits
    .globl wait
    .type wait, @function
wait:
    jmp *__libc_ponti_tabella+1700
    .size wait, .-wait

    .section .text.waitpid,"ax",@progbits
    .globl waitpid
    .type waitpid, @function
waitpid:
    jmp *__libc_ponti_tabella+1704
    .size waitpid, .-waitpid

    .section .text.wcscat,"ax",@progbits
    .globl wcscat
    .type wcscat, @function
wcscat:
    jmp *__libc_ponti_tabella+1708
    .size wcscat, .-wcscat

    .section .text.wcschr,"ax",@progbits
    .globl wcschr
    .type wcschr, @function
wcschr:
    jmp *__libc_ponti_tabella+1712
    .size wcschr, .-wcschr

    .section .text.wcscmp,"ax",@progbits
    .globl wcscmp
    .type wcscmp, @function
wcscmp:
    jmp *__libc_ponti_tabella+1716
    .size wcscmp, .-wcscmp

    .section .text.wcscpy,"ax",@progbits
    .globl wcscpy
    .type wcscpy, @function
wcscpy:
    jmp *__libc_ponti_tabella+1720
    .size wcscpy, .-wcscpy

    .section .text.wcscspn,"ax",@progbits
    .globl wcscspn
    .type wcscspn, @function
wcscspn:
    jmp *__libc_ponti_tabella+1724
    .size wcscspn, .-wcscspn

    .section .text.wcslen,"ax",@progbits
    .globl wcslen
    .type wcslen, @function
wcslen:
    jmp *__libc_ponti_tabella+1728
    .size wcslen, .-wcslen

    .section .text.wcsncat,"ax",@progbits
    .globl wcsncat
    .type wcsncat, @function
wcsncat:
    jmp *__libc_ponti_tabella+1732
    .size wcsncat, .-wcsncat

    .section .text.wcsncmp,"ax",@progbits
    .globl wcsncmp
    .type wcsncmp, @function
wcsncmp:
    jmp *__libc_ponti_tabella+1736
    .size wcsncmp, .-wcsncmp

    .section .text.wcsncpy,"ax",@progbits
    .globl wcsncpy
    .type wcsncpy, @function
wcsncpy:
    jmp *__libc_ponti_tabella+1740
    .size wcsncpy, .-wcsncpy

    .section .text.wcspbrk,"ax",@progbits
    .globl wcspbrk
    .type wcspbrk, @function
wcspbrk:
    jmp *__libc_ponti_tabella+1744
    .size wcspbrk, .-wcspbrk

    .section .text.wcsrchr,"ax",@progbits
    .globl wcsrchr
    .type wcsrchr, @function
wcsrchr:
    jmp *__libc_ponti_tabella+1748
    .size wcsrchr, .-wcsrchr

    .section .text.wcsspn,"ax",@progbits
    .globl wcsspn
    .type wcsspn, @function
wcsspn:
    jmp *__libc_ponti_tabella+1752
    .size wcsspn, .-wcsspn

    .section .text.wcsstr,"ax",@progbits
    .globl wcsstr
    .type wcsstr, @function
wcsstr:
    jmp *__libc_ponti_tabella+1756
    .size wcsstr, .-wcsstr

    .section .text.wcstod,"ax",@progbits
    .globl wcstod
    .type wcstod, @function
wcstod:
    jmp *__libc_ponti_tabella+1760
    .size wcstod, .-wcstod

    .section .text.wcstol,"ax",@progbits
    .globl wcstol
    .type wcstol, @function
wcstol:
    jmp *__libc_ponti_tabella+1764
    .size wcstol, .-wcstol

    .section .text.wcstoll,"ax",@progbits
    .globl wcstoll
    .type wcstoll, @function
wcstoll:
    jmp *__libc_ponti_tabella+1768
    .size wcstoll, .-wcstoll

    .section .text.wcstombs,"ax",@progbits
    .globl wcstombs
    .type wcstombs, @function
wcstombs:
    jmp *__libc_ponti_tabella+1772
    .size wcstombs, .-wcstombs

    .section .text.wcstoul,"ax",@progbits
    .globl wcstoul
    .type wcstoul, @function
wcstoul:
    jmp *__libc_ponti_tabella+1776
    .size wcstoul, .-wcstoul

    .section .text.wcstoull,"ax",@progbits
    .globl wcstoull
    .type wcstoull, @function
wcstoull:
    jmp *__libc_ponti_tabella+1780
    .size wcstoull, .-wcstoull

    .section .text.wctomb,"ax",@progbits
    .globl wctomb
    .type wctomb, @function
wctomb:
    jmp *__libc_ponti_tabella+1784
    .size wctomb, .-wctomb

    .section .text.wmemchr,"ax",@progbits
    .globl wmemchr
    .type wmemchr, @function
wmemchr:
    jmp *__libc_ponti_tabella+1788
    .size wmemchr, .-wmemchr

    .section .text.wmemcmp,"ax",@progbits
    .globl wmemcmp
    .type wmemcmp, @function
wmemcmp:
    jmp *__libc_ponti_tabella+1792
    .size wmemcmp, .-wmemcmp

    .section .text.wmemcpy,"ax",@progbits
    .globl wmemcpy
    .type wmemcpy, @function
wmemcpy:
    jmp *__libc_ponti_tabella+1796
    .size wmemcpy, .-wmemcpy

    .section .text.wmemmove,"ax",@progbits
    .globl wmemmove
    .type wmemmove, @function
wmemmove:
    jmp *__libc_ponti_tabella+1800
    .size wmemmove, .-wmemmove

    .section .text.wmemset,"ax",@progbits
    .globl wmemset
    .type wmemset, @function
wmemset:
    jmp *__libc_ponti_tabella+1804
    .size wmemset, .-wmemset

    .section .text.write,"ax",@progbits
    .globl write
    .type write, @function
write:
    jmp *__libc_ponti_tabella+1808
    .size write, .-write

    .section .note.GNU-stack,"",@progbits
