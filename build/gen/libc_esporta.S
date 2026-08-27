/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * La tabella che libc.so mette a disposizione: 338 funzioni, ognuna col suo
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
    .asciz "feof"
ln57:
    .asciz "ferror"
ln58:
    .asciz "fflush"
ln59:
    .asciz "fgetc"
ln60:
    .asciz "fgetpos"
ln61:
    .asciz "fgets"
ln62:
    .asciz "fileno"
ln63:
    .asciz "fopen"
ln64:
    .asciz "fpathconf"
ln65:
    .asciz "fprintf"
ln66:
    .asciz "fputc"
ln67:
    .asciz "fputs"
ln68:
    .asciz "fread"
ln69:
    .asciz "free"
ln70:
    .asciz "freopen"
ln71:
    .asciz "frexp"
ln72:
    .asciz "fscanf"
ln73:
    .asciz "fseek"
ln74:
    .asciz "fsetpos"
ln75:
    .asciz "fsize"
ln76:
    .asciz "fstat"
ln77:
    .asciz "ftell"
ln78:
    .asciz "fwrite"
ln79:
    .asciz "getc"
ln80:
    .asciz "getchar"
ln81:
    .asciz "getconf"
ln82:
    .asciz "getcwd"
ln83:
    .asciz "getegid"
ln84:
    .asciz "getentropy"
ln85:
    .asciz "getenv"
ln86:
    .asciz "geteuid"
ln87:
    .asciz "getgid"
ln88:
    .asciz "getpagesize"
ln89:
    .asciz "getpid"
ln90:
    .asciz "getrandom"
ln91:
    .asciz "getrusage"
ln92:
    .asciz "gets"
ln93:
    .asciz "gettimeofday"
ln94:
    .asciz "getuid"
ln95:
    .asciz "gmtime"
ln96:
    .asciz "gmtime_r"
ln97:
    .asciz "imaxabs"
ln98:
    .asciz "imaxdiv"
ln99:
    .asciz "interrompi"
ln100:
    .asciz "ioctl"
ln101:
    .asciz "ioport_bind"
ln102:
    .asciz "ioport_in"
ln103:
    .asciz "ioport_in16"
ln104:
    .asciz "ioport_in32"
ln105:
    .asciz "ioport_out"
ln106:
    .asciz "ioport_out16"
ln107:
    .asciz "ioport_out32"
ln108:
    .asciz "ipc_attendi"
ln109:
    .asciz "ipc_lookup"
ln110:
    .asciz "ipc_recv"
ln111:
    .asciz "ipc_recv_timeout"
ln112:
    .asciz "ipc_register"
ln113:
    .asciz "ipc_rimetti"
ln114:
    .asciz "ipc_send"
ln115:
    .asciz "irq_bind"
ln116:
    .asciz "irq_done"
ln117:
    .asciz "isalnum"
ln118:
    .asciz "isalpha"
ln119:
    .asciz "isascii"
ln120:
    .asciz "isatty"
ln121:
    .asciz "isblank"
ln122:
    .asciz "iscntrl"
ln123:
    .asciz "isdigit"
ln124:
    .asciz "isgraph"
ln125:
    .asciz "islower"
ln126:
    .asciz "isprint"
ln127:
    .asciz "ispunct"
ln128:
    .asciz "isspace"
ln129:
    .asciz "isupper"
ln130:
    .asciz "iswalnum"
ln131:
    .asciz "iswalpha"
ln132:
    .asciz "iswblank"
ln133:
    .asciz "iswcntrl"
ln134:
    .asciz "iswdigit"
ln135:
    .asciz "iswgraph"
ln136:
    .asciz "iswlower"
ln137:
    .asciz "iswprint"
ln138:
    .asciz "iswpunct"
ln139:
    .asciz "iswspace"
ln140:
    .asciz "iswupper"
ln141:
    .asciz "iswxdigit"
ln142:
    .asciz "isxdigit"
ln143:
    .asciz "labs"
ln144:
    .asciz "ldexp"
ln145:
    .asciz "ldiv"
ln146:
    .asciz "listdir"
ln147:
    .asciz "listdir_from"
ln148:
    .asciz "llabs"
ln149:
    .asciz "lldiv"
ln150:
    .asciz "localeconv"
ln151:
    .asciz "localtime"
ln152:
    .asciz "localtime_r"
ln153:
    .asciz "log_seriale"
ln154:
    .asciz "longjmp"
ln155:
    .asciz "lseek"
ln156:
    .asciz "lstat"
ln157:
    .asciz "malloc"
ln158:
    .asciz "malloc_usable_size"
ln159:
    .asciz "mblen"
ln160:
    .asciz "mbrtowc"
ln161:
    .asciz "mbstowcs"
ln162:
    .asciz "mbtowc"
ln163:
    .asciz "memalign"
ln164:
    .asciz "memchr"
ln165:
    .asciz "memcmp"
ln166:
    .asciz "memcpy"
ln167:
    .asciz "meminfo"
ln168:
    .asciz "memmove"
ln169:
    .asciz "memset"
ln170:
    .asciz "mkdir"
ln171:
    .asciz "mkstemp"
ln172:
    .asciz "mktemp"
ln173:
    .asciz "mktime"
ln174:
    .asciz "mmap"
ln175:
    .asciz "mmio_map"
ln176:
    .asciz "modo_testo"
ln177:
    .asciz "mount"
ln178:
    .asciz "mountinfo"
ln179:
    .asciz "munmap"
ln180:
    .asciz "nanosleep"
ln181:
    .asciz "nome_utente"
ln182:
    .asciz "open"
ln183:
    .asciz "opendir"
ln184:
    .asciz "osversion"
ln185:
    .asciz "partwrite"
ln186:
    .asciz "pathconf"
ln187:
    .asciz "pclose"
ln188:
    .asciz "perror"
ln189:
    .asciz "pipe"
ln190:
    .asciz "poll"
ln191:
    .asciz "popen"
ln192:
    .asciz "posix_memalign"
ln193:
    .asciz "printf"
ln194:
    .asciz "procinfo"
ln195:
    .asciz "pty_apri"
ln196:
    .asciz "pty_ctl"
ln197:
    .asciz "putc"
ln198:
    .asciz "putchar"
ln199:
    .asciz "putenv"
ln200:
    .asciz "puts"
ln201:
    .asciz "qsort"
ln202:
    .asciz "quick_exit"
ln203:
    .asciz "raise"
ln204:
    .asciz "rand"
ln205:
    .asciz "read"
ln206:
    .asciz "readdir"
ln207:
    .asciz "realloc"
ln208:
    .asciz "realpath"
ln209:
    .asciz "reboot"
ln210:
    .asciz "remove"
ln211:
    .asciz "rename"
ln212:
    .asciz "rewind"
ln213:
    .asciz "rewinddir"
ln214:
    .asciz "rmdir"
ln215:
    .asciz "sbrk"
ln216:
    .asciz "scanf"
ln217:
    .asciz "sched_yield"
ln218:
    .asciz "select"
ln219:
    .asciz "setbuf"
ln220:
    .asciz "setenv"
ln221:
    .asciz "setjmp"
ln222:
    .asciz "setlocale"
ln223:
    .asciz "setuid"
ln224:
    .asciz "setvbuf"
ln225:
    .asciz "sha256"
ln226:
    .asciz "sha256_esa"
ln227:
    .asciz "shm_apri"
ln228:
    .asciz "shm_chiudi"
ln229:
    .asciz "signal"
ln230:
    .asciz "sleep"
ln231:
    .asciz "snprintf"
ln232:
    .asciz "spawn"
ln233:
    .asciz "spawn_ex"
ln234:
    .asciz "spawn_su_console"
ln235:
    .asciz "spawn_utente"
ln236:
    .asciz "sprintf"
ln237:
    .asciz "sqrt"
ln238:
    .asciz "srand"
ln239:
    .asciz "sscanf"
ln240:
    .asciz "stat"
ln241:
    .asciz "statperm"
ln242:
    .asciz "statraw"
ln243:
    .asciz "strcasecmp"
ln244:
    .asciz "strcat"
ln245:
    .asciz "strchr"
ln246:
    .asciz "strcmp"
ln247:
    .asciz "strcoll"
ln248:
    .asciz "strcpy"
ln249:
    .asciz "strcspn"
ln250:
    .asciz "strdup"
ln251:
    .asciz "strerror"
ln252:
    .asciz "strftime"
ln253:
    .asciz "strlen"
ln254:
    .asciz "strncasecmp"
ln255:
    .asciz "strncat"
ln256:
    .asciz "strncmp"
ln257:
    .asciz "strncpy"
ln258:
    .asciz "strpbrk"
ln259:
    .asciz "strrchr"
ln260:
    .asciz "strsignal"
ln261:
    .asciz "strspn"
ln262:
    .asciz "strstr"
ln263:
    .asciz "strtod"
ln264:
    .asciz "strtof"
ln265:
    .asciz "strtoimax"
ln266:
    .asciz "strtok"
ln267:
    .asciz "strtol"
ln268:
    .asciz "strtold"
ln269:
    .asciz "strtoll"
ln270:
    .asciz "strtoul"
ln271:
    .asciz "strtoull"
ln272:
    .asciz "strtoumax"
ln273:
    .asciz "strxfrm"
ln274:
    .asciz "swprintf"
ln275:
    .asciz "sysconf"
ln276:
    .asciz "system"
ln277:
    .asciz "time"
ln278:
    .asciz "time_now"
ln279:
    .asciz "times"
ln280:
    .asciz "timespec_get"
ln281:
    .asciz "tmpfile"
ln282:
    .asciz "tmpnam"
ln283:
    .asciz "toascii"
ln284:
    .asciz "tolower"
ln285:
    .asciz "toupper"
ln286:
    .asciz "towlower"
ln287:
    .asciz "towupper"
ln288:
    .asciz "truncate"
ln289:
    .asciz "tty_clear"
ln290:
    .asciz "tty_getsize"
ln291:
    .asciz "tty_raw"
ln292:
    .asciz "umask"
ln293:
    .asciz "umount"
ln294:
    .asciz "ungetc"
ln295:
    .asciz "unlink"
ln296:
    .asciz "unsetenv"
ln297:
    .asciz "uptime_ms"
ln298:
    .asciz "usleep"
ln299:
    .asciz "utime"
ln300:
    .asciz "verboseboot"
ln301:
    .asciz "vfprintf"
ln302:
    .asciz "vfscanf"
ln303:
    .asciz "video_info"
ln304:
    .asciz "vprintf"
ln305:
    .asciz "vscanf"
ln306:
    .asciz "vsnprintf"
ln307:
    .asciz "vsprintf"
ln308:
    .asciz "vsscanf"
ln309:
    .asciz "vswprintf"
ln310:
    .asciz "wait"
ln311:
    .asciz "waitpid"
ln312:
    .asciz "wcscat"
ln313:
    .asciz "wcschr"
ln314:
    .asciz "wcscmp"
ln315:
    .asciz "wcscpy"
ln316:
    .asciz "wcscspn"
ln317:
    .asciz "wcslen"
ln318:
    .asciz "wcsncat"
ln319:
    .asciz "wcsncmp"
ln320:
    .asciz "wcsncpy"
ln321:
    .asciz "wcspbrk"
ln322:
    .asciz "wcsrchr"
ln323:
    .asciz "wcsspn"
ln324:
    .asciz "wcsstr"
ln325:
    .asciz "wcstod"
ln326:
    .asciz "wcstol"
ln327:
    .asciz "wcstoll"
ln328:
    .asciz "wcstombs"
ln329:
    .asciz "wcstoul"
ln330:
    .asciz "wcstoull"
ln331:
    .asciz "wctomb"
ln332:
    .asciz "wmemchr"
ln333:
    .asciz "wmemcmp"
ln334:
    .asciz "wmemcpy"
ln335:
    .asciz "wmemmove"
ln336:
    .asciz "wmemset"
ln337:
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
    .long ipc_recv
    .long ipc_recv_timeout
    .long ipc_register
    .long ipc_rimetti
    .long ipc_send
    .long irq_bind
    .long irq_done
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
    .long 338
    .long libc_nomi
    .long libc_indirizzi

    .section .note.GNU-stack,"",@progbits
