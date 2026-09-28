// =============================================================================
// tools/rust-exos/prova/src/allocatore.rs — @RUST-1: alloc sopra la malloc
//
// L'allocatore globale di Rust per EX-OS (28 settembre 2026). Box, Vec,
// String e le collezioni di `alloc` chiedono memoria qui, e qui la si chiede
// alla libc che c'e' gia'.
//
// ! L'ALLINEAMENTO SI GUARDA, NON SI DA' PER BUONO. La malloc di EX-OS
// garantisce 8 byte (HEAP_ALLINEA in lib/libc.c); un Layout puo' chiederne di
// piu' (un tipo con #[repr(align(64))], un u128 a 16). Oltre gli 8 si passa
// da memalign(), che la libc ha dal giorno in cui l'ha voluta libsupc++, e il
// cui blocco free() riconosce come gli altri.
//
// ! realloc SOLO sotto gli 8 byte: realloc() non conosce l'allineamento e
// restituirebbe un blocco allineato a 8 anche per chi ne aveva chiesti 64.
// Sopra si lascia fare a GlobalAlloc::realloc predefinito (alloca, copia,
// libera), che e' giusto per costruzione.
// =============================================================================
use core::alloc::{GlobalAlloc, Layout};

extern "C" {
    fn malloc(n: usize) -> *mut u8;
    fn free(p: *mut u8);
    fn realloc(p: *mut u8, n: usize) -> *mut u8;
    fn memalign(allineamento: usize, n: usize) -> *mut u8;
}

const MALLOC_ALLINEA: usize = 8;

pub struct AllocExos;

unsafe impl GlobalAlloc for AllocExos {
    unsafe fn alloc(&self, l: Layout) -> *mut u8 {
        if l.align() <= MALLOC_ALLINEA { malloc(l.size()) }
        else { memalign(l.align(), l.size()) }
    }

    unsafe fn dealloc(&self, p: *mut u8, _l: Layout) {
        free(p)
    }

    unsafe fn realloc(&self, p: *mut u8, l: Layout, nuova: usize) -> *mut u8 {
        if l.align() <= MALLOC_ALLINEA {
            return realloc(p, nuova);
        }
        let n = Layout::from_size_align_unchecked(nuova, l.align());
        let q = self.alloc(n);
        if !q.is_null() {
            core::ptr::copy_nonoverlapping(p, q, core::cmp::min(l.size(), nuova));
            self.dealloc(p, l);
        }
        q
    }
}
