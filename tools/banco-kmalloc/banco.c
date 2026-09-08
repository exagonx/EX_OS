/* =============================================================================
 * banco.c — kmalloc.c del kernel, messo alla prova sull'ospite
 *
 * Serve a rispondere a una domanda sola, senza QEMU e senza aspettare che il
 * caso si ripeta: quando lo heap e' fatto di due tratti NON attaccati, kfree
 * legge fuori? La RAM finta e' un mmap; subito dopo l'ultimo tratto c'e' una
 * pagina PROT_NONE, che e' la stessa cosa che «oltre la RAM, non mappato».
 * Se kfree ci mette il naso, il processo prende SIGSEGV — cioe' esattamente
 * il «Page Fault non gestito in ring0» di @DIF-PANIC, qui riprodotto a
 * comando invece che una volta su quattordici.
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <stdint.h>

static int g_verboso = 0;

void klog(int liv, const char *fmt, ...)
{
    va_list ap;
    if (!g_verboso && liv < 2) return;
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); putchar('\n');
}

void kpanic(const char *fmt, ...)
{
    va_list ap;
    printf("KPANIC: "); va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n"); exit(3);
}

/* -------------------------------------------------------------------------
 * La RAM finta e i tratti che il PMM consegna.
 *
 * Quattro tratti da 64 pagine, e SUBITO DOPO OGNI TRATTO una pagina
 * PROT_NONE. E' il rivelatore: dentro EX-OS quel che sta dopo un tratto e'
 * memoria di qualcun altro — leggerla non fa rumore, corrompe e basta — o,
 * quando il tratto arriva in cima alla RAM, non e' mappato affatto ed e' il
 * panic di @DIF-PANIC. Qui si sceglie la seconda: cosi' la lettura che non
 * doveva esserci si vede subito, invece di doverla aspettare.
 *
 * ! I TRATTI NON SI TOCCANO, e non e' una cattiveria del banco: e' quel che
 * fa pmm_alloc_pages_kernel quando fra un'espansione e l'altra qualcun altro
 * (uno stack di kernel, una page table) ha preso le pagine di mezzo.
 * ------------------------------------------------------------------------- */
#define PAGINA        4096u
#define TRATTI        4
#define PAGINE_TRATTO 64u

static uint8_t *g_ram;
static int      g_dato;

/* Il tratto k comincia a k * (PAGINE_TRATTO + 1) pagine: l'ultima pagina di
 * ogni gruppo e' quella cieca. */
static uint8_t *tratto(int k)
{
    return g_ram + (uint32_t)k * (PAGINE_TRATTO + 1) * PAGINA;
}

uint32_t pmm_alloc_pages_kernel(uint32_t count)
{
    if (g_dato >= TRATTI)      return 0;
    if (count > PAGINE_TRATTO) return 0;

    /* ! SI CONSEGNA IN CIMA AL TRATTO, non in fondo: cosi' la fine di quel
     * che il chiamante riceve coincide con l'inizio della pagina cieca, che
     * e' esattamente il caso da provare — l'ultimo blocco della regione. */
    return (uint32_t)(uintptr_t)(tratto(g_dato++) +
                                 (PAGINE_TRATTO - count) * PAGINA);
}

#include "kmalloc.c"

/* -------------------------------------------------------------------------
 * La prova
 * ------------------------------------------------------------------------- */
static void ram_prepara(void)
{
    size_t tot = (size_t)TRATTI * (PAGINE_TRATTO + 1) * PAGINA;
    int    k;

    g_ram = mmap(NULL, tot, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (g_ram == MAP_FAILED) { perror("mmap"); exit(2); }
    memset(g_ram, 0, tot);

    /* Con BANCO_CIECA_LEGGIBILE=1 la pagina dopo ogni tratto resta LEGGIBILE.
     * Serve alla controprova: il codice di prima non muore piu' — legge zeri,
     * la firma non vale, e rinuncia in silenzio alla coalescenza — cosi' si
     * puo' arrivare in fondo e contare quanto heap si e' perso per strada.
     * E' anche il caso piu' comune dentro EX-OS, dove dopo un tratto c'e'
     * quasi sempre memoria mappata di qualcun altro: il panic e' la faccia
     * rara del difetto, la frammentazione e' quella di tutti i giorni. */
    if (getenv("BANCO_CIECA_LEGGIBILE") == NULL)
        for (k = 0; k < TRATTI; k++)
            if (mprotect(tratto(k) + PAGINE_TRATTO * PAGINA, PAGINA,
                         PROT_NONE) != 0) {
                perror("mprotect"); exit(2);
            }

    printf("RAM finta: %d tratti da %u pagine, ognuno seguito da una pagina "
           "cieca\n", TRATTI, PAGINE_TRATTO);
    for (k = 0; k < TRATTI; k++)
        printf("  tratto %d: 0x%08x  cieca a 0x%08x\n", k,
               (uint32_t)(uintptr_t)tratto(k),
               (uint32_t)(uintptr_t)(tratto(k) + PAGINE_TRATTO * PAGINA));
}

int main(int argc, char **argv)
{
    void *a[512];
    int   i, n = 0;

    (void)argv;
    g_verboso = (argc > 1);
    setvbuf(stdout, NULL, _IONBF, 0);
    ram_prepara();

    kmalloc_init();

    /* Si prende finche' ce n'e': cosi' i tratti si riempiono tutti, e
     * l'ultimo blocco di ognuno arriva a filo della pagina cieca. */
    for (i = 0; i < 512; i++) {
        void *p = kmalloc(1024);
        if (!p) break;
        a[n++] = p;
    }
    printf("blocchi presi: %d\n", n);

    /* Con BANCO_MODO=rotta si riproduce @DIF-PANIC alla lettera: a un blocco
     * si SOVRASCRIVE la misura nell'intestazione con un numero assurdo — che
     * e' quel che fa chi scrive oltre il proprio blocco — e poi lo si libera.
     *
     * Prima del 7 settembre 2026 kfree calcolava `blocco + intestazione +
     * misura`, ci andava a leggere la firma, e moriva li':
     *
     *     Page Fault non gestito in ring0 a 0x0421308c (EIP=0x00109610)
     *     cmpl $0xdeadbeef,0x4(%ebx,%eax,1)     <- questa riga
     *
     * Adesso la misura non ci sta nella regione, kfree non la segue, e lo
     * DICE — perche' la misura sbagliata resta, ed e' l'unico indizio di chi
     * l'ha scritta. */
    {
        const char *modo = getenv("BANCO_MODO");

        if (modo != NULL && modo[0] == 'r' && n > 2) {
            /* ! QUESTO -7 SEGUE L'INTESTAZIONE DI kmalloc.c, e va cambiato
             * con lei: sono le parole {size, magic, flags, chiesti, chi,
             * prev, next}, e `size` e' la prima. Era -5 finche' i campi erano
             * cinque; l'8 settembre 2026 sono diventati sette (il canarino ha
             * portato `chiesti` e `chi`) e il banco, senza questa riga
             * aggiornata, rompeva il campo sbagliato e la prova non provava
             * piu' niente — senza dirlo. */
            uint32_t *intestazione = (uint32_t *)a[1] - 7;   /* size, magic, ... */

            printf("\nrompo l'intestazione di %p: misura %u -> %u\n",
                   a[1], intestazione[0], 0x4206000u);
            intestazione[0] = 0x4206000u;      /* ~66 MB in una regione da 256 KB */
            kfree(a[1]);
            printf("SOPRAVVISSUTO alla misura inventata\n");
            kmalloc_stats();
            return 0;
        }
    }

    /* Con BANCO_MODO=oltre si prova IL CANARINO, cioe' la parte di @DIF-PANIC
     * che il 7 settembre 2026 era rimasta aperta: l'intestazione rotta l'ha
     * scritta la lettura fuori regione, o qualcun altro che scrive oltre il
     * proprio blocco?
     *
     * Qui il colpevole c'e' e si conosce: si scrive UN BYTE oltre i byte
     * chiesti — il caso piu' comune e piu' invisibile, perche' finisce dentro
     * l'arrotondamento a otto e non tocca nessuna intestazione. Senza
     * canarino non se ne accorge nessuno, mai. Con il canarino, kfree lo dice
     * e dice anche chi aveva allocato quel blocco. */
    {
        const char *modo = getenv("BANCO_MODO");

        if (modo != NULL && modo[0] == 'o' && n > 3) {
            unsigned char *p = (unsigned char *)a[2];

            printf("\nscrivo UN byte oltre i 1024 chiesti di %p\n", a[2]);
            p[1024] = 0x41;          /* il byte di troppo */
            kfree(a[2]);
            printf("\nE il controllo completo trova anche quelli che nessuno libera:\n");
            p = (unsigned char *)a[3];
            /* ! DENTRO I QUATTRO BYTE DEL CANARINO, cioe' 1024..1027. Un byte
             * solo scritto piu' in la' — a 1030, dentro l'arrotondamento —
             * non lo vedrebbe nessuno, ed e' il limite onesto di questa
             * difesa: prende chi sconfina di seguito (un memcpy, una stringa:
             * cioe' tutti i casi veri), non chi tocca un byte a caso in
             * mezzo al riempimento. */
            p[1026] = 0x42;          /* mai liberato: lo trova solo la passata */
            kmalloc_verifica();
            kmalloc_stats();
            return 0;
        }
    }

    /* Con BANCO_ORDINE=primo si libera nell'ordine di presa, che e' il caso
     * che mette alla prova la coalescenza ALL'INDIETRO (quella in avanti li'
     * non trova mai un vicino gia' libero). Serve a verificare che le regioni
     * non facciano perdere nemmeno un byte rispetto a prima. */
    {
        const char *ord = getenv("BANCO_ORDINE");

        if (ord != NULL && ord[0] == 'p') {
            for (i = 0; i < n; i++) kfree(a[i]);
            printf("\nLIBERATI TUTTI (dal primo) SENZA USCIRE DAI TRATTI\n");
            kmalloc_stats();
            return 0;
        }
    }

    /* ULTIMO PER PRIMO. Il primo kfree e' quello del blocco piu' in alto di
     * un tratto: il suo «successivo fisicamente adiacente» e' la pagina
     * cieca, e la coalescenza in avanti va a leggerne la firma. Subito dopo
     * tocca ai blocchi degli altri tratti, e li' e' la scansione
     * all'indietro — che parte dal primo blocco dello HEAP — a uscire. */
    for (i = n - 1; i >= 0; i--) kfree(a[i]);

    printf("\nLIBERATI TUTTI SENZA USCIRE DAI TRATTI\n");
    kmalloc_stats();
    return 0;
}
