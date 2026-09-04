/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * La tabella che libc.so mette a disposizione: 352 funzioni, ognuna col suo
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
    .asciz "console_grafica"
ln37:
    .asciz "console_info"
ln38:
    .asciz "console_setfg"
ln39:
    .asciz "console_switch"
ln40:
    .asciz "console_write"
ln41:
    .asciz "ctime"
ln42:
    .asciz "difftime"
ln43:
    .asciz "diskinfo"
ln44:
    .asciz "div"
ln45:
    .asciz "diventa_root"
ln46:
    .asciz "dma_alloc"
ln47:
    .asciz "dup"
ln48:
    .asciz "dup2"
ln49:
    .asciz "execv"
ln50:
    .asciz "execvp"
ln51:
    .asciz "exit"
ln52:
    .asciz "fabs"
ln53:
    .asciz "fb_map"
ln54:
    .asciz "fchmod"
ln55:
    .asciz "fclose"
ln56:
    .asciz "fcntl"
ln57:
    .asciz "fdopen"
ln58:
    .asciz "fdprova"
ln59:
    .asciz "feof"
ln60:
    .asciz "ferror"
ln61:
    .asciz "fflush"
ln62:
    .asciz "fgetc"
ln63:
    .asciz "fgetpos"
ln64:
    .asciz "fgets"
ln65:
    .asciz "fileno"
ln66:
    .asciz "fopen"
ln67:
    .asciz "fpathconf"
ln68:
    .asciz "fprintf"
ln69:
    .asciz "fputc"
ln70:
    .asciz "fputs"
ln71:
    .asciz "fread"
ln72:
    .asciz "free"
ln73:
    .asciz "freopen"
ln74:
    .asciz "frexp"
ln75:
    .asciz "fscanf"
ln76:
    .asciz "fseek"
ln77:
    .asciz "fsetpos"
ln78:
    .asciz "fsize"
ln79:
    .asciz "fstat"
ln80:
    .asciz "ftell"
ln81:
    .asciz "fwrite"
ln82:
    .asciz "getc"
ln83:
    .asciz "getchar"
ln84:
    .asciz "getconf"
ln85:
    .asciz "getcwd"
ln86:
    .asciz "getegid"
ln87:
    .asciz "getentropy"
ln88:
    .asciz "getenv"
ln89:
    .asciz "geteuid"
ln90:
    .asciz "getgid"
ln91:
    .asciz "getpagesize"
ln92:
    .asciz "getpid"
ln93:
    .asciz "getrandom"
ln94:
    .asciz "getrusage"
ln95:
    .asciz "gets"
ln96:
    .asciz "gettimeofday"
ln97:
    .asciz "getuid"
ln98:
    .asciz "gmtime"
ln99:
    .asciz "gmtime_r"
ln100:
    .asciz "imaxabs"
ln101:
    .asciz "imaxdiv"
ln102:
    .asciz "interrompi"
ln103:
    .asciz "ioctl"
ln104:
    .asciz "ioport_bind"
ln105:
    .asciz "ioport_in"
ln106:
    .asciz "ioport_in16"
ln107:
    .asciz "ioport_in32"
ln108:
    .asciz "ioport_out"
ln109:
    .asciz "ioport_out16"
ln110:
    .asciz "ioport_out32"
ln111:
    .asciz "ipc_attendi"
ln112:
    .asciz "ipc_lookup"
ln113:
    .asciz "ipc_pronto"
ln114:
    .asciz "ipc_recv"
ln115:
    .asciz "ipc_recv_timeout"
ln116:
    .asciz "ipc_register"
ln117:
    .asciz "ipc_rimetti"
ln118:
    .asciz "ipc_scegli"
ln119:
    .asciz "ipc_send"
ln120:
    .asciz "irq_bind"
ln121:
    .asciz "irq_done"
ln122:
    .asciz "irq_unbind"
ln123:
    .asciz "isalnum"
ln124:
    .asciz "isalpha"
ln125:
    .asciz "isascii"
ln126:
    .asciz "isatty"
ln127:
    .asciz "isblank"
ln128:
    .asciz "iscntrl"
ln129:
    .asciz "isdigit"
ln130:
    .asciz "isgraph"
ln131:
    .asciz "islower"
ln132:
    .asciz "isprint"
ln133:
    .asciz "ispunct"
ln134:
    .asciz "isspace"
ln135:
    .asciz "isupper"
ln136:
    .asciz "iswalnum"
ln137:
    .asciz "iswalpha"
ln138:
    .asciz "iswblank"
ln139:
    .asciz "iswcntrl"
ln140:
    .asciz "iswdigit"
ln141:
    .asciz "iswgraph"
ln142:
    .asciz "iswlower"
ln143:
    .asciz "iswprint"
ln144:
    .asciz "iswpunct"
ln145:
    .asciz "iswspace"
ln146:
    .asciz "iswupper"
ln147:
    .asciz "iswxdigit"
ln148:
    .asciz "isxdigit"
ln149:
    .asciz "kbprova"
ln150:
    .asciz "kbstato"
ln151:
    .asciz "labs"
ln152:
    .asciz "ldexp"
ln153:
    .asciz "ldiv"
ln154:
    .asciz "listdir"
ln155:
    .asciz "listdir_from"
ln156:
    .asciz "llabs"
ln157:
    .asciz "lldiv"
ln158:
    .asciz "localeconv"
ln159:
    .asciz "localtime"
ln160:
    .asciz "localtime_r"
ln161:
    .asciz "log_seriale"
ln162:
    .asciz "longjmp"
ln163:
    .asciz "lseek"
ln164:
    .asciz "lstat"
ln165:
    .asciz "malloc"
ln166:
    .asciz "malloc_usable_size"
ln167:
    .asciz "mblen"
ln168:
    .asciz "mbrtowc"
ln169:
    .asciz "mbstowcs"
ln170:
    .asciz "mbtowc"
ln171:
    .asciz "memalign"
ln172:
    .asciz "memchr"
ln173:
    .asciz "memcmp"
ln174:
    .asciz "memcpy"
ln175:
    .asciz "meminfo"
ln176:
    .asciz "memmove"
ln177:
    .asciz "memset"
ln178:
    .asciz "mkdir"
ln179:
    .asciz "mkstemp"
ln180:
    .asciz "mktemp"
ln181:
    .asciz "mktime"
ln182:
    .asciz "mmap"
ln183:
    .asciz "mmio_map"
ln184:
    .asciz "modo_testo"
ln185:
    .asciz "mount"
ln186:
    .asciz "mountinfo"
ln187:
    .asciz "munmap"
ln188:
    .asciz "mutex_lascia"
ln189:
    .asciz "mutex_prendi"
ln190:
    .asciz "mutex_prova"
ln191:
    .asciz "nanosleep"
ln192:
    .asciz "nome_utente"
ln193:
    .asciz "open"
ln194:
    .asciz "opendir"
ln195:
    .asciz "osversion"
ln196:
    .asciz "partwrite"
ln197:
    .asciz "pathconf"
ln198:
    .asciz "pclose"
ln199:
    .asciz "perror"
ln200:
    .asciz "pipe"
ln201:
    .asciz "poll"
ln202:
    .asciz "popen"
ln203:
    .asciz "posix_memalign"
ln204:
    .asciz "printf"
ln205:
    .asciz "procinfo"
ln206:
    .asciz "pty_apri"
ln207:
    .asciz "pty_ctl"
ln208:
    .asciz "putc"
ln209:
    .asciz "putchar"
ln210:
    .asciz "putenv"
ln211:
    .asciz "puts"
ln212:
    .asciz "qsort"
ln213:
    .asciz "quick_exit"
ln214:
    .asciz "raise"
ln215:
    .asciz "rand"
ln216:
    .asciz "read"
ln217:
    .asciz "readdir"
ln218:
    .asciz "realloc"
ln219:
    .asciz "realpath"
ln220:
    .asciz "reboot"
ln221:
    .asciz "remove"
ln222:
    .asciz "rename"
ln223:
    .asciz "rewind"
ln224:
    .asciz "rewinddir"
ln225:
    .asciz "rmdir"
ln226:
    .asciz "sbrk"
ln227:
    .asciz "scanf"
ln228:
    .asciz "sched_yield"
ln229:
    .asciz "select"
ln230:
    .asciz "setbuf"
ln231:
    .asciz "setenv"
ln232:
    .asciz "setjmp"
ln233:
    .asciz "setlocale"
ln234:
    .asciz "setuid"
ln235:
    .asciz "setvbuf"
ln236:
    .asciz "sha256"
ln237:
    .asciz "sha256_esa"
ln238:
    .asciz "shm_apri"
ln239:
    .asciz "shm_chiudi"
ln240:
    .asciz "signal"
ln241:
    .asciz "sleep"
ln242:
    .asciz "snprintf"
ln243:
    .asciz "spawn"
ln244:
    .asciz "spawn_ex"
ln245:
    .asciz "spawn_su_console"
ln246:
    .asciz "spawn_utente"
ln247:
    .asciz "sprintf"
ln248:
    .asciz "sqrt"
ln249:
    .asciz "srand"
ln250:
    .asciz "sscanf"
ln251:
    .asciz "stat"
ln252:
    .asciz "statperm"
ln253:
    .asciz "statraw"
ln254:
    .asciz "strcasecmp"
ln255:
    .asciz "strcat"
ln256:
    .asciz "strchr"
ln257:
    .asciz "strcmp"
ln258:
    .asciz "strcoll"
ln259:
    .asciz "strcpy"
ln260:
    .asciz "strcspn"
ln261:
    .asciz "strdup"
ln262:
    .asciz "strerror"
ln263:
    .asciz "strftime"
ln264:
    .asciz "strlen"
ln265:
    .asciz "strncasecmp"
ln266:
    .asciz "strncat"
ln267:
    .asciz "strncmp"
ln268:
    .asciz "strncpy"
ln269:
    .asciz "strpbrk"
ln270:
    .asciz "strrchr"
ln271:
    .asciz "strsignal"
ln272:
    .asciz "strspn"
ln273:
    .asciz "strstr"
ln274:
    .asciz "strtod"
ln275:
    .asciz "strtof"
ln276:
    .asciz "strtoimax"
ln277:
    .asciz "strtok"
ln278:
    .asciz "strtol"
ln279:
    .asciz "strtold"
ln280:
    .asciz "strtoll"
ln281:
    .asciz "strtoul"
ln282:
    .asciz "strtoull"
ln283:
    .asciz "strtoumax"
ln284:
    .asciz "strxfrm"
ln285:
    .asciz "swprintf"
ln286:
    .asciz "sysconf"
ln287:
    .asciz "system"
ln288:
    .asciz "thread_attendi"
ln289:
    .asciz "thread_crea"
ln290:
    .asciz "thread_esci"
ln291:
    .asciz "time"
ln292:
    .asciz "time_now"
ln293:
    .asciz "times"
ln294:
    .asciz "timespec_get"
ln295:
    .asciz "tmpfile"
ln296:
    .asciz "tmpnam"
ln297:
    .asciz "toascii"
ln298:
    .asciz "tolower"
ln299:
    .asciz "toupper"
ln300:
    .asciz "towlower"
ln301:
    .asciz "towupper"
ln302:
    .asciz "truncate"
ln303:
    .asciz "tty_clear"
ln304:
    .asciz "tty_getsize"
ln305:
    .asciz "tty_raw"
ln306:
    .asciz "umask"
ln307:
    .asciz "umount"
ln308:
    .asciz "ungetc"
ln309:
    .asciz "unlink"
ln310:
    .asciz "unsetenv"
ln311:
    .asciz "uptime_ms"
ln312:
    .asciz "usleep"
ln313:
    .asciz "utime"
ln314:
    .asciz "verboseboot"
ln315:
    .asciz "vfprintf"
ln316:
    .asciz "vfscanf"
ln317:
    .asciz "video_info"
ln318:
    .asciz "vprintf"
ln319:
    .asciz "vscanf"
ln320:
    .asciz "vsnprintf"
ln321:
    .asciz "vsprintf"
ln322:
    .asciz "vsscanf"
ln323:
    .asciz "vswprintf"
ln324:
    .asciz "wait"
ln325:
    .asciz "waitpid"
ln326:
    .asciz "wcscat"
ln327:
    .asciz "wcschr"
ln328:
    .asciz "wcscmp"
ln329:
    .asciz "wcscpy"
ln330:
    .asciz "wcscspn"
ln331:
    .asciz "wcslen"
ln332:
    .asciz "wcsncat"
ln333:
    .asciz "wcsncmp"
ln334:
    .asciz "wcsncpy"
ln335:
    .asciz "wcspbrk"
ln336:
    .asciz "wcsrchr"
ln337:
    .asciz "wcsspn"
ln338:
    .asciz "wcsstr"
ln339:
    .asciz "wcstod"
ln340:
    .asciz "wcstol"
ln341:
    .asciz "wcstoll"
ln342:
    .asciz "wcstombs"
ln343:
    .asciz "wcstoul"
ln344:
    .asciz "wcstoull"
ln345:
    .asciz "wctomb"
ln346:
    .asciz "wmemchr"
ln347:
    .asciz "wmemcmp"
ln348:
    .asciz "wmemcpy"
ln349:
    .asciz "wmemmove"
ln350:
    .asciz "wmemset"
ln351:
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
    .long thread_esci
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
    .long 352
    .long libc_nomi
    .long libc_indirizzi

    .section .note.GNU-stack,"",@progbits
