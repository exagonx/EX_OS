/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * La tabella che libc.so mette a disposizione: 454 funzioni, ognuna col suo
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
    .asciz "cpu_lavora"
ln60:
    .asciz "ctime"
ln61:
    .asciz "difftime"
ln62:
    .asciz "diskinfo"
ln63:
    .asciz "div"
ln64:
    .asciz "diventa_root"
ln65:
    .asciz "dma_alloc"
ln66:
    .asciz "dup"
ln67:
    .asciz "dup2"
ln68:
    .asciz "execv"
ln69:
    .asciz "execvp"
ln70:
    .asciz "exit"
ln71:
    .asciz "fabs"
ln72:
    .asciz "fb_map"
ln73:
    .asciz "fchmod"
ln74:
    .asciz "fclose"
ln75:
    .asciz "fcntl"
ln76:
    .asciz "fdopen"
ln77:
    .asciz "fdprova"
ln78:
    .asciz "feof"
ln79:
    .asciz "ferror"
ln80:
    .asciz "fflush"
ln81:
    .asciz "fgetc"
ln82:
    .asciz "fgetpos"
ln83:
    .asciz "fgets"
ln84:
    .asciz "fileno"
ln85:
    .asciz "fopen"
ln86:
    .asciz "fpathconf"
ln87:
    .asciz "fprintf"
ln88:
    .asciz "fputc"
ln89:
    .asciz "fputs"
ln90:
    .asciz "fread"
ln91:
    .asciz "free"
ln92:
    .asciz "freopen"
ln93:
    .asciz "frexp"
ln94:
    .asciz "fscanf"
ln95:
    .asciz "fseek"
ln96:
    .asciz "fsetpos"
ln97:
    .asciz "fsize"
ln98:
    .asciz "fstat"
ln99:
    .asciz "ftell"
ln100:
    .asciz "fwrite"
ln101:
    .asciz "getc"
ln102:
    .asciz "getchar"
ln103:
    .asciz "getconf"
ln104:
    .asciz "getcwd"
ln105:
    .asciz "getegid"
ln106:
    .asciz "getentropy"
ln107:
    .asciz "getenv"
ln108:
    .asciz "geteuid"
ln109:
    .asciz "getgid"
ln110:
    .asciz "getpagesize"
ln111:
    .asciz "getpid"
ln112:
    .asciz "getrandom"
ln113:
    .asciz "getrusage"
ln114:
    .asciz "gets"
ln115:
    .asciz "gettimeofday"
ln116:
    .asciz "getuid"
ln117:
    .asciz "gmtime"
ln118:
    .asciz "gmtime_r"
ln119:
    .asciz "imaxabs"
ln120:
    .asciz "imaxdiv"
ln121:
    .asciz "interrompi"
ln122:
    .asciz "ioctl"
ln123:
    .asciz "ioport_bind"
ln124:
    .asciz "ioport_in"
ln125:
    .asciz "ioport_in16"
ln126:
    .asciz "ioport_in32"
ln127:
    .asciz "ioport_out"
ln128:
    .asciz "ioport_out16"
ln129:
    .asciz "ioport_out32"
ln130:
    .asciz "ipc_attendi"
ln131:
    .asciz "ipc_lookup"
ln132:
    .asciz "ipc_pronto"
ln133:
    .asciz "ipc_recv"
ln134:
    .asciz "ipc_recv_timeout"
ln135:
    .asciz "ipc_register"
ln136:
    .asciz "ipc_rimetti"
ln137:
    .asciz "ipc_scegli"
ln138:
    .asciz "ipc_send"
ln139:
    .asciz "irq_bind"
ln140:
    .asciz "irq_done"
ln141:
    .asciz "irq_unbind"
ln142:
    .asciz "isalnum"
ln143:
    .asciz "isalpha"
ln144:
    .asciz "isascii"
ln145:
    .asciz "isatty"
ln146:
    .asciz "isblank"
ln147:
    .asciz "iscntrl"
ln148:
    .asciz "isdigit"
ln149:
    .asciz "isgraph"
ln150:
    .asciz "islower"
ln151:
    .asciz "isprint"
ln152:
    .asciz "ispunct"
ln153:
    .asciz "isspace"
ln154:
    .asciz "isupper"
ln155:
    .asciz "iswalnum"
ln156:
    .asciz "iswalpha"
ln157:
    .asciz "iswblank"
ln158:
    .asciz "iswcntrl"
ln159:
    .asciz "iswdigit"
ln160:
    .asciz "iswgraph"
ln161:
    .asciz "iswlower"
ln162:
    .asciz "iswprint"
ln163:
    .asciz "iswpunct"
ln164:
    .asciz "iswspace"
ln165:
    .asciz "iswupper"
ln166:
    .asciz "iswxdigit"
ln167:
    .asciz "isxdigit"
ln168:
    .asciz "kbprova"
ln169:
    .asciz "kbstato"
ln170:
    .asciz "klog_leggi"
ln171:
    .asciz "labs"
ln172:
    .asciz "ldexp"
ln173:
    .asciz "ldiv"
ln174:
    .asciz "listdir"
ln175:
    .asciz "listdir_from"
ln176:
    .asciz "llabs"
ln177:
    .asciz "lldiv"
ln178:
    .asciz "localeconv"
ln179:
    .asciz "localtime"
ln180:
    .asciz "localtime_r"
ln181:
    .asciz "log_seriale"
ln182:
    .asciz "longjmp"
ln183:
    .asciz "lseek"
ln184:
    .asciz "lstat"
ln185:
    .asciz "malloc"
ln186:
    .asciz "malloc_usable_size"
ln187:
    .asciz "mblen"
ln188:
    .asciz "mbrtowc"
ln189:
    .asciz "mbstowcs"
ln190:
    .asciz "mbtowc"
ln191:
    .asciz "memalign"
ln192:
    .asciz "memchr"
ln193:
    .asciz "memcmp"
ln194:
    .asciz "memcpy"
ln195:
    .asciz "meminfo"
ln196:
    .asciz "memmove"
ln197:
    .asciz "memset"
ln198:
    .asciz "mkdir"
ln199:
    .asciz "mkstemp"
ln200:
    .asciz "mktemp"
ln201:
    .asciz "mktime"
ln202:
    .asciz "mmap"
ln203:
    .asciz "mmio_map"
ln204:
    .asciz "modo_testo"
ln205:
    .asciz "mount"
ln206:
    .asciz "mountinfo"
ln207:
    .asciz "munmap"
ln208:
    .asciz "mutex_lascia"
ln209:
    .asciz "mutex_prendi"
ln210:
    .asciz "mutex_prova"
ln211:
    .asciz "nanosleep"
ln212:
    .asciz "nome_utente"
ln213:
    .asciz "open"
ln214:
    .asciz "opendir"
ln215:
    .asciz "osversion"
ln216:
    .asciz "partwrite"
ln217:
    .asciz "pathconf"
ln218:
    .asciz "pclose"
ln219:
    .asciz "perror"
ln220:
    .asciz "pipe"
ln221:
    .asciz "poll"
ln222:
    .asciz "popen"
ln223:
    .asciz "posix_memalign"
ln224:
    .asciz "printf"
ln225:
    .asciz "proc_gruppo"
ln226:
    .asciz "procinfo"
ln227:
    .asciz "pthread_attr_destroy"
ln228:
    .asciz "pthread_attr_getdetachstate"
ln229:
    .asciz "pthread_attr_getschedparam"
ln230:
    .asciz "pthread_attr_getschedpolicy"
ln231:
    .asciz "pthread_attr_getstack"
ln232:
    .asciz "pthread_attr_getstacksize"
ln233:
    .asciz "pthread_attr_init"
ln234:
    .asciz "pthread_attr_setdetachstate"
ln235:
    .asciz "pthread_attr_setguardsize"
ln236:
    .asciz "pthread_attr_setinheritsched"
ln237:
    .asciz "pthread_attr_setschedparam"
ln238:
    .asciz "pthread_attr_setschedpolicy"
ln239:
    .asciz "pthread_attr_setscope"
ln240:
    .asciz "pthread_attr_setstacksize"
ln241:
    .asciz "pthread_cond_broadcast"
ln242:
    .asciz "pthread_cond_destroy"
ln243:
    .asciz "pthread_cond_init"
ln244:
    .asciz "pthread_cond_signal"
ln245:
    .asciz "pthread_cond_timedwait"
ln246:
    .asciz "pthread_cond_wait"
ln247:
    .asciz "pthread_condattr_destroy"
ln248:
    .asciz "pthread_condattr_getclock"
ln249:
    .asciz "pthread_condattr_init"
ln250:
    .asciz "pthread_condattr_setclock"
ln251:
    .asciz "pthread_create"
ln252:
    .asciz "pthread_detach"
ln253:
    .asciz "pthread_equal"
ln254:
    .asciz "pthread_exit"
ln255:
    .asciz "pthread_getattr_np"
ln256:
    .asciz "pthread_getname_np"
ln257:
    .asciz "pthread_getschedparam"
ln258:
    .asciz "pthread_getspecific"
ln259:
    .asciz "pthread_join"
ln260:
    .asciz "pthread_key_create"
ln261:
    .asciz "pthread_key_delete"
ln262:
    .asciz "pthread_kill"
ln263:
    .asciz "pthread_mutex_destroy"
ln264:
    .asciz "pthread_mutex_init"
ln265:
    .asciz "pthread_mutex_lock"
ln266:
    .asciz "pthread_mutex_timedlock"
ln267:
    .asciz "pthread_mutex_trylock"
ln268:
    .asciz "pthread_mutex_unlock"
ln269:
    .asciz "pthread_mutexattr_destroy"
ln270:
    .asciz "pthread_mutexattr_gettype"
ln271:
    .asciz "pthread_mutexattr_init"
ln272:
    .asciz "pthread_mutexattr_settype"
ln273:
    .asciz "pthread_once"
ln274:
    .asciz "pthread_rwlock_destroy"
ln275:
    .asciz "pthread_rwlock_init"
ln276:
    .asciz "pthread_rwlock_rdlock"
ln277:
    .asciz "pthread_rwlock_tryrdlock"
ln278:
    .asciz "pthread_rwlock_trywrlock"
ln279:
    .asciz "pthread_rwlock_unlock"
ln280:
    .asciz "pthread_rwlock_wrlock"
ln281:
    .asciz "pthread_self"
ln282:
    .asciz "pthread_setname_np"
ln283:
    .asciz "pthread_setschedparam"
ln284:
    .asciz "pthread_setspecific"
ln285:
    .asciz "pthread_sigmask"
ln286:
    .asciz "pthread_yield"
ln287:
    .asciz "pty_apri"
ln288:
    .asciz "pty_ctl"
ln289:
    .asciz "putc"
ln290:
    .asciz "putchar"
ln291:
    .asciz "putenv"
ln292:
    .asciz "puts"
ln293:
    .asciz "qsort"
ln294:
    .asciz "quick_exit"
ln295:
    .asciz "raise"
ln296:
    .asciz "rand"
ln297:
    .asciz "read"
ln298:
    .asciz "readdir"
ln299:
    .asciz "realloc"
ln300:
    .asciz "realpath"
ln301:
    .asciz "reboot"
ln302:
    .asciz "remove"
ln303:
    .asciz "rename"
ln304:
    .asciz "rewind"
ln305:
    .asciz "rewinddir"
ln306:
    .asciz "rmdir"
ln307:
    .asciz "sbrk"
ln308:
    .asciz "scanf"
ln309:
    .asciz "sched_yield"
ln310:
    .asciz "select"
ln311:
    .asciz "sem_destroy"
ln312:
    .asciz "sem_getvalue"
ln313:
    .asciz "sem_init"
ln314:
    .asciz "sem_post"
ln315:
    .asciz "sem_timedwait"
ln316:
    .asciz "sem_trywait"
ln317:
    .asciz "sem_wait"
ln318:
    .asciz "semaforo_lascia"
ln319:
    .asciz "semaforo_prendi"
ln320:
    .asciz "semaforo_prendi_ms"
ln321:
    .asciz "semaforo_prova"
ln322:
    .asciz "setbuf"
ln323:
    .asciz "setenv"
ln324:
    .asciz "setjmp"
ln325:
    .asciz "setlocale"
ln326:
    .asciz "setuid"
ln327:
    .asciz "setvbuf"
ln328:
    .asciz "sha256"
ln329:
    .asciz "sha256_avvia"
ln330:
    .asciz "sha256_dai"
ln331:
    .asciz "sha256_esa"
ln332:
    .asciz "sha256_fine"
ln333:
    .asciz "sha256_fine_esa"
ln334:
    .asciz "shm_apri"
ln335:
    .asciz "shm_chiudi"
ln336:
    .asciz "signal"
ln337:
    .asciz "sleep"
ln338:
    .asciz "snprintf"
ln339:
    .asciz "spawn"
ln340:
    .asciz "spawn_ex"
ln341:
    .asciz "spawn_su_console"
ln342:
    .asciz "spawn_utente"
ln343:
    .asciz "sprintf"
ln344:
    .asciz "sqrt"
ln345:
    .asciz "srand"
ln346:
    .asciz "sscanf"
ln347:
    .asciz "stat"
ln348:
    .asciz "statperm"
ln349:
    .asciz "statraw"
ln350:
    .asciz "strcasecmp"
ln351:
    .asciz "strcat"
ln352:
    .asciz "strchr"
ln353:
    .asciz "strcmp"
ln354:
    .asciz "strcoll"
ln355:
    .asciz "strcpy"
ln356:
    .asciz "strcspn"
ln357:
    .asciz "strdup"
ln358:
    .asciz "strerror"
ln359:
    .asciz "strerror_r"
ln360:
    .asciz "strftime"
ln361:
    .asciz "strlen"
ln362:
    .asciz "strncasecmp"
ln363:
    .asciz "strncat"
ln364:
    .asciz "strncmp"
ln365:
    .asciz "strncpy"
ln366:
    .asciz "strpbrk"
ln367:
    .asciz "strrchr"
ln368:
    .asciz "strsignal"
ln369:
    .asciz "strspn"
ln370:
    .asciz "strstr"
ln371:
    .asciz "strtod"
ln372:
    .asciz "strtof"
ln373:
    .asciz "strtoimax"
ln374:
    .asciz "strtok"
ln375:
    .asciz "strtok_r"
ln376:
    .asciz "strtol"
ln377:
    .asciz "strtold"
ln378:
    .asciz "strtoll"
ln379:
    .asciz "strtoul"
ln380:
    .asciz "strtoull"
ln381:
    .asciz "strtoumax"
ln382:
    .asciz "strxfrm"
ln383:
    .asciz "swprintf"
ln384:
    .asciz "sysconf"
ln385:
    .asciz "system"
ln386:
    .asciz "thread_attendi"
ln387:
    .asciz "thread_crea"
ln388:
    .asciz "thread_devo_fermarmi"
ln389:
    .asciz "thread_esci"
ln390:
    .asciz "thread_ferma"
ln391:
    .asciz "time"
ln392:
    .asciz "time_now"
ln393:
    .asciz "time_set"
ln394:
    .asciz "timegm"
ln395:
    .asciz "times"
ln396:
    .asciz "timespec_get"
ln397:
    .asciz "tmpfile"
ln398:
    .asciz "tmpnam"
ln399:
    .asciz "toascii"
ln400:
    .asciz "tolower"
ln401:
    .asciz "toupper"
ln402:
    .asciz "towlower"
ln403:
    .asciz "towupper"
ln404:
    .asciz "truncate"
ln405:
    .asciz "tty_clear"
ln406:
    .asciz "tty_getsize"
ln407:
    .asciz "tty_raw"
ln408:
    .asciz "umask"
ln409:
    .asciz "umount"
ln410:
    .asciz "ungetc"
ln411:
    .asciz "unlink"
ln412:
    .asciz "unsetenv"
ln413:
    .asciz "uptime_ms"
ln414:
    .asciz "usleep"
ln415:
    .asciz "utime"
ln416:
    .asciz "verboseboot"
ln417:
    .asciz "vfprintf"
ln418:
    .asciz "vfscanf"
ln419:
    .asciz "video_info"
ln420:
    .asciz "vprintf"
ln421:
    .asciz "vscanf"
ln422:
    .asciz "vsnprintf"
ln423:
    .asciz "vsprintf"
ln424:
    .asciz "vsscanf"
ln425:
    .asciz "vswprintf"
ln426:
    .asciz "wait"
ln427:
    .asciz "waitpid"
ln428:
    .asciz "wcscat"
ln429:
    .asciz "wcschr"
ln430:
    .asciz "wcscmp"
ln431:
    .asciz "wcscpy"
ln432:
    .asciz "wcscspn"
ln433:
    .asciz "wcslen"
ln434:
    .asciz "wcsncat"
ln435:
    .asciz "wcsncmp"
ln436:
    .asciz "wcsncpy"
ln437:
    .asciz "wcspbrk"
ln438:
    .asciz "wcsrchr"
ln439:
    .asciz "wcsspn"
ln440:
    .asciz "wcsstr"
ln441:
    .asciz "wcstod"
ln442:
    .asciz "wcstol"
ln443:
    .asciz "wcstoll"
ln444:
    .asciz "wcstombs"
ln445:
    .asciz "wcstoul"
ln446:
    .asciz "wcstoull"
ln447:
    .asciz "wctomb"
ln448:
    .asciz "wmemchr"
ln449:
    .asciz "wmemcmp"
ln450:
    .asciz "wmemcpy"
ln451:
    .asciz "wmemmove"
ln452:
    .asciz "wmemset"
ln453:
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
    .long ln453

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
    .long cpu_lavora
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
    .long 454
    .long libc_nomi
    .long libc_indirizzi

    .section .note.GNU-stack,"",@progbits
