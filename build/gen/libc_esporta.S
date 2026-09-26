/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * La tabella che libc.so mette a disposizione: 373 funzioni, ognuna col suo
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
    .asciz "close"
ln40:
    .asciz "closedir"
ln41:
    .asciz "condizione_aspetta"
ln42:
    .asciz "condizione_aspetta_ms"
ln43:
    .asciz "condizione_segnala"
ln44:
    .asciz "condizione_segnala_tutti"
ln45:
    .asciz "console_grafica"
ln46:
    .asciz "console_info"
ln47:
    .asciz "console_setfg"
ln48:
    .asciz "console_switch"
ln49:
    .asciz "console_testo"
ln50:
    .asciz "console_write"
ln51:
    .asciz "ctime"
ln52:
    .asciz "difftime"
ln53:
    .asciz "diskinfo"
ln54:
    .asciz "div"
ln55:
    .asciz "diventa_root"
ln56:
    .asciz "dma_alloc"
ln57:
    .asciz "dup"
ln58:
    .asciz "dup2"
ln59:
    .asciz "execv"
ln60:
    .asciz "execvp"
ln61:
    .asciz "exit"
ln62:
    .asciz "fabs"
ln63:
    .asciz "fb_map"
ln64:
    .asciz "fchmod"
ln65:
    .asciz "fclose"
ln66:
    .asciz "fcntl"
ln67:
    .asciz "fdopen"
ln68:
    .asciz "fdprova"
ln69:
    .asciz "feof"
ln70:
    .asciz "ferror"
ln71:
    .asciz "fflush"
ln72:
    .asciz "fgetc"
ln73:
    .asciz "fgetpos"
ln74:
    .asciz "fgets"
ln75:
    .asciz "fileno"
ln76:
    .asciz "fopen"
ln77:
    .asciz "fpathconf"
ln78:
    .asciz "fprintf"
ln79:
    .asciz "fputc"
ln80:
    .asciz "fputs"
ln81:
    .asciz "fread"
ln82:
    .asciz "free"
ln83:
    .asciz "freopen"
ln84:
    .asciz "frexp"
ln85:
    .asciz "fscanf"
ln86:
    .asciz "fseek"
ln87:
    .asciz "fsetpos"
ln88:
    .asciz "fsize"
ln89:
    .asciz "fstat"
ln90:
    .asciz "ftell"
ln91:
    .asciz "fwrite"
ln92:
    .asciz "getc"
ln93:
    .asciz "getchar"
ln94:
    .asciz "getconf"
ln95:
    .asciz "getcwd"
ln96:
    .asciz "getegid"
ln97:
    .asciz "getentropy"
ln98:
    .asciz "getenv"
ln99:
    .asciz "geteuid"
ln100:
    .asciz "getgid"
ln101:
    .asciz "getpagesize"
ln102:
    .asciz "getpid"
ln103:
    .asciz "getrandom"
ln104:
    .asciz "getrusage"
ln105:
    .asciz "gets"
ln106:
    .asciz "gettimeofday"
ln107:
    .asciz "getuid"
ln108:
    .asciz "gmtime"
ln109:
    .asciz "gmtime_r"
ln110:
    .asciz "imaxabs"
ln111:
    .asciz "imaxdiv"
ln112:
    .asciz "interrompi"
ln113:
    .asciz "ioctl"
ln114:
    .asciz "ioport_bind"
ln115:
    .asciz "ioport_in"
ln116:
    .asciz "ioport_in16"
ln117:
    .asciz "ioport_in32"
ln118:
    .asciz "ioport_out"
ln119:
    .asciz "ioport_out16"
ln120:
    .asciz "ioport_out32"
ln121:
    .asciz "ipc_attendi"
ln122:
    .asciz "ipc_lookup"
ln123:
    .asciz "ipc_pronto"
ln124:
    .asciz "ipc_recv"
ln125:
    .asciz "ipc_recv_timeout"
ln126:
    .asciz "ipc_register"
ln127:
    .asciz "ipc_rimetti"
ln128:
    .asciz "ipc_scegli"
ln129:
    .asciz "ipc_send"
ln130:
    .asciz "irq_bind"
ln131:
    .asciz "irq_done"
ln132:
    .asciz "irq_unbind"
ln133:
    .asciz "isalnum"
ln134:
    .asciz "isalpha"
ln135:
    .asciz "isascii"
ln136:
    .asciz "isatty"
ln137:
    .asciz "isblank"
ln138:
    .asciz "iscntrl"
ln139:
    .asciz "isdigit"
ln140:
    .asciz "isgraph"
ln141:
    .asciz "islower"
ln142:
    .asciz "isprint"
ln143:
    .asciz "ispunct"
ln144:
    .asciz "isspace"
ln145:
    .asciz "isupper"
ln146:
    .asciz "iswalnum"
ln147:
    .asciz "iswalpha"
ln148:
    .asciz "iswblank"
ln149:
    .asciz "iswcntrl"
ln150:
    .asciz "iswdigit"
ln151:
    .asciz "iswgraph"
ln152:
    .asciz "iswlower"
ln153:
    .asciz "iswprint"
ln154:
    .asciz "iswpunct"
ln155:
    .asciz "iswspace"
ln156:
    .asciz "iswupper"
ln157:
    .asciz "iswxdigit"
ln158:
    .asciz "isxdigit"
ln159:
    .asciz "kbprova"
ln160:
    .asciz "kbstato"
ln161:
    .asciz "labs"
ln162:
    .asciz "ldexp"
ln163:
    .asciz "ldiv"
ln164:
    .asciz "listdir"
ln165:
    .asciz "listdir_from"
ln166:
    .asciz "llabs"
ln167:
    .asciz "lldiv"
ln168:
    .asciz "localeconv"
ln169:
    .asciz "localtime"
ln170:
    .asciz "localtime_r"
ln171:
    .asciz "log_seriale"
ln172:
    .asciz "longjmp"
ln173:
    .asciz "lseek"
ln174:
    .asciz "lstat"
ln175:
    .asciz "malloc"
ln176:
    .asciz "malloc_usable_size"
ln177:
    .asciz "mblen"
ln178:
    .asciz "mbrtowc"
ln179:
    .asciz "mbstowcs"
ln180:
    .asciz "mbtowc"
ln181:
    .asciz "memalign"
ln182:
    .asciz "memchr"
ln183:
    .asciz "memcmp"
ln184:
    .asciz "memcpy"
ln185:
    .asciz "meminfo"
ln186:
    .asciz "memmove"
ln187:
    .asciz "memset"
ln188:
    .asciz "mkdir"
ln189:
    .asciz "mkstemp"
ln190:
    .asciz "mktemp"
ln191:
    .asciz "mktime"
ln192:
    .asciz "mmap"
ln193:
    .asciz "mmio_map"
ln194:
    .asciz "modo_testo"
ln195:
    .asciz "mount"
ln196:
    .asciz "mountinfo"
ln197:
    .asciz "munmap"
ln198:
    .asciz "mutex_lascia"
ln199:
    .asciz "mutex_prendi"
ln200:
    .asciz "mutex_prova"
ln201:
    .asciz "nanosleep"
ln202:
    .asciz "nome_utente"
ln203:
    .asciz "open"
ln204:
    .asciz "opendir"
ln205:
    .asciz "osversion"
ln206:
    .asciz "partwrite"
ln207:
    .asciz "pathconf"
ln208:
    .asciz "pclose"
ln209:
    .asciz "perror"
ln210:
    .asciz "pipe"
ln211:
    .asciz "poll"
ln212:
    .asciz "popen"
ln213:
    .asciz "posix_memalign"
ln214:
    .asciz "printf"
ln215:
    .asciz "procinfo"
ln216:
    .asciz "pty_apri"
ln217:
    .asciz "pty_ctl"
ln218:
    .asciz "putc"
ln219:
    .asciz "putchar"
ln220:
    .asciz "putenv"
ln221:
    .asciz "puts"
ln222:
    .asciz "qsort"
ln223:
    .asciz "quick_exit"
ln224:
    .asciz "raise"
ln225:
    .asciz "rand"
ln226:
    .asciz "read"
ln227:
    .asciz "readdir"
ln228:
    .asciz "realloc"
ln229:
    .asciz "realpath"
ln230:
    .asciz "reboot"
ln231:
    .asciz "remove"
ln232:
    .asciz "rename"
ln233:
    .asciz "rewind"
ln234:
    .asciz "rewinddir"
ln235:
    .asciz "rmdir"
ln236:
    .asciz "sbrk"
ln237:
    .asciz "scanf"
ln238:
    .asciz "sched_yield"
ln239:
    .asciz "select"
ln240:
    .asciz "semaforo_lascia"
ln241:
    .asciz "semaforo_prendi"
ln242:
    .asciz "semaforo_prendi_ms"
ln243:
    .asciz "semaforo_prova"
ln244:
    .asciz "setbuf"
ln245:
    .asciz "setenv"
ln246:
    .asciz "setjmp"
ln247:
    .asciz "setlocale"
ln248:
    .asciz "setuid"
ln249:
    .asciz "setvbuf"
ln250:
    .asciz "sha256"
ln251:
    .asciz "sha256_avvia"
ln252:
    .asciz "sha256_dai"
ln253:
    .asciz "sha256_esa"
ln254:
    .asciz "sha256_fine"
ln255:
    .asciz "sha256_fine_esa"
ln256:
    .asciz "shm_apri"
ln257:
    .asciz "shm_chiudi"
ln258:
    .asciz "signal"
ln259:
    .asciz "sleep"
ln260:
    .asciz "snprintf"
ln261:
    .asciz "spawn"
ln262:
    .asciz "spawn_ex"
ln263:
    .asciz "spawn_su_console"
ln264:
    .asciz "spawn_utente"
ln265:
    .asciz "sprintf"
ln266:
    .asciz "sqrt"
ln267:
    .asciz "srand"
ln268:
    .asciz "sscanf"
ln269:
    .asciz "stat"
ln270:
    .asciz "statperm"
ln271:
    .asciz "statraw"
ln272:
    .asciz "strcasecmp"
ln273:
    .asciz "strcat"
ln274:
    .asciz "strchr"
ln275:
    .asciz "strcmp"
ln276:
    .asciz "strcoll"
ln277:
    .asciz "strcpy"
ln278:
    .asciz "strcspn"
ln279:
    .asciz "strdup"
ln280:
    .asciz "strerror"
ln281:
    .asciz "strftime"
ln282:
    .asciz "strlen"
ln283:
    .asciz "strncasecmp"
ln284:
    .asciz "strncat"
ln285:
    .asciz "strncmp"
ln286:
    .asciz "strncpy"
ln287:
    .asciz "strpbrk"
ln288:
    .asciz "strrchr"
ln289:
    .asciz "strsignal"
ln290:
    .asciz "strspn"
ln291:
    .asciz "strstr"
ln292:
    .asciz "strtod"
ln293:
    .asciz "strtof"
ln294:
    .asciz "strtoimax"
ln295:
    .asciz "strtok"
ln296:
    .asciz "strtol"
ln297:
    .asciz "strtold"
ln298:
    .asciz "strtoll"
ln299:
    .asciz "strtoul"
ln300:
    .asciz "strtoull"
ln301:
    .asciz "strtoumax"
ln302:
    .asciz "strxfrm"
ln303:
    .asciz "swprintf"
ln304:
    .asciz "sysconf"
ln305:
    .asciz "system"
ln306:
    .asciz "thread_attendi"
ln307:
    .asciz "thread_crea"
ln308:
    .asciz "thread_devo_fermarmi"
ln309:
    .asciz "thread_esci"
ln310:
    .asciz "thread_ferma"
ln311:
    .asciz "time"
ln312:
    .asciz "time_now"
ln313:
    .asciz "time_set"
ln314:
    .asciz "times"
ln315:
    .asciz "timespec_get"
ln316:
    .asciz "tmpfile"
ln317:
    .asciz "tmpnam"
ln318:
    .asciz "toascii"
ln319:
    .asciz "tolower"
ln320:
    .asciz "toupper"
ln321:
    .asciz "towlower"
ln322:
    .asciz "towupper"
ln323:
    .asciz "truncate"
ln324:
    .asciz "tty_clear"
ln325:
    .asciz "tty_getsize"
ln326:
    .asciz "tty_raw"
ln327:
    .asciz "umask"
ln328:
    .asciz "umount"
ln329:
    .asciz "ungetc"
ln330:
    .asciz "unlink"
ln331:
    .asciz "unsetenv"
ln332:
    .asciz "uptime_ms"
ln333:
    .asciz "usleep"
ln334:
    .asciz "utime"
ln335:
    .asciz "verboseboot"
ln336:
    .asciz "vfprintf"
ln337:
    .asciz "vfscanf"
ln338:
    .asciz "video_info"
ln339:
    .asciz "vprintf"
ln340:
    .asciz "vscanf"
ln341:
    .asciz "vsnprintf"
ln342:
    .asciz "vsprintf"
ln343:
    .asciz "vsscanf"
ln344:
    .asciz "vswprintf"
ln345:
    .asciz "wait"
ln346:
    .asciz "waitpid"
ln347:
    .asciz "wcscat"
ln348:
    .asciz "wcschr"
ln349:
    .asciz "wcscmp"
ln350:
    .asciz "wcscpy"
ln351:
    .asciz "wcscspn"
ln352:
    .asciz "wcslen"
ln353:
    .asciz "wcsncat"
ln354:
    .asciz "wcsncmp"
ln355:
    .asciz "wcsncpy"
ln356:
    .asciz "wcspbrk"
ln357:
    .asciz "wcsrchr"
ln358:
    .asciz "wcsspn"
ln359:
    .asciz "wcsstr"
ln360:
    .asciz "wcstod"
ln361:
    .asciz "wcstol"
ln362:
    .asciz "wcstoll"
ln363:
    .asciz "wcstombs"
ln364:
    .asciz "wcstoul"
ln365:
    .asciz "wcstoull"
ln366:
    .asciz "wctomb"
ln367:
    .asciz "wmemchr"
ln368:
    .asciz "wmemcmp"
ln369:
    .asciz "wmemcpy"
ln370:
    .asciz "wmemmove"
ln371:
    .asciz "wmemset"
ln372:
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
    .long 373
    .long libc_nomi
    .long libc_indirizzi

    .section .note.GNU-stack,"",@progbits
