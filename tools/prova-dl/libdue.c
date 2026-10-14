/* tools/prova-dl/libdue.c - the library libuno.so depends on (DT_NEEDED) */
int contatore = 40;                      /* data another library reaches through its GOT */
int doppio(int x) { return 2 * x; }
