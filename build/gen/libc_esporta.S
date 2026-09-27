/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * La tabella che libc.so mette a disposizione: 375 funzioni, ognuna col suo
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
    .asciz "console_registro"
ln48:
    .asciz "console_setfg"
ln49:
    .asciz "console_switch"
ln50:
    .asciz "console_testo"
ln51:
    .asciz "console_write"
ln52:
    .asciz "ctime"
ln53:
    .asciz "difftime"
ln54:
    .asciz "diskinfo"
ln55:
    .asciz "div"
ln56:
    .asciz "diventa_root"
ln57:
    .asciz "dma_alloc"
ln58:
    .asciz "dup"
ln59:
    .asciz "dup2"
ln60:
    .asciz "execv"
ln61:
    .asciz "execvp"
ln62:
    .asciz "exit"
ln63:
    .asciz "fabs"
ln64:
    .asciz "fb_map"
ln65:
    .asciz "fchmod"
ln66:
    .asciz "fclose"
ln67:
    .asciz "fcntl"
ln68:
    .asciz "fdopen"
ln69:
    .asciz "fdprova"
ln70:
    .asciz "feof"
ln71:
    .asciz "ferror"
ln72:
    .asciz "fflush"
ln73:
    .asciz "fgetc"
ln74:
    .asciz "fgetpos"
ln75:
    .asciz "fgets"
ln76:
    .asciz "fileno"
ln77:
    .asciz "fopen"
ln78:
    .asciz "fpathconf"
ln79:
    .asciz "fprintf"
ln80:
    .asciz "fputc"
ln81:
    .asciz "fputs"
ln82:
    .asciz "fread"
ln83:
    .asciz "free"
ln84:
    .asciz "freopen"
ln85:
    .asciz "frexp"
ln86:
    .asciz "fscanf"
ln87:
    .asciz "fseek"
ln88:
    .asciz "fsetpos"
ln89:
    .asciz "fsize"
ln90:
    .asciz "fstat"
ln91:
    .asciz "ftell"
ln92:
    .asciz "fwrite"
ln93:
    .asciz "getc"
ln94:
    .asciz "getchar"
ln95:
    .asciz "getconf"
ln96:
    .asciz "getcwd"
ln97:
    .asciz "getegid"
ln98:
    .asciz "getentropy"
ln99:
    .asciz "getenv"
ln100:
    .asciz "geteuid"
ln101:
    .asciz "getgid"
ln102:
    .asciz "getpagesize"
ln103:
    .asciz "getpid"
ln104:
    .asciz "getrandom"
ln105:
    .asciz "getrusage"
ln106:
    .asciz "gets"
ln107:
    .asciz "gettimeofday"
ln108:
    .asciz "getuid"
ln109:
    .asciz "gmtime"
ln110:
    .asciz "gmtime_r"
ln111:
    .asciz "imaxabs"
ln112:
    .asciz "imaxdiv"
ln113:
    .asciz "interrompi"
ln114:
    .asciz "ioctl"
ln115:
    .asciz "ioport_bind"
ln116:
    .asciz "ioport_in"
ln117:
    .asciz "ioport_in16"
ln118:
    .asciz "ioport_in32"
ln119:
    .asciz "ioport_out"
ln120:
    .asciz "ioport_out16"
ln121:
    .asciz "ioport_out32"
ln122:
    .asciz "ipc_attendi"
ln123:
    .asciz "ipc_lookup"
ln124:
    .asciz "ipc_pronto"
ln125:
    .asciz "ipc_recv"
ln126:
    .asciz "ipc_recv_timeout"
ln127:
    .asciz "ipc_register"
ln128:
    .asciz "ipc_rimetti"
ln129:
    .asciz "ipc_scegli"
ln130:
    .asciz "ipc_send"
ln131:
    .asciz "irq_bind"
ln132:
    .asciz "irq_done"
ln133:
    .asciz "irq_unbind"
ln134:
    .asciz "isalnum"
ln135:
    .asciz "isalpha"
ln136:
    .asciz "isascii"
ln137:
    .asciz "isatty"
ln138:
    .asciz "isblank"
ln139:
    .asciz "iscntrl"
ln140:
    .asciz "isdigit"
ln141:
    .asciz "isgraph"
ln142:
    .asciz "islower"
ln143:
    .asciz "isprint"
ln144:
    .asciz "ispunct"
ln145:
    .asciz "isspace"
ln146:
    .asciz "isupper"
ln147:
    .asciz "iswalnum"
ln148:
    .asciz "iswalpha"
ln149:
    .asciz "iswblank"
ln150:
    .asciz "iswcntrl"
ln151:
    .asciz "iswdigit"
ln152:
    .asciz "iswgraph"
ln153:
    .asciz "iswlower"
ln154:
    .asciz "iswprint"
ln155:
    .asciz "iswpunct"
ln156:
    .asciz "iswspace"
ln157:
    .asciz "iswupper"
ln158:
    .asciz "iswxdigit"
ln159:
    .asciz "isxdigit"
ln160:
    .asciz "kbprova"
ln161:
    .asciz "kbstato"
ln162:
    .asciz "labs"
ln163:
    .asciz "ldexp"
ln164:
    .asciz "ldiv"
ln165:
    .asciz "listdir"
ln166:
    .asciz "listdir_from"
ln167:
    .asciz "llabs"
ln168:
    .asciz "lldiv"
ln169:
    .asciz "localeconv"
ln170:
    .asciz "localtime"
ln171:
    .asciz "localtime_r"
ln172:
    .asciz "log_seriale"
ln173:
    .asciz "longjmp"
ln174:
    .asciz "lseek"
ln175:
    .asciz "lstat"
ln176:
    .asciz "malloc"
ln177:
    .asciz "malloc_usable_size"
ln178:
    .asciz "mblen"
ln179:
    .asciz "mbrtowc"
ln180:
    .asciz "mbstowcs"
ln181:
    .asciz "mbtowc"
ln182:
    .asciz "memalign"
ln183:
    .asciz "memchr"
ln184:
    .asciz "memcmp"
ln185:
    .asciz "memcpy"
ln186:
    .asciz "meminfo"
ln187:
    .asciz "memmove"
ln188:
    .asciz "memset"
ln189:
    .asciz "mkdir"
ln190:
    .asciz "mkstemp"
ln191:
    .asciz "mktemp"
ln192:
    .asciz "mktime"
ln193:
    .asciz "mmap"
ln194:
    .asciz "mmio_map"
ln195:
    .asciz "modo_testo"
ln196:
    .asciz "mount"
ln197:
    .asciz "mountinfo"
ln198:
    .asciz "munmap"
ln199:
    .asciz "mutex_lascia"
ln200:
    .asciz "mutex_prendi"
ln201:
    .asciz "mutex_prova"
ln202:
    .asciz "nanosleep"
ln203:
    .asciz "nome_utente"
ln204:
    .asciz "open"
ln205:
    .asciz "opendir"
ln206:
    .asciz "osversion"
ln207:
    .asciz "partwrite"
ln208:
    .asciz "pathconf"
ln209:
    .asciz "pclose"
ln210:
    .asciz "perror"
ln211:
    .asciz "pipe"
ln212:
    .asciz "poll"
ln213:
    .asciz "popen"
ln214:
    .asciz "posix_memalign"
ln215:
    .asciz "printf"
ln216:
    .asciz "procinfo"
ln217:
    .asciz "pty_apri"
ln218:
    .asciz "pty_ctl"
ln219:
    .asciz "putc"
ln220:
    .asciz "putchar"
ln221:
    .asciz "putenv"
ln222:
    .asciz "puts"
ln223:
    .asciz "qsort"
ln224:
    .asciz "quick_exit"
ln225:
    .asciz "raise"
ln226:
    .asciz "rand"
ln227:
    .asciz "read"
ln228:
    .asciz "readdir"
ln229:
    .asciz "realloc"
ln230:
    .asciz "realpath"
ln231:
    .asciz "reboot"
ln232:
    .asciz "remove"
ln233:
    .asciz "rename"
ln234:
    .asciz "rewind"
ln235:
    .asciz "rewinddir"
ln236:
    .asciz "rmdir"
ln237:
    .asciz "sbrk"
ln238:
    .asciz "scanf"
ln239:
    .asciz "sched_yield"
ln240:
    .asciz "select"
ln241:
    .asciz "semaforo_lascia"
ln242:
    .asciz "semaforo_prendi"
ln243:
    .asciz "semaforo_prendi_ms"
ln244:
    .asciz "semaforo_prova"
ln245:
    .asciz "setbuf"
ln246:
    .asciz "setenv"
ln247:
    .asciz "setjmp"
ln248:
    .asciz "setlocale"
ln249:
    .asciz "setuid"
ln250:
    .asciz "setvbuf"
ln251:
    .asciz "sha256"
ln252:
    .asciz "sha256_avvia"
ln253:
    .asciz "sha256_dai"
ln254:
    .asciz "sha256_esa"
ln255:
    .asciz "sha256_fine"
ln256:
    .asciz "sha256_fine_esa"
ln257:
    .asciz "shm_apri"
ln258:
    .asciz "shm_chiudi"
ln259:
    .asciz "signal"
ln260:
    .asciz "sleep"
ln261:
    .asciz "snprintf"
ln262:
    .asciz "spawn"
ln263:
    .asciz "spawn_ex"
ln264:
    .asciz "spawn_su_console"
ln265:
    .asciz "spawn_utente"
ln266:
    .asciz "sprintf"
ln267:
    .asciz "sqrt"
ln268:
    .asciz "srand"
ln269:
    .asciz "sscanf"
ln270:
    .asciz "stat"
ln271:
    .asciz "statperm"
ln272:
    .asciz "statraw"
ln273:
    .asciz "strcasecmp"
ln274:
    .asciz "strcat"
ln275:
    .asciz "strchr"
ln276:
    .asciz "strcmp"
ln277:
    .asciz "strcoll"
ln278:
    .asciz "strcpy"
ln279:
    .asciz "strcspn"
ln280:
    .asciz "strdup"
ln281:
    .asciz "strerror"
ln282:
    .asciz "strftime"
ln283:
    .asciz "strlen"
ln284:
    .asciz "strncasecmp"
ln285:
    .asciz "strncat"
ln286:
    .asciz "strncmp"
ln287:
    .asciz "strncpy"
ln288:
    .asciz "strpbrk"
ln289:
    .asciz "strrchr"
ln290:
    .asciz "strsignal"
ln291:
    .asciz "strspn"
ln292:
    .asciz "strstr"
ln293:
    .asciz "strtod"
ln294:
    .asciz "strtof"
ln295:
    .asciz "strtoimax"
ln296:
    .asciz "strtok"
ln297:
    .asciz "strtok_r"
ln298:
    .asciz "strtol"
ln299:
    .asciz "strtold"
ln300:
    .asciz "strtoll"
ln301:
    .asciz "strtoul"
ln302:
    .asciz "strtoull"
ln303:
    .asciz "strtoumax"
ln304:
    .asciz "strxfrm"
ln305:
    .asciz "swprintf"
ln306:
    .asciz "sysconf"
ln307:
    .asciz "system"
ln308:
    .asciz "thread_attendi"
ln309:
    .asciz "thread_crea"
ln310:
    .asciz "thread_devo_fermarmi"
ln311:
    .asciz "thread_esci"
ln312:
    .asciz "thread_ferma"
ln313:
    .asciz "time"
ln314:
    .asciz "time_now"
ln315:
    .asciz "time_set"
ln316:
    .asciz "times"
ln317:
    .asciz "timespec_get"
ln318:
    .asciz "tmpfile"
ln319:
    .asciz "tmpnam"
ln320:
    .asciz "toascii"
ln321:
    .asciz "tolower"
ln322:
    .asciz "toupper"
ln323:
    .asciz "towlower"
ln324:
    .asciz "towupper"
ln325:
    .asciz "truncate"
ln326:
    .asciz "tty_clear"
ln327:
    .asciz "tty_getsize"
ln328:
    .asciz "tty_raw"
ln329:
    .asciz "umask"
ln330:
    .asciz "umount"
ln331:
    .asciz "ungetc"
ln332:
    .asciz "unlink"
ln333:
    .asciz "unsetenv"
ln334:
    .asciz "uptime_ms"
ln335:
    .asciz "usleep"
ln336:
    .asciz "utime"
ln337:
    .asciz "verboseboot"
ln338:
    .asciz "vfprintf"
ln339:
    .asciz "vfscanf"
ln340:
    .asciz "video_info"
ln341:
    .asciz "vprintf"
ln342:
    .asciz "vscanf"
ln343:
    .asciz "vsnprintf"
ln344:
    .asciz "vsprintf"
ln345:
    .asciz "vsscanf"
ln346:
    .asciz "vswprintf"
ln347:
    .asciz "wait"
ln348:
    .asciz "waitpid"
ln349:
    .asciz "wcscat"
ln350:
    .asciz "wcschr"
ln351:
    .asciz "wcscmp"
ln352:
    .asciz "wcscpy"
ln353:
    .asciz "wcscspn"
ln354:
    .asciz "wcslen"
ln355:
    .asciz "wcsncat"
ln356:
    .asciz "wcsncmp"
ln357:
    .asciz "wcsncpy"
ln358:
    .asciz "wcspbrk"
ln359:
    .asciz "wcsrchr"
ln360:
    .asciz "wcsspn"
ln361:
    .asciz "wcsstr"
ln362:
    .asciz "wcstod"
ln363:
    .asciz "wcstol"
ln364:
    .asciz "wcstoll"
ln365:
    .asciz "wcstombs"
ln366:
    .asciz "wcstoul"
ln367:
    .asciz "wcstoull"
ln368:
    .asciz "wctomb"
ln369:
    .asciz "wmemchr"
ln370:
    .asciz "wmemcmp"
ln371:
    .asciz "wmemcpy"
ln372:
    .asciz "wmemmove"
ln373:
    .asciz "wmemset"
ln374:
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
    .long 375
    .long libc_nomi
    .long libc_indirizzi

    .section .note.GNU-stack,"",@progbits
