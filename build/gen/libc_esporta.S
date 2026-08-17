/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * La tabella che libc.so mette a disposizione: 324 funzioni, ognuna col suo
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
    .asciz "fchmod"
ln50:
    .asciz "fclose"
ln51:
    .asciz "fcntl"
ln52:
    .asciz "fdopen"
ln53:
    .asciz "feof"
ln54:
    .asciz "ferror"
ln55:
    .asciz "fflush"
ln56:
    .asciz "fgetc"
ln57:
    .asciz "fgetpos"
ln58:
    .asciz "fgets"
ln59:
    .asciz "fileno"
ln60:
    .asciz "fopen"
ln61:
    .asciz "fpathconf"
ln62:
    .asciz "fprintf"
ln63:
    .asciz "fputc"
ln64:
    .asciz "fputs"
ln65:
    .asciz "fread"
ln66:
    .asciz "free"
ln67:
    .asciz "freopen"
ln68:
    .asciz "frexp"
ln69:
    .asciz "fscanf"
ln70:
    .asciz "fseek"
ln71:
    .asciz "fsetpos"
ln72:
    .asciz "fsize"
ln73:
    .asciz "fstat"
ln74:
    .asciz "ftell"
ln75:
    .asciz "fwrite"
ln76:
    .asciz "getc"
ln77:
    .asciz "getchar"
ln78:
    .asciz "getconf"
ln79:
    .asciz "getcwd"
ln80:
    .asciz "getegid"
ln81:
    .asciz "getentropy"
ln82:
    .asciz "getenv"
ln83:
    .asciz "geteuid"
ln84:
    .asciz "getgid"
ln85:
    .asciz "getpagesize"
ln86:
    .asciz "getpid"
ln87:
    .asciz "getrandom"
ln88:
    .asciz "getrusage"
ln89:
    .asciz "gets"
ln90:
    .asciz "gettimeofday"
ln91:
    .asciz "getuid"
ln92:
    .asciz "gmtime"
ln93:
    .asciz "imaxabs"
ln94:
    .asciz "imaxdiv"
ln95:
    .asciz "ioctl"
ln96:
    .asciz "ioport_bind"
ln97:
    .asciz "ioport_in"
ln98:
    .asciz "ioport_in16"
ln99:
    .asciz "ioport_in32"
ln100:
    .asciz "ioport_out"
ln101:
    .asciz "ioport_out16"
ln102:
    .asciz "ioport_out32"
ln103:
    .asciz "ipc_lookup"
ln104:
    .asciz "ipc_recv"
ln105:
    .asciz "ipc_recv_timeout"
ln106:
    .asciz "ipc_register"
ln107:
    .asciz "ipc_send"
ln108:
    .asciz "irq_bind"
ln109:
    .asciz "irq_done"
ln110:
    .asciz "isalnum"
ln111:
    .asciz "isalpha"
ln112:
    .asciz "isascii"
ln113:
    .asciz "isatty"
ln114:
    .asciz "isblank"
ln115:
    .asciz "iscntrl"
ln116:
    .asciz "isdigit"
ln117:
    .asciz "isgraph"
ln118:
    .asciz "islower"
ln119:
    .asciz "isprint"
ln120:
    .asciz "ispunct"
ln121:
    .asciz "isspace"
ln122:
    .asciz "isupper"
ln123:
    .asciz "iswalnum"
ln124:
    .asciz "iswalpha"
ln125:
    .asciz "iswblank"
ln126:
    .asciz "iswcntrl"
ln127:
    .asciz "iswdigit"
ln128:
    .asciz "iswgraph"
ln129:
    .asciz "iswlower"
ln130:
    .asciz "iswprint"
ln131:
    .asciz "iswpunct"
ln132:
    .asciz "iswspace"
ln133:
    .asciz "iswupper"
ln134:
    .asciz "iswxdigit"
ln135:
    .asciz "isxdigit"
ln136:
    .asciz "labs"
ln137:
    .asciz "ldexp"
ln138:
    .asciz "ldiv"
ln139:
    .asciz "listdir"
ln140:
    .asciz "listdir_from"
ln141:
    .asciz "llabs"
ln142:
    .asciz "lldiv"
ln143:
    .asciz "localeconv"
ln144:
    .asciz "localtime"
ln145:
    .asciz "log_seriale"
ln146:
    .asciz "longjmp"
ln147:
    .asciz "lseek"
ln148:
    .asciz "lstat"
ln149:
    .asciz "malloc"
ln150:
    .asciz "mblen"
ln151:
    .asciz "mbrtowc"
ln152:
    .asciz "mbstowcs"
ln153:
    .asciz "mbtowc"
ln154:
    .asciz "memalign"
ln155:
    .asciz "memchr"
ln156:
    .asciz "memcmp"
ln157:
    .asciz "memcpy"
ln158:
    .asciz "meminfo"
ln159:
    .asciz "memmove"
ln160:
    .asciz "memset"
ln161:
    .asciz "mkdir"
ln162:
    .asciz "mkstemp"
ln163:
    .asciz "mktemp"
ln164:
    .asciz "mktime"
ln165:
    .asciz "mmap"
ln166:
    .asciz "mmio_map"
ln167:
    .asciz "modo_testo"
ln168:
    .asciz "mount"
ln169:
    .asciz "mountinfo"
ln170:
    .asciz "munmap"
ln171:
    .asciz "nanosleep"
ln172:
    .asciz "open"
ln173:
    .asciz "opendir"
ln174:
    .asciz "osversion"
ln175:
    .asciz "partwrite"
ln176:
    .asciz "pathconf"
ln177:
    .asciz "pclose"
ln178:
    .asciz "perror"
ln179:
    .asciz "pipe"
ln180:
    .asciz "poll"
ln181:
    .asciz "popen"
ln182:
    .asciz "posix_memalign"
ln183:
    .asciz "printf"
ln184:
    .asciz "procinfo"
ln185:
    .asciz "putc"
ln186:
    .asciz "putchar"
ln187:
    .asciz "putenv"
ln188:
    .asciz "puts"
ln189:
    .asciz "qsort"
ln190:
    .asciz "quick_exit"
ln191:
    .asciz "raise"
ln192:
    .asciz "rand"
ln193:
    .asciz "read"
ln194:
    .asciz "readdir"
ln195:
    .asciz "realloc"
ln196:
    .asciz "realpath"
ln197:
    .asciz "reboot"
ln198:
    .asciz "remove"
ln199:
    .asciz "rename"
ln200:
    .asciz "rewind"
ln201:
    .asciz "rewinddir"
ln202:
    .asciz "rmdir"
ln203:
    .asciz "sbrk"
ln204:
    .asciz "scanf"
ln205:
    .asciz "sched_yield"
ln206:
    .asciz "select"
ln207:
    .asciz "setbuf"
ln208:
    .asciz "setenv"
ln209:
    .asciz "setjmp"
ln210:
    .asciz "setlocale"
ln211:
    .asciz "setuid"
ln212:
    .asciz "setvbuf"
ln213:
    .asciz "sha256"
ln214:
    .asciz "sha256_esa"
ln215:
    .asciz "shm_apri"
ln216:
    .asciz "shm_chiudi"
ln217:
    .asciz "signal"
ln218:
    .asciz "sleep"
ln219:
    .asciz "snprintf"
ln220:
    .asciz "spawn"
ln221:
    .asciz "spawn_ex"
ln222:
    .asciz "spawn_su_console"
ln223:
    .asciz "sprintf"
ln224:
    .asciz "sqrt"
ln225:
    .asciz "srand"
ln226:
    .asciz "sscanf"
ln227:
    .asciz "stat"
ln228:
    .asciz "statraw"
ln229:
    .asciz "strcasecmp"
ln230:
    .asciz "strcat"
ln231:
    .asciz "strchr"
ln232:
    .asciz "strcmp"
ln233:
    .asciz "strcoll"
ln234:
    .asciz "strcpy"
ln235:
    .asciz "strcspn"
ln236:
    .asciz "strdup"
ln237:
    .asciz "strerror"
ln238:
    .asciz "strftime"
ln239:
    .asciz "strlen"
ln240:
    .asciz "strncasecmp"
ln241:
    .asciz "strncat"
ln242:
    .asciz "strncmp"
ln243:
    .asciz "strncpy"
ln244:
    .asciz "strpbrk"
ln245:
    .asciz "strrchr"
ln246:
    .asciz "strsignal"
ln247:
    .asciz "strspn"
ln248:
    .asciz "strstr"
ln249:
    .asciz "strtod"
ln250:
    .asciz "strtof"
ln251:
    .asciz "strtoimax"
ln252:
    .asciz "strtok"
ln253:
    .asciz "strtol"
ln254:
    .asciz "strtold"
ln255:
    .asciz "strtoll"
ln256:
    .asciz "strtoul"
ln257:
    .asciz "strtoull"
ln258:
    .asciz "strtoumax"
ln259:
    .asciz "strxfrm"
ln260:
    .asciz "swprintf"
ln261:
    .asciz "sysconf"
ln262:
    .asciz "system"
ln263:
    .asciz "time"
ln264:
    .asciz "time_now"
ln265:
    .asciz "times"
ln266:
    .asciz "timespec_get"
ln267:
    .asciz "tmpfile"
ln268:
    .asciz "tmpnam"
ln269:
    .asciz "toascii"
ln270:
    .asciz "tolower"
ln271:
    .asciz "toupper"
ln272:
    .asciz "towlower"
ln273:
    .asciz "towupper"
ln274:
    .asciz "truncate"
ln275:
    .asciz "tty_clear"
ln276:
    .asciz "tty_getsize"
ln277:
    .asciz "tty_raw"
ln278:
    .asciz "umask"
ln279:
    .asciz "umount"
ln280:
    .asciz "ungetc"
ln281:
    .asciz "unlink"
ln282:
    .asciz "unsetenv"
ln283:
    .asciz "uptime_ms"
ln284:
    .asciz "usleep"
ln285:
    .asciz "utime"
ln286:
    .asciz "verboseboot"
ln287:
    .asciz "vfprintf"
ln288:
    .asciz "vfscanf"
ln289:
    .asciz "video_info"
ln290:
    .asciz "vprintf"
ln291:
    .asciz "vscanf"
ln292:
    .asciz "vsnprintf"
ln293:
    .asciz "vsprintf"
ln294:
    .asciz "vsscanf"
ln295:
    .asciz "vswprintf"
ln296:
    .asciz "wait"
ln297:
    .asciz "waitpid"
ln298:
    .asciz "wcscat"
ln299:
    .asciz "wcschr"
ln300:
    .asciz "wcscmp"
ln301:
    .asciz "wcscpy"
ln302:
    .asciz "wcscspn"
ln303:
    .asciz "wcslen"
ln304:
    .asciz "wcsncat"
ln305:
    .asciz "wcsncmp"
ln306:
    .asciz "wcsncpy"
ln307:
    .asciz "wcspbrk"
ln308:
    .asciz "wcsrchr"
ln309:
    .asciz "wcsspn"
ln310:
    .asciz "wcsstr"
ln311:
    .asciz "wcstod"
ln312:
    .asciz "wcstol"
ln313:
    .asciz "wcstoll"
ln314:
    .asciz "wcstombs"
ln315:
    .asciz "wcstoul"
ln316:
    .asciz "wcstoull"
ln317:
    .asciz "wctomb"
ln318:
    .asciz "wmemchr"
ln319:
    .asciz "wmemcmp"
ln320:
    .asciz "wmemcpy"
ln321:
    .asciz "wmemmove"
ln322:
    .asciz "wmemset"
ln323:
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
    .long 324
    .long libc_nomi
    .long libc_indirizzi

    .section .note.GNU-stack,"",@progbits
