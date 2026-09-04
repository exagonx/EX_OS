/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * La tabella che libc.so mette a disposizione: 362 funzioni, ognuna col suo
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
    .asciz "blkinfo"
ln23:
    .asciz "blkread"
ln24:
    .asciz "blkwrite"
ln25:
    .asciz "bootinstall"
ln26:
    .asciz "bootverify"
ln27:
    .asciz "bsearch"
ln28:
    .asciz "calloc"
ln29:
    .asciz "chdir"
ln30:
    .asciz "chmod"
ln31:
    .asciz "chown"
ln32:
    .asciz "clearerr"
ln33:
    .asciz "clock"
ln34:
    .asciz "close"
ln35:
    .asciz "closedir"
ln36:
    .asciz "condizione_aspetta"
ln37:
    .asciz "condizione_aspetta_ms"
ln38:
    .asciz "condizione_segnala"
ln39:
    .asciz "condizione_segnala_tutti"
ln40:
    .asciz "console_grafica"
ln41:
    .asciz "console_info"
ln42:
    .asciz "console_setfg"
ln43:
    .asciz "console_switch"
ln44:
    .asciz "console_write"
ln45:
    .asciz "ctime"
ln46:
    .asciz "difftime"
ln47:
    .asciz "diskinfo"
ln48:
    .asciz "div"
ln49:
    .asciz "diventa_root"
ln50:
    .asciz "dma_alloc"
ln51:
    .asciz "dup"
ln52:
    .asciz "dup2"
ln53:
    .asciz "execv"
ln54:
    .asciz "execvp"
ln55:
    .asciz "exit"
ln56:
    .asciz "fabs"
ln57:
    .asciz "fb_map"
ln58:
    .asciz "fchmod"
ln59:
    .asciz "fclose"
ln60:
    .asciz "fcntl"
ln61:
    .asciz "fdopen"
ln62:
    .asciz "fdprova"
ln63:
    .asciz "feof"
ln64:
    .asciz "ferror"
ln65:
    .asciz "fflush"
ln66:
    .asciz "fgetc"
ln67:
    .asciz "fgetpos"
ln68:
    .asciz "fgets"
ln69:
    .asciz "fileno"
ln70:
    .asciz "fopen"
ln71:
    .asciz "fpathconf"
ln72:
    .asciz "fprintf"
ln73:
    .asciz "fputc"
ln74:
    .asciz "fputs"
ln75:
    .asciz "fread"
ln76:
    .asciz "free"
ln77:
    .asciz "freopen"
ln78:
    .asciz "frexp"
ln79:
    .asciz "fscanf"
ln80:
    .asciz "fseek"
ln81:
    .asciz "fsetpos"
ln82:
    .asciz "fsize"
ln83:
    .asciz "fstat"
ln84:
    .asciz "ftell"
ln85:
    .asciz "fwrite"
ln86:
    .asciz "getc"
ln87:
    .asciz "getchar"
ln88:
    .asciz "getconf"
ln89:
    .asciz "getcwd"
ln90:
    .asciz "getegid"
ln91:
    .asciz "getentropy"
ln92:
    .asciz "getenv"
ln93:
    .asciz "geteuid"
ln94:
    .asciz "getgid"
ln95:
    .asciz "getpagesize"
ln96:
    .asciz "getpid"
ln97:
    .asciz "getrandom"
ln98:
    .asciz "getrusage"
ln99:
    .asciz "gets"
ln100:
    .asciz "gettimeofday"
ln101:
    .asciz "getuid"
ln102:
    .asciz "gmtime"
ln103:
    .asciz "gmtime_r"
ln104:
    .asciz "imaxabs"
ln105:
    .asciz "imaxdiv"
ln106:
    .asciz "interrompi"
ln107:
    .asciz "ioctl"
ln108:
    .asciz "ioport_bind"
ln109:
    .asciz "ioport_in"
ln110:
    .asciz "ioport_in16"
ln111:
    .asciz "ioport_in32"
ln112:
    .asciz "ioport_out"
ln113:
    .asciz "ioport_out16"
ln114:
    .asciz "ioport_out32"
ln115:
    .asciz "ipc_attendi"
ln116:
    .asciz "ipc_lookup"
ln117:
    .asciz "ipc_pronto"
ln118:
    .asciz "ipc_recv"
ln119:
    .asciz "ipc_recv_timeout"
ln120:
    .asciz "ipc_register"
ln121:
    .asciz "ipc_rimetti"
ln122:
    .asciz "ipc_scegli"
ln123:
    .asciz "ipc_send"
ln124:
    .asciz "irq_bind"
ln125:
    .asciz "irq_done"
ln126:
    .asciz "irq_unbind"
ln127:
    .asciz "isalnum"
ln128:
    .asciz "isalpha"
ln129:
    .asciz "isascii"
ln130:
    .asciz "isatty"
ln131:
    .asciz "isblank"
ln132:
    .asciz "iscntrl"
ln133:
    .asciz "isdigit"
ln134:
    .asciz "isgraph"
ln135:
    .asciz "islower"
ln136:
    .asciz "isprint"
ln137:
    .asciz "ispunct"
ln138:
    .asciz "isspace"
ln139:
    .asciz "isupper"
ln140:
    .asciz "iswalnum"
ln141:
    .asciz "iswalpha"
ln142:
    .asciz "iswblank"
ln143:
    .asciz "iswcntrl"
ln144:
    .asciz "iswdigit"
ln145:
    .asciz "iswgraph"
ln146:
    .asciz "iswlower"
ln147:
    .asciz "iswprint"
ln148:
    .asciz "iswpunct"
ln149:
    .asciz "iswspace"
ln150:
    .asciz "iswupper"
ln151:
    .asciz "iswxdigit"
ln152:
    .asciz "isxdigit"
ln153:
    .asciz "kbprova"
ln154:
    .asciz "kbstato"
ln155:
    .asciz "labs"
ln156:
    .asciz "ldexp"
ln157:
    .asciz "ldiv"
ln158:
    .asciz "listdir"
ln159:
    .asciz "listdir_from"
ln160:
    .asciz "llabs"
ln161:
    .asciz "lldiv"
ln162:
    .asciz "localeconv"
ln163:
    .asciz "localtime"
ln164:
    .asciz "localtime_r"
ln165:
    .asciz "log_seriale"
ln166:
    .asciz "longjmp"
ln167:
    .asciz "lseek"
ln168:
    .asciz "lstat"
ln169:
    .asciz "malloc"
ln170:
    .asciz "malloc_usable_size"
ln171:
    .asciz "mblen"
ln172:
    .asciz "mbrtowc"
ln173:
    .asciz "mbstowcs"
ln174:
    .asciz "mbtowc"
ln175:
    .asciz "memalign"
ln176:
    .asciz "memchr"
ln177:
    .asciz "memcmp"
ln178:
    .asciz "memcpy"
ln179:
    .asciz "meminfo"
ln180:
    .asciz "memmove"
ln181:
    .asciz "memset"
ln182:
    .asciz "mkdir"
ln183:
    .asciz "mkstemp"
ln184:
    .asciz "mktemp"
ln185:
    .asciz "mktime"
ln186:
    .asciz "mmap"
ln187:
    .asciz "mmio_map"
ln188:
    .asciz "modo_testo"
ln189:
    .asciz "mount"
ln190:
    .asciz "mountinfo"
ln191:
    .asciz "munmap"
ln192:
    .asciz "mutex_lascia"
ln193:
    .asciz "mutex_prendi"
ln194:
    .asciz "mutex_prova"
ln195:
    .asciz "nanosleep"
ln196:
    .asciz "nome_utente"
ln197:
    .asciz "open"
ln198:
    .asciz "opendir"
ln199:
    .asciz "osversion"
ln200:
    .asciz "partwrite"
ln201:
    .asciz "pathconf"
ln202:
    .asciz "pclose"
ln203:
    .asciz "perror"
ln204:
    .asciz "pipe"
ln205:
    .asciz "poll"
ln206:
    .asciz "popen"
ln207:
    .asciz "posix_memalign"
ln208:
    .asciz "printf"
ln209:
    .asciz "procinfo"
ln210:
    .asciz "pty_apri"
ln211:
    .asciz "pty_ctl"
ln212:
    .asciz "putc"
ln213:
    .asciz "putchar"
ln214:
    .asciz "putenv"
ln215:
    .asciz "puts"
ln216:
    .asciz "qsort"
ln217:
    .asciz "quick_exit"
ln218:
    .asciz "raise"
ln219:
    .asciz "rand"
ln220:
    .asciz "read"
ln221:
    .asciz "readdir"
ln222:
    .asciz "realloc"
ln223:
    .asciz "realpath"
ln224:
    .asciz "reboot"
ln225:
    .asciz "remove"
ln226:
    .asciz "rename"
ln227:
    .asciz "rewind"
ln228:
    .asciz "rewinddir"
ln229:
    .asciz "rmdir"
ln230:
    .asciz "sbrk"
ln231:
    .asciz "scanf"
ln232:
    .asciz "sched_yield"
ln233:
    .asciz "select"
ln234:
    .asciz "semaforo_lascia"
ln235:
    .asciz "semaforo_prendi"
ln236:
    .asciz "semaforo_prendi_ms"
ln237:
    .asciz "semaforo_prova"
ln238:
    .asciz "setbuf"
ln239:
    .asciz "setenv"
ln240:
    .asciz "setjmp"
ln241:
    .asciz "setlocale"
ln242:
    .asciz "setuid"
ln243:
    .asciz "setvbuf"
ln244:
    .asciz "sha256"
ln245:
    .asciz "sha256_esa"
ln246:
    .asciz "shm_apri"
ln247:
    .asciz "shm_chiudi"
ln248:
    .asciz "signal"
ln249:
    .asciz "sleep"
ln250:
    .asciz "snprintf"
ln251:
    .asciz "spawn"
ln252:
    .asciz "spawn_ex"
ln253:
    .asciz "spawn_su_console"
ln254:
    .asciz "spawn_utente"
ln255:
    .asciz "sprintf"
ln256:
    .asciz "sqrt"
ln257:
    .asciz "srand"
ln258:
    .asciz "sscanf"
ln259:
    .asciz "stat"
ln260:
    .asciz "statperm"
ln261:
    .asciz "statraw"
ln262:
    .asciz "strcasecmp"
ln263:
    .asciz "strcat"
ln264:
    .asciz "strchr"
ln265:
    .asciz "strcmp"
ln266:
    .asciz "strcoll"
ln267:
    .asciz "strcpy"
ln268:
    .asciz "strcspn"
ln269:
    .asciz "strdup"
ln270:
    .asciz "strerror"
ln271:
    .asciz "strftime"
ln272:
    .asciz "strlen"
ln273:
    .asciz "strncasecmp"
ln274:
    .asciz "strncat"
ln275:
    .asciz "strncmp"
ln276:
    .asciz "strncpy"
ln277:
    .asciz "strpbrk"
ln278:
    .asciz "strrchr"
ln279:
    .asciz "strsignal"
ln280:
    .asciz "strspn"
ln281:
    .asciz "strstr"
ln282:
    .asciz "strtod"
ln283:
    .asciz "strtof"
ln284:
    .asciz "strtoimax"
ln285:
    .asciz "strtok"
ln286:
    .asciz "strtol"
ln287:
    .asciz "strtold"
ln288:
    .asciz "strtoll"
ln289:
    .asciz "strtoul"
ln290:
    .asciz "strtoull"
ln291:
    .asciz "strtoumax"
ln292:
    .asciz "strxfrm"
ln293:
    .asciz "swprintf"
ln294:
    .asciz "sysconf"
ln295:
    .asciz "system"
ln296:
    .asciz "thread_attendi"
ln297:
    .asciz "thread_crea"
ln298:
    .asciz "thread_devo_fermarmi"
ln299:
    .asciz "thread_esci"
ln300:
    .asciz "thread_ferma"
ln301:
    .asciz "time"
ln302:
    .asciz "time_now"
ln303:
    .asciz "times"
ln304:
    .asciz "timespec_get"
ln305:
    .asciz "tmpfile"
ln306:
    .asciz "tmpnam"
ln307:
    .asciz "toascii"
ln308:
    .asciz "tolower"
ln309:
    .asciz "toupper"
ln310:
    .asciz "towlower"
ln311:
    .asciz "towupper"
ln312:
    .asciz "truncate"
ln313:
    .asciz "tty_clear"
ln314:
    .asciz "tty_getsize"
ln315:
    .asciz "tty_raw"
ln316:
    .asciz "umask"
ln317:
    .asciz "umount"
ln318:
    .asciz "ungetc"
ln319:
    .asciz "unlink"
ln320:
    .asciz "unsetenv"
ln321:
    .asciz "uptime_ms"
ln322:
    .asciz "usleep"
ln323:
    .asciz "utime"
ln324:
    .asciz "verboseboot"
ln325:
    .asciz "vfprintf"
ln326:
    .asciz "vfscanf"
ln327:
    .asciz "video_info"
ln328:
    .asciz "vprintf"
ln329:
    .asciz "vscanf"
ln330:
    .asciz "vsnprintf"
ln331:
    .asciz "vsprintf"
ln332:
    .asciz "vsscanf"
ln333:
    .asciz "vswprintf"
ln334:
    .asciz "wait"
ln335:
    .asciz "waitpid"
ln336:
    .asciz "wcscat"
ln337:
    .asciz "wcschr"
ln338:
    .asciz "wcscmp"
ln339:
    .asciz "wcscpy"
ln340:
    .asciz "wcscspn"
ln341:
    .asciz "wcslen"
ln342:
    .asciz "wcsncat"
ln343:
    .asciz "wcsncmp"
ln344:
    .asciz "wcsncpy"
ln345:
    .asciz "wcspbrk"
ln346:
    .asciz "wcsrchr"
ln347:
    .asciz "wcsspn"
ln348:
    .asciz "wcsstr"
ln349:
    .asciz "wcstod"
ln350:
    .asciz "wcstol"
ln351:
    .asciz "wcstoll"
ln352:
    .asciz "wcstombs"
ln353:
    .asciz "wcstoul"
ln354:
    .asciz "wcstoull"
ln355:
    .asciz "wctomb"
ln356:
    .asciz "wmemchr"
ln357:
    .asciz "wmemcmp"
ln358:
    .asciz "wmemcpy"
ln359:
    .asciz "wmemmove"
ln360:
    .asciz "wmemset"
ln361:
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
    .long 362
    .long libc_nomi
    .long libc_indirizzi

    .section .note.GNU-stack,"",@progbits
