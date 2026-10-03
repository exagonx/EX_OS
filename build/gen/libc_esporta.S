/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * La tabella che libc.so mette a disposizione: 448 funzioni, ognuna col suo
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
    .asciz "ctime"
ln56:
    .asciz "difftime"
ln57:
    .asciz "diskinfo"
ln58:
    .asciz "div"
ln59:
    .asciz "diventa_root"
ln60:
    .asciz "dma_alloc"
ln61:
    .asciz "dup"
ln62:
    .asciz "dup2"
ln63:
    .asciz "execv"
ln64:
    .asciz "execvp"
ln65:
    .asciz "exit"
ln66:
    .asciz "fabs"
ln67:
    .asciz "fb_map"
ln68:
    .asciz "fchmod"
ln69:
    .asciz "fclose"
ln70:
    .asciz "fcntl"
ln71:
    .asciz "fdopen"
ln72:
    .asciz "fdprova"
ln73:
    .asciz "feof"
ln74:
    .asciz "ferror"
ln75:
    .asciz "fflush"
ln76:
    .asciz "fgetc"
ln77:
    .asciz "fgetpos"
ln78:
    .asciz "fgets"
ln79:
    .asciz "fileno"
ln80:
    .asciz "fopen"
ln81:
    .asciz "fpathconf"
ln82:
    .asciz "fprintf"
ln83:
    .asciz "fputc"
ln84:
    .asciz "fputs"
ln85:
    .asciz "fread"
ln86:
    .asciz "free"
ln87:
    .asciz "freopen"
ln88:
    .asciz "frexp"
ln89:
    .asciz "fscanf"
ln90:
    .asciz "fseek"
ln91:
    .asciz "fsetpos"
ln92:
    .asciz "fsize"
ln93:
    .asciz "fstat"
ln94:
    .asciz "ftell"
ln95:
    .asciz "fwrite"
ln96:
    .asciz "getc"
ln97:
    .asciz "getchar"
ln98:
    .asciz "getconf"
ln99:
    .asciz "getcwd"
ln100:
    .asciz "getegid"
ln101:
    .asciz "getentropy"
ln102:
    .asciz "getenv"
ln103:
    .asciz "geteuid"
ln104:
    .asciz "getgid"
ln105:
    .asciz "getpagesize"
ln106:
    .asciz "getpid"
ln107:
    .asciz "getrandom"
ln108:
    .asciz "getrusage"
ln109:
    .asciz "gets"
ln110:
    .asciz "gettimeofday"
ln111:
    .asciz "getuid"
ln112:
    .asciz "gmtime"
ln113:
    .asciz "gmtime_r"
ln114:
    .asciz "imaxabs"
ln115:
    .asciz "imaxdiv"
ln116:
    .asciz "interrompi"
ln117:
    .asciz "ioctl"
ln118:
    .asciz "ioport_bind"
ln119:
    .asciz "ioport_in"
ln120:
    .asciz "ioport_in16"
ln121:
    .asciz "ioport_in32"
ln122:
    .asciz "ioport_out"
ln123:
    .asciz "ioport_out16"
ln124:
    .asciz "ioport_out32"
ln125:
    .asciz "ipc_attendi"
ln126:
    .asciz "ipc_lookup"
ln127:
    .asciz "ipc_pronto"
ln128:
    .asciz "ipc_recv"
ln129:
    .asciz "ipc_recv_timeout"
ln130:
    .asciz "ipc_register"
ln131:
    .asciz "ipc_rimetti"
ln132:
    .asciz "ipc_scegli"
ln133:
    .asciz "ipc_send"
ln134:
    .asciz "irq_bind"
ln135:
    .asciz "irq_done"
ln136:
    .asciz "irq_unbind"
ln137:
    .asciz "isalnum"
ln138:
    .asciz "isalpha"
ln139:
    .asciz "isascii"
ln140:
    .asciz "isatty"
ln141:
    .asciz "isblank"
ln142:
    .asciz "iscntrl"
ln143:
    .asciz "isdigit"
ln144:
    .asciz "isgraph"
ln145:
    .asciz "islower"
ln146:
    .asciz "isprint"
ln147:
    .asciz "ispunct"
ln148:
    .asciz "isspace"
ln149:
    .asciz "isupper"
ln150:
    .asciz "iswalnum"
ln151:
    .asciz "iswalpha"
ln152:
    .asciz "iswblank"
ln153:
    .asciz "iswcntrl"
ln154:
    .asciz "iswdigit"
ln155:
    .asciz "iswgraph"
ln156:
    .asciz "iswlower"
ln157:
    .asciz "iswprint"
ln158:
    .asciz "iswpunct"
ln159:
    .asciz "iswspace"
ln160:
    .asciz "iswupper"
ln161:
    .asciz "iswxdigit"
ln162:
    .asciz "isxdigit"
ln163:
    .asciz "kbprova"
ln164:
    .asciz "kbstato"
ln165:
    .asciz "labs"
ln166:
    .asciz "ldexp"
ln167:
    .asciz "ldiv"
ln168:
    .asciz "listdir"
ln169:
    .asciz "listdir_from"
ln170:
    .asciz "llabs"
ln171:
    .asciz "lldiv"
ln172:
    .asciz "localeconv"
ln173:
    .asciz "localtime"
ln174:
    .asciz "localtime_r"
ln175:
    .asciz "log_seriale"
ln176:
    .asciz "longjmp"
ln177:
    .asciz "lseek"
ln178:
    .asciz "lstat"
ln179:
    .asciz "malloc"
ln180:
    .asciz "malloc_usable_size"
ln181:
    .asciz "mblen"
ln182:
    .asciz "mbrtowc"
ln183:
    .asciz "mbstowcs"
ln184:
    .asciz "mbtowc"
ln185:
    .asciz "memalign"
ln186:
    .asciz "memchr"
ln187:
    .asciz "memcmp"
ln188:
    .asciz "memcpy"
ln189:
    .asciz "meminfo"
ln190:
    .asciz "memmove"
ln191:
    .asciz "memset"
ln192:
    .asciz "mkdir"
ln193:
    .asciz "mkstemp"
ln194:
    .asciz "mktemp"
ln195:
    .asciz "mktime"
ln196:
    .asciz "mmap"
ln197:
    .asciz "mmio_map"
ln198:
    .asciz "modo_testo"
ln199:
    .asciz "mount"
ln200:
    .asciz "mountinfo"
ln201:
    .asciz "munmap"
ln202:
    .asciz "mutex_lascia"
ln203:
    .asciz "mutex_prendi"
ln204:
    .asciz "mutex_prova"
ln205:
    .asciz "nanosleep"
ln206:
    .asciz "nome_utente"
ln207:
    .asciz "open"
ln208:
    .asciz "opendir"
ln209:
    .asciz "osversion"
ln210:
    .asciz "partwrite"
ln211:
    .asciz "pathconf"
ln212:
    .asciz "pclose"
ln213:
    .asciz "perror"
ln214:
    .asciz "pipe"
ln215:
    .asciz "poll"
ln216:
    .asciz "popen"
ln217:
    .asciz "posix_memalign"
ln218:
    .asciz "printf"
ln219:
    .asciz "proc_gruppo"
ln220:
    .asciz "procinfo"
ln221:
    .asciz "pthread_attr_destroy"
ln222:
    .asciz "pthread_attr_getdetachstate"
ln223:
    .asciz "pthread_attr_getschedparam"
ln224:
    .asciz "pthread_attr_getschedpolicy"
ln225:
    .asciz "pthread_attr_getstack"
ln226:
    .asciz "pthread_attr_getstacksize"
ln227:
    .asciz "pthread_attr_init"
ln228:
    .asciz "pthread_attr_setdetachstate"
ln229:
    .asciz "pthread_attr_setguardsize"
ln230:
    .asciz "pthread_attr_setinheritsched"
ln231:
    .asciz "pthread_attr_setschedparam"
ln232:
    .asciz "pthread_attr_setschedpolicy"
ln233:
    .asciz "pthread_attr_setscope"
ln234:
    .asciz "pthread_attr_setstacksize"
ln235:
    .asciz "pthread_cond_broadcast"
ln236:
    .asciz "pthread_cond_destroy"
ln237:
    .asciz "pthread_cond_init"
ln238:
    .asciz "pthread_cond_signal"
ln239:
    .asciz "pthread_cond_timedwait"
ln240:
    .asciz "pthread_cond_wait"
ln241:
    .asciz "pthread_condattr_destroy"
ln242:
    .asciz "pthread_condattr_getclock"
ln243:
    .asciz "pthread_condattr_init"
ln244:
    .asciz "pthread_condattr_setclock"
ln245:
    .asciz "pthread_create"
ln246:
    .asciz "pthread_detach"
ln247:
    .asciz "pthread_equal"
ln248:
    .asciz "pthread_exit"
ln249:
    .asciz "pthread_getattr_np"
ln250:
    .asciz "pthread_getname_np"
ln251:
    .asciz "pthread_getschedparam"
ln252:
    .asciz "pthread_getspecific"
ln253:
    .asciz "pthread_join"
ln254:
    .asciz "pthread_key_create"
ln255:
    .asciz "pthread_key_delete"
ln256:
    .asciz "pthread_kill"
ln257:
    .asciz "pthread_mutex_destroy"
ln258:
    .asciz "pthread_mutex_init"
ln259:
    .asciz "pthread_mutex_lock"
ln260:
    .asciz "pthread_mutex_timedlock"
ln261:
    .asciz "pthread_mutex_trylock"
ln262:
    .asciz "pthread_mutex_unlock"
ln263:
    .asciz "pthread_mutexattr_destroy"
ln264:
    .asciz "pthread_mutexattr_gettype"
ln265:
    .asciz "pthread_mutexattr_init"
ln266:
    .asciz "pthread_mutexattr_settype"
ln267:
    .asciz "pthread_once"
ln268:
    .asciz "pthread_rwlock_destroy"
ln269:
    .asciz "pthread_rwlock_init"
ln270:
    .asciz "pthread_rwlock_rdlock"
ln271:
    .asciz "pthread_rwlock_tryrdlock"
ln272:
    .asciz "pthread_rwlock_trywrlock"
ln273:
    .asciz "pthread_rwlock_unlock"
ln274:
    .asciz "pthread_rwlock_wrlock"
ln275:
    .asciz "pthread_self"
ln276:
    .asciz "pthread_setname_np"
ln277:
    .asciz "pthread_setschedparam"
ln278:
    .asciz "pthread_setspecific"
ln279:
    .asciz "pthread_sigmask"
ln280:
    .asciz "pthread_yield"
ln281:
    .asciz "pty_apri"
ln282:
    .asciz "pty_ctl"
ln283:
    .asciz "putc"
ln284:
    .asciz "putchar"
ln285:
    .asciz "putenv"
ln286:
    .asciz "puts"
ln287:
    .asciz "qsort"
ln288:
    .asciz "quick_exit"
ln289:
    .asciz "raise"
ln290:
    .asciz "rand"
ln291:
    .asciz "read"
ln292:
    .asciz "readdir"
ln293:
    .asciz "realloc"
ln294:
    .asciz "realpath"
ln295:
    .asciz "reboot"
ln296:
    .asciz "remove"
ln297:
    .asciz "rename"
ln298:
    .asciz "rewind"
ln299:
    .asciz "rewinddir"
ln300:
    .asciz "rmdir"
ln301:
    .asciz "sbrk"
ln302:
    .asciz "scanf"
ln303:
    .asciz "sched_yield"
ln304:
    .asciz "select"
ln305:
    .asciz "sem_destroy"
ln306:
    .asciz "sem_getvalue"
ln307:
    .asciz "sem_init"
ln308:
    .asciz "sem_post"
ln309:
    .asciz "sem_timedwait"
ln310:
    .asciz "sem_trywait"
ln311:
    .asciz "sem_wait"
ln312:
    .asciz "semaforo_lascia"
ln313:
    .asciz "semaforo_prendi"
ln314:
    .asciz "semaforo_prendi_ms"
ln315:
    .asciz "semaforo_prova"
ln316:
    .asciz "setbuf"
ln317:
    .asciz "setenv"
ln318:
    .asciz "setjmp"
ln319:
    .asciz "setlocale"
ln320:
    .asciz "setuid"
ln321:
    .asciz "setvbuf"
ln322:
    .asciz "sha256"
ln323:
    .asciz "sha256_avvia"
ln324:
    .asciz "sha256_dai"
ln325:
    .asciz "sha256_esa"
ln326:
    .asciz "sha256_fine"
ln327:
    .asciz "sha256_fine_esa"
ln328:
    .asciz "shm_apri"
ln329:
    .asciz "shm_chiudi"
ln330:
    .asciz "signal"
ln331:
    .asciz "sleep"
ln332:
    .asciz "snprintf"
ln333:
    .asciz "spawn"
ln334:
    .asciz "spawn_ex"
ln335:
    .asciz "spawn_su_console"
ln336:
    .asciz "spawn_utente"
ln337:
    .asciz "sprintf"
ln338:
    .asciz "sqrt"
ln339:
    .asciz "srand"
ln340:
    .asciz "sscanf"
ln341:
    .asciz "stat"
ln342:
    .asciz "statperm"
ln343:
    .asciz "statraw"
ln344:
    .asciz "strcasecmp"
ln345:
    .asciz "strcat"
ln346:
    .asciz "strchr"
ln347:
    .asciz "strcmp"
ln348:
    .asciz "strcoll"
ln349:
    .asciz "strcpy"
ln350:
    .asciz "strcspn"
ln351:
    .asciz "strdup"
ln352:
    .asciz "strerror"
ln353:
    .asciz "strerror_r"
ln354:
    .asciz "strftime"
ln355:
    .asciz "strlen"
ln356:
    .asciz "strncasecmp"
ln357:
    .asciz "strncat"
ln358:
    .asciz "strncmp"
ln359:
    .asciz "strncpy"
ln360:
    .asciz "strpbrk"
ln361:
    .asciz "strrchr"
ln362:
    .asciz "strsignal"
ln363:
    .asciz "strspn"
ln364:
    .asciz "strstr"
ln365:
    .asciz "strtod"
ln366:
    .asciz "strtof"
ln367:
    .asciz "strtoimax"
ln368:
    .asciz "strtok"
ln369:
    .asciz "strtok_r"
ln370:
    .asciz "strtol"
ln371:
    .asciz "strtold"
ln372:
    .asciz "strtoll"
ln373:
    .asciz "strtoul"
ln374:
    .asciz "strtoull"
ln375:
    .asciz "strtoumax"
ln376:
    .asciz "strxfrm"
ln377:
    .asciz "swprintf"
ln378:
    .asciz "sysconf"
ln379:
    .asciz "system"
ln380:
    .asciz "thread_attendi"
ln381:
    .asciz "thread_crea"
ln382:
    .asciz "thread_devo_fermarmi"
ln383:
    .asciz "thread_esci"
ln384:
    .asciz "thread_ferma"
ln385:
    .asciz "time"
ln386:
    .asciz "time_now"
ln387:
    .asciz "time_set"
ln388:
    .asciz "timegm"
ln389:
    .asciz "times"
ln390:
    .asciz "timespec_get"
ln391:
    .asciz "tmpfile"
ln392:
    .asciz "tmpnam"
ln393:
    .asciz "toascii"
ln394:
    .asciz "tolower"
ln395:
    .asciz "toupper"
ln396:
    .asciz "towlower"
ln397:
    .asciz "towupper"
ln398:
    .asciz "truncate"
ln399:
    .asciz "tty_clear"
ln400:
    .asciz "tty_getsize"
ln401:
    .asciz "tty_raw"
ln402:
    .asciz "umask"
ln403:
    .asciz "umount"
ln404:
    .asciz "ungetc"
ln405:
    .asciz "unlink"
ln406:
    .asciz "unsetenv"
ln407:
    .asciz "uptime_ms"
ln408:
    .asciz "usleep"
ln409:
    .asciz "utime"
ln410:
    .asciz "verboseboot"
ln411:
    .asciz "vfprintf"
ln412:
    .asciz "vfscanf"
ln413:
    .asciz "video_info"
ln414:
    .asciz "vprintf"
ln415:
    .asciz "vscanf"
ln416:
    .asciz "vsnprintf"
ln417:
    .asciz "vsprintf"
ln418:
    .asciz "vsscanf"
ln419:
    .asciz "vswprintf"
ln420:
    .asciz "wait"
ln421:
    .asciz "waitpid"
ln422:
    .asciz "wcscat"
ln423:
    .asciz "wcschr"
ln424:
    .asciz "wcscmp"
ln425:
    .asciz "wcscpy"
ln426:
    .asciz "wcscspn"
ln427:
    .asciz "wcslen"
ln428:
    .asciz "wcsncat"
ln429:
    .asciz "wcsncmp"
ln430:
    .asciz "wcsncpy"
ln431:
    .asciz "wcspbrk"
ln432:
    .asciz "wcsrchr"
ln433:
    .asciz "wcsspn"
ln434:
    .asciz "wcsstr"
ln435:
    .asciz "wcstod"
ln436:
    .asciz "wcstol"
ln437:
    .asciz "wcstoll"
ln438:
    .asciz "wcstombs"
ln439:
    .asciz "wcstoul"
ln440:
    .asciz "wcstoull"
ln441:
    .asciz "wctomb"
ln442:
    .asciz "wmemchr"
ln443:
    .asciz "wmemcmp"
ln444:
    .asciz "wmemcpy"
ln445:
    .asciz "wmemmove"
ln446:
    .asciz "wmemset"
ln447:
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
    .long 448
    .long libc_nomi
    .long libc_indirizzi

    .section .note.GNU-stack,"",@progbits
