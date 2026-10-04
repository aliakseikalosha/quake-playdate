# Performance infrastructure (Playdate)

[← Documentation index](../README.md)

The Playdate's CPU is fast compared with its memory. The performance work in this fork is built on
what was *measured* about that memory system, and a small set of headers, assembly files and
profiling hooks make the work safe and repeatable. This page describes those files. How the
optimisations are switched on is in the [build system](../build-system.md#options); the full list
of measured costs is in the project [README](../../README.md#profiling-on-the-device).

| File | Role |
| --- | --- |
| [`pdprof.h`](#pdprofh) | Section timers, counters and stack probes (compile to nothing without `PD_PROFILE`) |
| [`pd_stack.h`](#pd_stackh) | "Is there room on the fast stack for this buffer?" |
| [`pd_asm.h`](#pd_asmh) | Switches for the hand-written assembly and its A/B and check modes |
| [`asm_draw.h`](#asm_drawh) | Struct offsets shared between C and the original x86 assembly |
| [`d_scan_arm.S`](#d_scan_arms) | Thumb-2 textured span drawer |
| [`r_edge_arm.S`](#r_edge_arms) | Thumb-2 edge-scan routines |

## The memory model in one paragraph

The heap, `.bss` and code live in slow memory behind a small *write-through* data cache. Every byte
stored costs roughly 26 ns (a store to a global ≈ 100 ns) and every cache line that misses ≈ 1 µs.
The stack is fast internal RAM (a store ≈ 14 ns) but the game task's stack is only about 10 KB. So frame
time is set by **how many bytes are written to global memory and how many cache lines are touched**,
not by instruction counts. The rules that follow from this: keep hot scratch data in locals, write
outputs in ascending order one line at a time, prefer compact contiguous data, avoid `double`
(software floating point, 2–5 µs per operation).

---

## `pdprof.h`

[`winquake/pdprof.h`](../../winquake/pdprof.h). An optional on-device profiler (`-DPD_PROFILE=ON`).
**Everything compiles to nothing when `PD_PROFILE` is not defined**, so instrumented engine code
costs nothing in a release build. The implementation is [`pdprof.c`](../port/playdate.md#pdprofc).

### Sections and counters

```c
enum {  // timed sections; microseconds per frame in prof.csv. They nest: SCAN ⊃ DSURF ⊃ CACHE/SPANS/ZSPAN/OTHER
	P_FRAME, P_INPUT, P_SERVER, P_CLIENT, P_SCR, P_SETUP, P_WORLD, P_BENT, P_SCAN,
	P_DSURF, P_CACHE, P_SPANS, P_ZSPAN, P_OTHER, P_ENT, P_VIEW, P_PART, P_UPSCALE,
	P_HUD, P_VID, P_SND, P_PAL,
	/* finer sections, each nested in another */
	P_FACE, P_SEINS, ..., P_WSURFS,
	P_NSECT };

enum { C_SPANS, C_PIXELS, C_CBUILD, ..., C_NCNT };   // per-frame counters
```

### Macros

| Macro | Meaning |
| --- | --- |
| `PROF_BEGIN(s)` / `PROF_END(s)` | Time a section; `PROF_END` also counts the call. |
| `PROF_CNT(c, n)` | Add `n` to counter `c`. |
| `PROF_BEGINF(s)` / `PROF_ENDF(s)` / `PROF_CNTF(c, n)` | Same, but only with `PD_PROFILE_FINE` (each timer call costs ≈ 3 µs). |
| `PROF_STK(id)` | Record the lowest stack pointer reached at a probed function (`K_WORLD`, `K_ALIAS`, …). Logged as `STK` lines. |
| `PROF_SPANS(list)` | Count the spans and pixels of an `espan_t` list. |

Timing uses `system->getElapsedTime()` (game code is unprivileged, so the DWT cycle counter
faults); the timer is reset every frame to keep float precision; 1 tick = 1/168 µs.

### Example: how the renderer is instrumented

```c
// winquake/pdr_main.c, R_RenderView()
PROF_BEGIN(P_WORLD);
PDR_DrawWorld ();
PROF_END(P_WORLD);

PROF_BEGIN(P_ENT);
PDR_DrawEntities ();
PROF_END(P_ENT);

PROF_CNT(C_NODES,  pdr_c_nodes);
PROF_CNT(C_FACES,  pdr_c_faces);
PROF_CNT(C_SPANS,  pdr_c_spans);
```

Lifecycle functions: `pdprof_init`, `pdprof_open`, `pdprof_stage`, `pdprof_note`,
`pdprof_frame_begin`, `pdprof_frame_end`, `pdprof_count_spans`, `pdprof_bi_add` (per QuakeC builtin cost).

---

## `pd_stack.h`

[`winquake/pd_stack.h`](../../winquake/pd_stack.h). With `-DPD_STACK=ON` scratch buffers go on the
fast stack instead of in static memory, **but only while the whole call chain stays within
`PD_STACK_BUDGET` (6.5 KB) of where the frame started**. Otherwise the caller uses its static buffer.
The picture is identical either way.

```c
extern uintptr_t pd_stack_top;           // stack pointer when the frame started; 0 = not in a frame
static inline uintptr_t PD_StackPointer (void);          // reads sp (a host build takes a local's address)
static inline int PD_StackRoom (unsigned bytes);         // 1 if a buffer this big still fits the budget
```

The port sets `pd_stack_top` at the start of every frame
([`main.c`](../port/playdate.md#mainc)):

```c
pd_stack_top = PD_StackPointer();
poll_input();
qembd_frame();
pd_stack_top = 0;
```

### Usage pattern

The coverage mask of the Playdate renderer is read and written for every span, so it wants to live on
the stack. If there is no room it falls back to a static array:

```c
// winquake/pdr_world.c, PDR_DrawWorld()
static uint32_t cov_static[PDR_MAXH * PDR_CWORDS];
int rows = pdr_skip != 2 ? (pdr_vh + 1) / 2 : pdr_vh;

if (PD_StackRoom (rows * PDR_CWORDS * 4 + 2048))
{
	uint32_t cov[rows * PDR_CWORDS];       // C99 VLA on the fast stack
	PDR_DrawWorldWith (cov);
}
else
	PDR_DrawWorldWith (cov_static);
```

`PD_STACK_AB` (needs `PD_PROFILE`) switches between the two frame by frame using
`PD_STACK_ACTIVE()`, so one run times both. Without `PD_STACK`, `PD_StackRoom` is the constant `0`
and the compiler removes the stack branch.

---

## `pd_asm.h`

[`winquake/pd_asm.h`](../../winquake/pd_asm.h). Controls the hand-written Thumb-2 assembly. Each
assembly routine keeps its C twin, which stays the reference.

```c
#if defined(PD_ASM) && defined(__arm__)
#define PD_USE_ASM 1                 // the asm is compiled in and callable
```

| Mode | Defined by | Effect |
| --- | --- | --- |
| normal | `PD_ASM` | `PD_ASM_ACTIVE()` is `1`: always use the assembly. |
| A/B | `PD_ASM_AB` + `PD_PROFILE` | A hash of the frame number picks the assembly or the C each frame (`pd_asm_on`); `prof.csv` records which. Compare with `scripts/pd-report.py --ab`. |
| check | `PD_ASM_CHECK` + `PD_PROFILE` | Every call runs the assembly, keeps what it wrote, runs the C over the same input and counts differing pixels in `pd_asm_bad` (plus `ASMBAD` lines). Must stay 0 over the demos. |

`pd_asm_on` is reused by `PD_STACK_AB` and `PDR_EXPERIMENT(n)`
([`pdr.h`](renderer-pdr.md#pdrh)) as the "variant B" switch.

```c
// winquake/d_scan.c: the dispatcher that wraps the assembly
void D_DrawSpans8 (espan_t *pspan)
{
	d_spanparms_t p;

	if (!PD_ASM_ACTIVE()) { D_DrawSpans8_C (pspan); return; }   // A/B: this frame uses the C

	p.sdivzorigin = d_sdivzorigin;  /* ...copy the globals into one block... */
	D_DrawSpans8_ARM (pspan, &p);
#ifdef PD_ASM_CHECK
	D_AsmCheckSpans ("spans", pspan, D_DrawSpans8_C, (byte *)d_viewbuffer, screenwidth, 1);
#endif
}
```

---

## `asm_draw.h`

[`winquake/asm_draw.h`](../../winquake/asm_draw.h). Inherited from id Software's Quake source. It
`#define`s the byte offsets of `espan_t`, `sspan_t`, `spanpackage_t`, `edge_t` and `surf_t` for
assembly code that has to read C structs, with warnings that **it must match the C structures
at all times**:

```c
// !!! if this is changed, it must be changed in r_shared.h too !!!
#define espan_t_u      0
#define espan_t_v      4
#define espan_t_count  8
#define espan_t_pnext  12
#define espan_t_size   16
```

The original used it from x86 `.s` files. **No file in this tree includes it**: the ARM files hard-code
the offsets in their own comments (`espan_t: u, v, count, pnext at 0, 4, 8, 12`) and the C structs
carry the matching "must change in both places" warnings. It is kept as a reference for the layouts.

---

## `d_scan_arm.S`

[`winquake/d_scan_arm.S`](../../winquake/d_scan_arm.S). `D_DrawSpans8_ARM`: the textured span drawer
(`D_DrawSpans8` in `d_scan.c`) in Thumb-2 for the Cortex-M7, called from the dispatcher above.

```c
void D_DrawSpans8_ARM (espan_t *pspan, const d_spanparms_t *p);
```

It produces **exactly the pixels of the C version** (same float operations in the same order,
same fused multiply-adds, same truncating conversions, same clamps). It is faster because:

- all state lives in registers (the C spills most of it to the stack);
- the 1/z division for the *next* 16-pixel segment starts before the current segment is drawn, so the 14-cycle divider overlaps the texel fetches and stores;
- a texel address is one `SMLATB` (`pbase + (t >> 16) * cachewidth`) plus `(s >> 16)`;
- the last partial segment jumps into an unrolled run instead of a `switch`.

```asm
@ one texel: \d = pbase[(s >> 16) + (t >> 16) * cachewidth], then step s and t
.macro TEXEL d
	smlatb	\d, tfix, cw, pbase
	add	\d, \d, sfix, asr #16
	add	sfix, sfix, sstep
	add	tfix, tfix, tstep
	ldrb	\d, [\d]
.endm
```

Measured: ≈ 0.9 ms per frame off the span drawing in the demos.

## `r_edge_arm.S`

[`winquake/r_edge_arm.S`](../../winquake/r_edge_arm.S). Four routines for the array-based edge scan
(`R_ScanEdges` with `PD_FAST_EDGES`, `r_edge.c`):

| Routine | What it does |
| --- | --- |
| `R_GenerateLine_ARM(fe_line_t *g)` | One scan line of span generation: walks the active edge table, keeps the surface stack sorted on key, emits a span whenever the top surface changes, including the 1/z tie-break of two brush-model surfaces. |
| `R_InsertEdges_ARM(fe_ins_t *g)` | Merges the new edges of a scan line (sorted on `u`) into the active edge table. Stops when the table is full so the caller can grow it. |
| `R_RemoveEdges_ARM(aedge_t *act, int nact, edge_t *list)` | Removes every edge on the `nextremove` list; returns the new count. |
| `R_StepEdges_ARM(aedge_t *act, int nact)` | Steps all edges to the next scan line and bubbles any that crossed their predecessor. |

The C moves table entries with struct assignments, which GCC turns into `memmove`; newlib's
`memmove` copies an overlapping upward move one byte at a time. These routines move whole 16-byte
entries. Measured: 1.2 ms off edge insertion and 1.7 ms off span generation per frame.

```c
// winquake/r_edge.c: the call sites
nact = R_RemoveEdges_ARM (act, nact, removeedges[iv]);
...
R_StepEdges_ARM (act, nact);
```

> Both `.S` files are only built when `PD_ASM_BUILD` is on, which means a **device build with the
> original renderer** (`-DPD_NEW_RENDERER=OFF`). The default Playdate renderer
> ([`pdr_*.c`](renderer-pdr.md)) does not use them.
