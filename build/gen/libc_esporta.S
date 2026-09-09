/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * La tabella che libc.so mette a disposizione: 365 funzioni, ognuna col suo
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
    .asciz "blk_offri"
ln24:
    .asciz "blk_risposta"
ln25:
    .asciz "blkinfo"
ln26:
    .asciz "blkread"
ln27:
    .asciz "blkwrite"
ln28:
    .asciz "bootinstall"
ln29:
    .asciz "bootverify"
ln30:
    .asciz "bsearch"
ln31:
    .asciz "calloc"
ln32:
    .asciz "chdir"
ln33:
    .asciz "chmod"
ln34:
    .asciz "chown"
ln35:
    .asciz "clearerr"
ln36:
    .asciz "clock"
ln37:
    .asciz "close"
ln38:
    .asciz "closedir"
ln39:
    .asciz "condizione_aspetta"
ln40:
    .asciz "condizione_aspetta_ms"
ln41:
    .asciz "condizione_segnala"
ln42:
    .asciz "condizione_segnala_tutti"
ln43:
    .asciz "console_grafica"
ln44:
    .asciz "console_info"
ln45:
    .asciz "console_setfg"
ln46:
    .asciz "console_switch"
ln47:
    .asciz "console_write"
ln48:
    .asciz "ctime"
ln49:
    .asciz "difftime"
ln50:
    .asciz "diskinfo"
ln51:
    .asciz "div"
ln52:
    .asciz "diventa_root"
ln53:
    .asciz "dma_alloc"
ln54:
    .asciz "dup"
ln55:
    .asciz "dup2"
ln56:
    .asciz "execv"
ln57:
    .asciz "execvp"
ln58:
    .asciz "exit"
ln59:
    .asciz "fabs"
ln60:
    .asciz "fb_map"
ln61:
    .asciz "fchmod"
ln62:
    .asciz "fclose"
ln63:
    .asciz "fcntl"
ln64:
    .asciz "fdopen"
ln65:
    .asciz "fdprova"
ln66:
    .asciz "feof"
ln67:
    .asciz "ferror"
ln68:
    .asciz "fflush"
ln69:
    .asciz "fgetc"
ln70:
    .asciz "fgetpos"
ln71:
    .asciz "fgets"
ln72:
    .asciz "fileno"
ln73:
    .asciz "fopen"
ln74:
    .asciz "fpathconf"
ln75:
    .asciz "fprintf"
ln76:
    .asciz "fputc"
ln77:
    .asciz "fputs"
ln78:
    .asciz "fread"
ln79:
    .asciz "free"
ln80:
    .asciz "freopen"
ln81:
    .asciz "frexp"
ln82:
    .asciz "fscanf"
ln83:
    .asciz "fseek"
ln84:
    .asciz "fsetpos"
ln85:
    .asciz "fsize"
ln86:
    .asciz "fstat"
ln87:
    .asciz "ftell"
ln88:
    .asciz "fwrite"
ln89:
    .asciz "getc"
ln90:
    .asciz "getchar"
ln91:
    .asciz "getconf"
ln92:
    .asciz "getcwd"
ln93:
    .asciz "getegid"
ln94:
    .asciz "getentropy"
ln95:
    .asciz "getenv"
ln96:
    .asciz "geteuid"
ln97:
    .asciz "getgid"
ln98:
    .asciz "getpagesize"
ln99:
    .asciz "getpid"
ln100:
    .asciz "getrandom"
ln101:
    .asciz "getrusage"
ln102:
    .asciz "gets"
ln103:
    .asciz "gettimeofday"
ln104:
    .asciz "getuid"
ln105:
    .asciz "gmtime"
ln106:
    .asciz "gmtime_r"
ln107:
    .asciz "imaxabs"
ln108:
    .asciz "imaxdiv"
ln109:
    .asciz "interrompi"
ln110:
    .asciz "ioctl"
ln111:
    .asciz "ioport_bind"
ln112:
    .asciz "ioport_in"
ln113:
    .asciz "ioport_in16"
ln114:
    .asciz "ioport_in32"
ln115:
    .asciz "ioport_out"
ln116:
    .asciz "ioport_out16"
ln117:
    .asciz "ioport_out32"
ln118:
    .asciz "ipc_attendi"
ln119:
    .asciz "ipc_lookup"
ln120:
    .asciz "ipc_pronto"
ln121:
    .asciz "ipc_recv"
ln122:
    .asciz "ipc_recv_timeout"
ln123:
    .asciz "ipc_register"
ln124:
    .asciz "ipc_rimetti"
ln125:
    .asciz "ipc_scegli"
ln126:
    .asciz "ipc_send"
ln127:
    .asciz "irq_bind"
ln128:
    .asciz "irq_done"
ln129:
    .asciz "irq_unbind"
ln130:
    .asciz "isalnum"
ln131:
    .asciz "isalpha"
ln132:
    .asciz "isascii"
ln133:
    .asciz "isatty"
ln134:
    .asciz "isblank"
ln135:
    .asciz "iscntrl"
ln136:
    .asciz "isdigit"
ln137:
    .asciz "isgraph"
ln138:
    .asciz "islower"
ln139:
    .asciz "isprint"
ln140:
    .asciz "ispunct"
ln141:
    .asciz "isspace"
ln142:
    .asciz "isupper"
ln143:
    .asciz "iswalnum"
ln144:
    .asciz "iswalpha"
ln145:
    .asciz "iswblank"
ln146:
    .asciz "iswcntrl"
ln147:
    .asciz "iswdigit"
ln148:
    .asciz "iswgraph"
ln149:
    .asciz "iswlower"
ln150:
    .asciz "iswprint"
ln151:
    .asciz "iswpunct"
ln152:
    .asciz "iswspace"
ln153:
    .asciz "iswupper"
ln154:
    .asciz "iswxdigit"
ln155:
    .asciz "isxdigit"
ln156:
    .asciz "kbprova"
ln157:
    .asciz "kbstato"
ln158:
    .asciz "labs"
ln159:
    .asciz "ldexp"
ln160:
    .asciz "ldiv"
ln161:
    .asciz "listdir"
ln162:
    .asciz "listdir_from"
ln163:
    .asciz "llabs"
ln164:
    .asciz "lldiv"
ln165:
    .asciz "localeconv"
ln166:
    .asciz "localtime"
ln167:
    .asciz "localtime_r"
ln168:
    .asciz "log_seriale"
ln169:
    .asciz "longjmp"
ln170:
    .asciz "lseek"
ln171:
    .asciz "lstat"
ln172:
    .asciz "malloc"
ln173:
    .asciz "malloc_usable_size"
ln174:
    .asciz "mblen"
ln175:
    .asciz "mbrtowc"
ln176:
    .asciz "mbstowcs"
ln177:
    .asciz "mbtowc"
ln178:
    .asciz "memalign"
ln179:
    .asciz "memchr"
ln180:
    .asciz "memcmp"
ln181:
    .asciz "memcpy"
ln182:
    .asciz "meminfo"
ln183:
    .asciz "memmove"
ln184:
    .asciz "memset"
ln185:
    .asciz "mkdir"
ln186:
    .asciz "mkstemp"
ln187:
    .asciz "mktemp"
ln188:
    .asciz "mktime"
ln189:
    .asciz "mmap"
ln190:
    .asciz "mmio_map"
ln191:
    .asciz "modo_testo"
ln192:
    .asciz "mount"
ln193:
    .asciz "mountinfo"
ln194:
    .asciz "munmap"
ln195:
    .asciz "mutex_lascia"
ln196:
    .asciz "mutex_prendi"
ln197:
    .asciz "mutex_prova"
ln198:
    .asciz "nanosleep"
ln199:
    .asciz "nome_utente"
ln200:
    .asciz "open"
ln201:
    .asciz "opendir"
ln202:
    .asciz "osversion"
ln203:
    .asciz "partwrite"
ln204:
    .asciz "pathconf"
ln205:
    .asciz "pclose"
ln206:
    .asciz "perror"
ln207:
    .asciz "pipe"
ln208:
    .asciz "poll"
ln209:
    .asciz "popen"
ln210:
    .asciz "posix_memalign"
ln211:
    .asciz "printf"
ln212:
    .asciz "procinfo"
ln213:
    .asciz "pty_apri"
ln214:
    .asciz "pty_ctl"
ln215:
    .asciz "putc"
ln216:
    .asciz "putchar"
ln217:
    .asciz "putenv"
ln218:
    .asciz "puts"
ln219:
    .asciz "qsort"
ln220:
    .asciz "quick_exit"
ln221:
    .asciz "raise"
ln222:
    .asciz "rand"
ln223:
    .asciz "read"
ln224:
    .asciz "readdir"
ln225:
    .asciz "realloc"
ln226:
    .asciz "realpath"
ln227:
    .asciz "reboot"
ln228:
    .asciz "remove"
ln229:
    .asciz "rename"
ln230:
    .asciz "rewind"
ln231:
    .asciz "rewinddir"
ln232:
    .asciz "rmdir"
ln233:
    .asciz "sbrk"
ln234:
    .asciz "scanf"
ln235:
    .asciz "sched_yield"
ln236:
    .asciz "select"
ln237:
    .asciz "semaforo_lascia"
ln238:
    .asciz "semaforo_prendi"
ln239:
    .asciz "semaforo_prendi_ms"
ln240:
    .asciz "semaforo_prova"
ln241:
    .asciz "setbuf"
ln242:
    .asciz "setenv"
ln243:
    .asciz "setjmp"
ln244:
    .asciz "setlocale"
ln245:
    .asciz "setuid"
ln246:
    .asciz "setvbuf"
ln247:
    .asciz "sha256"
ln248:
    .asciz "sha256_esa"
ln249:
    .asciz "shm_apri"
ln250:
    .asciz "shm_chiudi"
ln251:
    .asciz "signal"
ln252:
    .asciz "sleep"
ln253:
    .asciz "snprintf"
ln254:
    .asciz "spawn"
ln255:
    .asciz "spawn_ex"
ln256:
    .asciz "spawn_su_console"
ln257:
    .asciz "spawn_utente"
ln258:
    .asciz "sprintf"
ln259:
    .asciz "sqrt"
ln260:
    .asciz "srand"
ln261:
    .asciz "sscanf"
ln262:
    .asciz "stat"
ln263:
    .asciz "statperm"
ln264:
    .asciz "statraw"
ln265:
    .asciz "strcasecmp"
ln266:
    .asciz "strcat"
ln267:
    .asciz "strchr"
ln268:
    .asciz "strcmp"
ln269:
    .asciz "strcoll"
ln270:
    .asciz "strcpy"
ln271:
    .asciz "strcspn"
ln272:
    .asciz "strdup"
ln273:
    .asciz "strerror"
ln274:
    .asciz "strftime"
ln275:
    .asciz "strlen"
ln276:
    .asciz "strncasecmp"
ln277:
    .asciz "strncat"
ln278:
    .asciz "strncmp"
ln279:
    .asciz "strncpy"
ln280:
    .asciz "strpbrk"
ln281:
    .asciz "strrchr"
ln282:
    .asciz "strsignal"
ln283:
    .asciz "strspn"
ln284:
    .asciz "strstr"
ln285:
    .asciz "strtod"
ln286:
    .asciz "strtof"
ln287:
    .asciz "strtoimax"
ln288:
    .asciz "strtok"
ln289:
    .asciz "strtol"
ln290:
    .asciz "strtold"
ln291:
    .asciz "strtoll"
ln292:
    .asciz "strtoul"
ln293:
    .asciz "strtoull"
ln294:
    .asciz "strtoumax"
ln295:
    .asciz "strxfrm"
ln296:
    .asciz "swprintf"
ln297:
    .asciz "sysconf"
ln298:
    .asciz "system"
ln299:
    .asciz "thread_attendi"
ln300:
    .asciz "thread_crea"
ln301:
    .asciz "thread_devo_fermarmi"
ln302:
    .asciz "thread_esci"
ln303:
    .asciz "thread_ferma"
ln304:
    .asciz "time"
ln305:
    .asciz "time_now"
ln306:
    .asciz "times"
ln307:
    .asciz "timespec_get"
ln308:
    .asciz "tmpfile"
ln309:
    .asciz "tmpnam"
ln310:
    .asciz "toascii"
ln311:
    .asciz "tolower"
ln312:
    .asciz "toupper"
ln313:
    .asciz "towlower"
ln314:
    .asciz "towupper"
ln315:
    .asciz "truncate"
ln316:
    .asciz "tty_clear"
ln317:
    .asciz "tty_getsize"
ln318:
    .asciz "tty_raw"
ln319:
    .asciz "umask"
ln320:
    .asciz "umount"
ln321:
    .asciz "ungetc"
ln322:
    .asciz "unlink"
ln323:
    .asciz "unsetenv"
ln324:
    .asciz "uptime_ms"
ln325:
    .asciz "usleep"
ln326:
    .asciz "utime"
ln327:
    .asciz "verboseboot"
ln328:
    .asciz "vfprintf"
ln329:
    .asciz "vfscanf"
ln330:
    .asciz "video_info"
ln331:
    .asciz "vprintf"
ln332:
    .asciz "vscanf"
ln333:
    .asciz "vsnprintf"
ln334:
    .asciz "vsprintf"
ln335:
    .asciz "vsscanf"
ln336:
    .asciz "vswprintf"
ln337:
    .asciz "wait"
ln338:
    .asciz "waitpid"
ln339:
    .asciz "wcscat"
ln340:
    .asciz "wcschr"
ln341:
    .asciz "wcscmp"
ln342:
    .asciz "wcscpy"
ln343:
    .asciz "wcscspn"
ln344:
    .asciz "wcslen"
ln345:
    .asciz "wcsncat"
ln346:
    .asciz "wcsncmp"
ln347:
    .asciz "wcsncpy"
ln348:
    .asciz "wcspbrk"
ln349:
    .asciz "wcsrchr"
ln350:
    .asciz "wcsspn"
ln351:
    .asciz "wcsstr"
ln352:
    .asciz "wcstod"
ln353:
    .asciz "wcstol"
ln354:
    .asciz "wcstoll"
ln355:
    .asciz "wcstombs"
ln356:
    .asciz "wcstoul"
ln357:
    .asciz "wcstoull"
ln358:
    .asciz "wctomb"
ln359:
    .asciz "wmemchr"
ln360:
    .asciz "wmemcmp"
ln361:
    .asciz "wmemcpy"
ln362:
    .asciz "wmemmove"
ln363:
    .asciz "wmemset"
ln364:
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
    .long blk_offri
    .long blk_risposta
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
    .long close
    .long closedir
    .long condizione_aspetta
    .long condizione_aspetta_ms
    .long condizione_segnala
    .long condizione_segnala_tutti
    .long console_grafica
    .long console_info
    .long console_setfg
    .long console_switch
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
    .long sha256_esa
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
    .long 365
    .long libc_nomi
    .long libc_indirizzi

    .section .note.GNU-stack,"",@progbits
