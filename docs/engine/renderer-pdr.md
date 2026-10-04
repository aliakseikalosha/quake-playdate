# The Playdate renderer (`pdr_*`)

[← Documentation index](../README.md)

`PD_NEW_RENDERER` (default **ON**) builds a replacement for Quake's software refresh, written
for the Playdate's memory system. It draws the same things as the original, uses the same cvars and
Options-menu settings, and keeps Quake's entry-point names ([`render.h`](../../winquake/render.h)), so the
rest of the engine is unchanged. With the option OFF the original `r_*.c` / `d_*.c` set is built
instead, see [Original renderer](renderer-original.md).

| File | Role | Lines |
| --- | --- | --- |
| [`pdr.h`](#pdrh) | Internal interface: constants, compact data structs, shared globals | 269 |
| [`pdr_main.c`](#pdr_mainc) | `R_RenderView` and per-frame setup | 912 |
| [`pdr_world.c`](#pdr_worldc) | Brush data build, BSP walk, coverage mask, brush entities | 1734 |
| [`pdr_span.c`](#pdr_spanc) | Textured, water and sky span drawing | 418 |
| [`pdr_light.c`](#pdr_lightc) | Light blocks, light styles, dynamic lights, `LightPoint` | 542 |
| [`pdr_alias.c`](#pdr_aliasc) | Alias models (monsters, items, the weapon) | 854 |
| [`pdr_sprite.c`](#pdr_spritec) | Sprites and particles | 535 |
| [`pdr_lowres.c`](#pdr_lowresc) | Hand-over of the half-resolution view to the display | 111 |

## Design in brief

```
R_RenderView()
 ├─ PDR_SetupFrame      axes, projection, frustum, interlace parity, PVS, dynamic lights
 ├─ PDR_SetupEntities   alias/sprite boxes → rows that need z; brush entities → sorted list
 ├─ PDR_SetupBmodels    clip each brush entity into the world leaves it touches
 ├─ PDR_DrawWorld       walk BSP front→back; coverage mask; record spans; draw faces
 ├─ PDR_DrawEntities    alias models and sprites (z-tested against the world)
 ├─ PDR_DrawAliasModel  the weapon (viewmodel)
 ├─ R_DrawParticles
 └─ D_UpscaleScreen  |  PDR_WarpScreen (underwater)
```

- **No edge list, no span sorting.** The BSP tree is walked front to back and every face is
  rasterised straight into the rows it covers, clipped against a per-row *coverage bit mask* of
  pixels already drawn. Every pixel is written once.
- **Compact map data.** At map load, nodes (32 bytes), leaves (16 bytes) and faces (vertices inline)
  are copied into arrays laid out in walk order, front-facing faces first.
- **No surface cache.** Faces are textured directly from the mip texture and colormap, with
  lighting from a small 8-bit *light block* per face, sampled bilinearly every 8 pixels.
- **Two passes.** The walk only records spans; faces are then drawn one after another so each
  face's texture, light block and set-up stay in the cache.
- **Z only where it is read.** The z buffer is written only inside screen rectangles of alias
  models, sprites and the weapon (the whole view while particles are alive).
- **Single precision only**, a polynomial `sin`/`cos`, no `double` in the frame.

The picture is not bit-identical to the original: lighting is interpolated over 8-pixel runs
instead of a 16×16 surface cache, and small alias models come out a pixel thinner. See the
[README](../../README.md#renderer) for measured device results and
[`tools.md`](../tools.md#toolshostcheckcompare-shotspy) for how the two are compared.

---

## `pdr.h`

Internal interface of the renderer. Key parts:

```c
#define PDR_MAXW   WARP_WIDTH
#define PDR_MAXH   WARP_HEIGHT
#define PDR_CWORDS ((PDR_MAXW + 31) / 32)   /* coverage words per row */
#define PDR_SUBDIV_SHIFT 3                  /* perspective-correct every 8 pixels, as D_DrawSpans8 */
```

### Compact brush data

```c
typedef struct {            /* 32 bytes: one cache line */
	float normal[3]; float dist;
	short minmaxs[6];
	short children[2];      /* >= 0: node; < 0: ~leaf */
} pdr_node_t;

typedef struct {            /* per node: its faces, front-facing first */
	unsigned short first, nfront, nback, pad;
} pdr_nodefaces_t;

typedef struct {            /* 16 bytes */
	short minmaxs[6]; unsigned short firstmark, nummarks;
} pdr_leaf_t;

typedef struct {            /* variable size: vertices follow the header */
	byte numverts, flags;   /* PF_SKY | PF_TURB | PF_NOLIGHT | PF_POW2 */
	unsigned short texinfo;
	short texturemins[2]; unsigned short extents[2];
	byte styles[MAXLIGHTMAPS]; int lightofs;
	float plane[4];
	float verts[][3];
} pdr_face_t;
```

`pdr_brush_t` bundles one model's compact arrays (`nodes`, `leafs`, `marks`, `facedata`,
`faceofs`, `spheres`, `origtopdr`, and `facebase`, the first slot of its faces in the per-face
state arrays). Brush models of one `.bsp` share `msurface_t` arrays, so the key is `surfaces`.

```c
pdr_brush_t *b = PDR_BrushForModel (m);       // build or find the compact data of a brush model
pdr_face_t  *f = PDR_FACE (b, faceindex);     // pdr numbering; vertices are inline
```

### View, clock and frustum globals

`pdr_wbasis` (eye and axes), `pdr_xcenter/ycenter/xscale/yscale`, the clamp limits
`pdr_umin/umax/vmin/vmax`, the view rectangle `pdr_vx/vy/vw/vh` in the half-resolution buffer,
`pdr_vbuf`/`pdr_zbuf`/`pdr_stride`, and `pdr_skip`: for interlacing, rows with `(y & 1) == pdr_skip`
are *not* drawn this frame; `2` means draw every row.

```c
#define PDR_ROW_SKIPPED(y) (((y) & 1) == pdr_skip)
```

The client clock is copied once per frame into `pdr_time` (float), `pdr_time10`
(texture animation), `pdr_turbofs` (water phase), because `cl.time` is a `double` and a `double`
operation is a multi-microsecond library call here.

### Experiments and counters

```c
#ifdef PD_PDR_AB
#define PDR_EXPERIMENT(n) (PDR_EXP == (n) && !pd_asm_on)   // variant B runs on alternate frames
#else
#define PDR_EXPERIMENT(n) 0
#endif

if (PDR_EXPERIMENT(1)) { /* alternative code, timed against the default over one run */ }
```

`PDR_CNT(x)` increments work counters only in profiling and host builds (on the device each would
be a store to slow memory in a hot loop).

### Span context

```c
typedef struct {
	float zistepu, zistepv, ziorigin;            /* 1/z gradients */
	float sdivzstepu, ..., tdivzorigin;          /* s/z, t/z gradients */
	int   sadjust, tadjust, bbextents, bbextentt;
	byte *tex; int tw, th, soff, toff, tshift, smask, tmask;   /* the mip texture */
	const byte *light; int lw, lh, lshift, lconst, lmaxs, lmaxt; /* the light block */
	byte *colormap;
	int   kind;                                  /* PDR_SPAN_LIT | PDR_SPAN_TURB | PDR_SPAN_SKY */
} pdr_spanctx_t;
```

The full list of functions it declares is spread through the sections below.

---

## `pdr_main.c`

Entry points and per-frame setup. It also holds the globals the old refresh owned and other files
still use (`r_refdef`, `r_origin`, `vpn`, `vright`, `vup`, `r_viewleaf`, `d_pzbuffer`, …) and
registers the render cvars.

### Cvars (registered in `R_Init`)

| Cvar | Default | Archived | Meaning |
| --- | --- | --- | --- |
| `r_clearcolor` | 2 | no | Background colour for uncovered pixels. |
| `r_waterwarp` | 1 | no | Underwater sine warp. |
| `r_interlace` | 1 | yes | "Interlaced" in the Options menu. |
| `r_maxdist` | 512 | yes | "Draw distance" (0 = unlimited). |
| `r_fullbright`, `r_drawentities`, `r_drawviewmodel`, `r_ambient` | 0 / 1 / 1 / 0 | no | As in Quake. |
| `d_mipcap` | 1 | yes | "Texture detail": 1 = low, 0 = high. |
| `d_mipscale` | 1 | no | Mip selection scale. |

### Functions

| Function | Purpose |
| --- | --- |
| `R_Init` | Registers cvars and the `pointfile` command; builds the turbulence tables; initialises particles. |
| `R_InitTextures` | Builds the 16×16 checkerboard `r_notexture_mip` (4 mip levels). |
| `R_NewMap` | Clears efrags and particles, resets light styles, builds the compact brush/light/alias data (`PDR_NewMapWorld`, `PDR_NewMapLight`, `PDR_NewMapAlias`). |
| `R_SetVrect`, `R_ViewChanged` | Compute the view rectangle (aligned to the 2×2 / byte grid the low-res hand-over needs) and the projection constants, halved because the 3D view is half resolution. |
| `PDR_SetupFrame` | Ambient light, dynamic-light list, clock copies, view axes, `r_viewleaf`, water-warp flag, interlace parity. |
| `PDR_SetupFrustum` | World-space frustum planes and the box-corner indices used for fast box tests. |
| `PDR_SetupEntities` | Walks the visible entity list: alias models and sprites are box-tested and their screen rectangles added to the z rows (`PDR_AddZRect`); brush entities are depth-sorted nearest first. It first calls `PDR_StoreStatics` and `PDR_ResetAliasSetups`. |
| `PDR_StoreStatics` | The compact static-entity list: for each static entity (torches, flames) it remembers up to `STATIC_LEAFS` (4) leaf numbers from its efrags (`0xff` = more, follow the chain), so whether it is in the PVS is a few bit tests instead of an efrag walk. Visible ones are appended to `cl_visedicts`. |
| `PDR_DrawEntities` | Draws the collected alias models and sprites. |
| `PDR_WarpScreen` | The underwater sine warp, with a slight compression so the edges do not wrap. |
| `R_RenderView` | The frame, as in the diagram above. |
| `PDR_SinCos`, `PDR_AngleVectors` | Polynomial sine/cosine (a quarter-turn reduction plus a degree-7/8 polynomial) replacing `sinf`/`cosf` (≈ 1 µs each). |
| `PDR_TransformToView` | Dot products with a basis. |
| `R_BoxBeyond` | Is a bounding box entirely beyond the draw distance? |
| `D_InitCaches`, `D_FlushCaches`, `D_SurfaceCacheForRes` | The "surface cache" interface of the old refresh, here backed by the 128 KB light-block pool. |
| `D_EnableBackBufferAccess` / `D_DisableBackBufferAccess` | Call `VID_LockBuffer` / `VID_UnlockBuffer`. |

```c
// the cheap sine/cosine: quarter-turn k plus a remainder in [-45, 45] degrees
float q = deg * (1.0f / 90.0f);
int   k = (int)(q >= 0 ? q + 0.5f : q - 0.5f);
float r = (deg - 90.0f * k) * (M_PI / 180.0f);
/* sr, cr from Taylor polynomials in r2 = r*r, then swap/negate by (k & 3) */
```

### Interlacing

When `r_interlace` is on, each frame draws only every other row, alternating each frame
(`pdr_skip = r_framecount & 1`); the display keeps the previous frame's other rows. A full frame
(`pdr_skip = 2`) is drawn when `r_interlace` is off, the view was resized (`r_viewchanged`), or the
world model changed (a new map), since the kept rows would not match.

```c
qboolean full = !r_interlace.value || r_viewchanged || lcd_x.value || cl.worldmodel != interlace_world;
interlace_world = cl.worldmodel;
pdr_skip = full ? 2 : (r_framecount & 1);
```

---

## `pdr_world.c`

World and brush-model geometry. Three phases:

### 1. Build (map load)

`PDR_BuildBrushes` → `PDR_BuildBrush(b, model)` copies a brush model's nodes, leaves and faces into
the compact structs. Faces are renumbered so the faces of node 0 come first (front-facing, then
back-facing), then node 1, and so on; `origtopdr[]` maps the original surface index to the new one. Each
face also gets a bounding sphere. `MAX_PDR_BRUSHES` is 64.

### 2. The walk

```c
static void PDR_Walk (void)         // iterative; wstack_t stack[96]
```

An explicit stack replaces recursion (`wstack_t { short node; byte clip; byte state; }`). For each node:

1. Skip if the view is already full (`pdr_covered >= pdr_covtotal`).
2. Skip if not in the PVS (`pdr_nodevis` bit).
3. `PDR_CullBox` against the frustum; `clip` carries which planes still need testing.
4. With a draw distance, `PDR_DistClass` classifies the box as near/far.
5. Push the node (to draw its faces on the way back) and descend into the near child.

On the way back it draws that node's faces for the side the eye is on (`PDR_WorldFace` for each face
whose visibility bit is set), then pushes the far child. Leaves go through `PDR_VisitLeaf`, which marks the leaf's
faces visible, or only its sky faces when it is beyond the draw distance (`CLIP_FAR`), and draws any brush-entity fragments in the leaf.

### 3. Coverage and drawing

```c
static uint32_t *pdr_cov;               /* PDR_CWORDS words per drawn row */
static inline uint32_t *CovRow (int y); /* row y's words (rows are halved when interlaced) */
static inline uint32_t RangeMask (int b0, int b1);   /* bits [b0, b1) of one word */
```

`PDR_WorldFace` transforms and clips a face, then `PDR_RasterFace` / `PDR_RasterRows` find, per
row, the runs of **uncovered** pixels inside the polygon and hand each to `PDR_Run`, which appends
the span to the face's list and sets the coverage bits. Spans are packed into one word:

```c
#define SPAN_PACK(y, x0, x1) ((unsigned)(y) | (unsigned)(x0) << 10 | (unsigned)(x1) << 20)
// unpacked in PDR_DrawFaces:   y = v & 1023, x0 = (v >> 10) & 1023, x1 = v >> 20
```

`PDR_DrawFaces` then runs once the walk is done (or when the span/face buffers fill):

```c
for (d = 0; d < pdr_numdfaces; d++) {
	PDR_SetupFace (df, &ctx);                 // gradients, mip level, texture, light block
	for (i = 0; i < df->nspans; i++) {
		PDR_DrawSpan (&ctx, y, x0, x1);
		if (pdr_zany) { /* clip to the entity rectangles */ PDR_ZSpan (&ctx, y, a, b); }
	}
}
```

Anything still uncovered is filled by `PDR_FillUncovered` with `r_clearcolor` (palette index 0 while
a draw distance is set, so culled geometry shows black). `PDR_DrawWorld` puts the coverage array on the
fast stack when there is room (see [`pd_stack.h`](perf-infrastructure.md#pd_stackh)).

### Brush entities (doors, platforms)

`PDR_AddBmodel` transforms the entity's vertices into world space and clips its faces into the world
BSP's leaves (`PDR_BmodelNode` → `PDR_BmodelFace` → `PDR_ClipFragment` → `PDR_StoreFragment`, using
`frag_t` records in a pool). When the walk reaches a leaf that holds fragments,
`PDR_DrawLeafFragments` rasterises them in place, so brush entities need no separate sort.
`PDR_ClearBmodels` resets the per-frame state.

### Other functions

| Function | Purpose |
| --- | --- |
| `PDR_NewMapWorld` | Builds the brushes and per-face state for a new level. |
| `PDR_MarkLeavesNow` | Marks the PVS (leaf and node visibility bits) from `r_viewleaf`. |
| `PDR_LeafVisible` | Is a leaf in the current PVS? (Used to cull static entities.) |
| `PDR_TextureAnimation` | Picks the animated texture frame (`pdr_time10`) and alternate textures. |
| `PDR_MipLevel` | Mip level from the screen-space scale and `d_mipcap`. |
| `PDR_SetupFace` | Fills a `pdr_spanctx_t`: 1/z, s/z and t/z gradients, adjusts, texture, light block. |

```c
// choose a texture and light for a face, then draw its spans
const pdr_face_t *f = PDR_FACE (brush, fi);
PDR_SetupFace (&dface, &ctx);       // → ctx.tex, ctx.light = PDR_FaceLight (...)
PDR_DrawSpan (&ctx, y, x0, x1);
```

---

## `pdr_span.c`

Draws one span of one face. Spans are textured straight from the mip texture (no surface cache):
every 8 pixels the perspective-correct `s` and `t` are computed (as `D_DrawSpans8`), and the light,
sampled bilinearly from the face's light block at those points, is stepped linearly in between and
applied through the colormap.

```c
void PDR_DrawSpan (const pdr_spanctx_t *c, int y, int x0, int x1)
{
	byte *dst = pdr_vbuf + y * pdr_stride + x0;
	switch (c->kind) {
	case PDR_SPAN_LIT:  SpanLit  (c, dst, y, x0, x1 - x0); break;
	case PDR_SPAN_TURB: SpanTurb (c, dst, y, x0, x1 - x0); break;
	default:            SpanSky  (dst, y, x0, x1 - x0);    break;
	}
}
```

| Function | Purpose |
| --- | --- |
| `SampleLight` | Bilinear light from the block at 16.16 surface coordinates (the block is padded, so the right and bottom neighbours can be read at the edge with weight 0). |
| `SpanLit` → `SpanLitPow2` / `SpanLitWrap` | `SpanLit` does the per-8-pixel perspective divide (`PDR_SUBDIV`) and steps `s`, `t` and the light between divides, then calls an inner loop: `*dst++ = cmap[((l >> 10) & 0x3f00) + tex[((t >> tshift) & tmask) + ((s >> 16) & smask)]]`. `SpanLitPow2` uses masks (texture width and height are powers of two, `c->tshift >= 0`); `SpanLitWrap` handles other sizes by wrapping `s` and `t` with a modulo. |
| `SpanTurb` | Water: the sine-warped texture lookup (`pdr_sintable`). |
| `SpanSky` / `Sky_uv_To_st` | Two-layer scrolling sky; `R_InitSky` splits the sky texture into its layers. |
| `PDR_ZSpan` | Writes 1/z for a span (only where entities will test against it). |
| `PDR_FillSpan` | Solid colour fill (uncovered pixels). |
| `PDR_InitTurb`, `PDR_SetupSky` | Table and per-frame sky set-up. |

---

## `pdr_light.c`

Replaces the surface cache with **light blocks**: each lit face keeps its lightmap with light styles
and dynamic lights applied, one byte per 16×16-texel sample. A block is a few hundred bytes; a
surface-cache entry for the same face would be tens of kilobytes.

- **Pool.** `PDR_InitLightCache` takes the 128 KB buffer from `D_InitCaches` and tiles it with
  `lentry_t { owner, size, dlight, styleval[], data[] }` entries in a ring (as Quake's cache). `AllocBlock` /
  `Evict` manage it; a face whose block cannot be allocated falls back to normal constant light.
- **`PDR_FaceLight(brush, fi, face, &lconst)`**: returns the block to sample, building or rebuilding it
  only if a style it uses changed value or a dynamic light reaches it; otherwise returns the cached
  block. Faces with no lightmap (`PF_NOLIGHT`) get a constant (`*lconst`) from the ambient level. Returns `NULL`
  and a constant for full-bright mode or maps without light data.
- **Styles.** `PDR_AnimateLights` only animates the styles some face of the level uses
  (`pdr_usedstyles`), writing `pdr_lightstyle[]` in 8.8 fixed point.
- **Dynamic lights.** `R_PushDlights` (the original's name) walks the BSP for each live light
  (`MarkLights`): every face on a plane within the light's radius gets that light's bit
  (`1u << index`) OR-ed into `pdr_dlbits[slot]` and `pdr_dlframe[slot]` stamped with the frame, so
  `PDR_FaceLight` knows it is `dynamic` this frame. `BuildBlock(..., dynamic, ...)` then adds each
  light's contribution to the samples inside its radius.
- **`PDR_LightPoint(p)`** (used by `R_LightPoint` for alias models): traces down 2048 units
  (`LightHit`), reads the lightmap at the hit, and remembers the last few traces (`lpcache`) so a
  stationary entity costs one trace.

```c
// span set-up (PDR_SetupFace): ask for the face's block
c->light = PDR_FaceLight (fi->brush, fi->fi, f, &c->lconst);
// c->light == NULL  →  the span uses the constant c->lconst instead of sampling a block
```

---

## `pdr_alias.c`

Alias models (monsters, items, weapons): the same transform, lighting and affine texturing as
`r_alias.c` / `d_polyse.c`, laid out for the device.

- **Compact model data.** `BuildAModel` copies each model's triangles and texture coordinates into
  `atri_t` (8 bytes) and `ast_t` (4 bytes), instead of 16 and 12, once per model
  (`MAX_AMODELS` = 256; models that turn up after load use a 48 KB `apool`).
- **Projected vertices on the stack.** `DrawModel` receives an `avert_t` array. `PDR_DrawAliasModel`
  puts it on the fast stack when `PD_StackRoom` allows; otherwise it uses a static array
  (`MAX_PDR_AVERTS` = 2048):

```c
if (PD_StackRoom (am->numverts * sizeof(avert_t) + 1536)) {
	avert_t av[am->numverts];
	DrawModel (e, viewmodel, &a, am, av);
} else
	DrawModel (e, viewmodel, &a, am, av_static);
```

- **Triangle rasteriser.** `RasterTri` sets up plane gradients once per triangle and draws it row by
  row, which suits the one-to-three-pixel triangles of a half-resolution view. Triangles are clipped to the
  near plane and the view edges (`ClipNear`, `ClipEdge`).
- **Per-entity setup** is cached (`SetupModelCached` → `SetupModel`, `SetupTransform`,
  `SetupFrameVerts`, `SetupSkin`) so the transform computed for the bounding-box/rectangle test
  is reused by the draw; `PDR_ResetAliasSetups` empties that cache at the start of entity set-up
  each frame.

| Function | Purpose |
| --- | --- |
| `PDR_AliasCulled(e)` | Bounding-sphere test against the four side frustum planes. (The draw distance is tested separately by `PDR_EntityBeyond` in `pdr_main.c`.) |
| `PDR_AliasRect(e, rect)` | The entity's screen rectangle, used to decide which rows need z. |
| `PDR_DrawAliasModel(e, viewmodel)` | Draw one model. |
| `PDR_NewMapAlias` | Build the compact data for every alias model the new level precaches. |

---

## `pdr_sprite.c`

Sprites (explosions, bubbles) and particles.

- **Sprites** are rare, so they are drawn simply: `SetupSprite` computes the quad's corners (facing
  upright / parallel / oriented), `ClipSprite` clips it to the view, and `PDR_DrawSprite` gives every pixel its
  perspective-correct texel and a z test. `PDR_SpriteRect` returns the screen rectangle.
- **Particles** are Quake's square dots with a z test. `R_DrawParticles` keeps Quake's behaviour and
  motion (`r_part.c`'s state, ramps and gravity); `PDR_StartParticles` / `PDR_DrawParticle` /
  `PDR_EndParticles` draw them. Particles beyond the draw distance are skipped.

```c
for (p = active_particles; p; p = p->next) {
	if (pdr_maxdist2 <= 0 || !R_BoxBeyond (p->org, p->org))
		PDR_DrawParticle (p);                      // z-tested square dot
	p->org[0] += p->vel[0] * frametime;            // motion stays as in r_part.c
	/* ... type-specific ramp and gravity ... */
}
```

---

## `pdr_lowres.c`

Hands the half-resolution 3D view to the display layer. The old refresh did the same in `d_scan.c`.

Expanding the view into `vid.buffer` costs 96 KB of writes (and the display pass would read them back),
so each row pair is *marked pending* and the display dithers pending pairs straight from the
half-resolution buffer ([`display.c`](../port/playdate.md#the-low-resolution-hand-over)). Only the row
pairs that something draws over (console, menu, HUD text; `DRAW_TOUCH` in `draw.h`) are expanded.

```c
int  qembd_lowres_rect[4];                   // view rectangle, full-resolution pixels
int  qembd_lowres_active;
const byte *qembd_lowres_src;  int qembd_lowres_stride;
byte qembd_lowres_pending[PD_RENDER_HEIGHT / 2];   // 1 = drawn this frame, 2 = kept (interlaced)
byte qembd_lowres_shown[PD_RENDER_HEIGHT / 2];
```

| Function | Purpose |
| --- | --- |
| `D_UpscaleScreen()` | Called at the end of `R_RenderView`: publish the buffer, mark every row pair pending (2 if interlaced and skipped, else 1 and clear `shown`). |
| `D_LowresTouch(y0, y1)` | Called by draw primitives before they write over the view: expand the pending row pairs in that range (`D_UpscaleRow`) and clear their flag. |
| `D_UpscaleRow(p)` | Doubles one row horizontally and vertically using 32-bit stores. |
| `D_LowresEndFrame()` | Called from `VID_Update` after the frame: `qembd_lowres_active = 0`. |

```c
// D_UpscaleRow: 4 half-res pixels → 8 full-res pixels on two rows
lo = ((lo | (lo << 8)) & 0x00ff00ff) * 0x101;     // abcd → aabb ccdd spread
d0[0] = d1[0] = lo;  d0[1] = d1[1] = hi;
```
