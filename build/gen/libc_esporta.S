/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * La tabella che libc.so mette a disposizione: 344 funzioni, ognuna col suo
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
    .asciz "blkinfo"
ln21:
    .asciz "blkread"
ln22:
    .asciz "blkwrite"
ln23:
    .asciz "bootinstall"
ln24:
    .asciz "bootverify"
ln25:
    .asciz "bsearch"
ln26:
    .asciz "calloc"
ln27:
    .asciz "chdir"
ln28:
    .asciz "chmod"
ln29:
    .asciz "chown"
ln30:
    .asciz "clearerr"
ln31:
    .asciz "clock"
ln32:
    .asciz "close"
ln33:
    .asciz "closedir"
ln34:
    .asciz "console_grafica"
ln35:
    .asciz "console_info"
ln36:
    .asciz "console_setfg"
ln37:
    .asciz "console_switch"
ln38:
    .asciz "console_write"
ln39:
    .asciz "ctime"
ln40:
    .asciz "difftime"
ln41:
    .asciz "diskinfo"
ln42:
    .asciz "div"
ln43:
    .asciz "diventa_root"
ln44:
    .asciz "dma_alloc"
ln45:
    .asciz "dup"
ln46:
    .asciz "dup2"
ln47:
    .asciz "execv"
ln48:
    .asciz "execvp"
ln49:
    .asciz "exit"
ln50:
    .asciz "fabs"
ln51:
    .asciz "fb_map"
ln52:
    .asciz "fchmod"
ln53:
    .asciz "fclose"
ln54:
    .asciz "fcntl"
ln55:
    .asciz "fdopen"
ln56:
    .asciz "fdprova"
ln57:
    .asciz "feof"
ln58:
    .asciz "ferror"
ln59:
    .asciz "fflush"
ln60:
    .asciz "fgetc"
ln61:
    .asciz "fgetpos"
ln62:
    .asciz "fgets"
ln63:
    .asciz "fileno"
ln64:
    .asciz "fopen"
ln65:
    .asciz "fpathconf"
ln66:
    .asciz "fprintf"
ln67:
    .asciz "fputc"
ln68:
    .asciz "fputs"
ln69:
    .asciz "fread"
ln70:
    .asciz "free"
ln71:
    .asciz "freopen"
ln72:
    .asciz "frexp"
ln73:
    .asciz "fscanf"
ln74:
    .asciz "fseek"
ln75:
    .asciz "fsetpos"
ln76:
    .asciz "fsize"
ln77:
    .asciz "fstat"
ln78:
    .asciz "ftell"
ln79:
    .asciz "fwrite"
ln80:
    .asciz "getc"
ln81:
    .asciz "getchar"
ln82:
    .asciz "getconf"
ln83:
    .asciz "getcwd"
ln84:
    .asciz "getegid"
ln85:
    .asciz "getentropy"
ln86:
    .asciz "getenv"
ln87:
    .asciz "geteuid"
ln88:
    .asciz "getgid"
ln89:
    .asciz "getpagesize"
ln90:
    .asciz "getpid"
ln91:
    .asciz "getrandom"
ln92:
    .asciz "getrusage"
ln93:
    .asciz "gets"
ln94:
    .asciz "gettimeofday"
ln95:
    .asciz "getuid"
ln96:
    .asciz "gmtime"
ln97:
    .asciz "gmtime_r"
ln98:
    .asciz "imaxabs"
ln99:
    .asciz "imaxdiv"
ln100:
    .asciz "interrompi"
ln101:
    .asciz "ioctl"
ln102:
    .asciz "ioport_bind"
ln103:
    .asciz "ioport_in"
ln104:
    .asciz "ioport_in16"
ln105:
    .asciz "ioport_in32"
ln106:
    .asciz "ioport_out"
ln107:
    .asciz "ioport_out16"
ln108:
    .asciz "ioport_out32"
ln109:
    .asciz "ipc_attendi"
ln110:
    .asciz "ipc_lookup"
ln111:
    .asciz "ipc_pronto"
ln112:
    .asciz "ipc_recv"
ln113:
    .asciz "ipc_recv_timeout"
ln114:
    .asciz "ipc_register"
ln115:
    .asciz "ipc_rimetti"
ln116:
    .asciz "ipc_scegli"
ln117:
    .asciz "ipc_send"
ln118:
    .asciz "irq_bind"
ln119:
    .asciz "irq_done"
ln120:
    .asciz "irq_unbind"
ln121:
    .asciz "isalnum"
ln122:
    .asciz "isalpha"
ln123:
    .asciz "isascii"
ln124:
    .asciz "isatty"
ln125:
    .asciz "isblank"
ln126:
    .asciz "iscntrl"
ln127:
    .asciz "isdigit"
ln128:
    .asciz "isgraph"
ln129:
    .asciz "islower"
ln130:
    .asciz "isprint"
ln131:
    .asciz "ispunct"
ln132:
    .asciz "isspace"
ln133:
    .asciz "isupper"
ln134:
    .asciz "iswalnum"
ln135:
    .asciz "iswalpha"
ln136:
    .asciz "iswblank"
ln137:
    .asciz "iswcntrl"
ln138:
    .asciz "iswdigit"
ln139:
    .asciz "iswgraph"
ln140:
    .asciz "iswlower"
ln141:
    .asciz "iswprint"
ln142:
    .asciz "iswpunct"
ln143:
    .asciz "iswspace"
ln144:
    .asciz "iswupper"
ln145:
    .asciz "iswxdigit"
ln146:
    .asciz "isxdigit"
ln147:
    .asciz "kbprova"
ln148:
    .asciz "kbstato"
ln149:
    .asciz "labs"
ln150:
    .asciz "ldexp"
ln151:
    .asciz "ldiv"
ln152:
    .asciz "listdir"
ln153:
    .asciz "listdir_from"
ln154:
    .asciz "llabs"
ln155:
    .asciz "lldiv"
ln156:
    .asciz "localeconv"
ln157:
    .asciz "localtime"
ln158:
    .asciz "localtime_r"
ln159:
    .asciz "log_seriale"
ln160:
    .asciz "longjmp"
ln161:
    .asciz "lseek"
ln162:
    .asciz "lstat"
ln163:
    .asciz "malloc"
ln164:
    .asciz "malloc_usable_size"
ln165:
    .asciz "mblen"
ln166:
    .asciz "mbrtowc"
ln167:
    .asciz "mbstowcs"
ln168:
    .asciz "mbtowc"
ln169:
    .asciz "memalign"
ln170:
    .asciz "memchr"
ln171:
    .asciz "memcmp"
ln172:
    .asciz "memcpy"
ln173:
    .asciz "meminfo"
ln174:
    .asciz "memmove"
ln175:
    .asciz "memset"
ln176:
    .asciz "mkdir"
ln177:
    .asciz "mkstemp"
ln178:
    .asciz "mktemp"
ln179:
    .asciz "mktime"
ln180:
    .asciz "mmap"
ln181:
    .asciz "mmio_map"
ln182:
    .asciz "modo_testo"
ln183:
    .asciz "mount"
ln184:
    .asciz "mountinfo"
ln185:
    .asciz "munmap"
ln186:
    .asciz "nanosleep"
ln187:
    .asciz "nome_utente"
ln188:
    .asciz "open"
ln189:
    .asciz "opendir"
ln190:
    .asciz "osversion"
ln191:
    .asciz "partwrite"
ln192:
    .asciz "pathconf"
ln193:
    .asciz "pclose"
ln194:
    .asciz "perror"
ln195:
    .asciz "pipe"
ln196:
    .asciz "poll"
ln197:
    .asciz "popen"
ln198:
    .asciz "posix_memalign"
ln199:
    .asciz "printf"
ln200:
    .asciz "procinfo"
ln201:
    .asciz "pty_apri"
ln202:
    .asciz "pty_ctl"
ln203:
    .asciz "putc"
ln204:
    .asciz "putchar"
ln205:
    .asciz "putenv"
ln206:
    .asciz "puts"
ln207:
    .asciz "qsort"
ln208:
    .asciz "quick_exit"
ln209:
    .asciz "raise"
ln210:
    .asciz "rand"
ln211:
    .asciz "read"
ln212:
    .asciz "readdir"
ln213:
    .asciz "realloc"
ln214:
    .asciz "realpath"
ln215:
    .asciz "reboot"
ln216:
    .asciz "remove"
ln217:
    .asciz "rename"
ln218:
    .asciz "rewind"
ln219:
    .asciz "rewinddir"
ln220:
    .asciz "rmdir"
ln221:
    .asciz "sbrk"
ln222:
    .asciz "scanf"
ln223:
    .asciz "sched_yield"
ln224:
    .asciz "select"
ln225:
    .asciz "setbuf"
ln226:
    .asciz "setenv"
ln227:
    .asciz "setjmp"
ln228:
    .asciz "setlocale"
ln229:
    .asciz "setuid"
ln230:
    .asciz "setvbuf"
ln231:
    .asciz "sha256"
ln232:
    .asciz "sha256_esa"
ln233:
    .asciz "shm_apri"
ln234:
    .asciz "shm_chiudi"
ln235:
    .asciz "signal"
ln236:
    .asciz "sleep"
ln237:
    .asciz "snprintf"
ln238:
    .asciz "spawn"
ln239:
    .asciz "spawn_ex"
ln240:
    .asciz "spawn_su_console"
ln241:
    .asciz "spawn_utente"
ln242:
    .asciz "sprintf"
ln243:
    .asciz "sqrt"
ln244:
    .asciz "srand"
ln245:
    .asciz "sscanf"
ln246:
    .asciz "stat"
ln247:
    .asciz "statperm"
ln248:
    .asciz "statraw"
ln249:
    .asciz "strcasecmp"
ln250:
    .asciz "strcat"
ln251:
    .asciz "strchr"
ln252:
    .asciz "strcmp"
ln253:
    .asciz "strcoll"
ln254:
    .asciz "strcpy"
ln255:
    .asciz "strcspn"
ln256:
    .asciz "strdup"
ln257:
    .asciz "strerror"
ln258:
    .asciz "strftime"
ln259:
    .asciz "strlen"
ln260:
    .asciz "strncasecmp"
ln261:
    .asciz "strncat"
ln262:
    .asciz "strncmp"
ln263:
    .asciz "strncpy"
ln264:
    .asciz "strpbrk"
ln265:
    .asciz "strrchr"
ln266:
    .asciz "strsignal"
ln267:
    .asciz "strspn"
ln268:
    .asciz "strstr"
ln269:
    .asciz "strtod"
ln270:
    .asciz "strtof"
ln271:
    .asciz "strtoimax"
ln272:
    .asciz "strtok"
ln273:
    .asciz "strtol"
ln274:
    .asciz "strtold"
ln275:
    .asciz "strtoll"
ln276:
    .asciz "strtoul"
ln277:
    .asciz "strtoull"
ln278:
    .asciz "strtoumax"
ln279:
    .asciz "strxfrm"
ln280:
    .asciz "swprintf"
ln281:
    .asciz "sysconf"
ln282:
    .asciz "system"
ln283:
    .asciz "time"
ln284:
    .asciz "time_now"
ln285:
    .asciz "times"
ln286:
    .asciz "timespec_get"
ln287:
    .asciz "tmpfile"
ln288:
    .asciz "tmpnam"
ln289:
    .asciz "toascii"
ln290:
    .asciz "tolower"
ln291:
    .asciz "toupper"
ln292:
    .asciz "towlower"
ln293:
    .asciz "towupper"
ln294:
    .asciz "truncate"
ln295:
    .asciz "tty_clear"
ln296:
    .asciz "tty_getsize"
ln297:
    .asciz "tty_raw"
ln298:
    .asciz "umask"
ln299:
    .asciz "umount"
ln300:
    .asciz "ungetc"
ln301:
    .asciz "unlink"
ln302:
    .asciz "unsetenv"
ln303:
    .asciz "uptime_ms"
ln304:
    .asciz "usleep"
ln305:
    .asciz "utime"
ln306:
    .asciz "verboseboot"
ln307:
    .asciz "vfprintf"
ln308:
    .asciz "vfscanf"
ln309:
    .asciz "video_info"
ln310:
    .asciz "vprintf"
ln311:
    .asciz "vscanf"
ln312:
    .asciz "vsnprintf"
ln313:
    .asciz "vsprintf"
ln314:
    .asciz "vsscanf"
ln315:
    .asciz "vswprintf"
ln316:
    .asciz "wait"
ln317:
    .asciz "waitpid"
ln318:
    .asciz "wcscat"
ln319:
    .asciz "wcschr"
ln320:
    .asciz "wcscmp"
ln321:
    .asciz "wcscpy"
ln322:
    .asciz "wcscspn"
ln323:
    .asciz "wcslen"
ln324:
    .asciz "wcsncat"
ln325:
    .asciz "wcsncmp"
ln326:
    .asciz "wcsncpy"
ln327:
    .asciz "wcspbrk"
ln328:
    .asciz "wcsrchr"
ln329:
    .asciz "wcsspn"
ln330:
    .asciz "wcsstr"
ln331:
    .asciz "wcstod"
ln332:
    .asciz "wcstol"
ln333:
    .asciz "wcstoll"
ln334:
    .asciz "wcstombs"
ln335:
    .asciz "wcstoul"
ln336:
    .asciz "wcstoull"
ln337:
    .asciz "wctomb"
ln338:
    .asciz "wmemchr"
ln339:
    .asciz "wmemcmp"
ln340:
    .asciz "wmemcpy"
ln341:
    .asciz "wmemmove"
ln342:
    .asciz "wmemset"
ln343:
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
    .long 344
    .long libc_nomi
    .long libc_indirizzi

    .section .note.GNU-stack,"",@progbits
