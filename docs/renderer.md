# Renderer

[← Documentation index](README.md) · [Source index](source-index.md)

What the Playdate renderer does differently from the original and what that is worth on the device. The code is described in [Playdate renderer](engine/renderer-pdr.md).

The default renderer (`PD_NEW_RENDERER`, `winquake/pdr_*.c`) is a rewrite of Quake's 3D drawing for the Playdate's memory system (a small write-through cache in front of slow memory, a fast but tiny stack; see [Profiling on the device](profiling.md)). It draws the same things as the original renderer (world, sky, water, brush entities, alias models, sprites, particles, dynamic lights, light styles, underwater warp, interlacing, draw distance) and uses the same cvars and Options menu settings, but works differently:

- **No edge list or span sorting.** The BSP tree is walked front to back, and every face is rasterized straight into the rows it covers, clipped against a per-row bit mask of the pixels already drawn (kept on the stack). A face that has no uncovered pixels costs only its row walk, and the walk stops as soon as the whole view is covered.
- **Compact map data.** At map load the nodes, leaves and faces are copied into small arrays laid out in walk order: 32-byte nodes, faces with their vertices inline, front-facing faces first. Each face also gets a bounding sphere and its texture set-up.
- **No surface cache.** Faces are textured directly from the mip texture and colormap, with lighting from a small 8-bit light block per face (lightmap resolution, from a 128 KB pool), sampled bilinearly every 8 pixels. Dynamic lights and light-style changes only rebuild those blocks (a few hundred bytes each) instead of whole texture-sized surfaces.
- **Two passes.** The walk only records the spans each face gets, and the faces are then drawn one after another, so each face's texture, light block and set-up stay in the cache while its spans are drawn.
- **Z only where it is read.** The z buffer is written only in the screen rectangles of the alias models, sprites and the weapon that this frame draws (the whole view when particles are visible).
- **Brush entities** (doors, platforms) are clipped into the world leaves they touch and drawn as part of the walk, so they need no separate sorting pass.
- **Alias models** use compact per-model triangle and texture coordinate lists, a stack buffer of projected vertices and their own triangle rasterizer.
- Floating point is single precision throughout, with a polynomial `sin`/`cos` and no `double` anywhere in the frame.

The picture is not bit-identical to the original: lighting is interpolated over 8-pixel runs rather than taken from a 16x16 surface cache, edges are rounded differently, and small alias models (torch flames, distant monsters) come out a pixel thinner, because the original draws them by recursive subdivision, which includes every edge pixel. Over every 25th frame of the three demos, 0.3% of the 3D view's pixels differ by more than a tenth of the brightness range (`tools/hostcheck/compare-shots.py`); most of that is texel rounding on close high-contrast walls and the random spread of particles. It takes ~150-250 KB more memory per map than the original's surface cache.

Measured on the device, playing the demos as timedemos (ms per frame of game work; *3D* is the renderer's sections: setup, world, brush entities, edge scan, entities, weapon view and particles):

| Interlaced on (the default settings) | original | new | change |
|---|---|---|---|
| demo1 total | 36.8 | 28.2 | -23.5% |
| demo1 3D | 27.2 | 18.2 | -33% |
| demo2 total | 36.1 | 29.1 | -19.2% |
| demo2 3D | 24.8 | 17.3 | -30% |
| demo3 total | 41.2 | 31.2 | -24.3% |
| demo3 3D | 31.1 | 21.5 | -31% |

| Interlaced off | original | new | change |
|---|---|---|---|
| demo1 total | 42.0 | 34.0 | -19.0% |
| demo2 total | 40.8 | 34.9 | -14.6% |
| demo3 total | 46.6 | 37.6 | -19.3% |

With the default settings of the time (texture detail low, interlaced on, draw distance 512; light levels did not exist yet, so this is `high`), demo1 runs at 27.9 fps instead of 24.2, demo2 at 27.8 instead of 24.7 and demo3 at 27.2 instead of 22.2. The original renderer already had all of the `PD_FAST_*`, `PD_ASM` and `PD_STACK` optimisations in these runs. The rest of the frame (client, HUD, display) is shared code and is unchanged, which is why the totals improve less than the 3D part. The ms figures in [Settings](settings.md) were measured with the original renderer.

**Light levels.** The "Light levels" option trades lighting precision and texture perspective for speed: below `high` the world uses only 16, 8 or 4 of the colormap's 64 light rows (picked for even steps of brightness on the LCD) and PSX-style affine texturing between the ends of each span. Span drawing is bound by cache misses, and the colormap lookups behind the light are a large part of them. Against `high` in the same run, the world draws 1.1-1.6 ms faster per frame at `medium` (the default), 1.2-1.8 ms at `low` and 1.3-2.0 ms at `lowest` (demos 1-3, interlaced). See [Settings](settings.md) for how it looks and [Light levels](engine/renderer-pdr.md#light-levels) for how it works.

Tried and dropped: integer (fixed-point) maths instead of float for the frustum box tests, the alias triangle rows and the per-8-pixel perspective divide. The Cortex-M7's single-precision FPU is as fast as its integer unit for adds and multiplies and its divide costs about the same, so none of them changed the frame by more than ±0.15 ms, and the integer perspective divide was slower (see [Profiling](profiling.md)).

Renderer experiments: `-DPD_PDR_EXP=n` (with `PD_PROFILE`) runs the code under `if (PDR_EXPERIMENT(n))` (`winquake/pdr.h`) on half of the frames, and `scripts/pd-report.py --ab` compares the two halves, as with `PD_ASM_AB`.
