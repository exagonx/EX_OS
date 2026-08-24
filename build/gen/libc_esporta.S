/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * La tabella che libc.so mette a disposizione: 334 funzioni, ognuna col suo
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
    .asciz "ipc_attendi"
ln107:
    .asciz "ipc_lookup"
ln108:
    .asciz "ipc_recv"
ln109:
    .asciz "ipc_recv_timeout"
ln110:
    .asciz "ipc_register"
ln111:
    .asciz "ipc_rimetti"
ln112:
    .asciz "ipc_send"
ln113:
    .asciz "irq_bind"
ln114:
    .asciz "irq_done"
ln115:
    .asciz "isalnum"
ln116:
    .asciz "isalpha"
ln117:
    .asciz "isascii"
ln118:
    .asciz "isatty"
ln119:
    .asciz "isblank"
ln120:
    .asciz "iscntrl"
ln121:
    .asciz "isdigit"
ln122:
    .asciz "isgraph"
ln123:
    .asciz "islower"
ln124:
    .asciz "isprint"
ln125:
    .asciz "ispunct"
ln126:
    .asciz "isspace"
ln127:
    .asciz "isupper"
ln128:
    .asciz "iswalnum"
ln129:
    .asciz "iswalpha"
ln130:
    .asciz "iswblank"
ln131:
    .asciz "iswcntrl"
ln132:
    .asciz "iswdigit"
ln133:
    .asciz "iswgraph"
ln134:
    .asciz "iswlower"
ln135:
    .asciz "iswprint"
ln136:
    .asciz "iswpunct"
ln137:
    .asciz "iswspace"
ln138:
    .asciz "iswupper"
ln139:
    .asciz "iswxdigit"
ln140:
    .asciz "isxdigit"
ln141:
    .asciz "labs"
ln142:
    .asciz "ldexp"
ln143:
    .asciz "ldiv"
ln144:
    .asciz "listdir"
ln145:
    .asciz "listdir_from"
ln146:
    .asciz "llabs"
ln147:
    .asciz "lldiv"
ln148:
    .asciz "localeconv"
ln149:
    .asciz "localtime"
ln150:
    .asciz "log_seriale"
ln151:
    .asciz "longjmp"
ln152:
    .asciz "lseek"
ln153:
    .asciz "lstat"
ln154:
    .asciz "malloc"
ln155:
    .asciz "mblen"
ln156:
    .asciz "mbrtowc"
ln157:
    .asciz "mbstowcs"
ln158:
    .asciz "mbtowc"
ln159:
    .asciz "memalign"
ln160:
    .asciz "memchr"
ln161:
    .asciz "memcmp"
ln162:
    .asciz "memcpy"
ln163:
    .asciz "meminfo"
ln164:
    .asciz "memmove"
ln165:
    .asciz "memset"
ln166:
    .asciz "mkdir"
ln167:
    .asciz "mkstemp"
ln168:
    .asciz "mktemp"
ln169:
    .asciz "mktime"
ln170:
    .asciz "mmap"
ln171:
    .asciz "mmio_map"
ln172:
    .asciz "modo_testo"
ln173:
    .asciz "mount"
ln174:
    .asciz "mountinfo"
ln175:
    .asciz "munmap"
ln176:
    .asciz "nanosleep"
ln177:
    .asciz "nome_utente"
ln178:
    .asciz "open"
ln179:
    .asciz "opendir"
ln180:
    .asciz "osversion"
ln181:
    .asciz "partwrite"
ln182:
    .asciz "pathconf"
ln183:
    .asciz "pclose"
ln184:
    .asciz "perror"
ln185:
    .asciz "pipe"
ln186:
    .asciz "poll"
ln187:
    .asciz "popen"
ln188:
    .asciz "posix_memalign"
ln189:
    .asciz "printf"
ln190:
    .asciz "procinfo"
ln191:
    .asciz "pty_apri"
ln192:
    .asciz "pty_ctl"
ln193:
    .asciz "putc"
ln194:
    .asciz "putchar"
ln195:
    .asciz "putenv"
ln196:
    .asciz "puts"
ln197:
    .asciz "qsort"
ln198:
    .asciz "quick_exit"
ln199:
    .asciz "raise"
ln200:
    .asciz "rand"
ln201:
    .asciz "read"
ln202:
    .asciz "readdir"
ln203:
    .asciz "realloc"
ln204:
    .asciz "realpath"
ln205:
    .asciz "reboot"
ln206:
    .asciz "remove"
ln207:
    .asciz "rename"
ln208:
    .asciz "rewind"
ln209:
    .asciz "rewinddir"
ln210:
    .asciz "rmdir"
ln211:
    .asciz "sbrk"
ln212:
    .asciz "scanf"
ln213:
    .asciz "sched_yield"
ln214:
    .asciz "select"
ln215:
    .asciz "setbuf"
ln216:
    .asciz "setenv"
ln217:
    .asciz "setjmp"
ln218:
    .asciz "setlocale"
ln219:
    .asciz "setuid"
ln220:
    .asciz "setvbuf"
ln221:
    .asciz "sha256"
ln222:
    .asciz "sha256_esa"
ln223:
    .asciz "shm_apri"
ln224:
    .asciz "shm_chiudi"
ln225:
    .asciz "signal"
ln226:
    .asciz "sleep"
ln227:
    .asciz "snprintf"
ln228:
    .asciz "spawn"
ln229:
    .asciz "spawn_ex"
ln230:
    .asciz "spawn_su_console"
ln231:
    .asciz "spawn_utente"
ln232:
    .asciz "sprintf"
ln233:
    .asciz "sqrt"
ln234:
    .asciz "srand"
ln235:
    .asciz "sscanf"
ln236:
    .asciz "stat"
ln237:
    .asciz "statperm"
ln238:
    .asciz "statraw"
ln239:
    .asciz "strcasecmp"
ln240:
    .asciz "strcat"
ln241:
    .asciz "strchr"
ln242:
    .asciz "strcmp"
ln243:
    .asciz "strcoll"
ln244:
    .asciz "strcpy"
ln245:
    .asciz "strcspn"
ln246:
    .asciz "strdup"
ln247:
    .asciz "strerror"
ln248:
    .asciz "strftime"
ln249:
    .asciz "strlen"
ln250:
    .asciz "strncasecmp"
ln251:
    .asciz "strncat"
ln252:
    .asciz "strncmp"
ln253:
    .asciz "strncpy"
ln254:
    .asciz "strpbrk"
ln255:
    .asciz "strrchr"
ln256:
    .asciz "strsignal"
ln257:
    .asciz "strspn"
ln258:
    .asciz "strstr"
ln259:
    .asciz "strtod"
ln260:
    .asciz "strtof"
ln261:
    .asciz "strtoimax"
ln262:
    .asciz "strtok"
ln263:
    .asciz "strtol"
ln264:
    .asciz "strtold"
ln265:
    .asciz "strtoll"
ln266:
    .asciz "strtoul"
ln267:
    .asciz "strtoull"
ln268:
    .asciz "strtoumax"
ln269:
    .asciz "strxfrm"
ln270:
    .asciz "swprintf"
ln271:
    .asciz "sysconf"
ln272:
    .asciz "system"
ln273:
    .asciz "time"
ln274:
    .asciz "time_now"
ln275:
    .asciz "times"
ln276:
    .asciz "timespec_get"
ln277:
    .asciz "tmpfile"
ln278:
    .asciz "tmpnam"
ln279:
    .asciz "toascii"
ln280:
    .asciz "tolower"
ln281:
    .asciz "toupper"
ln282:
    .asciz "towlower"
ln283:
    .asciz "towupper"
ln284:
    .asciz "truncate"
ln285:
    .asciz "tty_clear"
ln286:
    .asciz "tty_getsize"
ln287:
    .asciz "tty_raw"
ln288:
    .asciz "umask"
ln289:
    .asciz "umount"
ln290:
    .asciz "ungetc"
ln291:
    .asciz "unlink"
ln292:
    .asciz "unsetenv"
ln293:
    .asciz "uptime_ms"
ln294:
    .asciz "usleep"
ln295:
    .asciz "utime"
ln296:
    .asciz "verboseboot"
ln297:
    .asciz "vfprintf"
ln298:
    .asciz "vfscanf"
ln299:
    .asciz "video_info"
ln300:
    .asciz "vprintf"
ln301:
    .asciz "vscanf"
ln302:
    .asciz "vsnprintf"
ln303:
    .asciz "vsprintf"
ln304:
    .asciz "vsscanf"
ln305:
    .asciz "vswprintf"
ln306:
    .asciz "wait"
ln307:
    .asciz "waitpid"
ln308:
    .asciz "wcscat"
ln309:
    .asciz "wcschr"
ln310:
    .asciz "wcscmp"
ln311:
    .asciz "wcscpy"
ln312:
    .asciz "wcscspn"
ln313:
    .asciz "wcslen"
ln314:
    .asciz "wcsncat"
ln315:
    .asciz "wcsncmp"
ln316:
    .asciz "wcsncpy"
ln317:
    .asciz "wcspbrk"
ln318:
    .asciz "wcsrchr"
ln319:
    .asciz "wcsspn"
ln320:
    .asciz "wcsstr"
ln321:
    .asciz "wcstod"
ln322:
    .asciz "wcstol"
ln323:
    .asciz "wcstoll"
ln324:
    .asciz "wcstombs"
ln325:
    .asciz "wcstoul"
ln326:
    .asciz "wcstoull"
ln327:
    .asciz "wctomb"
ln328:
    .asciz "wmemchr"
ln329:
    .asciz "wmemcmp"
ln330:
    .asciz "wmemcpy"
ln331:
    .asciz "wmemmove"
ln332:
    .asciz "wmemset"
ln333:
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
    .long 334
    .long libc_nomi
    .long libc_indirizzi

    .section .note.GNU-stack,"",@progbits
