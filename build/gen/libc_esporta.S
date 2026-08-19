/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * La tabella che libc.so mette a disposizione: 332 funzioni, ognuna col suo
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
    .asciz "diventa_root"
ln43:
    .asciz "dma_alloc"
ln44:
    .asciz "dup"
ln45:
    .asciz "dup2"
ln46:
    .asciz "execv"
ln47:
    .asciz "execvp"
ln48:
    .asciz "exit"
ln49:
    .asciz "fabs"
ln50:
    .asciz "fb_map"
ln51:
    .asciz "fchmod"
ln52:
    .asciz "fclose"
ln53:
    .asciz "fcntl"
ln54:
    .asciz "fdopen"
ln55:
    .asciz "feof"
ln56:
    .asciz "ferror"
ln57:
    .asciz "fflush"
ln58:
    .asciz "fgetc"
ln59:
    .asciz "fgetpos"
ln60:
    .asciz "fgets"
ln61:
    .asciz "fileno"
ln62:
    .asciz "fopen"
ln63:
    .asciz "fpathconf"
ln64:
    .asciz "fprintf"
ln65:
    .asciz "fputc"
ln66:
    .asciz "fputs"
ln67:
    .asciz "fread"
ln68:
    .asciz "free"
ln69:
    .asciz "freopen"
ln70:
    .asciz "frexp"
ln71:
    .asciz "fscanf"
ln72:
    .asciz "fseek"
ln73:
    .asciz "fsetpos"
ln74:
    .asciz "fsize"
ln75:
    .asciz "fstat"
ln76:
    .asciz "ftell"
ln77:
    .asciz "fwrite"
ln78:
    .asciz "getc"
ln79:
    .asciz "getchar"
ln80:
    .asciz "getconf"
ln81:
    .asciz "getcwd"
ln82:
    .asciz "getegid"
ln83:
    .asciz "getentropy"
ln84:
    .asciz "getenv"
ln85:
    .asciz "geteuid"
ln86:
    .asciz "getgid"
ln87:
    .asciz "getpagesize"
ln88:
    .asciz "getpid"
ln89:
    .asciz "getrandom"
ln90:
    .asciz "getrusage"
ln91:
    .asciz "gets"
ln92:
    .asciz "gettimeofday"
ln93:
    .asciz "getuid"
ln94:
    .asciz "gmtime"
ln95:
    .asciz "imaxabs"
ln96:
    .asciz "imaxdiv"
ln97:
    .asciz "interrompi"
ln98:
    .asciz "ioctl"
ln99:
    .asciz "ioport_bind"
ln100:
    .asciz "ioport_in"
ln101:
    .asciz "ioport_in16"
ln102:
    .asciz "ioport_in32"
ln103:
    .asciz "ioport_out"
ln104:
    .asciz "ioport_out16"
ln105:
    .asciz "ioport_out32"
ln106:
    .asciz "ipc_lookup"
ln107:
    .asciz "ipc_recv"
ln108:
    .asciz "ipc_recv_timeout"
ln109:
    .asciz "ipc_register"
ln110:
    .asciz "ipc_rimetti"
ln111:
    .asciz "ipc_send"
ln112:
    .asciz "irq_bind"
ln113:
    .asciz "irq_done"
ln114:
    .asciz "isalnum"
ln115:
    .asciz "isalpha"
ln116:
    .asciz "isascii"
ln117:
    .asciz "isatty"
ln118:
    .asciz "isblank"
ln119:
    .asciz "iscntrl"
ln120:
    .asciz "isdigit"
ln121:
    .asciz "isgraph"
ln122:
    .asciz "islower"
ln123:
    .asciz "isprint"
ln124:
    .asciz "ispunct"
ln125:
    .asciz "isspace"
ln126:
    .asciz "isupper"
ln127:
    .asciz "iswalnum"
ln128:
    .asciz "iswalpha"
ln129:
    .asciz "iswblank"
ln130:
    .asciz "iswcntrl"
ln131:
    .asciz "iswdigit"
ln132:
    .asciz "iswgraph"
ln133:
    .asciz "iswlower"
ln134:
    .asciz "iswprint"
ln135:
    .asciz "iswpunct"
ln136:
    .asciz "iswspace"
ln137:
    .asciz "iswupper"
ln138:
    .asciz "iswxdigit"
ln139:
    .asciz "isxdigit"
ln140:
    .asciz "labs"
ln141:
    .asciz "ldexp"
ln142:
    .asciz "ldiv"
ln143:
    .asciz "listdir"
ln144:
    .asciz "listdir_from"
ln145:
    .asciz "llabs"
ln146:
    .asciz "lldiv"
ln147:
    .asciz "localeconv"
ln148:
    .asciz "localtime"
ln149:
    .asciz "log_seriale"
ln150:
    .asciz "longjmp"
ln151:
    .asciz "lseek"
ln152:
    .asciz "lstat"
ln153:
    .asciz "malloc"
ln154:
    .asciz "mblen"
ln155:
    .asciz "mbrtowc"
ln156:
    .asciz "mbstowcs"
ln157:
    .asciz "mbtowc"
ln158:
    .asciz "memalign"
ln159:
    .asciz "memchr"
ln160:
    .asciz "memcmp"
ln161:
    .asciz "memcpy"
ln162:
    .asciz "meminfo"
ln163:
    .asciz "memmove"
ln164:
    .asciz "memset"
ln165:
    .asciz "mkdir"
ln166:
    .asciz "mkstemp"
ln167:
    .asciz "mktemp"
ln168:
    .asciz "mktime"
ln169:
    .asciz "mmap"
ln170:
    .asciz "mmio_map"
ln171:
    .asciz "modo_testo"
ln172:
    .asciz "mount"
ln173:
    .asciz "mountinfo"
ln174:
    .asciz "munmap"
ln175:
    .asciz "nanosleep"
ln176:
    .asciz "nome_utente"
ln177:
    .asciz "open"
ln178:
    .asciz "opendir"
ln179:
    .asciz "osversion"
ln180:
    .asciz "partwrite"
ln181:
    .asciz "pathconf"
ln182:
    .asciz "pclose"
ln183:
    .asciz "perror"
ln184:
    .asciz "pipe"
ln185:
    .asciz "poll"
ln186:
    .asciz "popen"
ln187:
    .asciz "posix_memalign"
ln188:
    .asciz "printf"
ln189:
    .asciz "procinfo"
ln190:
    .asciz "pty_apri"
ln191:
    .asciz "pty_ctl"
ln192:
    .asciz "putc"
ln193:
    .asciz "putchar"
ln194:
    .asciz "putenv"
ln195:
    .asciz "puts"
ln196:
    .asciz "qsort"
ln197:
    .asciz "quick_exit"
ln198:
    .asciz "raise"
ln199:
    .asciz "rand"
ln200:
    .asciz "read"
ln201:
    .asciz "readdir"
ln202:
    .asciz "realloc"
ln203:
    .asciz "realpath"
ln204:
    .asciz "reboot"
ln205:
    .asciz "remove"
ln206:
    .asciz "rename"
ln207:
    .asciz "rewind"
ln208:
    .asciz "rewinddir"
ln209:
    .asciz "rmdir"
ln210:
    .asciz "sbrk"
ln211:
    .asciz "scanf"
ln212:
    .asciz "sched_yield"
ln213:
    .asciz "select"
ln214:
    .asciz "setbuf"
ln215:
    .asciz "setenv"
ln216:
    .asciz "setjmp"
ln217:
    .asciz "setlocale"
ln218:
    .asciz "setuid"
ln219:
    .asciz "setvbuf"
ln220:
    .asciz "sha256"
ln221:
    .asciz "sha256_esa"
ln222:
    .asciz "shm_apri"
ln223:
    .asciz "shm_chiudi"
ln224:
    .asciz "signal"
ln225:
    .asciz "sleep"
ln226:
    .asciz "snprintf"
ln227:
    .asciz "spawn"
ln228:
    .asciz "spawn_ex"
ln229:
    .asciz "spawn_su_console"
ln230:
    .asciz "sprintf"
ln231:
    .asciz "sqrt"
ln232:
    .asciz "srand"
ln233:
    .asciz "sscanf"
ln234:
    .asciz "stat"
ln235:
    .asciz "statperm"
ln236:
    .asciz "statraw"
ln237:
    .asciz "strcasecmp"
ln238:
    .asciz "strcat"
ln239:
    .asciz "strchr"
ln240:
    .asciz "strcmp"
ln241:
    .asciz "strcoll"
ln242:
    .asciz "strcpy"
ln243:
    .asciz "strcspn"
ln244:
    .asciz "strdup"
ln245:
    .asciz "strerror"
ln246:
    .asciz "strftime"
ln247:
    .asciz "strlen"
ln248:
    .asciz "strncasecmp"
ln249:
    .asciz "strncat"
ln250:
    .asciz "strncmp"
ln251:
    .asciz "strncpy"
ln252:
    .asciz "strpbrk"
ln253:
    .asciz "strrchr"
ln254:
    .asciz "strsignal"
ln255:
    .asciz "strspn"
ln256:
    .asciz "strstr"
ln257:
    .asciz "strtod"
ln258:
    .asciz "strtof"
ln259:
    .asciz "strtoimax"
ln260:
    .asciz "strtok"
ln261:
    .asciz "strtol"
ln262:
    .asciz "strtold"
ln263:
    .asciz "strtoll"
ln264:
    .asciz "strtoul"
ln265:
    .asciz "strtoull"
ln266:
    .asciz "strtoumax"
ln267:
    .asciz "strxfrm"
ln268:
    .asciz "swprintf"
ln269:
    .asciz "sysconf"
ln270:
    .asciz "system"
ln271:
    .asciz "time"
ln272:
    .asciz "time_now"
ln273:
    .asciz "times"
ln274:
    .asciz "timespec_get"
ln275:
    .asciz "tmpfile"
ln276:
    .asciz "tmpnam"
ln277:
    .asciz "toascii"
ln278:
    .asciz "tolower"
ln279:
    .asciz "toupper"
ln280:
    .asciz "towlower"
ln281:
    .asciz "towupper"
ln282:
    .asciz "truncate"
ln283:
    .asciz "tty_clear"
ln284:
    .asciz "tty_getsize"
ln285:
    .asciz "tty_raw"
ln286:
    .asciz "umask"
ln287:
    .asciz "umount"
ln288:
    .asciz "ungetc"
ln289:
    .asciz "unlink"
ln290:
    .asciz "unsetenv"
ln291:
    .asciz "uptime_ms"
ln292:
    .asciz "usleep"
ln293:
    .asciz "utime"
ln294:
    .asciz "verboseboot"
ln295:
    .asciz "vfprintf"
ln296:
    .asciz "vfscanf"
ln297:
    .asciz "video_info"
ln298:
    .asciz "vprintf"
ln299:
    .asciz "vscanf"
ln300:
    .asciz "vsnprintf"
ln301:
    .asciz "vsprintf"
ln302:
    .asciz "vsscanf"
ln303:
    .asciz "vswprintf"
ln304:
    .asciz "wait"
ln305:
    .asciz "waitpid"
ln306:
    .asciz "wcscat"
ln307:
    .asciz "wcschr"
ln308:
    .asciz "wcscmp"
ln309:
    .asciz "wcscpy"
ln310:
    .asciz "wcscspn"
ln311:
    .asciz "wcslen"
ln312:
    .asciz "wcsncat"
ln313:
    .asciz "wcsncmp"
ln314:
    .asciz "wcsncpy"
ln315:
    .asciz "wcspbrk"
ln316:
    .asciz "wcsrchr"
ln317:
    .asciz "wcsspn"
ln318:
    .asciz "wcsstr"
ln319:
    .asciz "wcstod"
ln320:
    .asciz "wcstol"
ln321:
    .asciz "wcstoll"
ln322:
    .asciz "wcstombs"
ln323:
    .asciz "wcstoul"
ln324:
    .asciz "wcstoull"
ln325:
    .asciz "wctomb"
ln326:
    .asciz "wmemchr"
ln327:
    .asciz "wmemcmp"
ln328:
    .asciz "wmemcpy"
ln329:
    .asciz "wmemmove"
ln330:
    .asciz "wmemset"
ln331:
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
    .long 332
    .long libc_nomi
    .long libc_indirizzi

    .section .note.GNU-stack,"",@progbits
