/* lib/start.S — EX-OS
 * Entry point _start per programmi che usano la libc.
 *
 * Perché non è in libc.c:
 *   GCC non supporta __attribute__((naked)) su x86-32 — ignora l'attributo
 *   e genera comunque un prolog (push ebx; sub esp,N) che sposta ESP prima
 *   che il nostro codice legga argc/argv. Questo file assembly garantisce
 *   zero prolog tra il punto di ingresso e la lettura dello stack.
 *
 * Stack al momento dell'ingresso (costruito da sys_spawn):
 *   [esp+0]  = 0           (fake return address — _start non ritorna mai)
 *   [esp+4]  = argc        (int)
 *   [esp+8]  = argv        (char**)
 *   [esp+12] = envp        (char**, dal 0.150; vale NULL se il padre non
 *                           ha passato un ambiente)
 *
 * Convenzione: _start e' .weak → i programmi che definiscono la propria
 * _start (es. shell, hello) la sovrascrivono al link.
 */

.section .text
.global _start
.weak   _start
.type   _start, @function

_start:
    movl  4(%esp), %eax      /* argc */
    movl  8(%esp), %ecx      /* argv */
    movl 12(%esp), %edx      /* envp */
    pushl %edx               /* terzo argomento di _libc_start */
    pushl %ecx               /* secondo */
    pushl %eax               /* primo */
    call  _libc_start        /* chiama main(argc, argv) e poi exit() */
    hlt                      /* non raggiunto */
.size _start, .-_start

/* Indica al linker che questo file non richiede uno stack eseguibile */
.section .note.GNU-stack,"",@progbits
