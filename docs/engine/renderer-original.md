# Original software renderer (`r_*`, `d_*`)

[← Documentation index](../README.md) · [Source index](../source-index.md)

This is id Software's WinQuake software renderer, kept in the tree and built when the Playdate renderer is switched off
(`-DPD_NEW_RENDERER=OFF`). It has two halves:

- **`r_*` ("refresh")**: decides *what* to draw. Walks the BSP, projects faces and models, builds an **edge list**, and produces, per surface, a list of horizontal **spans** to draw.
- **`d_*` ("driver")**: draws the spans: textured, lit surfaces from the **surface cache**, sky, water, alias model triangles, sprites, particles.

```
R_RenderView
 ├─ R_SetupFrame, R_MarkLeaves
 ├─ R_EdgeDrawing
 │    ├─ R_RenderWorld  (BSP walk, front to back)
 │    │     └─ R_RenderFace → R_ClipEdge / R_EmitEdge   → edge_t list
 │    ├─ R_ScanEdges    (per scan line: sort edges, find the nearest surface, emit spans)
 │    └─ D_DrawSurfaces (for each surface: D_CacheSurface → D_DrawSpans8 / Turbulent8 / sky ...)
 ├─ R_DrawEntitiesOnList (alias models, sprites), R_DrawViewModel, R_DrawParticles
 └─ D_UpscaleScreen | D_WarpScreen
```

On the Playdate, when this renderer is selected, the device-specific options `PD_FAST_EDGES`, `PD_FAST_ALIAS`, `PD_FAST_FACES`, `PD_FAST_SURFACES`, `PD_ASM` and `PD_STACK` apply to it
(see [Build system](../build-system.md#options) and [Performance infrastructure](perf-infrastructure.md)); each keeps the original code under `#else` and produces identical pictures.

> **Always built.** `r_efrag.c` and `r_part.c` are compiled in both renderer configurations (the Playdate renderer reuses entity fragments and particle simulation).
> `d_iface.h` is included by both too (it defines `WARP_WIDTH`, `particle_t`, …). Everything else on this page is built only with `PD_NEW_RENDERER=OFF`.

| File | Role |
| --- | --- |
| [`render.h`](#renderh) | The public refresh interface (`R_*`) |
| [`r_shared.h`](#r_sharedh), [`r_local.h`](#r_localh) | Types shared by the refresh and driver; refresh-private definitions |
| [`d_iface.h`](#d_ifaceh), [`d_local.h`](#d_localh) | Driver interface and driver-private definitions |
| [`r_main.c`](#r_mainc), [`r_misc.c`](#r_miscc), [`r_vars.c`](#r_varsc--d_varsc) | Frame control, set-up, global variables |
| [`r_bsp.c`](#r_bspc), [`r_draw.c`](#r_drawc), [`r_edge.c`](#r_edgec) | World traversal, face/edge emission, the edge scan |
| [`r_surf.c`](#r_surfc), [`r_light.c`](#r_lightc), [`r_sky.c`](#r_skyc) | Surface cache building, lighting, sky |
| [`r_alias.c`](#r_aliasc), [`r_aclip.c`](#r_aclipc), [`r_sprite.c`](#r_spritec) | Alias models, their clipping, sprites |
| [`r_part.c`](#r_partc), [`r_efrag.c`](#r_efragc) | Particles, entity fragments (always built) |
| [`d_init.c`](#d_initc), [`d_modech.c`](#d_modechc), [`d_vars.c`](#r_varsc--d_varsc) | Driver set-up |
| [`d_edge.c`](#d_edgec), [`d_surf.c`](#d_surfc), [`d_scan.c`](#d_scanc), [`d_sky.c`](#d_skyc) | Surface drawing |
| [`d_polyse.c`](#d_polysec), [`d_sprite.c`](#d_spritec), [`d_part.c`](#d_partc), [`d_zpoint.c`](#d_zpointc), [`d_fill.c`](#d_fillc) | Models, sprites, particles, points, fills |

---

## `render.h`

The interface the rest of the engine uses. It declares `entity_t` (what the client hands the renderer: `origin`, `angles`, `model`, `frame`, `colormap`, `effects`, `skinnum`, `efrag`, `dlightbits`, …),
`efrag_t` (an entity's membership of a BSP leaf), and `refdef_t r_refdef` (the view: `vrect`, `vieworg`, `viewangles`, `fov_x`/`fov_y`, `ambientlight`, and many pre-scaled edge limits).

```c
void R_Init (void);                   void R_NewMap (void);          // new level
void R_RenderView (void);             // draw a frame; r_refdef must be set first (V_RenderView does)
void R_ViewChanged (vrect_t *pvrect, int lineadj, float aspect);   // r_refdef or vid changed
void R_AddEfrags (entity_t *ent);     void R_RemoveEfrags (entity_t *ent);
void R_RocketTrail (vec3_t start, vec3_t end, int type);   void R_ParticleExplosion (vec3_t org);
void R_PushDlights (void);            // mark surfaces lit by dynamic lights
int  D_SurfaceCacheForRes (int width, int height);          void D_InitCaches (void *buffer, int size);
```

These are the names [the Playdate renderer](renderer-pdr.md) also implements, so the client and `view.c` are unchanged.

## `r_shared.h`

"General refresh-related stuff shared between the refresh and the driver."

- **`espan_t { u, v, count, pnext }`**: a horizontal run of pixels on scan line `v`.
- **`surf_t`**: a visible surface in the edge scan: `next`/`prev` (the active surface stack), `spans` (list to draw), `key` (BSP order), `last_u`, `spanstate`, `data` (the `msurface_t`), `entity`, `nearzi`,
  `insubmodel`, and `d_ziorigin/stepu/stepv`. Padded to 64 bytes (a cache line pair).
- Limits: `MAXVERTS` 16, `NUMSTACKEDGES` 2400, `NUMSTACKSURFACES` 800, `MAXSPANS` 3000, `MAXWIDTH` 1280, `MAXHEIGHT` 1024.
- Shared globals: `xcenter`, `ycenter`, `xscale`, `yscale`, `sintable`, `intsintable`, `d_lightstylevalue[256]`, `r_maxdist2`, `cachewidth`/`cacheblock` (the surface being drawn).

## `r_local.h`

Refresh-private: `alight_t` (viewmodel lighting), `bedge_t` (clipped brush-model edges), `auxvert_t`, `clipplane_t` and `view_clipplanes[4]`, the `r_*` debug cvars (`r_draworder`, `r_speeds`, `r_timegraph`,
`r_dspeeds`, `r_drawflat`, `r_maxedges`, `r_maxsurfs`, `r_reportedgeout`, …) and **the port's cvars** `r_interlace`, `r_maxdist` and the function `R_BoxBeyond`. Constants: `ALIAS_BASE_SIZE_RATIO` (1/11:
about one pixel per triangle for the player model), `BMODEL_FULLY_CLIPPED`, `CLIP_EPSILON`, `BACKFACE_EPSILON`.

## `d_iface.h`

The contract between refresh and driver. **`WARP_WIDTH` / `WARP_HEIGHT`** are the size of the half-resolution 3D buffer: `PD_RENDER_WIDTH/2 × PD_RENDER_HEIGHT/2` with `PD_LOWRES_3D`, otherwise 320×200.
Plus `emitpoint_t`, `particle_t` / `ptype_t` (`pt_static`, `pt_fire`, `pt_explode`, …), `polyvert_t`, `polydesc_t`, **`finalvert_t`** (projected alias vertex: `v[6]` = u, v, s, t, light, 1/z, plus flags; 28 bytes
since the x86 padding to 32 was dropped, so more vertices fit on the stack), `affinetridesc_t`, `spritedesc_t`, `zpointdesc_t`, and the driver entry points (`D_DrawSurfaces`, `D_PolysetDraw`, `D_DrawSprite`, …).
It also declares **`D_UpscaleScreen()` and `D_LowresEndFrame()`**, the half-resolution hand-over used with the display layer.

## `d_local.h`

Driver-private: `surfcache_t` (a node of the surface cache: `next`, `owner`, `lightadj[]`, `dlight`, `size`, `width`, `height`, `mipscale`, `texture`, `data[]`),
`sspan_t`, the span-drawing parameter globals (`d_sdivzstepu`, `d_ziorigin`, `sadjust`, …) and
**`d_spanparms_t`**: everything the textured span drawer needs for a surface in one struct that `D_CalcGradientsTo` fills, so `D_DrawSpans8_ARM` can read it from the stack (port addition, see below).

---

## `r_main.c`

Frame control. `R_RenderView` verifies the stack and hunk alignment and calls `R_RenderView_`:

```c
R_SetupFrame ();                // view vectors, frustum, PVS bookkeeping
R_MarkLeaves ();                // mark visible leaves from the PVS
R_EdgeDrawing ();               // world: edge list → spans → D_DrawSurfaces
R_DrawEntitiesOnList ();        // alias models and sprites
R_DrawViewModel ();             // the weapon
R_DrawParticles ();
if (r_dowarp) { D_WarpScreen ()/D_UpscaleScreen (); }   // underwater warp or the low-res hand-over
V_SetContentsColor (r_viewleaf->contents);
```

| Function | Purpose |
| --- | --- |
| `R_Init`, `R_InitTextures`, `R_InitTurb`, `R_NewMap` | Registers cvars, builds the checkerboard default texture and water tables, clears state per level. |
| `R_ViewChanged`, `R_SetVrect` | Recompute the view rectangle and projection constants (`xscale`, `yscale`, edge limits). |
| `R_MarkLeaves` | Marks leaves (and their parent nodes) that are in the PVS of the leaf containing the eye (`Mod_LeafPVS`). |
| `R_EdgeDrawing` | World edge-list drawing, with brush-entity set-up; `R_DrawBEntitiesOnList`, `R_BmodelCheckBBox`. |
| `R_DrawEntitiesOnList`, `R_DrawViewModel` | Walk `cl_visedicts`; alias models and sprites. |
| `R_BoxBeyond`, `R_EntityBeyond` | Draw-distance tests for the port's `r_maxdist`. |

**Port changes:** `PD_LOWRES_3D` halves the view rectangle (the 3D view is half resolution) and snaps it to the 2×2 / byte grid the display layer needs; `r_maxdist` culls entities beyond the draw distance (a
`static` buffer replaces a large stack array); `PD_FAST_ALIAS` shares per-entity set-up between the bounding-box test and the draw; profiling probes (`PROF_*`).

## `r_misc.c`

Set-up and debug. `R_SetupFrame` copies the view into `r_origin`, `vpn`, `vright`, `vup`, finds `r_viewleaf`, sets `r_dowarp` (underwater), and computes the frustum
(`R_TransformFrustum`, `R_SetUpFrustumIndexes`). `R_TransformPlane`. `R_CheckVariables`, `R_PrintTimes`, `R_PrintDSpeeds`, `R_PrintAliasStats`, `R_TimeGraph`, `R_LineGraph`, `R_TimeRefresh_f`
(the `timerefresh` command), `WarpPalette`, `Show` are diagnostics.

**Port change: interlacing (with `PD_LOWRES_3D`).** `R_SetupFrame` decides `r_interlace_skip`: when `r_interlace` is on, every other row is skipped alternately each frame (`r_framecount & 1`), except for a full frame when the view was resized, the underwater-warp state changed or the map changed
(the kept rows would not match). With `PD_LOWRES_3D`, `r_dowarp` is always true: the 3D view always renders into the low-resolution buffer.

```c
full = !r_interlace.value || r_viewchanged || (r_dowarp != r_dowarpold) || lcd_x.value || cl.worldmodel != interlace_world;
interlace_world = cl.worldmodel;
r_interlace_skip = full ? 2 : (r_framecount & 1);        // 2 = draw every row
```

World spans, sky, alias models, sprites and particles all skip rows with `(y & 1) == r_interlace_skip`.

## `r_vars.c` / `d_vars.c`

Definitions of the refresh and driver globals "collected in a contiguous block to avoid cache conflicts": `r_bmodelactive`; `d_sdivzstepu`…`d_ziorigin`, `sadjust`, `tadjust`, `bbextents`, `cacheblock`, `cachewidth`, `d_viewbuffer`,
`d_pzbuffer`, `d_zrowbytes`, `d_zwidth`. (The original x86 versions mirrored them in assembly; here they are plain C globals. The `PD_STACK` option moves the per-surface ones into a stack block.)

---

## `r_bsp.c`

World traversal and brush entities.

- **`R_RenderWorld`** sets up the clip planes and calls `R_RecursiveWorldNode(node, clipflags)`: front-to-back recursion over the BSP; at each node it frustum-culls the node (`R_CullNode`), recurses to the near child, calls `R_RenderFace` for each face of the node, then the far child. Leaves mark their faces visible (`R_MARK_LEAF_SURFACES`; with a draw distance only a far leaf's sky faces).
- **Brush entities (doors, platforms)**: `R_RotateBmodel`, `R_EntityRotate`, `R_DrawSubmodelPolygons`, `R_DrawSolidClippedSubmodelPolygons`, `R_RecursiveClipBPoly` (clip a submodel's polygons against the world BSP nodes it straddles).
- `R_NodeBeyond` — the draw-distance test for nodes.

```c
// surfaces already drawn this frame:
#ifdef PD_FAST_FACES
#define SURF_MARK(surf)   (r_surfvis[((surf) - r_worldsurfaces) >> 3] |= 1 << (((surf) - r_worldsurfaces) & 7))
#else
#define SURF_MARK(surf)   ((surf)->visframe = r_framecount)       // writes into every msurface_t
#endif
```

**Port changes:** with `PD_FAST_FACES` the world traversal keeps its bookkeeping (`edge_p`, `surface_p`, keys) on the stack (`rworld_t`) and surface visibility is **one bit per surface** (`r_surfvis`) rather than a stamp written into
every `msurface_t` (saves a store to slow memory per face); draw-distance culling.

## `r_draw.c`

Turns a face into edges. **`R_RenderFace(fa, clipflags)`** back-face culls the face, clips each edge against the view planes (`R_ClipEdge`, using `clipplane_t`s), projects them and emits `edge_t`s into the global edge list (`R_EmitEdge`,
`R_EmitCachedEdge` reuses an edge already emitted for the neighbouring face), and creates the `surf_t` (`R_PostSurface`). `R_RenderBmodelFace`, `R_RenderPoly` (`r_drawpolys` debug), `R_ZDrawSubmodelPolys`.

**Port changes:** with `PD_FAST_FACES` the per-edge working state (the last projected vertex, clipped/emitted flags, near 1/z, cache offset, free edge pointer: about twenty globals in the original) lives in an `rface_t` on the stack
(`R_ClipEdgeF`, `R_EmitEdgeF`, `R_RenderFaceW`); the free edge pointer is written back once per face. The edges, surfaces and cache offsets that come out are identical (`FastCeil` replaces `ceil`).

## `r_edge.c`

The **edge scan**: turns the unsorted edge list into spans.

1. `R_BeginEdgeFrame` clears per-frame state; `R_ScanEdges` bucket-sorts the new edges by their start scan line.
2. For each scan line (`iv`): merge that line's new edges into the **active edge table**, sorted by `u` (`R_InsertNewEdges`); walk it left to right (`R_GenerateSpans`), keeping a **stack of active surfaces** sorted by `key` (BSP order);
   whenever the top surface changes, emit a span for the one going away (`R_LeadingEdge`, `R_TrailingEdge`; two brush-model surfaces with the same key are ordered by 1/z); remove edges that end (`R_RemoveEdges`), and step the rest to the next line (`R_StepActiveU`).
3. Spans accumulate on each `surf_t.spans`; `D_DrawSurfaces` then draws them.

**Port changes (`PD_FAST_EDGES`, default ON):** the active edge table and surface stack are **arrays on the stack** (`aedge_t act[]`, `sentry_t stk[]`; sizes `FE_ACTIVE_STACK` 96, `FE_SURF_STACK` 32, growing into the heap only on a very busy line) instead of linked lists whose pointers live in
`edge_t` / `surf_t`, so an edge crossing no longer rewrites a dozen words of slow memory. The spans are identical. `FE_EMIT` and `FE_ZTEST` macros inline the hot paths; `R_ScanEdgeLines` returns before the surfaces are drawn so the ~3 KB of scan arrays are off the stack by then.
With `PD_USE_ASM` the per-line span generation, edge insertion, removal and stepping are the Thumb-2 routines of [`r_edge_arm.S`](perf-infrastructure.md#r_edge_arms), with `PD_ASM_CHECK` comparison helpers (`R_AsmLineCompare`, `R_AsmActCompare`). Interlaced frames
skip span generation on the kept rows (the edges are still stepped past them).

```c
// the structures the scan keeps (r_edge.c, PD_FAST_EDGES)
typedef struct { int u, u_step; unsigned short s0, s1; edge_t *e; } aedge_t;   // 16 bytes
typedef struct { surf_t *s; int key; int last_u; } sentry_t;                    // 12 bytes
```

---

## `r_surf.c`

Builds the **surface cache**: the lit texture of a face at a given mip level.

- `R_DrawSurface` builds one `surfcache_t`: `R_BuildLightMap` combines the face's lightmap samples with their light styles (`d_lightstylevalue`) and the dynamic lights (`R_AddDynamicLights`) into 16×16-texel light cells; then the
  texture is lit through the colormap block by block (`R_DrawSurfaceBlock8_mip0..3`, one per mip level).
- `R_GenTurbTile` / `R_GenTile` generate the warped water tile; `R_TextureAnimation` picks the current frame of an animated texture; `R_DlightAffects` tells whether a dynamic light would actually change a face.

**Port changes (`PD_FAST_SURFACES`, default ON):** bitmaps are built **row by row** (each row one run of stores instead of sixteen 16-byte segments, `R_DrawSurfaceRows`, `BUILDROWS` macros); and a surface that `R_MarkLights` flagged as dynamically lit but whose lightmap no light would change keeps its cache instead of being rebuilt
(48 % of the dynamic-light rebuilds in the demos). With `PD_STACK` the lightmap scratch is on the stack. Identical output.

## `r_light.c`

Lighting.

| Function | Purpose |
| --- | --- |
| `R_AnimateLight` | Evaluate the light-style patterns each frame into `d_lightstylevalue[]` (8.8 fixed point; `'a'` dark … `'m'` normal … `'z'` double). |
| `R_PushDlights`, `R_MarkLights` | Mark every surface within each live dynamic light's radius (a bit per light in `dlightbits`). |
| `R_LightPoint(p)` | The light level at a point for lighting alias models: traces straight down (2048 units) through the BSP (`RecursiveLightPoint`) and reads the lightmap. |

**Port changes:** `R_LightPoint` keeps a 16-entry cache (`LPCACHE`) of where each downward trace landed (`lighthit_t`, found by `RecursiveLightHit`, converted to a light level by `LightFromHit`), keyed by the exact origin, so a stationary entity costs one trace.
`R_LightPointFlush` empties the cache (new level, or the lightmaps changed).

## `r_sky.c`

The sky texture is 256×128: two 128×128 layers side by side. [`model.c`](models.md#modelc) calls `R_InitSky(mt)` when it loads a texture named `sky*`, and that splits it once:
the right half is copied into `newsky` (the **back layer**); the left half becomes `bottomsky` (the **front layer**, stored 131 bytes per row with its first three pixels repeated) plus `bottommask`, which is `0xff` where a front pixel is colour 0 (transparent) and `0` elsewhere.
`r_skysource` points at `newsky`, which the sky span drawer ([`d_sky.c`](#d_skyc)) reads.

| Step | Function | Called from | What it does |
| --- | --- | --- | --- |
| 1 | `R_SetSkyFrame` | `R_SetupFrame` ([`r_misc.c`](#r_miscc)) | Works out `skytime` from `cl.time`, wrapped at the period after which both layer speeds (`iskyspeed` 8, `iskyspeed2` 2) line up again, and clears `r_skymade`. |
| 2 | `R_MakeSky` | `D_DrawSurfaces` ([`d_edge.c`](#d_edgec)), once per frame, when a sky surface is visible and `r_skymade` is 0 | Composes the 128×128 sky image into `newsky`: per pixel `(back & mask) \| front`, with the front layer read at an offset scrolled by `skytime * skyspeed` in x and y. It returns at once when the offsets did not change since the last call. |
| alt. | `R_GenSkyTile`, `R_GenSkyTile16` | `R_GenTile` ([`r_surf.c`](#r_surfc)) | The same composition written to a surface-cache tile (8-bit and 16-bit) for a sky surface that goes through the surface cache. |

```c
// R_MakeSky, per output pixel: the back layer shows through wherever the front layer is transparent
*(byte *)pnewsky = (*((byte *)pnewsky + 128) & bottommask[ofs]) | bottomsky[ofs];
```

**Port change:** the 4-bytes-at-a-time variants of `R_MakeSky` and `R_GenSkyTile` (`UNALIGNED_OK`) were removed; the byte-wise loops are what remains.

---

## `r_alias.c`

Alias models (monsters, items, weapons), transformed and lit here, drawn by `d_polyse.c`.

| Function | Purpose |
| --- | --- |
| `R_AliasDrawModel(plighting)` | The entry point for one model: set up, check the bounding box, transform and project the vertices, draw. |
| `R_AliasCheckBBox` | Frustum-test the model's 8 bounding-box corners: reject, accept without clipping, or clip. |
| `R_AliasSetupFrame`, `R_AliasSetupSkin`, `R_AliasSetUpTransform`, `R_AliasTransformVector`, `R_AliasSetupLighting` | Choose the frame (with animation), the skin, build the object-to-view matrix, and compute `ambientlight`/`shadelight`. |
| `R_AliasPreparePoints`, `R_AliasPrepareUnclippedPoints` | Transform, light (`r_avertexnormals[162]`, [`anorms.h`](models.md#anormsh)) and project all vertices into `finalvert_t`. |
| `R_AliasTransformFinalVert`, `R_AliasTransformAndProjectFinalVerts`, `R_AliasProjectFinalVert`, `R_AliasAuxVert` | One vertex through the pipeline. |

**Port changes:** the projected-vertex array (`finalvert_t`, 28 bytes) is put on the stack for models up to about 180 vertices (`PD_STACK`); `PD_FAST_ALIAS` shares the transform between the bounding-box test and the draw.

## `r_aclip.c`

Clips one alias-model triangle that [`r_alias.c`](#r_aliasc) found crossing the near plane or a screen edge, so that [`d_polyse.c`](#d_polysec) only ever draws polygons inside the view.
`R_AliasClipTriangle(ptri)` is the entry point:

1. It copies the triangle's three projected vertices (`finalvert_t`, each with `ALIAS_*_CLIP` flags) into `fv[0]`. For a back-facing triangle, vertices on the model's seam (`ALIAS_ONSEAM`) get their s coordinate moved by `seamfixupX16`, to the back half of the skin.
2. It ORs the three vertices' flags. With `ALIAS_Z_CLIP` set it fetches the camera-space vertices (`auxvert_t`) and clips against the near plane first (`R_Alias_clip_z`), the one clip that needs the view-space z.
3. It then clips against each of left, right, bottom and top that any vertex crossed (`R_Alias_clip_left` …). Every pass is `R_AliasClip(in, out, flag, count, clip)`: it walks the polygon's edges and, where an edge crosses the plane, makes a new vertex by linear interpolation of position, s, t, light and z, then re-flags it. The two buffers `fv[0]` and `fv[1]` swap roles between passes (`pingpong`); a pass that leaves nothing ends the triangle.
4. The result has up to 8 vertices. They are clamped to the alias view rectangle and drawn as a fan of triangles through `D_PolysetDraw`.

**Port changes.** With `PD_STACK`, the clipped polygon (`fv`, `av`) lives on the fast stack when [`PD_StackRoom`](perf-infrastructure.md#pd_stackh) says it fits, and in static buffers otherwise; the picture is identical. With `PD_FAST_ALIAS`, the camera-space vertices are no longer stored for every vertex by `R_AliasTransformAndProjectFinalVerts`; this file recomputes the three it needs (`R_AliasAuxVert`). Single-precision literals throughout.

## `r_sprite.c`

Sprites (explosions, bubbles, beams): `R_DrawEntitiesOnList` ([`r_main.c`](#r_mainc)) calls `R_DrawSprite` for each sprite entity, which prepares a flat, textured quad and hands it to [`d_sprite.c`](#d_spritec).

| Function | What it does |
| --- | --- |
| `R_DrawSprite` | Picks the frame, then builds the sprite's three axes (`r_spritedesc.vpn`, `vright`, `vup`) according to the sprite's type: `SPR_FACING_UPRIGHT` (up is world z, right is perpendicular to the direction to the viewer), `SPR_VP_PARALLEL` (parallel to the view plane), `SPR_VP_PARALLEL_UPRIGHT`, `SPR_ORIENTED` (from the entity's angles) and `SPR_VP_PARALLEL_ORIENTED` (view-parallel, rotated by the entity's roll). The two "upright" types draw nothing when the view is within 1° of straight up or down, where their cross product is undefined. |
| `R_GetSpriteframe` | A single frame, or for a group the frame whose interval contains `cl.time + syncbase` modulo the group's full interval. |
| `R_RotateSprite` | For beam sprites (`beamlength` ≠ 0), moves the origin back along the sprite's view direction. |
| `R_SetupAndDrawSprite` | Culls a sprite facing away (`dot >= 0`), builds the four corners in world space with their s and t, clips the quad to the four frustum planes in world space (`R_ClipSpriteFace`, which ping-pongs between the two `clip_verts` buffers), transforms the result to view space (z clamped to `NEAR_CLIP`), projects it into `emitpoint_t`s (`u`, `v`, `zi`, `s`, `t`) and calls `D_DrawSprite`. |

**Port change:** single-precision constants and `sinf`/`cosf` only; nothing else differs from the original.

## `r_part.c`

**Always built.** The particle system's *simulation*: `R_InitParticles`, `R_ClearParticles`, a free list of `particle_t` (`r_numparticles`), and the effect spawners called by the client:
`R_RunParticleEffect` (spikes, blood), `R_ParticleExplosion`, `R_ParticleExplosion2`, `R_BlobExplosion`, `R_LavaSplash`, `R_TeleportSplash`, `R_EntityParticles` (the ring of particles around an `EF_BRIGHTFIELD` entity),
`R_RocketTrail` (rocket, grenade, blood, tracer, vore trails), `R_ParseParticleEffect`, `R_ReadPointFile_f` (`pointfile`: debug leak path). `R_DrawParticles` (motion and drawing) is here **only for the original renderer**
(`#ifndef PD_NEW_RENDERER`); the new renderer has its own in [`pdr_sprite.c`](renderer-pdr.md#pdr_spritec).

```c
// cl_tent.c, TE_WIZSPIKE: 30 particles of palette colour 20 where a spike hits a wall
R_RunParticleEffect (pos, vec3_origin, 20, 30);
// cl_main.c, CL_RelinkEntities: a rocket's smoke trail along the path it moved this frame
if (ent->model->flags & EF_ROCKET)
	R_RocketTrail (oldorg, ent->origin, 0);
```

## `r_efrag.c`

**Always built.** *Entity fragments*: each entity is split into `efrag_t`s, one per BSP leaf it touches, so a leaf knows which entities to draw and so static entities can be culled by the PVS.
`R_AddEfrags(ent)` (called when an entity moves or spawns; splits the entity's bounding box on the BSP with `R_SplitEntityOnNode`), `R_RemoveEfrags`, `R_StoreEfrags`, `R_SplitEntityOnNode2`.
Uses the fixed pool `cl_efrags[MAX_EFRAGS]` (640). The Playdate renderer uses the same efrags for static entities (torches) visibility (`PDR_StoreStatics`).

---

## `d_init.c`

`D_Init` sets the driver's flags (`r_drawpolys`, `r_worldpolysbacktofront`, `r_recursiveaffinetriangles`) and registers `d_subdiv16`, `d_mipcap`, `d_mipscale`; `D_SetupFrame` computes the per-frame mip scale (`scale_for_mip`, `d_scalemip[]`, `d_minmip`) from `d_mipcap`/`d_mipscale` and the view size;
`D_TurnZOn`; `D_EnableBackBufferAccess` / `D_DisableBackBufferAccess` (wrappers for `VID_LockBuffer` / `VID_UnlockBuffer`).

## `d_modech.c`

`D_ViewChanged` recomputes everything the driver derives from the view. `R_ViewChanged` ([`r_main.c`](#r_mainc)) calls it whenever the view rectangle, field of view or screen size changes.

| It sets | Meaning |
| --- | --- |
| `scale_for_mip` | The larger of `xscale` and `yscale`; [`d_init.c`](#d_initc) turns it into the per-frame mip scale. |
| `d_zrowbytes`, `d_zwidth` | The z buffer's row size, from `vid.width`. |
| `d_pix_min`, `d_pix_max`, `d_pix_shift` | The smallest and largest particle size and the shift that maps a particle's 1/z to a size; they scale with the view width relative to 320 ([`d_part.c`](#d_partc) uses them). |
| `d_y_aspect_shift` | 1 when the pixel aspect is above 1.4 (rows are drawn twice for tall pixels), else 0. |
| `d_vrectx`, `d_vrecty`, `d_vrectright_particle`, `d_vrectbottom_particle` | The view rectangle, with the right and bottom edges pulled in by the largest particle. |
| `d_scantable[y]` | The offset of row *y* of the colour buffer (`y * rowbytes`; `WARP_WIDTH` bytes per row while the underwater warp is on). |
| `zspantable[y]` | A pointer to row *y* of the z buffer. |

**Port changes.** `D_Patch`, which made x86 code writable for self-modifying assembly, was removed, and the particle-size arithmetic uses `float` constants.

---

## `d_edge.c`

**`D_DrawSurfaces`** is where the spans are drawn. For each `surf_t` in the edge scan: set the gradients (`D_CalcGradients` / `D_CalcGradientsTo`: the s/z, t/z and 1/z plane equations of the surface), then by kind: sky (`D_DrawSkyScans8`), water (`Turbulent8`), ordinary surface (`D_CacheSurface` + `D_DrawSpans8`), solid background (`D_DrawSolidSurface`), and write z spans (`D_DrawZSpans`) for brush entities. `D_MipLevelForScale(scale)` picks the mip from the surface's nearest 1/z. `D_DrawPoly`.

**Port changes:** `D_CalcGradientsTo` fills a stack `d_spanparms_t` (instead of a dozen globals read back by the span drawer); with `PD_FAST_EDGES` the span-list heads live on the scan's stack; with `r_maxdist` active, the culled background is palette index 0 (black) rather than `r_clearcolor`.

## `d_surf.c`

The **surface cache**: one block of memory (`D_InitCaches`, sized by `D_SurfaceCacheForRes`, about 600 KB at 320×200) in which `D_SCAlloc` allocates `surfcache_t`s with a rover that wraps and discards old ones (`d_roverwrapped`, `sc_rover`).
`D_CacheSurface(surface, miplevel)` returns the surface's cache entry, **rebuilding it** (calling `R_DrawSurface`) if it is missing, the texture frame changed, a light style changed, or a dynamic light touches it. `D_FlushCaches`, `D_SCDump`, `D_CheckCacheGuard`/`D_ClearCacheGuard`
(a debug guard word, disabled in this port), `D_log2`, `MaskForNum`. **Port:** with `PD_PROFILE_FINE` it records *why* each surface was built (new, dynamic light, texture changed, light gone).

## `d_scan.c`

The span rasterisers.

| Function | Purpose |
| --- | --- |
| `D_DrawSpans8(pspan)` | Textured, lit span of a cached surface: perspective-correct `s` and `t` every 16 pixels (`D_DrawSpans8_C`), linear in between. With `PD_ASM`, a dispatcher around [`D_DrawSpans8_ARM`](perf-infrastructure.md#d_scan_arms). |
| `Turbulent8(pspan)` | Water: the same with a sine warp (`sintable`, `r_turb_*`). |
| `D_DrawZSpans(pspan)` / `D_DrawZSpansP` | Write 1/z for a span. |
| `D_WarpScreen()` | The underwater warp: resample the view through a sine table. |
| **`D_UpscaleScreen`, `D_LowresTouch`, `D_LowresEndFrame`, `D_UpscaleRow`** | The half-resolution hand-over, same design as [`pdr_lowres.c`](renderer-pdr.md#pdr_lowresc) (row pairs marked pending; expanded only when something draws over them via `DRAW_TOUCH`). |

**Port changes:** the low-res hand-over; `SPAN_TOUCH` (preload the next span's header with an `ldr` so the miss overlaps the draw); the dispatcher and `D_AsmCheckSpans` for the assembly; interlaced frames skip kept rows.

## `d_sky.c`

`D_DrawSkyScans8(pspan)`: draws sky spans from the two scrolling layers (`D_Sky_uv_To_st` maps a screen position to sky coordinates). **Port change:** with `PD_LOWRES_3D` the mapping is centred on the half-resolution buffer (`WARP_WIDTH/2`, `WARP_HEIGHT/2`) instead of the full screen, because the sky is drawn into that buffer.

## `d_polyse.c`

**Alias model triangle rasteriser.** The original (`D_PolysetDraw` → `D_RasterizeAliasPolySmooth`): scan the left edge into an array of `spanpackage_t` (`D_PolysetScanLeftEdge`), walk the right edge, draw each span with Gouraud-lit affine texturing (`D_PolysetDrawSpans8`);
small or distant triangles use recursive subdivision (`D_PolysetRecursiveTriangle`, `D_PolysetDrawFinalVerts`); `D_PolysetCalcGradients` computes the plane gradients; `D_PolysetSetEdgeTable` picks the edge case (12 of them).

**Port change (`PD_FAST_ALIAS`):** a *fused* rasteriser (`D_FastRasterizeTriangle`, `D_FastLeftSegment`, `D_FastRightSegment`, `fedgetable_t`, `fgrad_t`) steps the left and right edges together row by row with **all state in locals** and draws each span immediately, instead of ~50 globals and a 32-bytes-per-row span buffer.
The arithmetic is exactly that of the original functions, so the pixels are identical. `D_PolysetRecursiveTriangle`/`DrawFinalVerts` still serve the subdivided case.

## `d_sprite.c`

Draws the clipped quad that [`r_sprite.c`](#r_spritec) projected (`r_spritedesc.pverts`, up to `MAXWORKINGVERTS` points) as a z-tested, textured polygon. `D_DrawSprite` runs four steps:

1. It finds the top and bottom vertices (`minindex`, `maxindex`) and returns if the polygon crosses no scan line; it then points the texture at the frame's pixels (`cacheblock`, `cachewidth`) and closes the polygon by copying vertex 0 past the end.
2. `D_SpriteCalculateGradients` transforms the sprite's axes to view space and fills in the same plane-equation variables the surface drawer uses ([`d_scan.c`](#d_scanc)): `d_sdivz*`, `d_tdivz*` and `d_zi*` (s/z, t/z and 1/z as linear functions of screen x and y), plus `sadjust`, `tadjust`, `bbextents`, `bbextentt`.
3. `D_SpriteScanLeftEdge` and `D_SpriteScanRightEdge` walk the polygon's two sides and fill one `sspan_t` per scan line: the left pass sets each span's `u`, the right pass its `count` (clamped to the view rectangle) and ends the list with `DS_SPAN_LIST_END`.
4. `D_SpriteDrawSpans` draws the spans: s, t and 1/z are computed exactly every 8 pixels and stepped linearly in between (the same 8-pixel runs as `D_DrawSpans8`); colour 255 is transparent and skipped, and a pixel is written, with its z, only where it is at least as near as what the z buffer holds.

**Port changes.** `R_ROW_SKIPPED(v)` skips the rows an interlaced frame keeps from the previous one. The `spans[MAXHEIGHT+1]` scratch array is `static` ("keep big buffers off the small device stack"). The x86 assembly alternative (`id386`) was removed, and the constants are single precision.

## `d_part.c`

Draws particles one at a time: `R_DrawParticles` ([`r_part.c`](#r_partc)) brackets its loop with `D_StartParticles` and `D_EndParticles`, which are empty in the software driver, and calls `D_DrawParticle(pparticle)` for each particle.

`D_DrawParticle`:

1. Transforms the particle's origin to view space (`r_pright`, `r_pup`, `r_ppn`) and drops it when nearer than `PARTICLE_Z_CLIP`.
2. Projects it to a screen position (`u`, `v`) and drops it outside the view rectangle; the right and bottom limits (`d_vrectright_particle`, `d_vrectbottom_particle`) are pulled in by the largest particle size so a block never leaves the view ([`d_modech.c`](#d_modechc) sets them).
3. Sizes it by distance: `pix = izi >> d_pix_shift`, clamped to `d_pix_min` .. `d_pix_max`, so near particles are bigger. It draws a `pix` × `pix` block (rows doubled when `d_y_aspect_shift` is 1), writing the particle's colour and z only where the z buffer says the particle is in front. Sizes 1 to 4 have their own unrolled loops.

**Port changes.** `screenwidth`, `d_zwidth` and `d_y_aspect_shift` are copied to locals once per particle, since a particle block rereads them for every row. When the frame is interlaced (`r_interlace_skip != 2`) the rows kept from the previous frame are skipped (`R_ROW_SKIPPED`). The x86 assembly alternative (`id386`) was removed, and the constants are single precision. The Playdate renderer has its own particle loop in [`pdr_sprite.c`](renderer-pdr.md#pdr_spritec).

## `d_zpoint.c`

`D_DrawZPoint`: draw a point with a z test (the driver hook for `zpointdesc_t`). Not called anywhere in this tree.

## `d_fill.c`

`D_FillRect(rect, colour)`: clear a rectangle, with an aligned 32-bit fast path. Not called anywhere in this tree.
