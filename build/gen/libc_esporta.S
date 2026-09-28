/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * La tabella che libc.so mette a disposizione: 445 funzioni, ognuna col suo
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
    .asciz "procinfo"
ln220:
    .asciz "pthread_attr_destroy"
ln221:
    .asciz "pthread_attr_getdetachstate"
ln222:
    .asciz "pthread_attr_getschedparam"
ln223:
    .asciz "pthread_attr_getschedpolicy"
ln224:
    .asciz "pthread_attr_getstack"
ln225:
    .asciz "pthread_attr_getstacksize"
ln226:
    .asciz "pthread_attr_init"
ln227:
    .asciz "pthread_attr_setdetachstate"
ln228:
    .asciz "pthread_attr_setguardsize"
ln229:
    .asciz "pthread_attr_setinheritsched"
ln230:
    .asciz "pthread_attr_setschedparam"
ln231:
    .asciz "pthread_attr_setschedpolicy"
ln232:
    .asciz "pthread_attr_setscope"
ln233:
    .asciz "pthread_attr_setstacksize"
ln234:
    .asciz "pthread_cond_broadcast"
ln235:
    .asciz "pthread_cond_destroy"
ln236:
    .asciz "pthread_cond_init"
ln237:
    .asciz "pthread_cond_signal"
ln238:
    .asciz "pthread_cond_timedwait"
ln239:
    .asciz "pthread_cond_wait"
ln240:
    .asciz "pthread_condattr_destroy"
ln241:
    .asciz "pthread_condattr_getclock"
ln242:
    .asciz "pthread_condattr_init"
ln243:
    .asciz "pthread_condattr_setclock"
ln244:
    .asciz "pthread_create"
ln245:
    .asciz "pthread_detach"
ln246:
    .asciz "pthread_equal"
ln247:
    .asciz "pthread_exit"
ln248:
    .asciz "pthread_getattr_np"
ln249:
    .asciz "pthread_getname_np"
ln250:
    .asciz "pthread_getschedparam"
ln251:
    .asciz "pthread_getspecific"
ln252:
    .asciz "pthread_join"
ln253:
    .asciz "pthread_key_create"
ln254:
    .asciz "pthread_key_delete"
ln255:
    .asciz "pthread_kill"
ln256:
    .asciz "pthread_mutex_destroy"
ln257:
    .asciz "pthread_mutex_init"
ln258:
    .asciz "pthread_mutex_lock"
ln259:
    .asciz "pthread_mutex_timedlock"
ln260:
    .asciz "pthread_mutex_trylock"
ln261:
    .asciz "pthread_mutex_unlock"
ln262:
    .asciz "pthread_mutexattr_destroy"
ln263:
    .asciz "pthread_mutexattr_gettype"
ln264:
    .asciz "pthread_mutexattr_init"
ln265:
    .asciz "pthread_mutexattr_settype"
ln266:
    .asciz "pthread_once"
ln267:
    .asciz "pthread_rwlock_destroy"
ln268:
    .asciz "pthread_rwlock_init"
ln269:
    .asciz "pthread_rwlock_rdlock"
ln270:
    .asciz "pthread_rwlock_tryrdlock"
ln271:
    .asciz "pthread_rwlock_trywrlock"
ln272:
    .asciz "pthread_rwlock_unlock"
ln273:
    .asciz "pthread_rwlock_wrlock"
ln274:
    .asciz "pthread_self"
ln275:
    .asciz "pthread_setname_np"
ln276:
    .asciz "pthread_setschedparam"
ln277:
    .asciz "pthread_setspecific"
ln278:
    .asciz "pthread_sigmask"
ln279:
    .asciz "pthread_yield"
ln280:
    .asciz "pty_apri"
ln281:
    .asciz "pty_ctl"
ln282:
    .asciz "putc"
ln283:
    .asciz "putchar"
ln284:
    .asciz "putenv"
ln285:
    .asciz "puts"
ln286:
    .asciz "qsort"
ln287:
    .asciz "quick_exit"
ln288:
    .asciz "raise"
ln289:
    .asciz "rand"
ln290:
    .asciz "read"
ln291:
    .asciz "readdir"
ln292:
    .asciz "realloc"
ln293:
    .asciz "realpath"
ln294:
    .asciz "reboot"
ln295:
    .asciz "remove"
ln296:
    .asciz "rename"
ln297:
    .asciz "rewind"
ln298:
    .asciz "rewinddir"
ln299:
    .asciz "rmdir"
ln300:
    .asciz "sbrk"
ln301:
    .asciz "scanf"
ln302:
    .asciz "sched_yield"
ln303:
    .asciz "select"
ln304:
    .asciz "sem_destroy"
ln305:
    .asciz "sem_getvalue"
ln306:
    .asciz "sem_init"
ln307:
    .asciz "sem_post"
ln308:
    .asciz "sem_timedwait"
ln309:
    .asciz "sem_trywait"
ln310:
    .asciz "sem_wait"
ln311:
    .asciz "semaforo_lascia"
ln312:
    .asciz "semaforo_prendi"
ln313:
    .asciz "semaforo_prendi_ms"
ln314:
    .asciz "semaforo_prova"
ln315:
    .asciz "setbuf"
ln316:
    .asciz "setenv"
ln317:
    .asciz "setjmp"
ln318:
    .asciz "setlocale"
ln319:
    .asciz "setuid"
ln320:
    .asciz "setvbuf"
ln321:
    .asciz "sha256"
ln322:
    .asciz "sha256_avvia"
ln323:
    .asciz "sha256_dai"
ln324:
    .asciz "sha256_esa"
ln325:
    .asciz "sha256_fine"
ln326:
    .asciz "sha256_fine_esa"
ln327:
    .asciz "shm_apri"
ln328:
    .asciz "shm_chiudi"
ln329:
    .asciz "signal"
ln330:
    .asciz "sleep"
ln331:
    .asciz "snprintf"
ln332:
    .asciz "spawn"
ln333:
    .asciz "spawn_ex"
ln334:
    .asciz "spawn_su_console"
ln335:
    .asciz "spawn_utente"
ln336:
    .asciz "sprintf"
ln337:
    .asciz "sqrt"
ln338:
    .asciz "srand"
ln339:
    .asciz "sscanf"
ln340:
    .asciz "stat"
ln341:
    .asciz "statperm"
ln342:
    .asciz "statraw"
ln343:
    .asciz "strcasecmp"
ln344:
    .asciz "strcat"
ln345:
    .asciz "strchr"
ln346:
    .asciz "strcmp"
ln347:
    .asciz "strcoll"
ln348:
    .asciz "strcpy"
ln349:
    .asciz "strcspn"
ln350:
    .asciz "strdup"
ln351:
    .asciz "strerror"
ln352:
    .asciz "strftime"
ln353:
    .asciz "strlen"
ln354:
    .asciz "strncasecmp"
ln355:
    .asciz "strncat"
ln356:
    .asciz "strncmp"
ln357:
    .asciz "strncpy"
ln358:
    .asciz "strpbrk"
ln359:
    .asciz "strrchr"
ln360:
    .asciz "strsignal"
ln361:
    .asciz "strspn"
ln362:
    .asciz "strstr"
ln363:
    .asciz "strtod"
ln364:
    .asciz "strtof"
ln365:
    .asciz "strtoimax"
ln366:
    .asciz "strtok"
ln367:
    .asciz "strtok_r"
ln368:
    .asciz "strtol"
ln369:
    .asciz "strtold"
ln370:
    .asciz "strtoll"
ln371:
    .asciz "strtoul"
ln372:
    .asciz "strtoull"
ln373:
    .asciz "strtoumax"
ln374:
    .asciz "strxfrm"
ln375:
    .asciz "swprintf"
ln376:
    .asciz "sysconf"
ln377:
    .asciz "system"
ln378:
    .asciz "thread_attendi"
ln379:
    .asciz "thread_crea"
ln380:
    .asciz "thread_devo_fermarmi"
ln381:
    .asciz "thread_esci"
ln382:
    .asciz "thread_ferma"
ln383:
    .asciz "time"
ln384:
    .asciz "time_now"
ln385:
    .asciz "time_set"
ln386:
    .asciz "times"
ln387:
    .asciz "timespec_get"
ln388:
    .asciz "tmpfile"
ln389:
    .asciz "tmpnam"
ln390:
    .asciz "toascii"
ln391:
    .asciz "tolower"
ln392:
    .asciz "toupper"
ln393:
    .asciz "towlower"
ln394:
    .asciz "towupper"
ln395:
    .asciz "truncate"
ln396:
    .asciz "tty_clear"
ln397:
    .asciz "tty_getsize"
ln398:
    .asciz "tty_raw"
ln399:
    .asciz "umask"
ln400:
    .asciz "umount"
ln401:
    .asciz "ungetc"
ln402:
    .asciz "unlink"
ln403:
    .asciz "unsetenv"
ln404:
    .asciz "uptime_ms"
ln405:
    .asciz "usleep"
ln406:
    .asciz "utime"
ln407:
    .asciz "verboseboot"
ln408:
    .asciz "vfprintf"
ln409:
    .asciz "vfscanf"
ln410:
    .asciz "video_info"
ln411:
    .asciz "vprintf"
ln412:
    .asciz "vscanf"
ln413:
    .asciz "vsnprintf"
ln414:
    .asciz "vsprintf"
ln415:
    .asciz "vsscanf"
ln416:
    .asciz "vswprintf"
ln417:
    .asciz "wait"
ln418:
    .asciz "waitpid"
ln419:
    .asciz "wcscat"
ln420:
    .asciz "wcschr"
ln421:
    .asciz "wcscmp"
ln422:
    .asciz "wcscpy"
ln423:
    .asciz "wcscspn"
ln424:
    .asciz "wcslen"
ln425:
    .asciz "wcsncat"
ln426:
    .asciz "wcsncmp"
ln427:
    .asciz "wcsncpy"
ln428:
    .asciz "wcspbrk"
ln429:
    .asciz "wcsrchr"
ln430:
    .asciz "wcsspn"
ln431:
    .asciz "wcsstr"
ln432:
    .asciz "wcstod"
ln433:
    .asciz "wcstol"
ln434:
    .asciz "wcstoll"
ln435:
    .asciz "wcstombs"
ln436:
    .asciz "wcstoul"
ln437:
    .asciz "wcstoull"
ln438:
    .asciz "wctomb"
ln439:
    .asciz "wmemchr"
ln440:
    .asciz "wmemcmp"
ln441:
    .asciz "wmemcpy"
ln442:
    .asciz "wmemmove"
ln443:
    .asciz "wmemset"
ln444:
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
    .long 445
    .long libc_nomi
    .long libc_indirizzi

    .section .note.GNU-stack,"",@progbits
