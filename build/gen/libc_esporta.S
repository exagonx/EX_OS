/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * La tabella che libc.so mette a disposizione: 322 funzioni, ognuna col suo
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
    .asciz "clearerr"
ln30:
    .asciz "clock"
ln31:
    .asciz "close"
ln32:
    .asciz "closedir"
ln33:
    .asciz "console_info"
ln34:
    .asciz "console_setfg"
ln35:
    .asciz "console_switch"
ln36:
    .asciz "console_write"
ln37:
    .asciz "ctime"
ln38:
    .asciz "difftime"
ln39:
    .asciz "diskinfo"
ln40:
    .asciz "div"
ln41:
    .asciz "dma_alloc"
ln42:
    .asciz "dup"
ln43:
    .asciz "dup2"
ln44:
    .asciz "execv"
ln45:
    .asciz "execvp"
ln46:
    .asciz "exit"
ln47:
    .asciz "fabs"
ln48:
    .asciz "fchmod"
ln49:
    .asciz "fclose"
ln50:
    .asciz "fcntl"
ln51:
    .asciz "fdopen"
ln52:
    .asciz "feof"
ln53:
    .asciz "ferror"
ln54:
    .asciz "fflush"
ln55:
    .asciz "fgetc"
ln56:
    .asciz "fgetpos"
ln57:
    .asciz "fgets"
ln58:
    .asciz "fileno"
ln59:
    .asciz "fopen"
ln60:
    .asciz "fpathconf"
ln61:
    .asciz "fprintf"
ln62:
    .asciz "fputc"
ln63:
    .asciz "fputs"
ln64:
    .asciz "fread"
ln65:
    .asciz "free"
ln66:
    .asciz "freopen"
ln67:
    .asciz "frexp"
ln68:
    .asciz "fscanf"
ln69:
    .asciz "fseek"
ln70:
    .asciz "fsetpos"
ln71:
    .asciz "fsize"
ln72:
    .asciz "fstat"
ln73:
    .asciz "ftell"
ln74:
    .asciz "fwrite"
ln75:
    .asciz "getc"
ln76:
    .asciz "getchar"
ln77:
    .asciz "getconf"
ln78:
    .asciz "getcwd"
ln79:
    .asciz "getegid"
ln80:
    .asciz "getentropy"
ln81:
    .asciz "getenv"
ln82:
    .asciz "geteuid"
ln83:
    .asciz "getgid"
ln84:
    .asciz "getpagesize"
ln85:
    .asciz "getpid"
ln86:
    .asciz "getrandom"
ln87:
    .asciz "getrusage"
ln88:
    .asciz "gets"
ln89:
    .asciz "gettimeofday"
ln90:
    .asciz "getuid"
ln91:
    .asciz "gmtime"
ln92:
    .asciz "imaxabs"
ln93:
    .asciz "imaxdiv"
ln94:
    .asciz "ioctl"
ln95:
    .asciz "ioport_bind"
ln96:
    .asciz "ioport_in"
ln97:
    .asciz "ioport_in16"
ln98:
    .asciz "ioport_in32"
ln99:
    .asciz "ioport_out"
ln100:
    .asciz "ioport_out16"
ln101:
    .asciz "ioport_out32"
ln102:
    .asciz "ipc_lookup"
ln103:
    .asciz "ipc_recv"
ln104:
    .asciz "ipc_recv_timeout"
ln105:
    .asciz "ipc_register"
ln106:
    .asciz "ipc_send"
ln107:
    .asciz "irq_bind"
ln108:
    .asciz "irq_done"
ln109:
    .asciz "isalnum"
ln110:
    .asciz "isalpha"
ln111:
    .asciz "isascii"
ln112:
    .asciz "isatty"
ln113:
    .asciz "isblank"
ln114:
    .asciz "iscntrl"
ln115:
    .asciz "isdigit"
ln116:
    .asciz "isgraph"
ln117:
    .asciz "islower"
ln118:
    .asciz "isprint"
ln119:
    .asciz "ispunct"
ln120:
    .asciz "isspace"
ln121:
    .asciz "isupper"
ln122:
    .asciz "iswalnum"
ln123:
    .asciz "iswalpha"
ln124:
    .asciz "iswblank"
ln125:
    .asciz "iswcntrl"
ln126:
    .asciz "iswdigit"
ln127:
    .asciz "iswgraph"
ln128:
    .asciz "iswlower"
ln129:
    .asciz "iswprint"
ln130:
    .asciz "iswpunct"
ln131:
    .asciz "iswspace"
ln132:
    .asciz "iswupper"
ln133:
    .asciz "iswxdigit"
ln134:
    .asciz "isxdigit"
ln135:
    .asciz "labs"
ln136:
    .asciz "ldexp"
ln137:
    .asciz "ldiv"
ln138:
    .asciz "listdir"
ln139:
    .asciz "listdir_from"
ln140:
    .asciz "llabs"
ln141:
    .asciz "lldiv"
ln142:
    .asciz "localeconv"
ln143:
    .asciz "localtime"
ln144:
    .asciz "log_seriale"
ln145:
    .asciz "longjmp"
ln146:
    .asciz "lseek"
ln147:
    .asciz "lstat"
ln148:
    .asciz "malloc"
ln149:
    .asciz "mblen"
ln150:
    .asciz "mbrtowc"
ln151:
    .asciz "mbstowcs"
ln152:
    .asciz "mbtowc"
ln153:
    .asciz "memalign"
ln154:
    .asciz "memchr"
ln155:
    .asciz "memcmp"
ln156:
    .asciz "memcpy"
ln157:
    .asciz "meminfo"
ln158:
    .asciz "memmove"
ln159:
    .asciz "memset"
ln160:
    .asciz "mkdir"
ln161:
    .asciz "mkstemp"
ln162:
    .asciz "mktemp"
ln163:
    .asciz "mktime"
ln164:
    .asciz "mmap"
ln165:
    .asciz "mmio_map"
ln166:
    .asciz "modo_testo"
ln167:
    .asciz "mount"
ln168:
    .asciz "mountinfo"
ln169:
    .asciz "munmap"
ln170:
    .asciz "nanosleep"
ln171:
    .asciz "open"
ln172:
    .asciz "opendir"
ln173:
    .asciz "osversion"
ln174:
    .asciz "partwrite"
ln175:
    .asciz "pathconf"
ln176:
    .asciz "pclose"
ln177:
    .asciz "perror"
ln178:
    .asciz "pipe"
ln179:
    .asciz "poll"
ln180:
    .asciz "popen"
ln181:
    .asciz "posix_memalign"
ln182:
    .asciz "printf"
ln183:
    .asciz "procinfo"
ln184:
    .asciz "putc"
ln185:
    .asciz "putchar"
ln186:
    .asciz "putenv"
ln187:
    .asciz "puts"
ln188:
    .asciz "qsort"
ln189:
    .asciz "quick_exit"
ln190:
    .asciz "raise"
ln191:
    .asciz "rand"
ln192:
    .asciz "read"
ln193:
    .asciz "readdir"
ln194:
    .asciz "realloc"
ln195:
    .asciz "realpath"
ln196:
    .asciz "reboot"
ln197:
    .asciz "remove"
ln198:
    .asciz "rename"
ln199:
    .asciz "rewind"
ln200:
    .asciz "rewinddir"
ln201:
    .asciz "rmdir"
ln202:
    .asciz "sbrk"
ln203:
    .asciz "scanf"
ln204:
    .asciz "sched_yield"
ln205:
    .asciz "select"
ln206:
    .asciz "setbuf"
ln207:
    .asciz "setenv"
ln208:
    .asciz "setjmp"
ln209:
    .asciz "setlocale"
ln210:
    .asciz "setvbuf"
ln211:
    .asciz "sha256"
ln212:
    .asciz "sha256_esa"
ln213:
    .asciz "shm_apri"
ln214:
    .asciz "shm_chiudi"
ln215:
    .asciz "signal"
ln216:
    .asciz "sleep"
ln217:
    .asciz "snprintf"
ln218:
    .asciz "spawn"
ln219:
    .asciz "spawn_ex"
ln220:
    .asciz "spawn_su_console"
ln221:
    .asciz "sprintf"
ln222:
    .asciz "sqrt"
ln223:
    .asciz "srand"
ln224:
    .asciz "sscanf"
ln225:
    .asciz "stat"
ln226:
    .asciz "statraw"
ln227:
    .asciz "strcasecmp"
ln228:
    .asciz "strcat"
ln229:
    .asciz "strchr"
ln230:
    .asciz "strcmp"
ln231:
    .asciz "strcoll"
ln232:
    .asciz "strcpy"
ln233:
    .asciz "strcspn"
ln234:
    .asciz "strdup"
ln235:
    .asciz "strerror"
ln236:
    .asciz "strftime"
ln237:
    .asciz "strlen"
ln238:
    .asciz "strncasecmp"
ln239:
    .asciz "strncat"
ln240:
    .asciz "strncmp"
ln241:
    .asciz "strncpy"
ln242:
    .asciz "strpbrk"
ln243:
    .asciz "strrchr"
ln244:
    .asciz "strsignal"
ln245:
    .asciz "strspn"
ln246:
    .asciz "strstr"
ln247:
    .asciz "strtod"
ln248:
    .asciz "strtof"
ln249:
    .asciz "strtoimax"
ln250:
    .asciz "strtok"
ln251:
    .asciz "strtol"
ln252:
    .asciz "strtold"
ln253:
    .asciz "strtoll"
ln254:
    .asciz "strtoul"
ln255:
    .asciz "strtoull"
ln256:
    .asciz "strtoumax"
ln257:
    .asciz "strxfrm"
ln258:
    .asciz "swprintf"
ln259:
    .asciz "sysconf"
ln260:
    .asciz "system"
ln261:
    .asciz "time"
ln262:
    .asciz "time_now"
ln263:
    .asciz "times"
ln264:
    .asciz "timespec_get"
ln265:
    .asciz "tmpfile"
ln266:
    .asciz "tmpnam"
ln267:
    .asciz "toascii"
ln268:
    .asciz "tolower"
ln269:
    .asciz "toupper"
ln270:
    .asciz "towlower"
ln271:
    .asciz "towupper"
ln272:
    .asciz "truncate"
ln273:
    .asciz "tty_clear"
ln274:
    .asciz "tty_getsize"
ln275:
    .asciz "tty_raw"
ln276:
    .asciz "umask"
ln277:
    .asciz "umount"
ln278:
    .asciz "ungetc"
ln279:
    .asciz "unlink"
ln280:
    .asciz "unsetenv"
ln281:
    .asciz "uptime_ms"
ln282:
    .asciz "usleep"
ln283:
    .asciz "utime"
ln284:
    .asciz "verboseboot"
ln285:
    .asciz "vfprintf"
ln286:
    .asciz "vfscanf"
ln287:
    .asciz "video_info"
ln288:
    .asciz "vprintf"
ln289:
    .asciz "vscanf"
ln290:
    .asciz "vsnprintf"
ln291:
    .asciz "vsprintf"
ln292:
    .asciz "vsscanf"
ln293:
    .asciz "vswprintf"
ln294:
    .asciz "wait"
ln295:
    .asciz "waitpid"
ln296:
    .asciz "wcscat"
ln297:
    .asciz "wcschr"
ln298:
    .asciz "wcscmp"
ln299:
    .asciz "wcscpy"
ln300:
    .asciz "wcscspn"
ln301:
    .asciz "wcslen"
ln302:
    .asciz "wcsncat"
ln303:
    .asciz "wcsncmp"
ln304:
    .asciz "wcsncpy"
ln305:
    .asciz "wcspbrk"
ln306:
    .asciz "wcsrchr"
ln307:
    .asciz "wcsspn"
ln308:
    .asciz "wcsstr"
ln309:
    .asciz "wcstod"
ln310:
    .asciz "wcstol"
ln311:
    .asciz "wcstoll"
ln312:
    .asciz "wcstombs"
ln313:
    .asciz "wcstoul"
ln314:
    .asciz "wcstoull"
ln315:
    .asciz "wctomb"
ln316:
    .asciz "wmemchr"
ln317:
    .asciz "wmemcmp"
ln318:
    .asciz "wmemcpy"
ln319:
    .asciz "wmemmove"
ln320:
    .asciz "wmemset"
ln321:
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
    .long 322
    .long libc_nomi
    .long libc_indirizzi

    .section .note.GNU-stack,"",@progbits
