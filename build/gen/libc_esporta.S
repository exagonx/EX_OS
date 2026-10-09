/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * La tabella che libc.so mette a disposizione: 453 funzioni, ognuna col suo
 * nome. Chi la legge risolve per NOME, quindi l'ordine di questo file non e'
 * parte dell'ABI e puo' cambiare a ogni ricostruzione senza rompere niente.
 */
    .section .rodata
    .align 4
ln0:
    .asciz "_Exit"
ln1:
    .asciz "__environ_dove"
ln2:
    .asciz "__errno_dove"
ln3:
    .asciz "__libc_distruttori_registra"
ln4:
    .asciz "__stderr_dove"
ln5:
    .asciz "__stdin_dove"
ln6:
    .asciz "__stdout_dove"
ln7:
    .asciz "_assert_fallita"
ln8:
    .asciz "_exit"
ln9:
    .asciz "abort"
ln10:
    .asciz "abs"
ln11:
    .asciz "access"
ln12:
    .asciz "ahci_guarda"
ln13:
    .asciz "ahci_passa"
ln14:
    .asciz "ahci_stato"
ln15:
    .asciz "aligned_alloc"
ln16:
    .asciz "asctime"
ln17:
    .asciz "at_quick_exit"
ln18:
    .asciz "atexit"
ln19:
    .asciz "atof"
ln20:
    .asciz "atoi"
ln21:
    .asciz "atol"
ln22:
    .asciz "atoll"
ln23:
    .asciz "attesa_dormi"
ln24:
    .asciz "attesa_sveglia"
ln25:
    .asciz "blk_attendi"
ln26:
    .asciz "blk_espelli"
ln27:
    .asciz "blk_offri"
ln28:
    .asciz "blk_risposta"
ln29:
    .asciz "blk_scansiona"
ln30:
    .asciz "blkinfo"
ln31:
    .asciz "blkread"
ln32:
    .asciz "blkwrite"
ln33:
    .asciz "bootinstall"
ln34:
    .asciz "bootverify"
ln35:
    .asciz "bsearch"
ln36:
    .asciz "calloc"
ln37:
    .asciz "chdir"
ln38:
    .asciz "chmod"
ln39:
    .asciz "chown"
ln40:
    .asciz "clearerr"
ln41:
    .asciz "clock"
ln42:
    .asciz "clock_getres"
ln43:
    .asciz "clock_gettime"
ln44:
    .asciz "close"
ln45:
    .asciz "closedir"
ln46:
    .asciz "condizione_aspetta"
ln47:
    .asciz "condizione_aspetta_ms"
ln48:
    .asciz "condizione_segnala"
ln49:
    .asciz "condizione_segnala_tutti"
ln50:
    .asciz "console_ctrlc"
ln51:
    .asciz "console_grafica"
ln52:
    .asciz "console_info"
ln53:
    .asciz "console_registro"
ln54:
    .asciz "console_setfg"
ln55:
    .asciz "console_switch"
ln56:
    .asciz "console_testo"
ln57:
    .asciz "console_write"
ln58:
    .asciz "cpu_info"
ln59:
    .asciz "ctime"
ln60:
    .asciz "difftime"
ln61:
    .asciz "diskinfo"
ln62:
    .asciz "div"
ln63:
    .asciz "diventa_root"
ln64:
    .asciz "dma_alloc"
ln65:
    .asciz "dup"
ln66:
    .asciz "dup2"
ln67:
    .asciz "execv"
ln68:
    .asciz "execvp"
ln69:
    .asciz "exit"
ln70:
    .asciz "fabs"
ln71:
    .asciz "fb_map"
ln72:
    .asciz "fchmod"
ln73:
    .asciz "fclose"
ln74:
    .asciz "fcntl"
ln75:
    .asciz "fdopen"
ln76:
    .asciz "fdprova"
ln77:
    .asciz "feof"
ln78:
    .asciz "ferror"
ln79:
    .asciz "fflush"
ln80:
    .asciz "fgetc"
ln81:
    .asciz "fgetpos"
ln82:
    .asciz "fgets"
ln83:
    .asciz "fileno"
ln84:
    .asciz "fopen"
ln85:
    .asciz "fpathconf"
ln86:
    .asciz "fprintf"
ln87:
    .asciz "fputc"
ln88:
    .asciz "fputs"
ln89:
    .asciz "fread"
ln90:
    .asciz "free"
ln91:
    .asciz "freopen"
ln92:
    .asciz "frexp"
ln93:
    .asciz "fscanf"
ln94:
    .asciz "fseek"
ln95:
    .asciz "fsetpos"
ln96:
    .asciz "fsize"
ln97:
    .asciz "fstat"
ln98:
    .asciz "ftell"
ln99:
    .asciz "fwrite"
ln100:
    .asciz "getc"
ln101:
    .asciz "getchar"
ln102:
    .asciz "getconf"
ln103:
    .asciz "getcwd"
ln104:
    .asciz "getegid"
ln105:
    .asciz "getentropy"
ln106:
    .asciz "getenv"
ln107:
    .asciz "geteuid"
ln108:
    .asciz "getgid"
ln109:
    .asciz "getpagesize"
ln110:
    .asciz "getpid"
ln111:
    .asciz "getrandom"
ln112:
    .asciz "getrusage"
ln113:
    .asciz "gets"
ln114:
    .asciz "gettimeofday"
ln115:
    .asciz "getuid"
ln116:
    .asciz "gmtime"
ln117:
    .asciz "gmtime_r"
ln118:
    .asciz "imaxabs"
ln119:
    .asciz "imaxdiv"
ln120:
    .asciz "interrompi"
ln121:
    .asciz "ioctl"
ln122:
    .asciz "ioport_bind"
ln123:
    .asciz "ioport_in"
ln124:
    .asciz "ioport_in16"
ln125:
    .asciz "ioport_in32"
ln126:
    .asciz "ioport_out"
ln127:
    .asciz "ioport_out16"
ln128:
    .asciz "ioport_out32"
ln129:
    .asciz "ipc_attendi"
ln130:
    .asciz "ipc_lookup"
ln131:
    .asciz "ipc_pronto"
ln132:
    .asciz "ipc_recv"
ln133:
    .asciz "ipc_recv_timeout"
ln134:
    .asciz "ipc_register"
ln135:
    .asciz "ipc_rimetti"
ln136:
    .asciz "ipc_scegli"
ln137:
    .asciz "ipc_send"
ln138:
    .asciz "irq_bind"
ln139:
    .asciz "irq_done"
ln140:
    .asciz "irq_unbind"
ln141:
    .asciz "isalnum"
ln142:
    .asciz "isalpha"
ln143:
    .asciz "isascii"
ln144:
    .asciz "isatty"
ln145:
    .asciz "isblank"
ln146:
    .asciz "iscntrl"
ln147:
    .asciz "isdigit"
ln148:
    .asciz "isgraph"
ln149:
    .asciz "islower"
ln150:
    .asciz "isprint"
ln151:
    .asciz "ispunct"
ln152:
    .asciz "isspace"
ln153:
    .asciz "isupper"
ln154:
    .asciz "iswalnum"
ln155:
    .asciz "iswalpha"
ln156:
    .asciz "iswblank"
ln157:
    .asciz "iswcntrl"
ln158:
    .asciz "iswdigit"
ln159:
    .asciz "iswgraph"
ln160:
    .asciz "iswlower"
ln161:
    .asciz "iswprint"
ln162:
    .asciz "iswpunct"
ln163:
    .asciz "iswspace"
ln164:
    .asciz "iswupper"
ln165:
    .asciz "iswxdigit"
ln166:
    .asciz "isxdigit"
ln167:
    .asciz "kbprova"
ln168:
    .asciz "kbstato"
ln169:
    .asciz "klog_leggi"
ln170:
    .asciz "labs"
ln171:
    .asciz "ldexp"
ln172:
    .asciz "ldiv"
ln173:
    .asciz "listdir"
ln174:
    .asciz "listdir_from"
ln175:
    .asciz "llabs"
ln176:
    .asciz "lldiv"
ln177:
    .asciz "localeconv"
ln178:
    .asciz "localtime"
ln179:
    .asciz "localtime_r"
ln180:
    .asciz "log_seriale"
ln181:
    .asciz "longjmp"
ln182:
    .asciz "lseek"
ln183:
    .asciz "lstat"
ln184:
    .asciz "malloc"
ln185:
    .asciz "malloc_usable_size"
ln186:
    .asciz "mblen"
ln187:
    .asciz "mbrtowc"
ln188:
    .asciz "mbstowcs"
ln189:
    .asciz "mbtowc"
ln190:
    .asciz "memalign"
ln191:
    .asciz "memchr"
ln192:
    .asciz "memcmp"
ln193:
    .asciz "memcpy"
ln194:
    .asciz "meminfo"
ln195:
    .asciz "memmove"
ln196:
    .asciz "memset"
ln197:
    .asciz "mkdir"
ln198:
    .asciz "mkstemp"
ln199:
    .asciz "mktemp"
ln200:
    .asciz "mktime"
ln201:
    .asciz "mmap"
ln202:
    .asciz "mmio_map"
ln203:
    .asciz "modo_testo"
ln204:
    .asciz "mount"
ln205:
    .asciz "mountinfo"
ln206:
    .asciz "munmap"
ln207:
    .asciz "mutex_lascia"
ln208:
    .asciz "mutex_prendi"
ln209:
    .asciz "mutex_prova"
ln210:
    .asciz "nanosleep"
ln211:
    .asciz "nome_utente"
ln212:
    .asciz "open"
ln213:
    .asciz "opendir"
ln214:
    .asciz "osversion"
ln215:
    .asciz "partwrite"
ln216:
    .asciz "pathconf"
ln217:
    .asciz "pclose"
ln218:
    .asciz "perror"
ln219:
    .asciz "pipe"
ln220:
    .asciz "poll"
ln221:
    .asciz "popen"
ln222:
    .asciz "posix_memalign"
ln223:
    .asciz "printf"
ln224:
    .asciz "proc_gruppo"
ln225:
    .asciz "procinfo"
ln226:
    .asciz "pthread_attr_destroy"
ln227:
    .asciz "pthread_attr_getdetachstate"
ln228:
    .asciz "pthread_attr_getschedparam"
ln229:
    .asciz "pthread_attr_getschedpolicy"
ln230:
    .asciz "pthread_attr_getstack"
ln231:
    .asciz "pthread_attr_getstacksize"
ln232:
    .asciz "pthread_attr_init"
ln233:
    .asciz "pthread_attr_setdetachstate"
ln234:
    .asciz "pthread_attr_setguardsize"
ln235:
    .asciz "pthread_attr_setinheritsched"
ln236:
    .asciz "pthread_attr_setschedparam"
ln237:
    .asciz "pthread_attr_setschedpolicy"
ln238:
    .asciz "pthread_attr_setscope"
ln239:
    .asciz "pthread_attr_setstacksize"
ln240:
    .asciz "pthread_cond_broadcast"
ln241:
    .asciz "pthread_cond_destroy"
ln242:
    .asciz "pthread_cond_init"
ln243:
    .asciz "pthread_cond_signal"
ln244:
    .asciz "pthread_cond_timedwait"
ln245:
    .asciz "pthread_cond_wait"
ln246:
    .asciz "pthread_condattr_destroy"
ln247:
    .asciz "pthread_condattr_getclock"
ln248:
    .asciz "pthread_condattr_init"
ln249:
    .asciz "pthread_condattr_setclock"
ln250:
    .asciz "pthread_create"
ln251:
    .asciz "pthread_detach"
ln252:
    .asciz "pthread_equal"
ln253:
    .asciz "pthread_exit"
ln254:
    .asciz "pthread_getattr_np"
ln255:
    .asciz "pthread_getname_np"
ln256:
    .asciz "pthread_getschedparam"
ln257:
    .asciz "pthread_getspecific"
ln258:
    .asciz "pthread_join"
ln259:
    .asciz "pthread_key_create"
ln260:
    .asciz "pthread_key_delete"
ln261:
    .asciz "pthread_kill"
ln262:
    .asciz "pthread_mutex_destroy"
ln263:
    .asciz "pthread_mutex_init"
ln264:
    .asciz "pthread_mutex_lock"
ln265:
    .asciz "pthread_mutex_timedlock"
ln266:
    .asciz "pthread_mutex_trylock"
ln267:
    .asciz "pthread_mutex_unlock"
ln268:
    .asciz "pthread_mutexattr_destroy"
ln269:
    .asciz "pthread_mutexattr_gettype"
ln270:
    .asciz "pthread_mutexattr_init"
ln271:
    .asciz "pthread_mutexattr_settype"
ln272:
    .asciz "pthread_once"
ln273:
    .asciz "pthread_rwlock_destroy"
ln274:
    .asciz "pthread_rwlock_init"
ln275:
    .asciz "pthread_rwlock_rdlock"
ln276:
    .asciz "pthread_rwlock_tryrdlock"
ln277:
    .asciz "pthread_rwlock_trywrlock"
ln278:
    .asciz "pthread_rwlock_unlock"
ln279:
    .asciz "pthread_rwlock_wrlock"
ln280:
    .asciz "pthread_self"
ln281:
    .asciz "pthread_setname_np"
ln282:
    .asciz "pthread_setschedparam"
ln283:
    .asciz "pthread_setspecific"
ln284:
    .asciz "pthread_sigmask"
ln285:
    .asciz "pthread_yield"
ln286:
    .asciz "pty_apri"
ln287:
    .asciz "pty_ctl"
ln288:
    .asciz "putc"
ln289:
    .asciz "putchar"
ln290:
    .asciz "putenv"
ln291:
    .asciz "puts"
ln292:
    .asciz "qsort"
ln293:
    .asciz "quick_exit"
ln294:
    .asciz "raise"
ln295:
    .asciz "rand"
ln296:
    .asciz "read"
ln297:
    .asciz "readdir"
ln298:
    .asciz "realloc"
ln299:
    .asciz "realpath"
ln300:
    .asciz "reboot"
ln301:
    .asciz "remove"
ln302:
    .asciz "rename"
ln303:
    .asciz "rewind"
ln304:
    .asciz "rewinddir"
ln305:
    .asciz "rmdir"
ln306:
    .asciz "sbrk"
ln307:
    .asciz "scanf"
ln308:
    .asciz "sched_yield"
ln309:
    .asciz "select"
ln310:
    .asciz "sem_destroy"
ln311:
    .asciz "sem_getvalue"
ln312:
    .asciz "sem_init"
ln313:
    .asciz "sem_post"
ln314:
    .asciz "sem_timedwait"
ln315:
    .asciz "sem_trywait"
ln316:
    .asciz "sem_wait"
ln317:
    .asciz "semaforo_lascia"
ln318:
    .asciz "semaforo_prendi"
ln319:
    .asciz "semaforo_prendi_ms"
ln320:
    .asciz "semaforo_prova"
ln321:
    .asciz "setbuf"
ln322:
    .asciz "setenv"
ln323:
    .asciz "setjmp"
ln324:
    .asciz "setlocale"
ln325:
    .asciz "setuid"
ln326:
    .asciz "setvbuf"
ln327:
    .asciz "sha256"
ln328:
    .asciz "sha256_avvia"
ln329:
    .asciz "sha256_dai"
ln330:
    .asciz "sha256_esa"
ln331:
    .asciz "sha256_fine"
ln332:
    .asciz "sha256_fine_esa"
ln333:
    .asciz "shm_apri"
ln334:
    .asciz "shm_chiudi"
ln335:
    .asciz "signal"
ln336:
    .asciz "sleep"
ln337:
    .asciz "snprintf"
ln338:
    .asciz "spawn"
ln339:
    .asciz "spawn_ex"
ln340:
    .asciz "spawn_su_console"
ln341:
    .asciz "spawn_utente"
ln342:
    .asciz "sprintf"
ln343:
    .asciz "sqrt"
ln344:
    .asciz "srand"
ln345:
    .asciz "sscanf"
ln346:
    .asciz "stat"
ln347:
    .asciz "statperm"
ln348:
    .asciz "statraw"
ln349:
    .asciz "strcasecmp"
ln350:
    .asciz "strcat"
ln351:
    .asciz "strchr"
ln352:
    .asciz "strcmp"
ln353:
    .asciz "strcoll"
ln354:
    .asciz "strcpy"
ln355:
    .asciz "strcspn"
ln356:
    .asciz "strdup"
ln357:
    .asciz "strerror"
ln358:
    .asciz "strerror_r"
ln359:
    .asciz "strftime"
ln360:
    .asciz "strlen"
ln361:
    .asciz "strncasecmp"
ln362:
    .asciz "strncat"
ln363:
    .asciz "strncmp"
ln364:
    .asciz "strncpy"
ln365:
    .asciz "strpbrk"
ln366:
    .asciz "strrchr"
ln367:
    .asciz "strsignal"
ln368:
    .asciz "strspn"
ln369:
    .asciz "strstr"
ln370:
    .asciz "strtod"
ln371:
    .asciz "strtof"
ln372:
    .asciz "strtoimax"
ln373:
    .asciz "strtok"
ln374:
    .asciz "strtok_r"
ln375:
    .asciz "strtol"
ln376:
    .asciz "strtold"
ln377:
    .asciz "strtoll"
ln378:
    .asciz "strtoul"
ln379:
    .asciz "strtoull"
ln380:
    .asciz "strtoumax"
ln381:
    .asciz "strxfrm"
ln382:
    .asciz "swprintf"
ln383:
    .asciz "sysconf"
ln384:
    .asciz "system"
ln385:
    .asciz "thread_attendi"
ln386:
    .asciz "thread_crea"
ln387:
    .asciz "thread_devo_fermarmi"
ln388:
    .asciz "thread_esci"
ln389:
    .asciz "thread_ferma"
ln390:
    .asciz "time"
ln391:
    .asciz "time_now"
ln392:
    .asciz "time_set"
ln393:
    .asciz "timegm"
ln394:
    .asciz "times"
ln395:
    .asciz "timespec_get"
ln396:
    .asciz "tmpfile"
ln397:
    .asciz "tmpnam"
ln398:
    .asciz "toascii"
ln399:
    .asciz "tolower"
ln400:
    .asciz "toupper"
ln401:
    .asciz "towlower"
ln402:
    .asciz "towupper"
ln403:
    .asciz "truncate"
ln404:
    .asciz "tty_clear"
ln405:
    .asciz "tty_getsize"
ln406:
    .asciz "tty_raw"
ln407:
    .asciz "umask"
ln408:
    .asciz "umount"
ln409:
    .asciz "ungetc"
ln410:
    .asciz "unlink"
ln411:
    .asciz "unsetenv"
ln412:
    .asciz "uptime_ms"
ln413:
    .asciz "usleep"
ln414:
    .asciz "utime"
ln415:
    .asciz "verboseboot"
ln416:
    .asciz "vfprintf"
ln417:
    .asciz "vfscanf"
ln418:
    .asciz "video_info"
ln419:
    .asciz "vprintf"
ln420:
    .asciz "vscanf"
ln421:
    .asciz "vsnprintf"
ln422:
    .asciz "vsprintf"
ln423:
    .asciz "vsscanf"
ln424:
    .asciz "vswprintf"
ln425:
    .asciz "wait"
ln426:
    .asciz "waitpid"
ln427:
    .asciz "wcscat"
ln428:
    .asciz "wcschr"
ln429:
    .asciz "wcscmp"
ln430:
    .asciz "wcscpy"
ln431:
    .asciz "wcscspn"
ln432:
    .asciz "wcslen"
ln433:
    .asciz "wcsncat"
ln434:
    .asciz "wcsncmp"
ln435:
    .asciz "wcsncpy"
ln436:
    .asciz "wcspbrk"
ln437:
    .asciz "wcsrchr"
ln438:
    .asciz "wcsspn"
ln439:
    .asciz "wcsstr"
ln440:
    .asciz "wcstod"
ln441:
    .asciz "wcstol"
ln442:
    .asciz "wcstoll"
ln443:
    .asciz "wcstombs"
ln444:
    .asciz "wcstoul"
ln445:
    .asciz "wcstoull"
ln446:
    .asciz "wctomb"
ln447:
    .asciz "wmemchr"
ln448:
    .asciz "wmemcmp"
ln449:
    .asciz "wmemcpy"
ln450:
    .asciz "wmemmove"
ln451:
    .asciz "wmemset"
ln452:
    .asciz "write"

    .align 4
libc_nomi:
    .long ln0
    .long ln1
    .long ln2
    .long ln3
    .long ln4
    .long ln5
    .long ln6
    .long ln7
    .long ln8
    .long ln9
    .long ln10
    .long ln11
    .long ln12
    .long ln13
    .long ln14
    .long ln15
    .long ln16
    .long ln17
    .long ln18
    .long ln19
    .long ln20
    .long ln21
    .long ln22
    .long ln23
    .long ln24
    .long ln25
    .long ln26
    .long ln27
    .long ln28
    .long ln29
    .long ln30
    .long ln31
    .long ln32
    .long ln33
    .long ln34
    .long ln35
    .long ln36
    .long ln37
    .long ln38
    .long ln39
    .long ln40
    .long ln41
    .long ln42
    .long ln43
    .long ln44
    .long ln45
    .long ln46
    .long ln47
    .long ln48
    .long ln49
    .long ln50
    .long ln51
    .long ln52
    .long ln53
    .long ln54
    .long ln55
    .long ln56
    .long ln57
    .long ln58
    .long ln59
    .long ln60
    .long ln61
    .long ln62
    .long ln63
    .long ln64
    .long ln65
    .long ln66
    .long ln67
    .long ln68
    .long ln69
    .long ln70
    .long ln71
    .long ln72
    .long ln73
    .long ln74
    .long ln75
    .long ln76
    .long ln77
    .long ln78
    .long ln79
    .long ln80
    .long ln81
    .long ln82
    .long ln83
    .long ln84
    .long ln85
    .long ln86
    .long ln87
    .long ln88
    .long ln89
    .long ln90
    .long ln91
    .long ln92
    .long ln93
    .long ln94
    .long ln95
    .long ln96
    .long ln97
    .long ln98
    .long ln99
    .long ln100
    .long ln101
    .long ln102
    .long ln103
    .long ln104
    .long ln105
    .long ln106
    .long ln107
    .long ln108
    .long ln109
    .long ln110
    .long ln111
    .long ln112
    .long ln113
    .long ln114
    .long ln115
    .long ln116
    .long ln117
    .long ln118
    .long ln119
    .long ln120
    .long ln121
    .long ln122
    .long ln123
    .long ln124
    .long ln125
    .long ln126
    .long ln127
    .long ln128
    .long ln129
    .long ln130
    .long ln131
    .long ln132
    .long ln133
    .long ln134
    .long ln135
    .long ln136
    .long ln137
    .long ln138
    .long ln139
    .long ln140
    .long ln141
    .long ln142
    .long ln143
    .long ln144
    .long ln145
    .long ln146
    .long ln147
    .long ln148
    .long ln149
    .long ln150
    .long ln151
    .long ln152
    .long ln153
    .long ln154
    .long ln155
    .long ln156
    .long ln157
    .long ln158
    .long ln159
    .long ln160
    .long ln161
    .long ln162
    .long ln163
    .long ln164
    .long ln165
    .long ln166
    .long ln167
    .long ln168
    .long ln169
    .long ln170
    .long ln171
    .long ln172
    .long ln173
    .long ln174
    .long ln175
    .long ln176
    .long ln177
    .long ln178
    .long ln179
    .long ln180
    .long ln181
    .long ln182
    .long ln183
    .long ln184
    .long ln185
    .long ln186
    .long ln187
    .long ln188
    .long ln189
    .long ln190
    .long ln191
    .long ln192
    .long ln193
    .long ln194
    .long ln195
    .long ln196
    .long ln197
    .long ln198
    .long ln199
    .long ln200
    .long ln201
    .long ln202
    .long ln203
    .long ln204
    .long ln205
    .long ln206
    .long ln207
    .long ln208
    .long ln209
    .long ln210
    .long ln211
    .long ln212
    .long ln213
    .long ln214
    .long ln215
    .long ln216
    .long ln217
    .long ln218
    .long ln219
    .long ln220
    .long ln221
    .long ln222
    .long ln223
    .long ln224
    .long ln225
    .long ln226
    .long ln227
    .long ln228
    .long ln229
    .long ln230
    .long ln231
    .long ln232
    .long ln233
    .long ln234
    .long ln235
    .long ln236
    .long ln237
    .long ln238
    .long ln239
    .long ln240
    .long ln241
    .long ln242
    .long ln243
    .long ln244
    .long ln245
    .long ln246
    .long ln247
    .long ln248
    .long ln249
    .long ln250
    .long ln251
    .long ln252
    .long ln253
    .long ln254
    .long ln255
    .long ln256
    .long ln257
    .long ln258
    .long ln259
    .long ln260
    .long ln261
    .long ln262
    .long ln263
    .long ln264
    .long ln265
    .long ln266
    .long ln267
    .long ln268
    .long ln269
    .long ln270
    .long ln271
    .long ln272
    .long ln273
    .long ln274
    .long ln275
    .long ln276
    .long ln277
    .long ln278
    .long ln279
    .long ln280
    .long ln281
    .long ln282
    .long ln283
    .long ln284
    .long ln285
    .long ln286
    .long ln287
    .long ln288
    .long ln289
    .long ln290
    .long ln291
    .long ln292
    .long ln293
    .long ln294
    .long ln295
    .long ln296
    .long ln297
    .long ln298
    .long ln299
    .long ln300
    .long ln301
    .long ln302
    .long ln303
    .long ln304
    .long ln305
    .long ln306
    .long ln307
    .long ln308
    .long ln309
    .long ln310
    .long ln311
    .long ln312
    .long ln313
    .long ln314
    .long ln315
    .long ln316
    .long ln317
    .long ln318
    .long ln319
    .long ln320
    .long ln321
    .long ln322
    .long ln323
    .long ln324
    .long ln325
    .long ln326
    .long ln327
    .long ln328
    .long ln329
    .long ln330
    .long ln331
    .long ln332
    .long ln333
    .long ln334
    .long ln335
    .long ln336
    .long ln337
    .long ln338
    .long ln339
    .long ln340
    .long ln341
    .long ln342
    .long ln343
    .long ln344
    .long ln345
    .long ln346
    .long ln347
    .long ln348
    .long ln349
    .long ln350
    .long ln351
    .long ln352
    .long ln353
    .long ln354
    .long ln355
    .long ln356
    .long ln357
    .long ln358
    .long ln359
    .long ln360
    .long ln361
    .long ln362
    .long ln363
    .long ln364
    .long ln365
    .long ln366
    .long ln367
    .long ln368
    .long ln369
    .long ln370
    .long ln371
    .long ln372
    .long ln373
    .long ln374
    .long ln375
    .long ln376
    .long ln377
    .long ln378
    .long ln379
    .long ln380
    .long ln381
    .long ln382
    .long ln383
    .long ln384
    .long ln385
    .long ln386
    .long ln387
    .long ln388
    .long ln389
    .long ln390
    .long ln391
    .long ln392
    .long ln393
    .long ln394
    .long ln395
    .long ln396
    .long ln397
    .long ln398
    .long ln399
    .long ln400
    .long ln401
    .long ln402
    .long ln403
    .long ln404
    .long ln405
    .long ln406
    .long ln407
    .long ln408
    .long ln409
    .long ln410
    .long ln411
    .long ln412
    .long ln413
    .long ln414
    .long ln415
    .long ln416
    .long ln417
    .long ln418
    .long ln419
    .long ln420
    .long ln421
    .long ln422
    .long ln423
    .long ln424
    .long ln425
    .long ln426
    .long ln427
    .long ln428
    .long ln429
    .long ln430
    .long ln431
    .long ln432
    .long ln433
    .long ln434
    .long ln435
    .long ln436
    .long ln437
    .long ln438
    .long ln439
    .long ln440
    .long ln441
    .long ln442
    .long ln443
    .long ln444
    .long ln445
    .long ln446
    .long ln447
    .long ln448
    .long ln449
    .long ln450
    .long ln451
    .long ln452

    .align 4
libc_indirizzi:
    .long _Exit
    .long __environ_dove
    .long __errno_dove
    .long __libc_distruttori_registra
    .long __stderr_dove
    .long __stdin_dove
    .long __stdout_dove
    .long _assert_fallita
    .long _exit
    .long abort
    .long abs
    .long access
    .long ahci_guarda
    .long ahci_passa
    .long ahci_stato
    .long aligned_alloc
    .long asctime
    .long at_quick_exit
    .long atexit
    .long atof
    .long atoi
    .long atol
    .long atoll
    .long attesa_dormi
    .long attesa_sveglia
    .long blk_attendi
    .long blk_espelli
    .long blk_offri
    .long blk_risposta
    .long blk_scansiona
    .long blkinfo
    .long blkread
    .long blkwrite
    .long bootinstall
    .long bootverify
    .long bsearch
    .long calloc
    .long chdir
    .long chmod
    .long chown
    .long clearerr
    .long clock
    .long clock_getres
    .long clock_gettime
    .long close
    .long closedir
    .long condizione_aspetta
    .long condizione_aspetta_ms
    .long condizione_segnala
    .long condizione_segnala_tutti
    .long console_ctrlc
    .long console_grafica
    .long console_info
    .long console_registro
    .long console_setfg
    .long console_switch
    .long console_testo
    .long console_write
    .long cpu_info
    .long ctime
    .long difftime
    .long diskinfo
    .long div
    .long diventa_root
    .long dma_alloc
    .long dup
    .long dup2
    .long execv
    .long execvp
    .long exit
    .long fabs
    .long fb_map
    .long fchmod
    .long fclose
    .long fcntl
    .long fdopen
    .long fdprova
    .long feof
    .long ferror
    .long fflush
    .long fgetc
    .long fgetpos
    .long fgets
    .long fileno
    .long fopen
    .long fpathconf
    .long fprintf
    .long fputc
    .long fputs
    .long fread
    .long free
    .long freopen
    .long frexp
    .long fscanf
    .long fseek
    .long fsetpos
    .long fsize
    .long fstat
    .long ftell
    .long fwrite
    .long getc
    .long getchar
    .long getconf
    .long getcwd
    .long getegid
    .long getentropy
    .long getenv
    .long geteuid
    .long getgid
    .long getpagesize
    .long getpid
    .long getrandom
    .long getrusage
    .long gets
    .long gettimeofday
    .long getuid
    .long gmtime
    .long gmtime_r
    .long imaxabs
    .long imaxdiv
    .long interrompi
    .long ioctl
    .long ioport_bind
    .long ioport_in
    .long ioport_in16
    .long ioport_in32
    .long ioport_out
    .long ioport_out16
    .long ioport_out32
    .long ipc_attendi
    .long ipc_lookup
    .long ipc_pronto
    .long ipc_recv
    .long ipc_recv_timeout
    .long ipc_register
    .long ipc_rimetti
    .long ipc_scegli
    .long ipc_send
    .long irq_bind
    .long irq_done
    .long irq_unbind
    .long isalnum
    .long isalpha
    .long isascii
    .long isatty
    .long isblank
    .long iscntrl
    .long isdigit
    .long isgraph
    .long islower
    .long isprint
    .long ispunct
    .long isspace
    .long isupper
    .long iswalnum
    .long iswalpha
    .long iswblank
    .long iswcntrl
    .long iswdigit
    .long iswgraph
    .long iswlower
    .long iswprint
    .long iswpunct
    .long iswspace
    .long iswupper
    .long iswxdigit
    .long isxdigit
    .long kbprova
    .long kbstato
    .long klog_leggi
    .long labs
    .long ldexp
    .long ldiv
    .long listdir
    .long listdir_from
    .long llabs
    .long lldiv
    .long localeconv
    .long localtime
    .long localtime_r
    .long log_seriale
    .long longjmp
    .long lseek
    .long lstat
    .long malloc
    .long malloc_usable_size
    .long mblen
    .long mbrtowc
    .long mbstowcs
    .long mbtowc
    .long memalign
    .long memchr
    .long memcmp
    .long memcpy
    .long meminfo
    .long memmove
    .long memset
    .long mkdir
    .long mkstemp
    .long mktemp
    .long mktime
    .long mmap
    .long mmio_map
    .long modo_testo
    .long mount
    .long mountinfo
    .long munmap
    .long mutex_lascia
    .long mutex_prendi
    .long mutex_prova
    .long nanosleep
    .long nome_utente
    .long open
    .long opendir
    .long osversion
    .long partwrite
    .long pathconf
    .long pclose
    .long perror
    .long pipe
    .long poll
    .long popen
    .long posix_memalign
    .long printf
    .long proc_gruppo
    .long procinfo
    .long pthread_attr_destroy
    .long pthread_attr_getdetachstate
    .long pthread_attr_getschedparam
    .long pthread_attr_getschedpolicy
    .long pthread_attr_getstack
    .long pthread_attr_getstacksize
    .long pthread_attr_init
    .long pthread_attr_setdetachstate
    .long pthread_attr_setguardsize
    .long pthread_attr_setinheritsched
    .long pthread_attr_setschedparam
    .long pthread_attr_setschedpolicy
    .long pthread_attr_setscope
    .long pthread_attr_setstacksize
    .long pthread_cond_broadcast
    .long pthread_cond_destroy
    .long pthread_cond_init
    .long pthread_cond_signal
    .long pthread_cond_timedwait
    .long pthread_cond_wait
    .long pthread_condattr_destroy
    .long pthread_condattr_getclock
    .long pthread_condattr_init
    .long pthread_condattr_setclock
    .long pthread_create
    .long pthread_detach
    .long pthread_equal
    .long pthread_exit
    .long pthread_getattr_np
    .long pthread_getname_np
    .long pthread_getschedparam
    .long pthread_getspecific
    .long pthread_join
    .long pthread_key_create
    .long pthread_key_delete
    .long pthread_kill
    .long pthread_mutex_destroy
    .long pthread_mutex_init
    .long pthread_mutex_lock
    .long pthread_mutex_timedlock
    .long pthread_mutex_trylock
    .long pthread_mutex_unlock
    .long pthread_mutexattr_destroy
    .long pthread_mutexattr_gettype
    .long pthread_mutexattr_init
    .long pthread_mutexattr_settype
    .long pthread_once
    .long pthread_rwlock_destroy
    .long pthread_rwlock_init
    .long pthread_rwlock_rdlock
    .long pthread_rwlock_tryrdlock
    .long pthread_rwlock_trywrlock
    .long pthread_rwlock_unlock
    .long pthread_rwlock_wrlock
    .long pthread_self
    .long pthread_setname_np
    .long pthread_setschedparam
    .long pthread_setspecific
    .long pthread_sigmask
    .long pthread_yield
    .long pty_apri
    .long pty_ctl
    .long putc
    .long putchar
    .long putenv
    .long puts
    .long qsort
    .long quick_exit
    .long raise
    .long rand
    .long read
    .long readdir
    .long realloc
    .long realpath
    .long reboot
    .long remove
    .long rename
    .long rewind
    .long rewinddir
    .long rmdir
    .long sbrk
    .long scanf
    .long sched_yield
    .long select
    .long sem_destroy
    .long sem_getvalue
    .long sem_init
    .long sem_post
    .long sem_timedwait
    .long sem_trywait
    .long sem_wait
    .long semaforo_lascia
    .long semaforo_prendi
    .long semaforo_prendi_ms
    .long semaforo_prova
    .long setbuf
    .long setenv
    .long setjmp
    .long setlocale
    .long setuid
    .long setvbuf
    .long sha256
    .long sha256_avvia
    .long sha256_dai
    .long sha256_esa
    .long sha256_fine
    .long sha256_fine_esa
    .long shm_apri
    .long shm_chiudi
    .long signal
    .long sleep
    .long snprintf
    .long spawn
    .long spawn_ex
    .long spawn_su_console
    .long spawn_utente
    .long sprintf
    .long sqrt
    .long srand
    .long sscanf
    .long stat
    .long statperm
    .long statraw
    .long strcasecmp
    .long strcat
    .long strchr
    .long strcmp
    .long strcoll
    .long strcpy
    .long strcspn
    .long strdup
    .long strerror
    .long strerror_r
    .long strftime
    .long strlen
    .long strncasecmp
    .long strncat
    .long strncmp
    .long strncpy
    .long strpbrk
    .long strrchr
    .long strsignal
    .long strspn
    .long strstr
    .long strtod
    .long strtof
    .long strtoimax
    .long strtok
    .long strtok_r
    .long strtol
    .long strtold
    .long strtoll
    .long strtoul
    .long strtoull
    .long strtoumax
    .long strxfrm
    .long swprintf
    .long sysconf
    .long system
    .long thread_attendi
    .long thread_crea
    .long thread_devo_fermarmi
    .long thread_esci
    .long thread_ferma
    .long time
    .long time_now
    .long time_set
    .long timegm
    .long times
    .long timespec_get
    .long tmpfile
    .long tmpnam
    .long toascii
    .long tolower
    .long toupper
    .long towlower
    .long towupper
    .long truncate
    .long tty_clear
    .long tty_getsize
    .long tty_raw
    .long umask
    .long umount
    .long ungetc
    .long unlink
    .long unsetenv
    .long uptime_ms
    .long usleep
    .long utime
    .long verboseboot
    .long vfprintf
    .long vfscanf
    .long video_info
    .long vprintf
    .long vscanf
    .long vsnprintf
    .long vsprintf
    .long vsscanf
    .long vswprintf
    .long wait
    .long waitpid
    .long wcscat
    .long wcschr
    .long wcscmp
    .long wcscpy
    .long wcscspn
    .long wcslen
    .long wcsncat
    .long wcsncmp
    .long wcsncpy
    .long wcspbrk
    .long wcsrchr
    .long wcsspn
    .long wcsstr
    .long wcstod
    .long wcstol
    .long wcstoll
    .long wcstombs
    .long wcstoul
    .long wcstoull
    .long wctomb
    .long wmemchr
    .long wmemcmp
    .long wmemcpy
    .long wmemmove
    .long wmemset
    .long write

    .section .exlib_testa,"a"
    .align 4
    .globl libc_tabella
libc_tabella:
    .long 0x424C5845        /* EXLIB_MAGIA, 'EXLB' */
    .long 1                 /* EXLIB_VERSIONE */
    .long 453
    .long libc_nomi
    .long libc_indirizzi

    .section .note.GNU-stack,"",@progbits
