/* tools/exilla/banco-heap.c — l allocatore della libc di EX-OS, provato sull host
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Lo compila e lo lancia tools/exilla/banco-heap.sh: un milione di malloc,
 * free, realloc e memalign a caso, con i dati di ogni blocco controllati e,
 * ogni 5000 operazioni, la lista per indirizzo e i cassetti verificati per
 * intero. Il codice provato e quello VERO, estratto da lib/libc.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#define SYS_SBRK 1
static char *g_arena, *g_brk; static size_t g_cap = 512u<<20;
static int32_t fake_sbrk(int incr){ char *v=g_brk; if (incr<0 && g_brk+incr<g_arena) return -1; if (g_brk+incr>g_arena+g_cap) return -12; g_brk+=incr; return (int32_t)(uintptr_t)v; }
#define _syscall1(n,x) fake_sbrk((int)(x))
void *mysbrk(int incr){ int32_t r=fake_sbrk(incr); return ((uint32_t)r>=0xFFFFF001u)?(void*)-1:(void*)(uintptr_t)r; }
#define sbrk mysbrk
#include "heap_estratto.c"
#undef sbrk
#define N 20000
static unsigned char *p[N]; static size_t sz[N]; static unsigned char pat[N];
static void riempi(int i){ memset(p[i], pat[i], sz[i]); }
static void verifica(int i){ for(size_t k=0;k<sz[i];k++) if(p[i][k]!=pat[i]){printf("DATI ROVINATI slot %d byte %zu\n",i,k);exit(1);} }
static void controlla(void){
  size_t liberi=0, nei=0; Blocco *b, *pr=NULL;
  for(b=heap_primo;b;b=b->succ){ if(b->prec!=pr){puts("prec errato");exit(1);} if(pr && (char*)pr+BLOCCO_HDR+pr->dim!=(char*)b){puts("buco/sovrapposizione");exit(1);} if(((uintptr_t)BLOCCO_DATI(b))&15){puts("non allineato");exit(1);} if(b->libero&&b->dim)liberi++; pr=b; }
  if(pr!=heap_ultimo){puts("ultimo errato");exit(1);}
  for(unsigned i=0;i<HEAP_CASSETTI;i++){ Blocco *q=NULL; for(b=g_cassetto[i];b;b=LEGAMI(b)->dopo){ if(!b->libero){puts("occupato in cassetto");exit(1);} if(cassetto_di(b->dim)!=i){puts("cassetto sbagliato");exit(1);} if(LEGAMI(b)->prima!=q){puts("legami errati");exit(1);} q=b; nei++; } }
  if(nei!=liberi){printf("liberi %zu, nei cassetti %zu\n",liberi,nei);exit(1);}
}
int main(void){
  g_arena=aligned_alloc(4096,g_cap); g_brk=g_arena; srand(12345);
  long ops=0, max_heap=0;
  for(ops=0;ops<1000000;ops++){
    int i=rand()%N, op=rand()%10;
    if(!p[i]){
      size_t s = (rand()%100<98) ? 1+rand()%1500 : 1+rand()%(1<<20);
      if(op==0){ size_t al=16u<<(rand()%9); p[i]=heap_memalign(al,s); if(p[i]&&((uintptr_t)p[i]&(al-1))){puts("memalign storto");return 1;} }
      else p[i]=heap_malloc(s);
      if(!p[i]){printf("malloc NULL a %ld\n",ops);return 1;}
      sz[i]=s; pat[i]=(unsigned char)rand(); riempi(i);
    } else if(op<6){ verifica(i); heap_free(p[i]); p[i]=NULL; }
    else { verifica(i); size_t s=(rand()%100<98)?1+rand()%1500:1+rand()%(1<<19); unsigned char *q=heap_realloc(p[i],s); if(!q){puts("realloc NULL");return 1;} size_t m=s<sz[i]?s:sz[i]; for(size_t k=0;k<m;k++) if(q[k]!=pat[i]){puts("realloc perde dati");return 1;} p[i]=q; sz[i]=s; riempi(i); }
    if(ops%5000==0) controlla();
    if(g_brk-g_arena>max_heap) max_heap=g_brk-g_arena;
  }
  for(int i=0;i<N;i++) if(p[i]){verifica(i); heap_free(p[i]);}
  controlla();
  { long nb=0, nl=0; for(Blocco*b=heap_primo;b;b=b->succ){nb++; if(b->libero) nl++;} printf("blocchi alla fine: %ld (liberi %ld)\n", nb, nl); }
  printf("OK: %ld operazioni, heap massimo %ld MB, alla fine %ld KB\n",ops,max_heap>>20,(long)((g_brk-g_arena)>>10));
  return 0;
}
