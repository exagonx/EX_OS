/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * La tabella che libc.so mette a disposizione: 366 funzioni, ognuna col suo
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
    .asciz "blk_scansiona"
ln26:
    .asciz "blkinfo"
ln27:
    .asciz "blkread"
ln28:
    .asciz "blkwrite"
ln29:
    .asciz "bootinstall"
ln30:
    .asciz "bootverify"
ln31:
    .asciz "bsearch"
ln32:
    .asciz "calloc"
ln33:
    .asciz "chdir"
ln34:
    .asciz "chmod"
ln35:
    .asciz "chown"
ln36:
    .asciz "clearerr"
ln37:
    .asciz "clock"
ln38:
    .asciz "close"
ln39:
    .asciz "closedir"
ln40:
    .asciz "condizione_aspetta"
ln41:
    .asciz "condizione_aspetta_ms"
ln42:
    .asciz "condizione_segnala"
ln43:
    .asciz "condizione_segnala_tutti"
ln44:
    .asciz "console_grafica"
ln45:
    .asciz "console_info"
ln46:
    .asciz "console_setfg"
ln47:
    .asciz "console_switch"
ln48:
    .asciz "console_write"
ln49:
    .asciz "ctime"
ln50:
    .asciz "difftime"
ln51:
    .asciz "diskinfo"
ln52:
    .asciz "div"
ln53:
    .asciz "diventa_root"
ln54:
    .asciz "dma_alloc"
ln55:
    .asciz "dup"
ln56:
    .asciz "dup2"
ln57:
    .asciz "execv"
ln58:
    .asciz "execvp"
ln59:
    .asciz "exit"
ln60:
    .asciz "fabs"
ln61:
    .asciz "fb_map"
ln62:
    .asciz "fchmod"
ln63:
    .asciz "fclose"
ln64:
    .asciz "fcntl"
ln65:
    .asciz "fdopen"
ln66:
    .asciz "fdprova"
ln67:
    .asciz "feof"
ln68:
    .asciz "ferror"
ln69:
    .asciz "fflush"
ln70:
    .asciz "fgetc"
ln71:
    .asciz "fgetpos"
ln72:
    .asciz "fgets"
ln73:
    .asciz "fileno"
ln74:
    .asciz "fopen"
ln75:
    .asciz "fpathconf"
ln76:
    .asciz "fprintf"
ln77:
    .asciz "fputc"
ln78:
    .asciz "fputs"
ln79:
    .asciz "fread"
ln80:
    .asciz "free"
ln81:
    .asciz "freopen"
ln82:
    .asciz "frexp"
ln83:
    .asciz "fscanf"
ln84:
    .asciz "fseek"
ln85:
    .asciz "fsetpos"
ln86:
    .asciz "fsize"
ln87:
    .asciz "fstat"
ln88:
    .asciz "ftell"
ln89:
    .asciz "fwrite"
ln90:
    .asciz "getc"
ln91:
    .asciz "getchar"
ln92:
    .asciz "getconf"
ln93:
    .asciz "getcwd"
ln94:
    .asciz "getegid"
ln95:
    .asciz "getentropy"
ln96:
    .asciz "getenv"
ln97:
    .asciz "geteuid"
ln98:
    .asciz "getgid"
ln99:
    .asciz "getpagesize"
ln100:
    .asciz "getpid"
ln101:
    .asciz "getrandom"
ln102:
    .asciz "getrusage"
ln103:
    .asciz "gets"
ln104:
    .asciz "gettimeofday"
ln105:
    .asciz "getuid"
ln106:
    .asciz "gmtime"
ln107:
    .asciz "gmtime_r"
ln108:
    .asciz "imaxabs"
ln109:
    .asciz "imaxdiv"
ln110:
    .asciz "interrompi"
ln111:
    .asciz "ioctl"
ln112:
    .asciz "ioport_bind"
ln113:
    .asciz "ioport_in"
ln114:
    .asciz "ioport_in16"
ln115:
    .asciz "ioport_in32"
ln116:
    .asciz "ioport_out"
ln117:
    .asciz "ioport_out16"
ln118:
    .asciz "ioport_out32"
ln119:
    .asciz "ipc_attendi"
ln120:
    .asciz "ipc_lookup"
ln121:
    .asciz "ipc_pronto"
ln122:
    .asciz "ipc_recv"
ln123:
    .asciz "ipc_recv_timeout"
ln124:
    .asciz "ipc_register"
ln125:
    .asciz "ipc_rimetti"
ln126:
    .asciz "ipc_scegli"
ln127:
    .asciz "ipc_send"
ln128:
    .asciz "irq_bind"
ln129:
    .asciz "irq_done"
ln130:
    .asciz "irq_unbind"
ln131:
    .asciz "isalnum"
ln132:
    .asciz "isalpha"
ln133:
    .asciz "isascii"
ln134:
    .asciz "isatty"
ln135:
    .asciz "isblank"
ln136:
    .asciz "iscntrl"
ln137:
    .asciz "isdigit"
ln138:
    .asciz "isgraph"
ln139:
    .asciz "islower"
ln140:
    .asciz "isprint"
ln141:
    .asciz "ispunct"
ln142:
    .asciz "isspace"
ln143:
    .asciz "isupper"
ln144:
    .asciz "iswalnum"
ln145:
    .asciz "iswalpha"
ln146:
    .asciz "iswblank"
ln147:
    .asciz "iswcntrl"
ln148:
    .asciz "iswdigit"
ln149:
    .asciz "iswgraph"
ln150:
    .asciz "iswlower"
ln151:
    .asciz "iswprint"
ln152:
    .asciz "iswpunct"
ln153:
    .asciz "iswspace"
ln154:
    .asciz "iswupper"
ln155:
    .asciz "iswxdigit"
ln156:
    .asciz "isxdigit"
ln157:
    .asciz "kbprova"
ln158:
    .asciz "kbstato"
ln159:
    .asciz "labs"
ln160:
    .asciz "ldexp"
ln161:
    .asciz "ldiv"
ln162:
    .asciz "listdir"
ln163:
    .asciz "listdir_from"
ln164:
    .asciz "llabs"
ln165:
    .asciz "lldiv"
ln166:
    .asciz "localeconv"
ln167:
    .asciz "localtime"
ln168:
    .asciz "localtime_r"
ln169:
    .asciz "log_seriale"
ln170:
    .asciz "longjmp"
ln171:
    .asciz "lseek"
ln172:
    .asciz "lstat"
ln173:
    .asciz "malloc"
ln174:
    .asciz "malloc_usable_size"
ln175:
    .asciz "mblen"
ln176:
    .asciz "mbrtowc"
ln177:
    .asciz "mbstowcs"
ln178:
    .asciz "mbtowc"
ln179:
    .asciz "memalign"
ln180:
    .asciz "memchr"
ln181:
    .asciz "memcmp"
ln182:
    .asciz "memcpy"
ln183:
    .asciz "meminfo"
ln184:
    .asciz "memmove"
ln185:
    .asciz "memset"
ln186:
    .asciz "mkdir"
ln187:
    .asciz "mkstemp"
ln188:
    .asciz "mktemp"
ln189:
    .asciz "mktime"
ln190:
    .asciz "mmap"
ln191:
    .asciz "mmio_map"
ln192:
    .asciz "modo_testo"
ln193:
    .asciz "mount"
ln194:
    .asciz "mountinfo"
ln195:
    .asciz "munmap"
ln196:
    .asciz "mutex_lascia"
ln197:
    .asciz "mutex_prendi"
ln198:
    .asciz "mutex_prova"
ln199:
    .asciz "nanosleep"
ln200:
    .asciz "nome_utente"
ln201:
    .asciz "open"
ln202:
    .asciz "opendir"
ln203:
    .asciz "osversion"
ln204:
    .asciz "partwrite"
ln205:
    .asciz "pathconf"
ln206:
    .asciz "pclose"
ln207:
    .asciz "perror"
ln208:
    .asciz "pipe"
ln209:
    .asciz "poll"
ln210:
    .asciz "popen"
ln211:
    .asciz "posix_memalign"
ln212:
    .asciz "printf"
ln213:
    .asciz "procinfo"
ln214:
    .asciz "pty_apri"
ln215:
    .asciz "pty_ctl"
ln216:
    .asciz "putc"
ln217:
    .asciz "putchar"
ln218:
    .asciz "putenv"
ln219:
    .asciz "puts"
ln220:
    .asciz "qsort"
ln221:
    .asciz "quick_exit"
ln222:
    .asciz "raise"
ln223:
    .asciz "rand"
ln224:
    .asciz "read"
ln225:
    .asciz "readdir"
ln226:
    .asciz "realloc"
ln227:
    .asciz "realpath"
ln228:
    .asciz "reboot"
ln229:
    .asciz "remove"
ln230:
    .asciz "rename"
ln231:
    .asciz "rewind"
ln232:
    .asciz "rewinddir"
ln233:
    .asciz "rmdir"
ln234:
    .asciz "sbrk"
ln235:
    .asciz "scanf"
ln236:
    .asciz "sched_yield"
ln237:
    .asciz "select"
ln238:
    .asciz "semaforo_lascia"
ln239:
    .asciz "semaforo_prendi"
ln240:
    .asciz "semaforo_prendi_ms"
ln241:
    .asciz "semaforo_prova"
ln242:
    .asciz "setbuf"
ln243:
    .asciz "setenv"
ln244:
    .asciz "setjmp"
ln245:
    .asciz "setlocale"
ln246:
    .asciz "setuid"
ln247:
    .asciz "setvbuf"
ln248:
    .asciz "sha256"
ln249:
    .asciz "sha256_esa"
ln250:
    .asciz "shm_apri"
ln251:
    .asciz "shm_chiudi"
ln252:
    .asciz "signal"
ln253:
    .asciz "sleep"
ln254:
    .asciz "snprintf"
ln255:
    .asciz "spawn"
ln256:
    .asciz "spawn_ex"
ln257:
    .asciz "spawn_su_console"
ln258:
    .asciz "spawn_utente"
ln259:
    .asciz "sprintf"
ln260:
    .asciz "sqrt"
ln261:
    .asciz "srand"
ln262:
    .asciz "sscanf"
ln263:
    .asciz "stat"
ln264:
    .asciz "statperm"
ln265:
    .asciz "statraw"
ln266:
    .asciz "strcasecmp"
ln267:
    .asciz "strcat"
ln268:
    .asciz "strchr"
ln269:
    .asciz "strcmp"
ln270:
    .asciz "strcoll"
ln271:
    .asciz "strcpy"
ln272:
    .asciz "strcspn"
ln273:
    .asciz "strdup"
ln274:
    .asciz "strerror"
ln275:
    .asciz "strftime"
ln276:
    .asciz "strlen"
ln277:
    .asciz "strncasecmp"
ln278:
    .asciz "strncat"
ln279:
    .asciz "strncmp"
ln280:
    .asciz "strncpy"
ln281:
    .asciz "strpbrk"
ln282:
    .asciz "strrchr"
ln283:
    .asciz "strsignal"
ln284:
    .asciz "strspn"
ln285:
    .asciz "strstr"
ln286:
    .asciz "strtod"
ln287:
    .asciz "strtof"
ln288:
    .asciz "strtoimax"
ln289:
    .asciz "strtok"
ln290:
    .asciz "strtol"
ln291:
    .asciz "strtold"
ln292:
    .asciz "strtoll"
ln293:
    .asciz "strtoul"
ln294:
    .asciz "strtoull"
ln295:
    .asciz "strtoumax"
ln296:
    .asciz "strxfrm"
ln297:
    .asciz "swprintf"
ln298:
    .asciz "sysconf"
ln299:
    .asciz "system"
ln300:
    .asciz "thread_attendi"
ln301:
    .asciz "thread_crea"
ln302:
    .asciz "thread_devo_fermarmi"
ln303:
    .asciz "thread_esci"
ln304:
    .asciz "thread_ferma"
ln305:
    .asciz "time"
ln306:
    .asciz "time_now"
ln307:
    .asciz "times"
ln308:
    .asciz "timespec_get"
ln309:
    .asciz "tmpfile"
ln310:
    .asciz "tmpnam"
ln311:
    .asciz "toascii"
ln312:
    .asciz "tolower"
ln313:
    .asciz "toupper"
ln314:
    .asciz "towlower"
ln315:
    .asciz "towupper"
ln316:
    .asciz "truncate"
ln317:
    .asciz "tty_clear"
ln318:
    .asciz "tty_getsize"
ln319:
    .asciz "tty_raw"
ln320:
    .asciz "umask"
ln321:
    .asciz "umount"
ln322:
    .asciz "ungetc"
ln323:
    .asciz "unlink"
ln324:
    .asciz "unsetenv"
ln325:
    .asciz "uptime_ms"
ln326:
    .asciz "usleep"
ln327:
    .asciz "utime"
ln328:
    .asciz "verboseboot"
ln329:
    .asciz "vfprintf"
ln330:
    .asciz "vfscanf"
ln331:
    .asciz "video_info"
ln332:
    .asciz "vprintf"
ln333:
    .asciz "vscanf"
ln334:
    .asciz "vsnprintf"
ln335:
    .asciz "vsprintf"
ln336:
    .asciz "vsscanf"
ln337:
    .asciz "vswprintf"
ln338:
    .asciz "wait"
ln339:
    .asciz "waitpid"
ln340:
    .asciz "wcscat"
ln341:
    .asciz "wcschr"
ln342:
    .asciz "wcscmp"
ln343:
    .asciz "wcscpy"
ln344:
    .asciz "wcscspn"
ln345:
    .asciz "wcslen"
ln346:
    .asciz "wcsncat"
ln347:
    .asciz "wcsncmp"
ln348:
    .asciz "wcsncpy"
ln349:
    .asciz "wcspbrk"
ln350:
    .asciz "wcsrchr"
ln351:
    .asciz "wcsspn"
ln352:
    .asciz "wcsstr"
ln353:
    .asciz "wcstod"
ln354:
    .asciz "wcstol"
ln355:
    .asciz "wcstoll"
ln356:
    .asciz "wcstombs"
ln357:
    .asciz "wcstoul"
ln358:
    .asciz "wcstoull"
ln359:
    .asciz "wctomb"
ln360:
    .asciz "wmemchr"
ln361:
    .asciz "wmemcmp"
ln362:
    .asciz "wmemcpy"
ln363:
    .asciz "wmemmove"
ln364:
    .asciz "wmemset"
ln365:
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
    .long 366
    .long libc_nomi
    .long libc_indirizzi

    .section .note.GNU-stack,"",@progbits
