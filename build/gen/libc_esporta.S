/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * La tabella che libc.so mette a disposizione: 325 funzioni, ognuna col suo
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
    .asciz "ioctl"
ln97:
    .asciz "ioport_bind"
ln98:
    .asciz "ioport_in"
ln99:
    .asciz "ioport_in16"
ln100:
    .asciz "ioport_in32"
ln101:
    .asciz "ioport_out"
ln102:
    .asciz "ioport_out16"
ln103:
    .asciz "ioport_out32"
ln104:
    .asciz "ipc_lookup"
ln105:
    .asciz "ipc_recv"
ln106:
    .asciz "ipc_recv_timeout"
ln107:
    .asciz "ipc_register"
ln108:
    .asciz "ipc_send"
ln109:
    .asciz "irq_bind"
ln110:
    .asciz "irq_done"
ln111:
    .asciz "isalnum"
ln112:
    .asciz "isalpha"
ln113:
    .asciz "isascii"
ln114:
    .asciz "isatty"
ln115:
    .asciz "isblank"
ln116:
    .asciz "iscntrl"
ln117:
    .asciz "isdigit"
ln118:
    .asciz "isgraph"
ln119:
    .asciz "islower"
ln120:
    .asciz "isprint"
ln121:
    .asciz "ispunct"
ln122:
    .asciz "isspace"
ln123:
    .asciz "isupper"
ln124:
    .asciz "iswalnum"
ln125:
    .asciz "iswalpha"
ln126:
    .asciz "iswblank"
ln127:
    .asciz "iswcntrl"
ln128:
    .asciz "iswdigit"
ln129:
    .asciz "iswgraph"
ln130:
    .asciz "iswlower"
ln131:
    .asciz "iswprint"
ln132:
    .asciz "iswpunct"
ln133:
    .asciz "iswspace"
ln134:
    .asciz "iswupper"
ln135:
    .asciz "iswxdigit"
ln136:
    .asciz "isxdigit"
ln137:
    .asciz "labs"
ln138:
    .asciz "ldexp"
ln139:
    .asciz "ldiv"
ln140:
    .asciz "listdir"
ln141:
    .asciz "listdir_from"
ln142:
    .asciz "llabs"
ln143:
    .asciz "lldiv"
ln144:
    .asciz "localeconv"
ln145:
    .asciz "localtime"
ln146:
    .asciz "log_seriale"
ln147:
    .asciz "longjmp"
ln148:
    .asciz "lseek"
ln149:
    .asciz "lstat"
ln150:
    .asciz "malloc"
ln151:
    .asciz "mblen"
ln152:
    .asciz "mbrtowc"
ln153:
    .asciz "mbstowcs"
ln154:
    .asciz "mbtowc"
ln155:
    .asciz "memalign"
ln156:
    .asciz "memchr"
ln157:
    .asciz "memcmp"
ln158:
    .asciz "memcpy"
ln159:
    .asciz "meminfo"
ln160:
    .asciz "memmove"
ln161:
    .asciz "memset"
ln162:
    .asciz "mkdir"
ln163:
    .asciz "mkstemp"
ln164:
    .asciz "mktemp"
ln165:
    .asciz "mktime"
ln166:
    .asciz "mmap"
ln167:
    .asciz "mmio_map"
ln168:
    .asciz "modo_testo"
ln169:
    .asciz "mount"
ln170:
    .asciz "mountinfo"
ln171:
    .asciz "munmap"
ln172:
    .asciz "nanosleep"
ln173:
    .asciz "open"
ln174:
    .asciz "opendir"
ln175:
    .asciz "osversion"
ln176:
    .asciz "partwrite"
ln177:
    .asciz "pathconf"
ln178:
    .asciz "pclose"
ln179:
    .asciz "perror"
ln180:
    .asciz "pipe"
ln181:
    .asciz "poll"
ln182:
    .asciz "popen"
ln183:
    .asciz "posix_memalign"
ln184:
    .asciz "printf"
ln185:
    .asciz "procinfo"
ln186:
    .asciz "putc"
ln187:
    .asciz "putchar"
ln188:
    .asciz "putenv"
ln189:
    .asciz "puts"
ln190:
    .asciz "qsort"
ln191:
    .asciz "quick_exit"
ln192:
    .asciz "raise"
ln193:
    .asciz "rand"
ln194:
    .asciz "read"
ln195:
    .asciz "readdir"
ln196:
    .asciz "realloc"
ln197:
    .asciz "realpath"
ln198:
    .asciz "reboot"
ln199:
    .asciz "remove"
ln200:
    .asciz "rename"
ln201:
    .asciz "rewind"
ln202:
    .asciz "rewinddir"
ln203:
    .asciz "rmdir"
ln204:
    .asciz "sbrk"
ln205:
    .asciz "scanf"
ln206:
    .asciz "sched_yield"
ln207:
    .asciz "select"
ln208:
    .asciz "setbuf"
ln209:
    .asciz "setenv"
ln210:
    .asciz "setjmp"
ln211:
    .asciz "setlocale"
ln212:
    .asciz "setuid"
ln213:
    .asciz "setvbuf"
ln214:
    .asciz "sha256"
ln215:
    .asciz "sha256_esa"
ln216:
    .asciz "shm_apri"
ln217:
    .asciz "shm_chiudi"
ln218:
    .asciz "signal"
ln219:
    .asciz "sleep"
ln220:
    .asciz "snprintf"
ln221:
    .asciz "spawn"
ln222:
    .asciz "spawn_ex"
ln223:
    .asciz "spawn_su_console"
ln224:
    .asciz "sprintf"
ln225:
    .asciz "sqrt"
ln226:
    .asciz "srand"
ln227:
    .asciz "sscanf"
ln228:
    .asciz "stat"
ln229:
    .asciz "statraw"
ln230:
    .asciz "strcasecmp"
ln231:
    .asciz "strcat"
ln232:
    .asciz "strchr"
ln233:
    .asciz "strcmp"
ln234:
    .asciz "strcoll"
ln235:
    .asciz "strcpy"
ln236:
    .asciz "strcspn"
ln237:
    .asciz "strdup"
ln238:
    .asciz "strerror"
ln239:
    .asciz "strftime"
ln240:
    .asciz "strlen"
ln241:
    .asciz "strncasecmp"
ln242:
    .asciz "strncat"
ln243:
    .asciz "strncmp"
ln244:
    .asciz "strncpy"
ln245:
    .asciz "strpbrk"
ln246:
    .asciz "strrchr"
ln247:
    .asciz "strsignal"
ln248:
    .asciz "strspn"
ln249:
    .asciz "strstr"
ln250:
    .asciz "strtod"
ln251:
    .asciz "strtof"
ln252:
    .asciz "strtoimax"
ln253:
    .asciz "strtok"
ln254:
    .asciz "strtol"
ln255:
    .asciz "strtold"
ln256:
    .asciz "strtoll"
ln257:
    .asciz "strtoul"
ln258:
    .asciz "strtoull"
ln259:
    .asciz "strtoumax"
ln260:
    .asciz "strxfrm"
ln261:
    .asciz "swprintf"
ln262:
    .asciz "sysconf"
ln263:
    .asciz "system"
ln264:
    .asciz "time"
ln265:
    .asciz "time_now"
ln266:
    .asciz "times"
ln267:
    .asciz "timespec_get"
ln268:
    .asciz "tmpfile"
ln269:
    .asciz "tmpnam"
ln270:
    .asciz "toascii"
ln271:
    .asciz "tolower"
ln272:
    .asciz "toupper"
ln273:
    .asciz "towlower"
ln274:
    .asciz "towupper"
ln275:
    .asciz "truncate"
ln276:
    .asciz "tty_clear"
ln277:
    .asciz "tty_getsize"
ln278:
    .asciz "tty_raw"
ln279:
    .asciz "umask"
ln280:
    .asciz "umount"
ln281:
    .asciz "ungetc"
ln282:
    .asciz "unlink"
ln283:
    .asciz "unsetenv"
ln284:
    .asciz "uptime_ms"
ln285:
    .asciz "usleep"
ln286:
    .asciz "utime"
ln287:
    .asciz "verboseboot"
ln288:
    .asciz "vfprintf"
ln289:
    .asciz "vfscanf"
ln290:
    .asciz "video_info"
ln291:
    .asciz "vprintf"
ln292:
    .asciz "vscanf"
ln293:
    .asciz "vsnprintf"
ln294:
    .asciz "vsprintf"
ln295:
    .asciz "vsscanf"
ln296:
    .asciz "vswprintf"
ln297:
    .asciz "wait"
ln298:
    .asciz "waitpid"
ln299:
    .asciz "wcscat"
ln300:
    .asciz "wcschr"
ln301:
    .asciz "wcscmp"
ln302:
    .asciz "wcscpy"
ln303:
    .asciz "wcscspn"
ln304:
    .asciz "wcslen"
ln305:
    .asciz "wcsncat"
ln306:
    .asciz "wcsncmp"
ln307:
    .asciz "wcsncpy"
ln308:
    .asciz "wcspbrk"
ln309:
    .asciz "wcsrchr"
ln310:
    .asciz "wcsspn"
ln311:
    .asciz "wcsstr"
ln312:
    .asciz "wcstod"
ln313:
    .asciz "wcstol"
ln314:
    .asciz "wcstoll"
ln315:
    .asciz "wcstombs"
ln316:
    .asciz "wcstoul"
ln317:
    .asciz "wcstoull"
ln318:
    .asciz "wctomb"
ln319:
    .asciz "wmemchr"
ln320:
    .asciz "wmemcmp"
ln321:
    .asciz "wmemcpy"
ln322:
    .asciz "wmemmove"
ln323:
    .asciz "wmemset"
ln324:
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
    .long 325
    .long libc_nomi
    .long libc_indirizzi

    .section .note.GNU-stack,"",@progbits
