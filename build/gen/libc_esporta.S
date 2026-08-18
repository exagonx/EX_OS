/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * La tabella che libc.so mette a disposizione: 328 funzioni, ognuna col suo
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
    .asciz "ipc_send"
ln110:
    .asciz "irq_bind"
ln111:
    .asciz "irq_done"
ln112:
    .asciz "isalnum"
ln113:
    .asciz "isalpha"
ln114:
    .asciz "isascii"
ln115:
    .asciz "isatty"
ln116:
    .asciz "isblank"
ln117:
    .asciz "iscntrl"
ln118:
    .asciz "isdigit"
ln119:
    .asciz "isgraph"
ln120:
    .asciz "islower"
ln121:
    .asciz "isprint"
ln122:
    .asciz "ispunct"
ln123:
    .asciz "isspace"
ln124:
    .asciz "isupper"
ln125:
    .asciz "iswalnum"
ln126:
    .asciz "iswalpha"
ln127:
    .asciz "iswblank"
ln128:
    .asciz "iswcntrl"
ln129:
    .asciz "iswdigit"
ln130:
    .asciz "iswgraph"
ln131:
    .asciz "iswlower"
ln132:
    .asciz "iswprint"
ln133:
    .asciz "iswpunct"
ln134:
    .asciz "iswspace"
ln135:
    .asciz "iswupper"
ln136:
    .asciz "iswxdigit"
ln137:
    .asciz "isxdigit"
ln138:
    .asciz "labs"
ln139:
    .asciz "ldexp"
ln140:
    .asciz "ldiv"
ln141:
    .asciz "listdir"
ln142:
    .asciz "listdir_from"
ln143:
    .asciz "llabs"
ln144:
    .asciz "lldiv"
ln145:
    .asciz "localeconv"
ln146:
    .asciz "localtime"
ln147:
    .asciz "log_seriale"
ln148:
    .asciz "longjmp"
ln149:
    .asciz "lseek"
ln150:
    .asciz "lstat"
ln151:
    .asciz "malloc"
ln152:
    .asciz "mblen"
ln153:
    .asciz "mbrtowc"
ln154:
    .asciz "mbstowcs"
ln155:
    .asciz "mbtowc"
ln156:
    .asciz "memalign"
ln157:
    .asciz "memchr"
ln158:
    .asciz "memcmp"
ln159:
    .asciz "memcpy"
ln160:
    .asciz "meminfo"
ln161:
    .asciz "memmove"
ln162:
    .asciz "memset"
ln163:
    .asciz "mkdir"
ln164:
    .asciz "mkstemp"
ln165:
    .asciz "mktemp"
ln166:
    .asciz "mktime"
ln167:
    .asciz "mmap"
ln168:
    .asciz "mmio_map"
ln169:
    .asciz "modo_testo"
ln170:
    .asciz "mount"
ln171:
    .asciz "mountinfo"
ln172:
    .asciz "munmap"
ln173:
    .asciz "nanosleep"
ln174:
    .asciz "open"
ln175:
    .asciz "opendir"
ln176:
    .asciz "osversion"
ln177:
    .asciz "partwrite"
ln178:
    .asciz "pathconf"
ln179:
    .asciz "pclose"
ln180:
    .asciz "perror"
ln181:
    .asciz "pipe"
ln182:
    .asciz "poll"
ln183:
    .asciz "popen"
ln184:
    .asciz "posix_memalign"
ln185:
    .asciz "printf"
ln186:
    .asciz "procinfo"
ln187:
    .asciz "pty_apri"
ln188:
    .asciz "pty_ctl"
ln189:
    .asciz "putc"
ln190:
    .asciz "putchar"
ln191:
    .asciz "putenv"
ln192:
    .asciz "puts"
ln193:
    .asciz "qsort"
ln194:
    .asciz "quick_exit"
ln195:
    .asciz "raise"
ln196:
    .asciz "rand"
ln197:
    .asciz "read"
ln198:
    .asciz "readdir"
ln199:
    .asciz "realloc"
ln200:
    .asciz "realpath"
ln201:
    .asciz "reboot"
ln202:
    .asciz "remove"
ln203:
    .asciz "rename"
ln204:
    .asciz "rewind"
ln205:
    .asciz "rewinddir"
ln206:
    .asciz "rmdir"
ln207:
    .asciz "sbrk"
ln208:
    .asciz "scanf"
ln209:
    .asciz "sched_yield"
ln210:
    .asciz "select"
ln211:
    .asciz "setbuf"
ln212:
    .asciz "setenv"
ln213:
    .asciz "setjmp"
ln214:
    .asciz "setlocale"
ln215:
    .asciz "setuid"
ln216:
    .asciz "setvbuf"
ln217:
    .asciz "sha256"
ln218:
    .asciz "sha256_esa"
ln219:
    .asciz "shm_apri"
ln220:
    .asciz "shm_chiudi"
ln221:
    .asciz "signal"
ln222:
    .asciz "sleep"
ln223:
    .asciz "snprintf"
ln224:
    .asciz "spawn"
ln225:
    .asciz "spawn_ex"
ln226:
    .asciz "spawn_su_console"
ln227:
    .asciz "sprintf"
ln228:
    .asciz "sqrt"
ln229:
    .asciz "srand"
ln230:
    .asciz "sscanf"
ln231:
    .asciz "stat"
ln232:
    .asciz "statraw"
ln233:
    .asciz "strcasecmp"
ln234:
    .asciz "strcat"
ln235:
    .asciz "strchr"
ln236:
    .asciz "strcmp"
ln237:
    .asciz "strcoll"
ln238:
    .asciz "strcpy"
ln239:
    .asciz "strcspn"
ln240:
    .asciz "strdup"
ln241:
    .asciz "strerror"
ln242:
    .asciz "strftime"
ln243:
    .asciz "strlen"
ln244:
    .asciz "strncasecmp"
ln245:
    .asciz "strncat"
ln246:
    .asciz "strncmp"
ln247:
    .asciz "strncpy"
ln248:
    .asciz "strpbrk"
ln249:
    .asciz "strrchr"
ln250:
    .asciz "strsignal"
ln251:
    .asciz "strspn"
ln252:
    .asciz "strstr"
ln253:
    .asciz "strtod"
ln254:
    .asciz "strtof"
ln255:
    .asciz "strtoimax"
ln256:
    .asciz "strtok"
ln257:
    .asciz "strtol"
ln258:
    .asciz "strtold"
ln259:
    .asciz "strtoll"
ln260:
    .asciz "strtoul"
ln261:
    .asciz "strtoull"
ln262:
    .asciz "strtoumax"
ln263:
    .asciz "strxfrm"
ln264:
    .asciz "swprintf"
ln265:
    .asciz "sysconf"
ln266:
    .asciz "system"
ln267:
    .asciz "time"
ln268:
    .asciz "time_now"
ln269:
    .asciz "times"
ln270:
    .asciz "timespec_get"
ln271:
    .asciz "tmpfile"
ln272:
    .asciz "tmpnam"
ln273:
    .asciz "toascii"
ln274:
    .asciz "tolower"
ln275:
    .asciz "toupper"
ln276:
    .asciz "towlower"
ln277:
    .asciz "towupper"
ln278:
    .asciz "truncate"
ln279:
    .asciz "tty_clear"
ln280:
    .asciz "tty_getsize"
ln281:
    .asciz "tty_raw"
ln282:
    .asciz "umask"
ln283:
    .asciz "umount"
ln284:
    .asciz "ungetc"
ln285:
    .asciz "unlink"
ln286:
    .asciz "unsetenv"
ln287:
    .asciz "uptime_ms"
ln288:
    .asciz "usleep"
ln289:
    .asciz "utime"
ln290:
    .asciz "verboseboot"
ln291:
    .asciz "vfprintf"
ln292:
    .asciz "vfscanf"
ln293:
    .asciz "video_info"
ln294:
    .asciz "vprintf"
ln295:
    .asciz "vscanf"
ln296:
    .asciz "vsnprintf"
ln297:
    .asciz "vsprintf"
ln298:
    .asciz "vsscanf"
ln299:
    .asciz "vswprintf"
ln300:
    .asciz "wait"
ln301:
    .asciz "waitpid"
ln302:
    .asciz "wcscat"
ln303:
    .asciz "wcschr"
ln304:
    .asciz "wcscmp"
ln305:
    .asciz "wcscpy"
ln306:
    .asciz "wcscspn"
ln307:
    .asciz "wcslen"
ln308:
    .asciz "wcsncat"
ln309:
    .asciz "wcsncmp"
ln310:
    .asciz "wcsncpy"
ln311:
    .asciz "wcspbrk"
ln312:
    .asciz "wcsrchr"
ln313:
    .asciz "wcsspn"
ln314:
    .asciz "wcsstr"
ln315:
    .asciz "wcstod"
ln316:
    .asciz "wcstol"
ln317:
    .asciz "wcstoll"
ln318:
    .asciz "wcstombs"
ln319:
    .asciz "wcstoul"
ln320:
    .asciz "wcstoull"
ln321:
    .asciz "wctomb"
ln322:
    .asciz "wmemchr"
ln323:
    .asciz "wmemcmp"
ln324:
    .asciz "wmemcpy"
ln325:
    .asciz "wmemmove"
ln326:
    .asciz "wmemset"
ln327:
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
    .long 328
    .long libc_nomi
    .long libc_indirizzi

    .section .note.GNU-stack,"",@progbits
