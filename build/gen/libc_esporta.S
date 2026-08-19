/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * La tabella che libc.so mette a disposizione: 329 funzioni, ognuna col suo
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
    .asciz "console_info"
ln35:
    .asciz "console_setfg"
ln36:
    .asciz "console_switch"
ln37:
    .asciz "console_write"
ln38:
    .asciz "ctime"
ln39:
    .asciz "difftime"
ln40:
    .asciz "diskinfo"
ln41:
    .asciz "div"
ln42:
    .asciz "dma_alloc"
ln43:
    .asciz "dup"
ln44:
    .asciz "dup2"
ln45:
    .asciz "execv"
ln46:
    .asciz "execvp"
ln47:
    .asciz "exit"
ln48:
    .asciz "fabs"
ln49:
    .asciz "fb_map"
ln50:
    .asciz "fchmod"
ln51:
    .asciz "fclose"
ln52:
    .asciz "fcntl"
ln53:
    .asciz "fdopen"
ln54:
    .asciz "feof"
ln55:
    .asciz "ferror"
ln56:
    .asciz "fflush"
ln57:
    .asciz "fgetc"
ln58:
    .asciz "fgetpos"
ln59:
    .asciz "fgets"
ln60:
    .asciz "fileno"
ln61:
    .asciz "fopen"
ln62:
    .asciz "fpathconf"
ln63:
    .asciz "fprintf"
ln64:
    .asciz "fputc"
ln65:
    .asciz "fputs"
ln66:
    .asciz "fread"
ln67:
    .asciz "free"
ln68:
    .asciz "freopen"
ln69:
    .asciz "frexp"
ln70:
    .asciz "fscanf"
ln71:
    .asciz "fseek"
ln72:
    .asciz "fsetpos"
ln73:
    .asciz "fsize"
ln74:
    .asciz "fstat"
ln75:
    .asciz "ftell"
ln76:
    .asciz "fwrite"
ln77:
    .asciz "getc"
ln78:
    .asciz "getchar"
ln79:
    .asciz "getconf"
ln80:
    .asciz "getcwd"
ln81:
    .asciz "getegid"
ln82:
    .asciz "getentropy"
ln83:
    .asciz "getenv"
ln84:
    .asciz "geteuid"
ln85:
    .asciz "getgid"
ln86:
    .asciz "getpagesize"
ln87:
    .asciz "getpid"
ln88:
    .asciz "getrandom"
ln89:
    .asciz "getrusage"
ln90:
    .asciz "gets"
ln91:
    .asciz "gettimeofday"
ln92:
    .asciz "getuid"
ln93:
    .asciz "gmtime"
ln94:
    .asciz "imaxabs"
ln95:
    .asciz "imaxdiv"
ln96:
    .asciz "interrompi"
ln97:
    .asciz "ioctl"
ln98:
    .asciz "ioport_bind"
ln99:
    .asciz "ioport_in"
ln100:
    .asciz "ioport_in16"
ln101:
    .asciz "ioport_in32"
ln102:
    .asciz "ioport_out"
ln103:
    .asciz "ioport_out16"
ln104:
    .asciz "ioport_out32"
ln105:
    .asciz "ipc_lookup"
ln106:
    .asciz "ipc_recv"
ln107:
    .asciz "ipc_recv_timeout"
ln108:
    .asciz "ipc_register"
ln109:
    .asciz "ipc_rimetti"
ln110:
    .asciz "ipc_send"
ln111:
    .asciz "irq_bind"
ln112:
    .asciz "irq_done"
ln113:
    .asciz "isalnum"
ln114:
    .asciz "isalpha"
ln115:
    .asciz "isascii"
ln116:
    .asciz "isatty"
ln117:
    .asciz "isblank"
ln118:
    .asciz "iscntrl"
ln119:
    .asciz "isdigit"
ln120:
    .asciz "isgraph"
ln121:
    .asciz "islower"
ln122:
    .asciz "isprint"
ln123:
    .asciz "ispunct"
ln124:
    .asciz "isspace"
ln125:
    .asciz "isupper"
ln126:
    .asciz "iswalnum"
ln127:
    .asciz "iswalpha"
ln128:
    .asciz "iswblank"
ln129:
    .asciz "iswcntrl"
ln130:
    .asciz "iswdigit"
ln131:
    .asciz "iswgraph"
ln132:
    .asciz "iswlower"
ln133:
    .asciz "iswprint"
ln134:
    .asciz "iswpunct"
ln135:
    .asciz "iswspace"
ln136:
    .asciz "iswupper"
ln137:
    .asciz "iswxdigit"
ln138:
    .asciz "isxdigit"
ln139:
    .asciz "labs"
ln140:
    .asciz "ldexp"
ln141:
    .asciz "ldiv"
ln142:
    .asciz "listdir"
ln143:
    .asciz "listdir_from"
ln144:
    .asciz "llabs"
ln145:
    .asciz "lldiv"
ln146:
    .asciz "localeconv"
ln147:
    .asciz "localtime"
ln148:
    .asciz "log_seriale"
ln149:
    .asciz "longjmp"
ln150:
    .asciz "lseek"
ln151:
    .asciz "lstat"
ln152:
    .asciz "malloc"
ln153:
    .asciz "mblen"
ln154:
    .asciz "mbrtowc"
ln155:
    .asciz "mbstowcs"
ln156:
    .asciz "mbtowc"
ln157:
    .asciz "memalign"
ln158:
    .asciz "memchr"
ln159:
    .asciz "memcmp"
ln160:
    .asciz "memcpy"
ln161:
    .asciz "meminfo"
ln162:
    .asciz "memmove"
ln163:
    .asciz "memset"
ln164:
    .asciz "mkdir"
ln165:
    .asciz "mkstemp"
ln166:
    .asciz "mktemp"
ln167:
    .asciz "mktime"
ln168:
    .asciz "mmap"
ln169:
    .asciz "mmio_map"
ln170:
    .asciz "modo_testo"
ln171:
    .asciz "mount"
ln172:
    .asciz "mountinfo"
ln173:
    .asciz "munmap"
ln174:
    .asciz "nanosleep"
ln175:
    .asciz "open"
ln176:
    .asciz "opendir"
ln177:
    .asciz "osversion"
ln178:
    .asciz "partwrite"
ln179:
    .asciz "pathconf"
ln180:
    .asciz "pclose"
ln181:
    .asciz "perror"
ln182:
    .asciz "pipe"
ln183:
    .asciz "poll"
ln184:
    .asciz "popen"
ln185:
    .asciz "posix_memalign"
ln186:
    .asciz "printf"
ln187:
    .asciz "procinfo"
ln188:
    .asciz "pty_apri"
ln189:
    .asciz "pty_ctl"
ln190:
    .asciz "putc"
ln191:
    .asciz "putchar"
ln192:
    .asciz "putenv"
ln193:
    .asciz "puts"
ln194:
    .asciz "qsort"
ln195:
    .asciz "quick_exit"
ln196:
    .asciz "raise"
ln197:
    .asciz "rand"
ln198:
    .asciz "read"
ln199:
    .asciz "readdir"
ln200:
    .asciz "realloc"
ln201:
    .asciz "realpath"
ln202:
    .asciz "reboot"
ln203:
    .asciz "remove"
ln204:
    .asciz "rename"
ln205:
    .asciz "rewind"
ln206:
    .asciz "rewinddir"
ln207:
    .asciz "rmdir"
ln208:
    .asciz "sbrk"
ln209:
    .asciz "scanf"
ln210:
    .asciz "sched_yield"
ln211:
    .asciz "select"
ln212:
    .asciz "setbuf"
ln213:
    .asciz "setenv"
ln214:
    .asciz "setjmp"
ln215:
    .asciz "setlocale"
ln216:
    .asciz "setuid"
ln217:
    .asciz "setvbuf"
ln218:
    .asciz "sha256"
ln219:
    .asciz "sha256_esa"
ln220:
    .asciz "shm_apri"
ln221:
    .asciz "shm_chiudi"
ln222:
    .asciz "signal"
ln223:
    .asciz "sleep"
ln224:
    .asciz "snprintf"
ln225:
    .asciz "spawn"
ln226:
    .asciz "spawn_ex"
ln227:
    .asciz "spawn_su_console"
ln228:
    .asciz "sprintf"
ln229:
    .asciz "sqrt"
ln230:
    .asciz "srand"
ln231:
    .asciz "sscanf"
ln232:
    .asciz "stat"
ln233:
    .asciz "statraw"
ln234:
    .asciz "strcasecmp"
ln235:
    .asciz "strcat"
ln236:
    .asciz "strchr"
ln237:
    .asciz "strcmp"
ln238:
    .asciz "strcoll"
ln239:
    .asciz "strcpy"
ln240:
    .asciz "strcspn"
ln241:
    .asciz "strdup"
ln242:
    .asciz "strerror"
ln243:
    .asciz "strftime"
ln244:
    .asciz "strlen"
ln245:
    .asciz "strncasecmp"
ln246:
    .asciz "strncat"
ln247:
    .asciz "strncmp"
ln248:
    .asciz "strncpy"
ln249:
    .asciz "strpbrk"
ln250:
    .asciz "strrchr"
ln251:
    .asciz "strsignal"
ln252:
    .asciz "strspn"
ln253:
    .asciz "strstr"
ln254:
    .asciz "strtod"
ln255:
    .asciz "strtof"
ln256:
    .asciz "strtoimax"
ln257:
    .asciz "strtok"
ln258:
    .asciz "strtol"
ln259:
    .asciz "strtold"
ln260:
    .asciz "strtoll"
ln261:
    .asciz "strtoul"
ln262:
    .asciz "strtoull"
ln263:
    .asciz "strtoumax"
ln264:
    .asciz "strxfrm"
ln265:
    .asciz "swprintf"
ln266:
    .asciz "sysconf"
ln267:
    .asciz "system"
ln268:
    .asciz "time"
ln269:
    .asciz "time_now"
ln270:
    .asciz "times"
ln271:
    .asciz "timespec_get"
ln272:
    .asciz "tmpfile"
ln273:
    .asciz "tmpnam"
ln274:
    .asciz "toascii"
ln275:
    .asciz "tolower"
ln276:
    .asciz "toupper"
ln277:
    .asciz "towlower"
ln278:
    .asciz "towupper"
ln279:
    .asciz "truncate"
ln280:
    .asciz "tty_clear"
ln281:
    .asciz "tty_getsize"
ln282:
    .asciz "tty_raw"
ln283:
    .asciz "umask"
ln284:
    .asciz "umount"
ln285:
    .asciz "ungetc"
ln286:
    .asciz "unlink"
ln287:
    .asciz "unsetenv"
ln288:
    .asciz "uptime_ms"
ln289:
    .asciz "usleep"
ln290:
    .asciz "utime"
ln291:
    .asciz "verboseboot"
ln292:
    .asciz "vfprintf"
ln293:
    .asciz "vfscanf"
ln294:
    .asciz "video_info"
ln295:
    .asciz "vprintf"
ln296:
    .asciz "vscanf"
ln297:
    .asciz "vsnprintf"
ln298:
    .asciz "vsprintf"
ln299:
    .asciz "vsscanf"
ln300:
    .asciz "vswprintf"
ln301:
    .asciz "wait"
ln302:
    .asciz "waitpid"
ln303:
    .asciz "wcscat"
ln304:
    .asciz "wcschr"
ln305:
    .asciz "wcscmp"
ln306:
    .asciz "wcscpy"
ln307:
    .asciz "wcscspn"
ln308:
    .asciz "wcslen"
ln309:
    .asciz "wcsncat"
ln310:
    .asciz "wcsncmp"
ln311:
    .asciz "wcsncpy"
ln312:
    .asciz "wcspbrk"
ln313:
    .asciz "wcsrchr"
ln314:
    .asciz "wcsspn"
ln315:
    .asciz "wcsstr"
ln316:
    .asciz "wcstod"
ln317:
    .asciz "wcstol"
ln318:
    .asciz "wcstoll"
ln319:
    .asciz "wcstombs"
ln320:
    .asciz "wcstoul"
ln321:
    .asciz "wcstoull"
ln322:
    .asciz "wctomb"
ln323:
    .asciz "wmemchr"
ln324:
    .asciz "wmemcmp"
ln325:
    .asciz "wmemcpy"
ln326:
    .asciz "wmemmove"
ln327:
    .asciz "wmemset"
ln328:
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
    .long console_info
    .long console_setfg
    .long console_switch
    .long console_write
    .long ctime
    .long difftime
    .long diskinfo
    .long div
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
    .long log_seriale
    .long longjmp
    .long lseek
    .long lstat
    .long malloc
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
    .long sprintf
    .long sqrt
    .long srand
    .long sscanf
    .long stat
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
    .long 329
    .long libc_nomi
    .long libc_indirizzi

    .section .note.GNU-stack,"",@progbits
