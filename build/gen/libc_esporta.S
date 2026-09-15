/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * La tabella che libc.so mette a disposizione: 371 funzioni, ognuna col suo
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
    .asciz "console_write"
ln50:
    .asciz "ctime"
ln51:
    .asciz "difftime"
ln52:
    .asciz "diskinfo"
ln53:
    .asciz "div"
ln54:
    .asciz "diventa_root"
ln55:
    .asciz "dma_alloc"
ln56:
    .asciz "dup"
ln57:
    .asciz "dup2"
ln58:
    .asciz "execv"
ln59:
    .asciz "execvp"
ln60:
    .asciz "exit"
ln61:
    .asciz "fabs"
ln62:
    .asciz "fb_map"
ln63:
    .asciz "fchmod"
ln64:
    .asciz "fclose"
ln65:
    .asciz "fcntl"
ln66:
    .asciz "fdopen"
ln67:
    .asciz "fdprova"
ln68:
    .asciz "feof"
ln69:
    .asciz "ferror"
ln70:
    .asciz "fflush"
ln71:
    .asciz "fgetc"
ln72:
    .asciz "fgetpos"
ln73:
    .asciz "fgets"
ln74:
    .asciz "fileno"
ln75:
    .asciz "fopen"
ln76:
    .asciz "fpathconf"
ln77:
    .asciz "fprintf"
ln78:
    .asciz "fputc"
ln79:
    .asciz "fputs"
ln80:
    .asciz "fread"
ln81:
    .asciz "free"
ln82:
    .asciz "freopen"
ln83:
    .asciz "frexp"
ln84:
    .asciz "fscanf"
ln85:
    .asciz "fseek"
ln86:
    .asciz "fsetpos"
ln87:
    .asciz "fsize"
ln88:
    .asciz "fstat"
ln89:
    .asciz "ftell"
ln90:
    .asciz "fwrite"
ln91:
    .asciz "getc"
ln92:
    .asciz "getchar"
ln93:
    .asciz "getconf"
ln94:
    .asciz "getcwd"
ln95:
    .asciz "getegid"
ln96:
    .asciz "getentropy"
ln97:
    .asciz "getenv"
ln98:
    .asciz "geteuid"
ln99:
    .asciz "getgid"
ln100:
    .asciz "getpagesize"
ln101:
    .asciz "getpid"
ln102:
    .asciz "getrandom"
ln103:
    .asciz "getrusage"
ln104:
    .asciz "gets"
ln105:
    .asciz "gettimeofday"
ln106:
    .asciz "getuid"
ln107:
    .asciz "gmtime"
ln108:
    .asciz "gmtime_r"
ln109:
    .asciz "imaxabs"
ln110:
    .asciz "imaxdiv"
ln111:
    .asciz "interrompi"
ln112:
    .asciz "ioctl"
ln113:
    .asciz "ioport_bind"
ln114:
    .asciz "ioport_in"
ln115:
    .asciz "ioport_in16"
ln116:
    .asciz "ioport_in32"
ln117:
    .asciz "ioport_out"
ln118:
    .asciz "ioport_out16"
ln119:
    .asciz "ioport_out32"
ln120:
    .asciz "ipc_attendi"
ln121:
    .asciz "ipc_lookup"
ln122:
    .asciz "ipc_pronto"
ln123:
    .asciz "ipc_recv"
ln124:
    .asciz "ipc_recv_timeout"
ln125:
    .asciz "ipc_register"
ln126:
    .asciz "ipc_rimetti"
ln127:
    .asciz "ipc_scegli"
ln128:
    .asciz "ipc_send"
ln129:
    .asciz "irq_bind"
ln130:
    .asciz "irq_done"
ln131:
    .asciz "irq_unbind"
ln132:
    .asciz "isalnum"
ln133:
    .asciz "isalpha"
ln134:
    .asciz "isascii"
ln135:
    .asciz "isatty"
ln136:
    .asciz "isblank"
ln137:
    .asciz "iscntrl"
ln138:
    .asciz "isdigit"
ln139:
    .asciz "isgraph"
ln140:
    .asciz "islower"
ln141:
    .asciz "isprint"
ln142:
    .asciz "ispunct"
ln143:
    .asciz "isspace"
ln144:
    .asciz "isupper"
ln145:
    .asciz "iswalnum"
ln146:
    .asciz "iswalpha"
ln147:
    .asciz "iswblank"
ln148:
    .asciz "iswcntrl"
ln149:
    .asciz "iswdigit"
ln150:
    .asciz "iswgraph"
ln151:
    .asciz "iswlower"
ln152:
    .asciz "iswprint"
ln153:
    .asciz "iswpunct"
ln154:
    .asciz "iswspace"
ln155:
    .asciz "iswupper"
ln156:
    .asciz "iswxdigit"
ln157:
    .asciz "isxdigit"
ln158:
    .asciz "kbprova"
ln159:
    .asciz "kbstato"
ln160:
    .asciz "labs"
ln161:
    .asciz "ldexp"
ln162:
    .asciz "ldiv"
ln163:
    .asciz "listdir"
ln164:
    .asciz "listdir_from"
ln165:
    .asciz "llabs"
ln166:
    .asciz "lldiv"
ln167:
    .asciz "localeconv"
ln168:
    .asciz "localtime"
ln169:
    .asciz "localtime_r"
ln170:
    .asciz "log_seriale"
ln171:
    .asciz "longjmp"
ln172:
    .asciz "lseek"
ln173:
    .asciz "lstat"
ln174:
    .asciz "malloc"
ln175:
    .asciz "malloc_usable_size"
ln176:
    .asciz "mblen"
ln177:
    .asciz "mbrtowc"
ln178:
    .asciz "mbstowcs"
ln179:
    .asciz "mbtowc"
ln180:
    .asciz "memalign"
ln181:
    .asciz "memchr"
ln182:
    .asciz "memcmp"
ln183:
    .asciz "memcpy"
ln184:
    .asciz "meminfo"
ln185:
    .asciz "memmove"
ln186:
    .asciz "memset"
ln187:
    .asciz "mkdir"
ln188:
    .asciz "mkstemp"
ln189:
    .asciz "mktemp"
ln190:
    .asciz "mktime"
ln191:
    .asciz "mmap"
ln192:
    .asciz "mmio_map"
ln193:
    .asciz "modo_testo"
ln194:
    .asciz "mount"
ln195:
    .asciz "mountinfo"
ln196:
    .asciz "munmap"
ln197:
    .asciz "mutex_lascia"
ln198:
    .asciz "mutex_prendi"
ln199:
    .asciz "mutex_prova"
ln200:
    .asciz "nanosleep"
ln201:
    .asciz "nome_utente"
ln202:
    .asciz "open"
ln203:
    .asciz "opendir"
ln204:
    .asciz "osversion"
ln205:
    .asciz "partwrite"
ln206:
    .asciz "pathconf"
ln207:
    .asciz "pclose"
ln208:
    .asciz "perror"
ln209:
    .asciz "pipe"
ln210:
    .asciz "poll"
ln211:
    .asciz "popen"
ln212:
    .asciz "posix_memalign"
ln213:
    .asciz "printf"
ln214:
    .asciz "procinfo"
ln215:
    .asciz "pty_apri"
ln216:
    .asciz "pty_ctl"
ln217:
    .asciz "putc"
ln218:
    .asciz "putchar"
ln219:
    .asciz "putenv"
ln220:
    .asciz "puts"
ln221:
    .asciz "qsort"
ln222:
    .asciz "quick_exit"
ln223:
    .asciz "raise"
ln224:
    .asciz "rand"
ln225:
    .asciz "read"
ln226:
    .asciz "readdir"
ln227:
    .asciz "realloc"
ln228:
    .asciz "realpath"
ln229:
    .asciz "reboot"
ln230:
    .asciz "remove"
ln231:
    .asciz "rename"
ln232:
    .asciz "rewind"
ln233:
    .asciz "rewinddir"
ln234:
    .asciz "rmdir"
ln235:
    .asciz "sbrk"
ln236:
    .asciz "scanf"
ln237:
    .asciz "sched_yield"
ln238:
    .asciz "select"
ln239:
    .asciz "semaforo_lascia"
ln240:
    .asciz "semaforo_prendi"
ln241:
    .asciz "semaforo_prendi_ms"
ln242:
    .asciz "semaforo_prova"
ln243:
    .asciz "setbuf"
ln244:
    .asciz "setenv"
ln245:
    .asciz "setjmp"
ln246:
    .asciz "setlocale"
ln247:
    .asciz "setuid"
ln248:
    .asciz "setvbuf"
ln249:
    .asciz "sha256"
ln250:
    .asciz "sha256_avvia"
ln251:
    .asciz "sha256_dai"
ln252:
    .asciz "sha256_esa"
ln253:
    .asciz "sha256_fine"
ln254:
    .asciz "sha256_fine_esa"
ln255:
    .asciz "shm_apri"
ln256:
    .asciz "shm_chiudi"
ln257:
    .asciz "signal"
ln258:
    .asciz "sleep"
ln259:
    .asciz "snprintf"
ln260:
    .asciz "spawn"
ln261:
    .asciz "spawn_ex"
ln262:
    .asciz "spawn_su_console"
ln263:
    .asciz "spawn_utente"
ln264:
    .asciz "sprintf"
ln265:
    .asciz "sqrt"
ln266:
    .asciz "srand"
ln267:
    .asciz "sscanf"
ln268:
    .asciz "stat"
ln269:
    .asciz "statperm"
ln270:
    .asciz "statraw"
ln271:
    .asciz "strcasecmp"
ln272:
    .asciz "strcat"
ln273:
    .asciz "strchr"
ln274:
    .asciz "strcmp"
ln275:
    .asciz "strcoll"
ln276:
    .asciz "strcpy"
ln277:
    .asciz "strcspn"
ln278:
    .asciz "strdup"
ln279:
    .asciz "strerror"
ln280:
    .asciz "strftime"
ln281:
    .asciz "strlen"
ln282:
    .asciz "strncasecmp"
ln283:
    .asciz "strncat"
ln284:
    .asciz "strncmp"
ln285:
    .asciz "strncpy"
ln286:
    .asciz "strpbrk"
ln287:
    .asciz "strrchr"
ln288:
    .asciz "strsignal"
ln289:
    .asciz "strspn"
ln290:
    .asciz "strstr"
ln291:
    .asciz "strtod"
ln292:
    .asciz "strtof"
ln293:
    .asciz "strtoimax"
ln294:
    .asciz "strtok"
ln295:
    .asciz "strtol"
ln296:
    .asciz "strtold"
ln297:
    .asciz "strtoll"
ln298:
    .asciz "strtoul"
ln299:
    .asciz "strtoull"
ln300:
    .asciz "strtoumax"
ln301:
    .asciz "strxfrm"
ln302:
    .asciz "swprintf"
ln303:
    .asciz "sysconf"
ln304:
    .asciz "system"
ln305:
    .asciz "thread_attendi"
ln306:
    .asciz "thread_crea"
ln307:
    .asciz "thread_devo_fermarmi"
ln308:
    .asciz "thread_esci"
ln309:
    .asciz "thread_ferma"
ln310:
    .asciz "time"
ln311:
    .asciz "time_now"
ln312:
    .asciz "times"
ln313:
    .asciz "timespec_get"
ln314:
    .asciz "tmpfile"
ln315:
    .asciz "tmpnam"
ln316:
    .asciz "toascii"
ln317:
    .asciz "tolower"
ln318:
    .asciz "toupper"
ln319:
    .asciz "towlower"
ln320:
    .asciz "towupper"
ln321:
    .asciz "truncate"
ln322:
    .asciz "tty_clear"
ln323:
    .asciz "tty_getsize"
ln324:
    .asciz "tty_raw"
ln325:
    .asciz "umask"
ln326:
    .asciz "umount"
ln327:
    .asciz "ungetc"
ln328:
    .asciz "unlink"
ln329:
    .asciz "unsetenv"
ln330:
    .asciz "uptime_ms"
ln331:
    .asciz "usleep"
ln332:
    .asciz "utime"
ln333:
    .asciz "verboseboot"
ln334:
    .asciz "vfprintf"
ln335:
    .asciz "vfscanf"
ln336:
    .asciz "video_info"
ln337:
    .asciz "vprintf"
ln338:
    .asciz "vscanf"
ln339:
    .asciz "vsnprintf"
ln340:
    .asciz "vsprintf"
ln341:
    .asciz "vsscanf"
ln342:
    .asciz "vswprintf"
ln343:
    .asciz "wait"
ln344:
    .asciz "waitpid"
ln345:
    .asciz "wcscat"
ln346:
    .asciz "wcschr"
ln347:
    .asciz "wcscmp"
ln348:
    .asciz "wcscpy"
ln349:
    .asciz "wcscspn"
ln350:
    .asciz "wcslen"
ln351:
    .asciz "wcsncat"
ln352:
    .asciz "wcsncmp"
ln353:
    .asciz "wcsncpy"
ln354:
    .asciz "wcspbrk"
ln355:
    .asciz "wcsrchr"
ln356:
    .asciz "wcsspn"
ln357:
    .asciz "wcsstr"
ln358:
    .asciz "wcstod"
ln359:
    .asciz "wcstol"
ln360:
    .asciz "wcstoll"
ln361:
    .asciz "wcstombs"
ln362:
    .asciz "wcstoul"
ln363:
    .asciz "wcstoull"
ln364:
    .asciz "wctomb"
ln365:
    .asciz "wmemchr"
ln366:
    .asciz "wmemcmp"
ln367:
    .asciz "wmemcpy"
ln368:
    .asciz "wmemmove"
ln369:
    .asciz "wmemset"
ln370:
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
    .long 371
    .long libc_nomi
    .long libc_indirizzi

    .section .note.GNU-stack,"",@progbits
