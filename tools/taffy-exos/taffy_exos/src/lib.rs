// =============================================================================
// tools/taffy-exos/taffy_exos/src/lib.rs — Taffy behind a C interface
//
// Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later
//
// EXBrowser's flex and grid containers (debug/implementazione_BROWSER002.txt,
// P7). Taffy 0.16.0 (MIT) places the boxes; it never sees text: for every
// leaf it asks the browser "how big are you, given this much room", and the
// browser answers by laying the element out with its own engine. The C side
// is exwin/bin/exbrowser/browser_taffy.c; the two must agree on TxStile.
//
// ! NO std, ONLY core AND alloc: the library becomes libtaffy_exos.so for
// 32 and 64 bit, and the 32-bit std of EX-OS uses local-exec TLS, which a
// shared object cannot have. Taffy supports this (default-features = false,
// "alloc"): its maps become BTreeMap, its float helpers its own. Memory is
// the libc's malloc (see Alloc below); a panic aborts.
//
// ! THE TYPES AT THE BORDER ARE C TYPES: a handle, numbers, a #[repr(C)]
// struct. Nothing of Rust crosses it.
// =============================================================================
#![no_std]

extern crate alloc;

use alloc::boxed::Box;
use alloc::vec::Vec;
use core::alloc::{GlobalAlloc, Layout};
use taffy::prelude::*;
use taffy::compute_leaf_layout;
use taffy::{MinMax, Point};
use taffy::style::{GridTemplateArea, GridTemplateAreas};
use taffy::style::{GridTemplateComponent, GridTemplateRepetition, RepetitionCount,
                   MaxTrackSizingFunction, MinTrackSizingFunction, Overflow};

// ---------------------------------------------------------------------------
// The libc's memory (as tools/rust-exos/prova/src/allocatore.rs)
// ---------------------------------------------------------------------------
extern "C" {
    fn malloc(n: usize) -> *mut u8;
    fn free(p: *mut u8);
    fn realloc(p: *mut u8, n: usize) -> *mut u8;
    fn memalign(allineamento: usize, n: usize) -> *mut u8;
    fn abort() -> !;
}

const MALLOC_ALLINEA: usize = 8;

struct Alloc;

unsafe impl GlobalAlloc for Alloc {
    unsafe fn alloc(&self, l: Layout) -> *mut u8 {
        if l.align() <= MALLOC_ALLINEA { malloc(l.size()) } else { memalign(l.align(), l.size()) }
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

#[global_allocator]
static A: Alloc = Alloc;

#[panic_handler]
fn panico(_: &core::panic::PanicInfo) -> ! {
    unsafe { abort() }
}

// ---------------------------------------------------------------------------
// What C writes
// ---------------------------------------------------------------------------
/// A length: tipo 0 auto, 1 px, 2 percent (v from 0 to 1)
#[repr(C)]
#[derive(Clone, Copy)]
pub struct TxDim {
    pub tipo: u8,
    pub v: f32,
}

/// A grid track: minmax(min, max). tipo: 0 auto, 1 px, 2 percent (0..1),
/// 3 fr (max only), 4 min-content, 5 max-content, 6 fit-content(px) (max only)
#[repr(C)]
#[derive(Clone, Copy)]
pub struct TxTraccia {
    pub min_tipo: u8,
    pub max_tipo: u8,
    pub min_v: f32,
    pub max_v: f32,
}

/// A named area of grid-template-areas: its lines, from 1, start and end.
#[repr(C)]
pub struct TxArea {
    pub nome: *const u8,       // NUL-terminated
    pub r0: u16,
    pub r1: u16,
    pub c0: u16,
    pub c1: u16,
}

/// One box. The C struct browser_taffy.h declares, field for field.
#[repr(C)]
pub struct TxStile {
    pub col: *const TxTraccia,
    pub rig: *const TxTraccia,
    pub auto_col: *const TxTraccia,
    pub auto_rig: *const TxTraccia,
    pub aree: *const TxArea,
    pub nome_col_inizio: *const u8,   // a line name, NUL-terminated, or null
    pub nome_col_fine: *const u8,
    pub nome_rig_inizio: *const u8,
    pub nome_rig_fine: *const u8,
    pub grow: f32,
    pub shrink: f32,
    pub basis: TxDim,
    pub w: TxDim,
    pub h: TxDim,
    pub min_w: TxDim,
    pub min_h: TxDim,
    pub max_w: TxDim,
    pub max_h: TxDim,
    pub margine: [TxDim; 4],   // top right bottom left, as CSS
    pub inset: [TxDim; 4],
    pub padding: [f32; 4],
    pub bordo: [f32; 4],
    pub gap_col: f32,
    pub gap_rig: f32,
    pub col_inizio: i16,       // 0 auto; a line (+/-); 10000 + n = span n
    pub col_fine: i16,
    pub rig_inizio: i16,
    pub rig_fine: i16,
    pub display: u8,           // 0 none, 1 block, 2 flex, 3 grid
    pub dir: u8,               // 0 row, 1 column, 2 row-reverse, 3 column-reverse
    pub wrap: u8,              // 0 nowrap, 1 wrap, 2 wrap-reverse
    pub justify_content: u8,   // 0 normal, 1 start, 2 end, 3 center, 4 between, 5 around, 6 evenly, 7 stretch
    pub align_items: u8,       // 0 normal, 1 start, 2 end, 3 center, 4 baseline, 5 stretch
    pub align_self: u8,        // 0 auto, else as align_items
    pub align_content: u8,     // as justify_content
    pub justify_items: u8,     // as align_items
    pub justify_self: u8,      // as align_self
    pub posizione: u8,         // 0 in the flow, 1 absolute
    pub flusso: u8,            // grid-auto-flow: 0 row, 1 column, 2 row dense, 3 column dense
    pub n_col: u8,
    pub n_rig: u8,
    pub n_auto_col: u8,
    pub n_auto_rig: u8,
    pub col_rip: u8,           // 0 none, 1 auto-fill, 2 auto-fit: tracks col_rip_da .. +col_rip_n repeat
    pub col_rip_da: u8,
    pub col_rip_n: u8,
    pub rig_rip: u8,
    pub rig_rip_da: u8,
    pub rig_rip_n: u8,
    pub nascosto: u8,          // overflow hidden: the box does not grow for its content's min size
    pub n_aree: u8,
    pub aree_righe: u16,
    pub aree_colonne: u16,
}

/// The browser's answer to "how big are you": `avail` >= 0 definite, -1 min
/// content, -2 max content; `noto` NaN = not known.
pub type TxMisura = extern "C" fn(utente: *mut core::ffi::c_void, ctx: u32,
                                  noto_w: f32, noto_h: f32, avail_w: f32, avail_h: f32,
                                  w: *mut f32, h: *mut f32);

pub struct Albero {
    t: TaffyTree<u32>,
}

// ---------------------------------------------------------------------------
// From C to Taffy
// ---------------------------------------------------------------------------
fn dim(d: TxDim) -> Dimension {
    match d.tipo { 1 => Dimension::length(d.v), 2 => Dimension::percent(d.v), _ => Dimension::auto() }
}

fn lpa(d: TxDim) -> LengthPercentageAuto {
    match d.tipo {
        1 => LengthPercentageAuto::length(d.v),
        2 => LengthPercentageAuto::percent(d.v),
        _ => LengthPercentageAuto::auto(),
    }
}

fn allinea(v: u8) -> Option<AlignItems> {
    match v {
        1 => Some(AlignItems::START), 2 => Some(AlignItems::END), 3 => Some(AlignItems::CENTER),
        4 => Some(AlignItems::BASELINE), 5 => Some(AlignItems::STRETCH), _ => None,
    }
}

fn contenuto(v: u8) -> AlignContent {
    match v {
        1 => AlignContent::START, 2 => AlignContent::END, 3 => AlignContent::CENTER,
        4 => AlignContent::SPACE_BETWEEN, 5 => AlignContent::SPACE_AROUND,
        6 => AlignContent::SPACE_EVENLY, 7 => AlignContent::STRETCH, _ => AlignContent::NORMAL,
    }
}

fn traccia(t: &TxTraccia) -> TrackSizingFunction {
    let min = match t.min_tipo {
        1 => MinTrackSizingFunction::length(t.min_v),
        2 => MinTrackSizingFunction::percent(t.min_v),
        4 => MinTrackSizingFunction::min_content(),
        5 => MinTrackSizingFunction::max_content(),
        _ => MinTrackSizingFunction::auto(),
    };
    let max = match t.max_tipo {
        1 => MaxTrackSizingFunction::length(t.max_v),
        2 => MaxTrackSizingFunction::percent(t.max_v),
        3 => MaxTrackSizingFunction::fr(t.max_v),
        4 => MaxTrackSizingFunction::min_content(),
        5 => MaxTrackSizingFunction::max_content(),
        6 => MaxTrackSizingFunction::fit_content_px(t.max_v),
        _ => MaxTrackSizingFunction::auto(),
    };
    MinMax { min, max }
}

unsafe fn tracce(p: *const TxTraccia, n: u8) -> &'static [TxTraccia] {
    if p.is_null() || n == 0 { &[] } else { core::slice::from_raw_parts(p, n as usize) }
}

unsafe fn modello(p: *const TxTraccia, n: u8, rip: u8, da: u8, rn: u8)
        -> Vec<GridTemplateComponent<alloc::string::String>> {
    let t = tracce(p, n);
    let mut v = Vec::new();
    let mut i = 0usize;
    while i < t.len() {
        if rip != 0 && i == da as usize && rn > 0 && (da as usize + rn as usize) <= t.len() {
            let mut r = Vec::new();
            for k in 0..rn as usize { r.push(traccia(&t[i + k])); }
            v.push(GridTemplateComponent::Repeat(GridTemplateRepetition {
                count: if rip == 2 { RepetitionCount::AutoFit } else { RepetitionCount::AutoFill },
                tracks: r,
                line_names: Vec::new(),
            }));
            i += rn as usize;
            continue;
        }
        v.push(GridTemplateComponent::Single(traccia(&t[i])));
        i += 1;
    }
    v
}

unsafe fn testo(p: *const u8) -> Option<alloc::string::String> {
    if p.is_null() || *p == 0 { return None; }
    let c = core::ffi::CStr::from_ptr(p as *const core::ffi::c_char);
    c.to_str().ok().map(alloc::string::String::from)
}

/// A line: a name (with its index, 0 = none) if there is one, else a number
unsafe fn linea_n(v: i16, nome: *const u8) -> GridPlacement<alloc::string::String> {
    match testo(nome) {
        Some(n) => GridPlacement::NamedLine(n, if v >= 10000 { 0 } else { v }),
        None => linea(v),
    }
}

fn linea(v: i16) -> GridPlacement<alloc::string::String> {
    if v == 0 { GridPlacement::Auto }
    else if v >= 10000 { GridPlacement::Span((v - 10000).max(1) as u16) }
    else { <GridPlacement<alloc::string::String> as TaffyGridLine>::from_line_index(v) }
}

unsafe fn stile(s: &TxStile) -> Style {
    let mut st = Style::default();
    st.display = match s.display { 0 => Display::None, 2 => Display::Flex, 3 => Display::Grid, _ => Display::Block };
    st.position = if s.posizione == 1 { Position::Absolute } else { Position::Relative };
    st.box_sizing = BoxSizing::BorderBox;
    st.flex_direction = match s.dir {
        1 => FlexDirection::Column, 2 => FlexDirection::RowReverse,
        3 => FlexDirection::ColumnReverse, _ => FlexDirection::Row,
    };
    st.flex_wrap = match s.wrap { 1 => FlexWrap::Wrap, 2 => FlexWrap::WrapReverse, _ => FlexWrap::NoWrap };
    st.flex_grow = s.grow;
    st.flex_shrink = s.shrink;
    st.flex_basis = dim(s.basis);
    st.size = Size { width: dim(s.w), height: dim(s.h) };
    st.min_size = Size { width: lpa(s.min_w), height: lpa(s.min_h) };
    st.max_size = Size { width: lpa(s.max_w), height: lpa(s.max_h) };
    st.margin = Rect { top: lpa(s.margine[0]), right: lpa(s.margine[1]),
                       bottom: lpa(s.margine[2]), left: lpa(s.margine[3]) };
    st.inset = Rect { top: lpa(s.inset[0]), right: lpa(s.inset[1]),
                      bottom: lpa(s.inset[2]), left: lpa(s.inset[3]) };
    st.padding = Rect { top: LengthPercentage::length(s.padding[0]), right: LengthPercentage::length(s.padding[1]),
                        bottom: LengthPercentage::length(s.padding[2]), left: LengthPercentage::length(s.padding[3]) };
    st.border = Rect { top: LengthPercentage::length(s.bordo[0]), right: LengthPercentage::length(s.bordo[1]),
                       bottom: LengthPercentage::length(s.bordo[2]), left: LengthPercentage::length(s.bordo[3]) };
    st.gap = Size { width: LengthPercentage::length(s.gap_col), height: LengthPercentage::length(s.gap_rig) };
    st.justify_content = contenuto(s.justify_content);
    st.align_content = contenuto(s.align_content);
    st.align_items = allinea(s.align_items).unwrap_or(AlignItems::NORMAL);
    st.align_self = allinea(s.align_self);
    st.justify_items = allinea(s.justify_items).unwrap_or(AlignItems::NORMAL);
    st.justify_self = allinea(s.justify_self);
    if s.nascosto != 0 { st.overflow = Point { x: Overflow::Hidden, y: Overflow::Hidden }; }
    st.grid_template_columns = modello(s.col, s.n_col, s.col_rip, s.col_rip_da, s.col_rip_n);
    st.grid_template_rows = modello(s.rig, s.n_rig, s.rig_rip, s.rig_rip_da, s.rig_rip_n);
    st.grid_auto_columns = tracce(s.auto_col, s.n_auto_col).iter().map(traccia).collect();
    st.grid_auto_rows = tracce(s.auto_rig, s.n_auto_rig).iter().map(traccia).collect();
    st.grid_auto_flow = match s.flusso {
        1 => GridAutoFlow::Column, 2 => GridAutoFlow::RowDense, 3 => GridAutoFlow::ColumnDense,
        _ => GridAutoFlow::Row,
    };
    st.grid_column = Line { start: linea_n(s.col_inizio, s.nome_col_inizio), end: linea_n(s.col_fine, s.nome_col_fine) };
    st.grid_row = Line { start: linea_n(s.rig_inizio, s.nome_rig_inizio), end: linea_n(s.rig_fine, s.nome_rig_fine) };
    if s.n_aree > 0 && !s.aree.is_null() {
        let mut aree = Vec::new();
        for a in core::slice::from_raw_parts(s.aree, s.n_aree as usize) {
            if let Some(n) = testo(a.nome) {
                aree.push(GridTemplateArea { name: n, row_start: a.r0, row_end: a.r1,
                                             column_start: a.c0, column_end: a.c1 });
            }
        }
        st.grid_template_areas = Some(GridTemplateAreas { areas: aree, row_count: s.aree_righe,
                                                          column_count: s.aree_colonne });
    }
    st
}

// ---------------------------------------------------------------------------
// The interface
// ---------------------------------------------------------------------------
#[no_mangle]
pub extern "C" fn tx_nuovo() -> *mut Albero {
    let mut t: TaffyTree<u32> = TaffyTree::new();
    // ! WHOLE PIXELS: the browser draws on a pixel grid, and Taffy rounds
    // keeping the sum of the parts equal to the whole (no gaps between boxes)
    t.enable_rounding();
    Box::into_raw(Box::new(Albero { t }))
}

#[no_mangle]
pub unsafe extern "C" fn tx_libera(a: *mut Albero) {
    if !a.is_null() { drop(Box::from_raw(a)); }
}

/// A box. `ctx` 0: a container, whose size comes from its children; else
/// the browser's number for the element (ctx - 1 is its own index), handed
/// back to the measure function. u64::MAX on error.
#[no_mangle]
pub unsafe extern "C" fn tx_nodo(a: *mut Albero, s: *const TxStile, ctx: u32) -> u64 {
    if a.is_null() || s.is_null() { return u64::MAX; }
    let a = &mut *a;
    let st = stile(&*s);
    let r = if ctx == 0 { a.t.new_leaf(st) } else { a.t.new_leaf_with_context(st, ctx) };
    r.map(u64::from).unwrap_or(u64::MAX)
}

#[no_mangle]
pub unsafe extern "C" fn tx_figlio(a: *mut Albero, padre: u64, figlio: u64) -> i32 {
    if a.is_null() { return -1; }
    if (&mut *a).t.add_child(NodeId::from(padre), NodeId::from(figlio)).is_ok() { 0 } else { -1 }
}

fn spazio(v: f32) -> AvailableSpace {
    if v.is_nan() || v == -2.0 { AvailableSpace::MaxContent }
    else if v == -1.0 { AvailableSpace::MinContent }
    else { AvailableSpace::Definite(v) }
}

fn spazio_c(v: AvailableSpace) -> f32 {
    match v { AvailableSpace::Definite(x) => x, AvailableSpace::MinContent => -1.0, AvailableSpace::MaxContent => -2.0 }
}

/// Places the tree under `radice` in `larg` x `alt` (NaN: as much as the
/// content wants). 0 done, -1 not.
#[no_mangle]
pub unsafe extern "C" fn tx_calcola(a: *mut Albero, radice: u64, larg: f32, alt: f32,
                                    misura: Option<TxMisura>, utente: *mut core::ffi::c_void) -> i32 {
    if a.is_null() { return -1; }
    let a = &mut *a;
    let r = a.t.compute_layout_with_measure(
        NodeId::from(radice),
        Size { width: spazio(larg), height: spazio(alt) },
        |input, _id, ctx, st| {
            let c = ctx.map(|c| *c);
            compute_leaf_layout(input, st, |_, _| 0.0, |noto, avail| {
                let (Some(cb), Some(c)) = (misura, c) else { return Size::ZERO };
                let (mut w, mut h) = (0f32, 0f32);
                cb(utente, c, noto.width.unwrap_or(f32::NAN), noto.height.unwrap_or(f32::NAN),
                   spazio_c(avail.width), spazio_c(avail.height), &mut w, &mut h);
                Size { width: w, height: h }
            })
        });
    if r.is_ok() { 0 } else { -1 }
}

/// Where a box ended up, relative to its parent's border box: x, y, w, h.
#[no_mangle]
pub unsafe extern "C" fn tx_esito(a: *const Albero, n: u64, out: *mut f32) -> i32 {
    if a.is_null() || out.is_null() { return -1; }
    match (&*a).t.layout(NodeId::from(n)) {
        Ok(l) => {
            *out = l.location.x;
            *out.add(1) = l.location.y;
            *out.add(2) = l.size.width;
            *out.add(3) = l.size.height;
            0
        }
        Err(_) => -1,
    }
}

/// A leaf's size has changed (the browser measured it for real): Taffy's
/// cache of it, and of its ancestors, is forgotten.
#[no_mangle]
pub unsafe extern "C" fn tx_sporca(a: *mut Albero, n: u64) -> i32 {
    if a.is_null() { return -1; }
    if (&mut *a).t.mark_dirty(NodeId::from(n)).is_ok() { 0 } else { -1 }
}

/// The size of TxStile as Rust sees it: the C side checks it is its own.
#[no_mangle]
pub extern "C" fn tx_misura_stile() -> u32 {
    core::mem::size_of::<TxStile>() as u32
}
