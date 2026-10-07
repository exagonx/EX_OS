/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * La tabella che libc.so mette a disposizione: 449 funzioni, ognuna col suo
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
    .asciz "aligned_alloc"
ln13:
    .asciz "asctime"
ln14:
    .asciz "at_quick_exit"
ln15:
    .asciz "atexit"
ln16:
    .asciz "atof"
ln17:
    .asciz "atoi"
ln18:
    .asciz "atol"
ln19:
    .asciz "atoll"
ln20:
    .asciz "attesa_dormi"
ln21:
    .asciz "attesa_sveglia"
ln22:
    .asciz "blk_attendi"
ln23:
    .asciz "blk_espelli"
ln24:
    .asciz "blk_offri"
ln25:
    .asciz "blk_risposta"
ln26:
    .asciz "blk_scansiona"
ln27:
    .asciz "blkinfo"
ln28:
    .asciz "blkread"
ln29:
    .asciz "blkwrite"
ln30:
    .asciz "bootinstall"
ln31:
    .asciz "bootverify"
ln32:
    .asciz "bsearch"
ln33:
    .asciz "calloc"
ln34:
    .asciz "chdir"
ln35:
    .asciz "chmod"
ln36:
    .asciz "chown"
ln37:
    .asciz "clearerr"
ln38:
    .asciz "clock"
ln39:
    .asciz "clock_getres"
ln40:
    .asciz "clock_gettime"
ln41:
    .asciz "close"
ln42:
    .asciz "closedir"
ln43:
    .asciz "condizione_aspetta"
ln44:
    .asciz "condizione_aspetta_ms"
ln45:
    .asciz "condizione_segnala"
ln46:
    .asciz "condizione_segnala_tutti"
ln47:
    .asciz "console_ctrlc"
ln48:
    .asciz "console_grafica"
ln49:
    .asciz "console_info"
ln50:
    .asciz "console_registro"
ln51:
    .asciz "console_setfg"
ln52:
    .asciz "console_switch"
ln53:
    .asciz "console_testo"
ln54:
    .asciz "console_write"
ln55:
    .asciz "cpu_info"
ln56:
    .asciz "ctime"
ln57:
    .asciz "difftime"
ln58:
    .asciz "diskinfo"
ln59:
    .asciz "div"
ln60:
    .asciz "diventa_root"
ln61:
    .asciz "dma_alloc"
ln62:
    .asciz "dup"
ln63:
    .asciz "dup2"
ln64:
    .asciz "execv"
ln65:
    .asciz "execvp"
ln66:
    .asciz "exit"
ln67:
    .asciz "fabs"
ln68:
    .asciz "fb_map"
ln69:
    .asciz "fchmod"
ln70:
    .asciz "fclose"
ln71:
    .asciz "fcntl"
ln72:
    .asciz "fdopen"
ln73:
    .asciz "fdprova"
ln74:
    .asciz "feof"
ln75:
    .asciz "ferror"
ln76:
    .asciz "fflush"
ln77:
    .asciz "fgetc"
ln78:
    .asciz "fgetpos"
ln79:
    .asciz "fgets"
ln80:
    .asciz "fileno"
ln81:
    .asciz "fopen"
ln82:
    .asciz "fpathconf"
ln83:
    .asciz "fprintf"
ln84:
    .asciz "fputc"
ln85:
    .asciz "fputs"
ln86:
    .asciz "fread"
ln87:
    .asciz "free"
ln88:
    .asciz "freopen"
ln89:
    .asciz "frexp"
ln90:
    .asciz "fscanf"
ln91:
    .asciz "fseek"
ln92:
    .asciz "fsetpos"
ln93:
    .asciz "fsize"
ln94:
    .asciz "fstat"
ln95:
    .asciz "ftell"
ln96:
    .asciz "fwrite"
ln97:
    .asciz "getc"
ln98:
    .asciz "getchar"
ln99:
    .asciz "getconf"
ln100:
    .asciz "getcwd"
ln101:
    .asciz "getegid"
ln102:
    .asciz "getentropy"
ln103:
    .asciz "getenv"
ln104:
    .asciz "geteuid"
ln105:
    .asciz "getgid"
ln106:
    .asciz "getpagesize"
ln107:
    .asciz "getpid"
ln108:
    .asciz "getrandom"
ln109:
    .asciz "getrusage"
ln110:
    .asciz "gets"
ln111:
    .asciz "gettimeofday"
ln112:
    .asciz "getuid"
ln113:
    .asciz "gmtime"
ln114:
    .asciz "gmtime_r"
ln115:
    .asciz "imaxabs"
ln116:
    .asciz "imaxdiv"
ln117:
    .asciz "interrompi"
ln118:
    .asciz "ioctl"
ln119:
    .asciz "ioport_bind"
ln120:
    .asciz "ioport_in"
ln121:
    .asciz "ioport_in16"
ln122:
    .asciz "ioport_in32"
ln123:
    .asciz "ioport_out"
ln124:
    .asciz "ioport_out16"
ln125:
    .asciz "ioport_out32"
ln126:
    .asciz "ipc_attendi"
ln127:
    .asciz "ipc_lookup"
ln128:
    .asciz "ipc_pronto"
ln129:
    .asciz "ipc_recv"
ln130:
    .asciz "ipc_recv_timeout"
ln131:
    .asciz "ipc_register"
ln132:
    .asciz "ipc_rimetti"
ln133:
    .asciz "ipc_scegli"
ln134:
    .asciz "ipc_send"
ln135:
    .asciz "irq_bind"
ln136:
    .asciz "irq_done"
ln137:
    .asciz "irq_unbind"
ln138:
    .asciz "isalnum"
ln139:
    .asciz "isalpha"
ln140:
    .asciz "isascii"
ln141:
    .asciz "isatty"
ln142:
    .asciz "isblank"
ln143:
    .asciz "iscntrl"
ln144:
    .asciz "isdigit"
ln145:
    .asciz "isgraph"
ln146:
    .asciz "islower"
ln147:
    .asciz "isprint"
ln148:
    .asciz "ispunct"
ln149:
    .asciz "isspace"
ln150:
    .asciz "isupper"
ln151:
    .asciz "iswalnum"
ln152:
    .asciz "iswalpha"
ln153:
    .asciz "iswblank"
ln154:
    .asciz "iswcntrl"
ln155:
    .asciz "iswdigit"
ln156:
    .asciz "iswgraph"
ln157:
    .asciz "iswlower"
ln158:
    .asciz "iswprint"
ln159:
    .asciz "iswpunct"
ln160:
    .asciz "iswspace"
ln161:
    .asciz "iswupper"
ln162:
    .asciz "iswxdigit"
ln163:
    .asciz "isxdigit"
ln164:
    .asciz "kbprova"
ln165:
    .asciz "kbstato"
ln166:
    .asciz "labs"
ln167:
    .asciz "ldexp"
ln168:
    .asciz "ldiv"
ln169:
    .asciz "listdir"
ln170:
    .asciz "listdir_from"
ln171:
    .asciz "llabs"
ln172:
    .asciz "lldiv"
ln173:
    .asciz "localeconv"
ln174:
    .asciz "localtime"
ln175:
    .asciz "localtime_r"
ln176:
    .asciz "log_seriale"
ln177:
    .asciz "longjmp"
ln178:
    .asciz "lseek"
ln179:
    .asciz "lstat"
ln180:
    .asciz "malloc"
ln181:
    .asciz "malloc_usable_size"
ln182:
    .asciz "mblen"
ln183:
    .asciz "mbrtowc"
ln184:
    .asciz "mbstowcs"
ln185:
    .asciz "mbtowc"
ln186:
    .asciz "memalign"
ln187:
    .asciz "memchr"
ln188:
    .asciz "memcmp"
ln189:
    .asciz "memcpy"
ln190:
    .asciz "meminfo"
ln191:
    .asciz "memmove"
ln192:
    .asciz "memset"
ln193:
    .asciz "mkdir"
ln194:
    .asciz "mkstemp"
ln195:
    .asciz "mktemp"
ln196:
    .asciz "mktime"
ln197:
    .asciz "mmap"
ln198:
    .asciz "mmio_map"
ln199:
    .asciz "modo_testo"
ln200:
    .asciz "mount"
ln201:
    .asciz "mountinfo"
ln202:
    .asciz "munmap"
ln203:
    .asciz "mutex_lascia"
ln204:
    .asciz "mutex_prendi"
ln205:
    .asciz "mutex_prova"
ln206:
    .asciz "nanosleep"
ln207:
    .asciz "nome_utente"
ln208:
    .asciz "open"
ln209:
    .asciz "opendir"
ln210:
    .asciz "osversion"
ln211:
    .asciz "partwrite"
ln212:
    .asciz "pathconf"
ln213:
    .asciz "pclose"
ln214:
    .asciz "perror"
ln215:
    .asciz "pipe"
ln216:
    .asciz "poll"
ln217:
    .asciz "popen"
ln218:
    .asciz "posix_memalign"
ln219:
    .asciz "printf"
ln220:
    .asciz "proc_gruppo"
ln221:
    .asciz "procinfo"
ln222:
    .asciz "pthread_attr_destroy"
ln223:
    .asciz "pthread_attr_getdetachstate"
ln224:
    .asciz "pthread_attr_getschedparam"
ln225:
    .asciz "pthread_attr_getschedpolicy"
ln226:
    .asciz "pthread_attr_getstack"
ln227:
    .asciz "pthread_attr_getstacksize"
ln228:
    .asciz "pthread_attr_init"
ln229:
    .asciz "pthread_attr_setdetachstate"
ln230:
    .asciz "pthread_attr_setguardsize"
ln231:
    .asciz "pthread_attr_setinheritsched"
ln232:
    .asciz "pthread_attr_setschedparam"
ln233:
    .asciz "pthread_attr_setschedpolicy"
ln234:
    .asciz "pthread_attr_setscope"
ln235:
    .asciz "pthread_attr_setstacksize"
ln236:
    .asciz "pthread_cond_broadcast"
ln237:
    .asciz "pthread_cond_destroy"
ln238:
    .asciz "pthread_cond_init"
ln239:
    .asciz "pthread_cond_signal"
ln240:
    .asciz "pthread_cond_timedwait"
ln241:
    .asciz "pthread_cond_wait"
ln242:
    .asciz "pthread_condattr_destroy"
ln243:
    .asciz "pthread_condattr_getclock"
ln244:
    .asciz "pthread_condattr_init"
ln245:
    .asciz "pthread_condattr_setclock"
ln246:
    .asciz "pthread_create"
ln247:
    .asciz "pthread_detach"
ln248:
    .asciz "pthread_equal"
ln249:
    .asciz "pthread_exit"
ln250:
    .asciz "pthread_getattr_np"
ln251:
    .asciz "pthread_getname_np"
ln252:
    .asciz "pthread_getschedparam"
ln253:
    .asciz "pthread_getspecific"
ln254:
    .asciz "pthread_join"
ln255:
    .asciz "pthread_key_create"
ln256:
    .asciz "pthread_key_delete"
ln257:
    .asciz "pthread_kill"
ln258:
    .asciz "pthread_mutex_destroy"
ln259:
    .asciz "pthread_mutex_init"
ln260:
    .asciz "pthread_mutex_lock"
ln261:
    .asciz "pthread_mutex_timedlock"
ln262:
    .asciz "pthread_mutex_trylock"
ln263:
    .asciz "pthread_mutex_unlock"
ln264:
    .asciz "pthread_mutexattr_destroy"
ln265:
    .asciz "pthread_mutexattr_gettype"
ln266:
    .asciz "pthread_mutexattr_init"
ln267:
    .asciz "pthread_mutexattr_settype"
ln268:
    .asciz "pthread_once"
ln269:
    .asciz "pthread_rwlock_destroy"
ln270:
    .asciz "pthread_rwlock_init"
ln271:
    .asciz "pthread_rwlock_rdlock"
ln272:
    .asciz "pthread_rwlock_tryrdlock"
ln273:
    .asciz "pthread_rwlock_trywrlock"
ln274:
    .asciz "pthread_rwlock_unlock"
ln275:
    .asciz "pthread_rwlock_wrlock"
ln276:
    .asciz "pthread_self"
ln277:
    .asciz "pthread_setname_np"
ln278:
    .asciz "pthread_setschedparam"
ln279:
    .asciz "pthread_setspecific"
ln280:
    .asciz "pthread_sigmask"
ln281:
    .asciz "pthread_yield"
ln282:
    .asciz "pty_apri"
ln283:
    .asciz "pty_ctl"
ln284:
    .asciz "putc"
ln285:
    .asciz "putchar"
ln286:
    .asciz "putenv"
ln287:
    .asciz "puts"
ln288:
    .asciz "qsort"
ln289:
    .asciz "quick_exit"
ln290:
    .asciz "raise"
ln291:
    .asciz "rand"
ln292:
    .asciz "read"
ln293:
    .asciz "readdir"
ln294:
    .asciz "realloc"
ln295:
    .asciz "realpath"
ln296:
    .asciz "reboot"
ln297:
    .asciz "remove"
ln298:
    .asciz "rename"
ln299:
    .asciz "rewind"
ln300:
    .asciz "rewinddir"
ln301:
    .asciz "rmdir"
ln302:
    .asciz "sbrk"
ln303:
    .asciz "scanf"
ln304:
    .asciz "sched_yield"
ln305:
    .asciz "select"
ln306:
    .asciz "sem_destroy"
ln307:
    .asciz "sem_getvalue"
ln308:
    .asciz "sem_init"
ln309:
    .asciz "sem_post"
ln310:
    .asciz "sem_timedwait"
ln311:
    .asciz "sem_trywait"
ln312:
    .asciz "sem_wait"
ln313:
    .asciz "semaforo_lascia"
ln314:
    .asciz "semaforo_prendi"
ln315:
    .asciz "semaforo_prendi_ms"
ln316:
    .asciz "semaforo_prova"
ln317:
    .asciz "setbuf"
ln318:
    .asciz "setenv"
ln319:
    .asciz "setjmp"
ln320:
    .asciz "setlocale"
ln321:
    .asciz "setuid"
ln322:
    .asciz "setvbuf"
ln323:
    .asciz "sha256"
ln324:
    .asciz "sha256_avvia"
ln325:
    .asciz "sha256_dai"
ln326:
    .asciz "sha256_esa"
ln327:
    .asciz "sha256_fine"
ln328:
    .asciz "sha256_fine_esa"
ln329:
    .asciz "shm_apri"
ln330:
    .asciz "shm_chiudi"
ln331:
    .asciz "signal"
ln332:
    .asciz "sleep"
ln333:
    .asciz "snprintf"
ln334:
    .asciz "spawn"
ln335:
    .asciz "spawn_ex"
ln336:
    .asciz "spawn_su_console"
ln337:
    .asciz "spawn_utente"
ln338:
    .asciz "sprintf"
ln339:
    .asciz "sqrt"
ln340:
    .asciz "srand"
ln341:
    .asciz "sscanf"
ln342:
    .asciz "stat"
ln343:
    .asciz "statperm"
ln344:
    .asciz "statraw"
ln345:
    .asciz "strcasecmp"
ln346:
    .asciz "strcat"
ln347:
    .asciz "strchr"
ln348:
    .asciz "strcmp"
ln349:
    .asciz "strcoll"
ln350:
    .asciz "strcpy"
ln351:
    .asciz "strcspn"
ln352:
    .asciz "strdup"
ln353:
    .asciz "strerror"
ln354:
    .asciz "strerror_r"
ln355:
    .asciz "strftime"
ln356:
    .asciz "strlen"
ln357:
    .asciz "strncasecmp"
ln358:
    .asciz "strncat"
ln359:
    .asciz "strncmp"
ln360:
    .asciz "strncpy"
ln361:
    .asciz "strpbrk"
ln362:
    .asciz "strrchr"
ln363:
    .asciz "strsignal"
ln364:
    .asciz "strspn"
ln365:
    .asciz "strstr"
ln366:
    .asciz "strtod"
ln367:
    .asciz "strtof"
ln368:
    .asciz "strtoimax"
ln369:
    .asciz "strtok"
ln370:
    .asciz "strtok_r"
ln371:
    .asciz "strtol"
ln372:
    .asciz "strtold"
ln373:
    .asciz "strtoll"
ln374:
    .asciz "strtoul"
ln375:
    .asciz "strtoull"
ln376:
    .asciz "strtoumax"
ln377:
    .asciz "strxfrm"
ln378:
    .asciz "swprintf"
ln379:
    .asciz "sysconf"
ln380:
    .asciz "system"
ln381:
    .asciz "thread_attendi"
ln382:
    .asciz "thread_crea"
ln383:
    .asciz "thread_devo_fermarmi"
ln384:
    .asciz "thread_esci"
ln385:
    .asciz "thread_ferma"
ln386:
    .asciz "time"
ln387:
    .asciz "time_now"
ln388:
    .asciz "time_set"
ln389:
    .asciz "timegm"
ln390:
    .asciz "times"
ln391:
    .asciz "timespec_get"
ln392:
    .asciz "tmpfile"
ln393:
    .asciz "tmpnam"
ln394:
    .asciz "toascii"
ln395:
    .asciz "tolower"
ln396:
    .asciz "toupper"
ln397:
    .asciz "towlower"
ln398:
    .asciz "towupper"
ln399:
    .asciz "truncate"
ln400:
    .asciz "tty_clear"
ln401:
    .asciz "tty_getsize"
ln402:
    .asciz "tty_raw"
ln403:
    .asciz "umask"
ln404:
    .asciz "umount"
ln405:
    .asciz "ungetc"
ln406:
    .asciz "unlink"
ln407:
    .asciz "unsetenv"
ln408:
    .asciz "uptime_ms"
ln409:
    .asciz "usleep"
ln410:
    .asciz "utime"
ln411:
    .asciz "verboseboot"
ln412:
    .asciz "vfprintf"
ln413:
    .asciz "vfscanf"
ln414:
    .asciz "video_info"
ln415:
    .asciz "vprintf"
ln416:
    .asciz "vscanf"
ln417:
    .asciz "vsnprintf"
ln418:
    .asciz "vsprintf"
ln419:
    .asciz "vsscanf"
ln420:
    .asciz "vswprintf"
ln421:
    .asciz "wait"
ln422:
    .asciz "waitpid"
ln423:
    .asciz "wcscat"
ln424:
    .asciz "wcschr"
ln425:
    .asciz "wcscmp"
ln426:
    .asciz "wcscpy"
ln427:
    .asciz "wcscspn"
ln428:
    .asciz "wcslen"
ln429:
    .asciz "wcsncat"
ln430:
    .asciz "wcsncmp"
ln431:
    .asciz "wcsncpy"
ln432:
    .asciz "wcspbrk"
ln433:
    .asciz "wcsrchr"
ln434:
    .asciz "wcsspn"
ln435:
    .asciz "wcsstr"
ln436:
    .asciz "wcstod"
ln437:
    .asciz "wcstol"
ln438:
    .asciz "wcstoll"
ln439:
    .asciz "wcstombs"
ln440:
    .asciz "wcstoul"
ln441:
    .asciz "wcstoull"
ln442:
    .asciz "wctomb"
ln443:
    .asciz "wmemchr"
ln444:
    .asciz "wmemcmp"
ln445:
    .asciz "wmemcpy"
ln446:
    .asciz "wmemmove"
ln447:
    .asciz "wmemset"
ln448:
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
    .long 449
    .long libc_nomi
    .long libc_indirizzi

    .section .note.GNU-stack,"",@progbits
