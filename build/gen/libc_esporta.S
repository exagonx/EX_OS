/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * La tabella che libc.so mette a disposizione: 339 funzioni, ognuna col suo
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
    .asciz "irq_unbind"
ln118:
    .asciz "isalnum"
ln119:
    .asciz "isalpha"
ln120:
    .asciz "isascii"
ln121:
    .asciz "isatty"
ln122:
    .asciz "isblank"
ln123:
    .asciz "iscntrl"
ln124:
    .asciz "isdigit"
ln125:
    .asciz "isgraph"
ln126:
    .asciz "islower"
ln127:
    .asciz "isprint"
ln128:
    .asciz "ispunct"
ln129:
    .asciz "isspace"
ln130:
    .asciz "isupper"
ln131:
    .asciz "iswalnum"
ln132:
    .asciz "iswalpha"
ln133:
    .asciz "iswblank"
ln134:
    .asciz "iswcntrl"
ln135:
    .asciz "iswdigit"
ln136:
    .asciz "iswgraph"
ln137:
    .asciz "iswlower"
ln138:
    .asciz "iswprint"
ln139:
    .asciz "iswpunct"
ln140:
    .asciz "iswspace"
ln141:
    .asciz "iswupper"
ln142:
    .asciz "iswxdigit"
ln143:
    .asciz "isxdigit"
ln144:
    .asciz "labs"
ln145:
    .asciz "ldexp"
ln146:
    .asciz "ldiv"
ln147:
    .asciz "listdir"
ln148:
    .asciz "listdir_from"
ln149:
    .asciz "llabs"
ln150:
    .asciz "lldiv"
ln151:
    .asciz "localeconv"
ln152:
    .asciz "localtime"
ln153:
    .asciz "localtime_r"
ln154:
    .asciz "log_seriale"
ln155:
    .asciz "longjmp"
ln156:
    .asciz "lseek"
ln157:
    .asciz "lstat"
ln158:
    .asciz "malloc"
ln159:
    .asciz "malloc_usable_size"
ln160:
    .asciz "mblen"
ln161:
    .asciz "mbrtowc"
ln162:
    .asciz "mbstowcs"
ln163:
    .asciz "mbtowc"
ln164:
    .asciz "memalign"
ln165:
    .asciz "memchr"
ln166:
    .asciz "memcmp"
ln167:
    .asciz "memcpy"
ln168:
    .asciz "meminfo"
ln169:
    .asciz "memmove"
ln170:
    .asciz "memset"
ln171:
    .asciz "mkdir"
ln172:
    .asciz "mkstemp"
ln173:
    .asciz "mktemp"
ln174:
    .asciz "mktime"
ln175:
    .asciz "mmap"
ln176:
    .asciz "mmio_map"
ln177:
    .asciz "modo_testo"
ln178:
    .asciz "mount"
ln179:
    .asciz "mountinfo"
ln180:
    .asciz "munmap"
ln181:
    .asciz "nanosleep"
ln182:
    .asciz "nome_utente"
ln183:
    .asciz "open"
ln184:
    .asciz "opendir"
ln185:
    .asciz "osversion"
ln186:
    .asciz "partwrite"
ln187:
    .asciz "pathconf"
ln188:
    .asciz "pclose"
ln189:
    .asciz "perror"
ln190:
    .asciz "pipe"
ln191:
    .asciz "poll"
ln192:
    .asciz "popen"
ln193:
    .asciz "posix_memalign"
ln194:
    .asciz "printf"
ln195:
    .asciz "procinfo"
ln196:
    .asciz "pty_apri"
ln197:
    .asciz "pty_ctl"
ln198:
    .asciz "putc"
ln199:
    .asciz "putchar"
ln200:
    .asciz "putenv"
ln201:
    .asciz "puts"
ln202:
    .asciz "qsort"
ln203:
    .asciz "quick_exit"
ln204:
    .asciz "raise"
ln205:
    .asciz "rand"
ln206:
    .asciz "read"
ln207:
    .asciz "readdir"
ln208:
    .asciz "realloc"
ln209:
    .asciz "realpath"
ln210:
    .asciz "reboot"
ln211:
    .asciz "remove"
ln212:
    .asciz "rename"
ln213:
    .asciz "rewind"
ln214:
    .asciz "rewinddir"
ln215:
    .asciz "rmdir"
ln216:
    .asciz "sbrk"
ln217:
    .asciz "scanf"
ln218:
    .asciz "sched_yield"
ln219:
    .asciz "select"
ln220:
    .asciz "setbuf"
ln221:
    .asciz "setenv"
ln222:
    .asciz "setjmp"
ln223:
    .asciz "setlocale"
ln224:
    .asciz "setuid"
ln225:
    .asciz "setvbuf"
ln226:
    .asciz "sha256"
ln227:
    .asciz "sha256_esa"
ln228:
    .asciz "shm_apri"
ln229:
    .asciz "shm_chiudi"
ln230:
    .asciz "signal"
ln231:
    .asciz "sleep"
ln232:
    .asciz "snprintf"
ln233:
    .asciz "spawn"
ln234:
    .asciz "spawn_ex"
ln235:
    .asciz "spawn_su_console"
ln236:
    .asciz "spawn_utente"
ln237:
    .asciz "sprintf"
ln238:
    .asciz "sqrt"
ln239:
    .asciz "srand"
ln240:
    .asciz "sscanf"
ln241:
    .asciz "stat"
ln242:
    .asciz "statperm"
ln243:
    .asciz "statraw"
ln244:
    .asciz "strcasecmp"
ln245:
    .asciz "strcat"
ln246:
    .asciz "strchr"
ln247:
    .asciz "strcmp"
ln248:
    .asciz "strcoll"
ln249:
    .asciz "strcpy"
ln250:
    .asciz "strcspn"
ln251:
    .asciz "strdup"
ln252:
    .asciz "strerror"
ln253:
    .asciz "strftime"
ln254:
    .asciz "strlen"
ln255:
    .asciz "strncasecmp"
ln256:
    .asciz "strncat"
ln257:
    .asciz "strncmp"
ln258:
    .asciz "strncpy"
ln259:
    .asciz "strpbrk"
ln260:
    .asciz "strrchr"
ln261:
    .asciz "strsignal"
ln262:
    .asciz "strspn"
ln263:
    .asciz "strstr"
ln264:
    .asciz "strtod"
ln265:
    .asciz "strtof"
ln266:
    .asciz "strtoimax"
ln267:
    .asciz "strtok"
ln268:
    .asciz "strtol"
ln269:
    .asciz "strtold"
ln270:
    .asciz "strtoll"
ln271:
    .asciz "strtoul"
ln272:
    .asciz "strtoull"
ln273:
    .asciz "strtoumax"
ln274:
    .asciz "strxfrm"
ln275:
    .asciz "swprintf"
ln276:
    .asciz "sysconf"
ln277:
    .asciz "system"
ln278:
    .asciz "time"
ln279:
    .asciz "time_now"
ln280:
    .asciz "times"
ln281:
    .asciz "timespec_get"
ln282:
    .asciz "tmpfile"
ln283:
    .asciz "tmpnam"
ln284:
    .asciz "toascii"
ln285:
    .asciz "tolower"
ln286:
    .asciz "toupper"
ln287:
    .asciz "towlower"
ln288:
    .asciz "towupper"
ln289:
    .asciz "truncate"
ln290:
    .asciz "tty_clear"
ln291:
    .asciz "tty_getsize"
ln292:
    .asciz "tty_raw"
ln293:
    .asciz "umask"
ln294:
    .asciz "umount"
ln295:
    .asciz "ungetc"
ln296:
    .asciz "unlink"
ln297:
    .asciz "unsetenv"
ln298:
    .asciz "uptime_ms"
ln299:
    .asciz "usleep"
ln300:
    .asciz "utime"
ln301:
    .asciz "verboseboot"
ln302:
    .asciz "vfprintf"
ln303:
    .asciz "vfscanf"
ln304:
    .asciz "video_info"
ln305:
    .asciz "vprintf"
ln306:
    .asciz "vscanf"
ln307:
    .asciz "vsnprintf"
ln308:
    .asciz "vsprintf"
ln309:
    .asciz "vsscanf"
ln310:
    .asciz "vswprintf"
ln311:
    .asciz "wait"
ln312:
    .asciz "waitpid"
ln313:
    .asciz "wcscat"
ln314:
    .asciz "wcschr"
ln315:
    .asciz "wcscmp"
ln316:
    .asciz "wcscpy"
ln317:
    .asciz "wcscspn"
ln318:
    .asciz "wcslen"
ln319:
    .asciz "wcsncat"
ln320:
    .asciz "wcsncmp"
ln321:
    .asciz "wcsncpy"
ln322:
    .asciz "wcspbrk"
ln323:
    .asciz "wcsrchr"
ln324:
    .asciz "wcsspn"
ln325:
    .asciz "wcsstr"
ln326:
    .asciz "wcstod"
ln327:
    .asciz "wcstol"
ln328:
    .asciz "wcstoll"
ln329:
    .asciz "wcstombs"
ln330:
    .asciz "wcstoul"
ln331:
    .asciz "wcstoull"
ln332:
    .asciz "wctomb"
ln333:
    .asciz "wmemchr"
ln334:
    .asciz "wmemcmp"
ln335:
    .asciz "wmemcpy"
ln336:
    .asciz "wmemmove"
ln337:
    .asciz "wmemset"
ln338:
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
    .long 339
    .long libc_nomi
    .long libc_indirizzi

    .section .note.GNU-stack,"",@progbits
