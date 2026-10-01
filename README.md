# Quake for Playdate

A WinQuake port for the [Playdate](https://play.date/), running on the device and in the Playdate Simulator.

Based on the original [Quake GPL source](https://github.com/id-Software/Quake), through [sysprog21/quake-embedded](https://github.com/sysprog21/quake-embedded).

![Quake demo 1 on the Playdate screen](docs/demo.gif)

*Demo 1 as the Playdate screen shows it. Captured on a computer from the port's own 1-bit display output (see [the last section](#checking-that-an-optimisation-does-not-change-the-picture)), not filmed on a device, so it says nothing about device speed.*

## Controls

| Input | In the game | In Quake's menus |
| --- | --- | --- |
| D-pad up / down | Walk forward / back | Move the cursor |
| D-pad left / right | Turn left / right (crank out: strafe) | Change the highlighted value |
| A | Fire | Select |
| B | Jump | Back |
| Crank | Turn left / right (clockwise = right) | - |

You run by default; the Options menu's "Always Run" turns that off. While the title-screen demo plays, A or B opens Quake's menu.

**Crank out.** Pulling the crank out of the body changes two things:

- The crank turns the view, so D-pad left / right strafe instead of turning.
- Autofire is active (below).

**Autofire.** With the crank out and "Autofire" on in the Options menu (it is on by default), the game holds fire for you while a live monster is in your line of fire: on the crosshair, or inside the range Quake's own auto-aim would turn toward. The axe only swings at what is within reach, and grenade and rocket launchers fire once every 2 seconds instead of being held. Hold A to fire yourself at any time.

**System menu** (the Playdate's menu button):

- **Game Menu** opens Quake's own menu (new game, save and load, Options).
- **Weapon** picks a weapon you own and have ammo for. It appears during play once you can choose between two or more. There is no weapon-cycling button, so this is how you switch.
- **Show FPS** toggles the frame-rate counter (on at launch).

## Building

Requires the [Playdate SDK](https://play.date/dev/) (`PLAYDATE_SDK_PATH` set) and, for the device, the Arm toolchain the SDK installs.

Game data is not included. Copy your `pak0.pak` (the freely distributable shareware one is fine) to
`port/boards/playdate/Source/id1/pak0.pak` before building, or into the game's Data folder at `id1/`.

```shell
git clone https://github.com/aliakseikalosha/quake-playdate && cd quake-playdate

# Simulator
mkdir build-sim && cd build-sim
cmake -DBOARD_NAME=playdate .. && make          # -> quake.pdx
cd ..

# Device
mkdir build-dev && cd build-dev
cmake -DCMAKE_TOOLCHAIN_FILE=../port/boards/playdate/toolchain.cmake -DBOARD_NAME=playdate .. && make
```

Every build gets the next build number: after `pdc`, `port/boards/playdate/pdx_buildnumber.cmake` writes it into the built `.pdx`'s `pdxinfo` (`buildNumber=`). The last number handed out is kept in `port/boards/playdate/.build_number` (not in git, shared by the device, simulator and profiling build directories), so `Source/pdxinfo` stays untouched; the count continues from whichever is larger, that file or the `buildNumber` in `Source/pdxinfo`. A build with nothing to recompile keeps its number.

`-DPD_RENDER_WIDTH=400 -DPD_RENDER_HEIGHT=240` renders at full panel resolution (default 320x200, scaled, for speed).

Performance knobs (device build, pass to `cmake`):

- `-DPD_NEW_RENDERER=ON` (default) draws the 3D view with the renderer written for the Playdate (`winquake/pdr_*.c`, see [Renderer](#renderer)); `OFF` builds Quake's original software refresh (`r_*.c`, `d_*.c`). The `PD_FAST_*`, `PD_ASM` and `PD_STACK` options below only apply to the original renderer.

- `-DPD_REFRESH_RATE=30` frames per second the system asks for (max 50, `0` = as fast as possible). Raise it if lighter scenes have headroom.
- `-DPD_OPT=-O3` optimisation level. Measured on a device: `-O2` is the same as `-O3` and `-Os` is no faster, so there is little to gain here; compare with "Show FPS" in the system menu.
- `-DPD_FAST_EDGES=ON` (default) array-based edge scan, about 6 ms per frame faster in the demos with identical output; it also keeps each surface's span list head on the stack while scanning. `OFF` uses the original linked-list version.
- `-DPD_FAST_ALIAS=ON` (default) alias models (monsters, weapon) are drawn with their per-triangle state in locals instead of ~50 globals plus a span buffer, with the two-buffer pixel stores split into separate runs, and their per-entity setup shares work (transform reused between the bbox check and the draw, `R_LightPoint` remembers where its trace landed for an exact origin). Identical output; `OFF` uses the original code.
- `-DPD_FAST_FACES=ON` (default) world faces keep their edge-emission scratch state and the traversal's bookkeeping (`edge_p`, `surface_p`, keys) on the stack, and world surface visibility is one bit per surface instead of a stamp in every `msurface_t`. Identical output; `OFF` uses the original code.
- `-DPD_FAST_SURFACES=ON` (default) surface cache bitmaps are built row by row (each row is one run of stores instead of sixteen 16-byte segments), and a surface that R_MarkLights flagged as dynamically lit but whose lightmap no light would actually change keeps its cache instead of being rebuilt (48% of the dynamic-light rebuilds in the demos; a rebuild would give the same texels). Identical output; `OFF` uses the original code.
- `-DPD_ASM=ON` (default, device build only) hand-written Thumb-2 assembly for the Cortex-M7 in the hottest CPU-bound loops, each with its C twin kept as the reference: the textured span drawer (`winquake/d_scan_arm.S`) and the edge scan's per-line span generation and active edge table upkeep (`winquake/r_edge_arm.S`). 4-5 ms per frame faster in the demos (demo1 -3.9 ms, demo2 -4.7, demo3 -5.2; 7-9%) with identical output; `OFF` uses the C.
- `-DPD_STACK=ON` (default) puts scratch data on the fast stack instead of in slow static memory, where writing it and reading it back cost ~26 ns per byte plus a miss per line: the per-surface span drawing parameters (gradients, texture adjustments; they used to be a dozen globals written for every surface), a surface cache build's lightmap, an alias model's projected vertices (models up to ~180 vertices: weapons, ogres, zombies, soldiers; not players or dogs) and the alias clipper's scratch polygon. A buffer only goes on the stack while the call chain stays within 6.5 KB of the frame start (`winquake/pd_stack.h`), and the edge scan now returns before the surfaces are drawn so the ~3 KB of scan arrays are off the stack by then. 2.6-3.6 ms per frame faster in the demos (5-7%) with identical output; `OFF` uses the static buffers.
- `-DPD_LOWRES_3D=ON` (default) renders the 3D view at half resolution; the display layer dithers it straight from the half-resolution buffer and only expands the rows that the console, menu or HUD text draw over.

## Renderer

The default renderer (`PD_NEW_RENDERER`, `winquake/pdr_*.c`) is a rewrite of Quake's 3D drawing for the Playdate's memory system (a small write-through cache in front of slow memory, a fast but tiny stack; see [Profiling on the device](#profiling-on-the-device)). It draws the same things as the original renderer (world, sky, water, brush entities, alias models, sprites, particles, dynamic lights, light styles, underwater warp, interlacing, draw distance) and uses the same cvars and Options menu settings, but works differently:

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

With the default settings (texture detail low, interlaced on, draw distance 512), demo1 runs at 27.9 fps instead of 24.2, demo2 at 27.8 instead of 24.7 and demo3 at 27.2 instead of 22.2. The original renderer already had all of the `PD_FAST_*`, `PD_ASM` and `PD_STACK` optimisations in these runs. The rest of the frame (client, HUD, display) is shared code and is unchanged, which is why the totals improve less than the 3D part. The ms figures in [Settings](#settings) were measured with the original renderer.

Renderer experiments: `-DPD_PDR_EXP=n` (with `PD_PROFILE`) runs the code under `if (PDR_EXPERIMENT(n))` (`winquake/pdr.h`) on half of the frames, and `scripts/pd-report.py --ab` compares the two halves, as with `PD_ASM_AB`.

## Settings

The defaults are the settings the author plays with: texture detail low, interlaced on, draw distance 512, crank speed 1.4, max framerate 30 (plus Quake's own defaults). An existing config.cfg keeps whatever it says; "Reset defaults" goes back to these.

The Options menu (drawn with a double-size font; the Customize controls, Go to console, Screen size, Invert mouse, Lookspring and Lookstrafe rows are gone, so the console can no longer be opened on the device; `viewsize` and the others keep whatever config.cfg says) keeps its settings (brightness, **crank speed**, volume, always run, autofire, **texture detail**, **interlaced**, **draw distance**, **max framerate** ...) in `config.cfg` in the game's Data folder. It is written when you leave the Options menu and when the system pauses, locks or terminates the game, and read at the next launch. Key bindings are not saved (they come from `default.cfg` and the port's own button mapping).

The Load and Save menus use the same double-size font. A slot shows the level name cut to 15 characters and the kills as `killed/total` (the full 39-character save comment does not fit at that size).

Texture detail: `low` (default, `d_mipcap 1`, the sharpest mip level is never used; about 1.6 ms per frame faster in the demos, about 3%, with visibly softer textures) or `high` (`d_mipcap 0`).

Interlaced: `on` (default, `r_interlace 1`) or `off` (`r_interlace 0`). When on, each frame draws only every other row of the 3D view (alternating odd and even rows), and the other rows keep the previous frame, so moving edges comb slightly. World spans, sky, alias models, sprites and particles all skip the kept rows, and the display does not re-dither them either. The edge scan, world traversal and surface cache builds still run in full, so the gain is less than half: demo1 44.5 -> 39.2 ms per frame on the device (-12%, scan 16.0 -> 11.6 ms). A resized view or a new map always gets one full frame.

Crank speed: degrees of view turn per degree of crank (`crank_speed`, 0.2-3 in steps of 0.2, default 1.4). It replaces the Mouse Speed slider.

Draw distance: a slider over 256, 384, 512, 768, 1024, 1536, 2048, 3072 and unlimited (the right end; `r_maxdist` in Quake units, default 512, 0 = unlimited). World leaves whose bounding box is entirely beyond it are not drawn (except their sky, so the sky still shows behind them), and neither are models, brush entities or particles beyond it; what is culled is drawn black (the background surface uses palette index 0 instead of `r_clearcolor` while a distance is set). A face is still drawn while any near leaf holds it, so the cut follows leaf boundaries rather than a clean line. demo1 is mostly indoors: 768 saves about 0.5 ms per frame there (40.2 -> 39.7); open areas gain more.

Max framerate: 30, 50 or unlimited (`pd_maxfps`, 0 = unlimited): the rate the Playdate calls the game at (`display->setRefreshRate`). The default is 30; the build's `PD_REFRESH_RATE` only applies while the game loads, until the settings are read. A change applies on the next frame, also from the console or config.cfg. Above 30 the battery drains faster, and a higher cap only helps in scenes the game renders in under 33 ms (50) or 20 ms (unlimited).

Under water, slime and lava Quake blends the whole palette 50-60% towards the liquid's colour. On the 1-bit display only the luminance survives, so that blend just squeezed the picture into the midtones (under water black became mid grey and the highlights clipped to white). The port leaves the liquid tint out of the palette (`V_UpdatePalette` in `winquake/view.c`), so the view keeps the full black-to-white range; the underwater warp, and the damage, pickup and powerup flashes, are unchanged.

## Profiling on the device

`-DPD_PROFILE=ON` builds an on-device profiler (nothing is compiled in otherwise): per-frame section timings and counters go to `prof.csv` in the game's Data folder. Add `-DPD_BENCH=ON` to play the demos back with `timedemo` (every demo frame is rendered, so builds can be compared frame for frame; `-DPD_BENCH_COUNT=1` plays only demo1, `-DPD_BENCH_CMDS="d_mipcap 1"` runs console commands first) and `-DPD_PROFILE_FINE=ON` for more detailed sections (adds about 2 ms per frame of timer overhead).

```shell
cmake -DCMAKE_TOOLCHAIN_FILE=../port/boards/playdate/toolchain.cmake -DBOARD_NAME=playdate \
      -DPD_PROFILE=ON -DPD_BENCH=ON -DPD_BENCH_COUNT=1 .. && make
scripts/pd-bench.sh build-prof demo1 105        # installs quake_PROF.pdx, runs, fetches bench-results/demo1.csv
scripts/pd-report.py bench-results/demo1.csv [other.csv]
```

A profiling build shows up in the launcher as "Quake Playdate (profile, ...)" with its options listed (demos, `fine`, `asm A/B`, `asm check`, `stack A/B`), so it is not mistaken for the game; it still shares the game's bundle ID and so its Data folder (`config.cfg`, saves, `prof.csv`).

The device sleeps after a few minutes and disconnects USB, so keep runs short or set Auto-lock to Never (`-DPD_BENCH_FIRST=2 -DPD_BENCH_COUNT=1` plays demo2 alone).

Stack versus static buffers: `-DPD_STACK_AB=ON` (with `PD_PROFILE`) does the same for `PD_STACK`, with the assembly always on.

Assembly versus C: `-DPD_ASM_AB=ON` (with `PD_PROFILE`) runs either the assembly or the C in each frame, picked by a hash of the frame number, and `scripts/pd-report.py --ab bench-results/x.csv` compares the two within one run (same binary, same scenes). Trust its "paired" column, which compares each frame with its neighbour: the plain means of the two halves differ by up to ~1 ms in sections the assembly does not touch. `-DPD_ASM_CHECK=ON` instead runs the assembly and then the C over the same input on every call and counts what differs (`asm_bad` column, `ASMBAD` lines); it must stay 0.

What the measurements showed about this hardware (useful when optimising):

- The heap, `.bss` and code live in slow memory behind a write-through data cache of about 16 KB: every byte stored costs about 26 ns (a 32-bit store to a global about 100 ns) and every cache line that misses about 1 us. The stack is fast internal RAM (a store is about 14 ns), but the game task's stack is only about 10 KB.
- So frame time is set by how many bytes are written to global memory and how many cache lines are touched, not by instruction counts. Keep hot scratch state in locals, avoid rewriting large buffers, and prefer compact, contiguous data.
- Static data layout alone moves a section by 2 ms or so, so compare builds only with identical instrumentation. The `PD_PROFILE_FINE` timers are cold-call expensive and distorted some sections by up to 3 ms, so confirm a result with the coarse profiler (or the "game says: N frames, T seconds" line) before believing it. The same binary run twice agrees to within ~0.3 ms, so differences between builds are layout, not noise.
- Scattered stores are the expensive kind: a store to a line that is not being streamed costs 0.4-0.9 us even when it hits the cache, while a run of adjacent stores costs ~26 ns/byte. Alternating stores between two buffers (view byte, then z halfword, per pixel) costs about twice as much as writing each buffer in its own run. Keep scratch state in locals, write outputs in ascending order into one line at a time.
- Independent cache misses do not overlap each other (four loads in flight take four times one miss), and there is no prefetch, but an early load does overlap with ALU work. A store to an uncached line does not allocate it, so data written and then read back soon after misses.
- Instruction counts do matter where the data is on the stack or already cached: the span drawer's per-span set-up (divides, clamps, spilled state) was ~1 us of each span's cost, and the edge scan spent most of its time in newlib's `memmove`, which GCC calls for struct-array shifts and which copies an overlapping block that moves up one byte at a time. The assembly versions took 0.9 ms off the spans, 1.2 ms off edge insertion and 1.7 ms off span generation. The loops that only store (z spans, surface cache rows) are bound by the bus, and asm does not help them.
- Globals written for every item of a hot loop add up: moving the dozen span-drawing parameters that were set per surface (~50 a frame) into a block on the stack saved ~0.8 ms, far more than 26 ns per byte suggests, as the stores stall the span drawing that follows. The same goes for any scratch buffer that is written and read straight back (lightmaps, projected vertices, clipped polygons: another ~2 ms).
- The stack's real size is not documented (the SDK's `STACK_SIZE` only reaches an unused assembler define). The world traversal of a `PD_PROFILE_FINE` build goes ~7 KB below the frame start every frame without trouble; `PD_STACK` keeps its buffers within 6.5 KB. `-fstack-usage` (e.g. `-DCMAKE_C_FLAGS=-fstack-usage`) lists every function's frame, and the `STK` lines of `prof.csv` show the measured depths.
- `sinf`/`cosf` ~1 us, `sqrtf` ~0.7 us, `floorf` ~0.5 us, any `double` mul/div ~2-5 us and `sqrt(double)` 6.5 us (software floating point).
- Cost of a store run is per 32-byte line touched (~0.7 us), almost regardless of how many bytes it writes: whole rows cost 24 ns/byte, 16-byte segments at a row stride 41-49, an unaligned 18-byte span ~76 (1.4 us per span).
- An early load (a "prefetch by touch") overlaps with ALU work (~73% of a ~1.75 us miss hidden) and with stores (~78%), but not with loads, even cache hits (~1% hidden), so it only helps ahead of load-free work such as FP set-up.
- Surface cache: 73% of the builds in demo1 were dynamic-light rebuilds (muzzle flashes, projectiles), the rest first-time builds of newly visible surfaces; light styles and animated textures are negligible. The cache never thrashes (693 KB, ~4 MB of hunk is free). `d_mipcap 1` (coarser textures, changes the picture) is worth ~1.6 ms of scan: it is the "Texture detail" row of the Options menu (high by default).
- The deepest stack use is the world traversal recursion (~5-6 KB below the frame entry, 64 bytes per BSP level) and, with `PD_STACK`, the drawing of a large alias model; the probes (`PROF_STK`, logged as `STK` lines) show them.

## Checking that an optimisation does not change the picture

`tools/hostcheck` runs the real engine and `display.c` on the host. `tools/hostcheck/run.sh` checks the low-res upscale invariants over scripted scenes (walking, console, menus, HUD, view sizes, demos); `HGOLD=file tools/hostcheck/run.sh` writes a hash of every LCD frame of the three demos plus a hash of the 8-bit render buffer and z buffer (`NO_FAST_ALIAS=1` / `NO_FAST_FACES=1` / `NO_FAST_SURFACES=1` build the original code paths, `EXTRA_DEFS="-DFOO=1"` adds compiler flags), and `tools/hostcheck/golden-compare.py a b` compares two such files (build a reference checkout with `TREE=/path OUT=ref NO_LAZY_CHECK=1` if it predates the lazy upscale). Needs clang and the Playdate SDK headers.

`NEW=1` builds either harness with the new renderer (`NEW=1 tools/hostcheck/run.sh` checks the upscale invariants with it, `HMAPS=1` loads every shareware map and turns, walks and fires in each, as a crash and limits check). `HSHOTS=<dir>` plays the three demos and writes every 25th frame's 3D view (`dD_NNNN.ppm`) and LCD picture; `tools/hostcheck/compare-shots.py <old dir> <new dir> [<out dir>]` prints how many pixels differ and writes side-by-side PNGs. For example, to compare the two renderers (after one `tools/hostcheck/run.sh`, which sets up the run directory):

```shell
tools/hostcheck/build.sh && NEW=1 OUT=hnew tools/hostcheck/build.sh
mkdir -p /tmp/old /tmp/new && cd tools/hostcheck/out/run
HSHOTS=/tmp/old ../hostcheck && HSHOTS=/tmp/new ../hnew && cd -
tools/hostcheck/compare-shots.py /tmp/old /tmp/new /tmp/sbs
```

`HFRAMES=<prefix> tools/hostcheck/run.sh` plays a demo back in real time and writes what the LCD shows as `<prefix>-NNNN.pbm` pictures (`HDEMO` picks the demo, `HSKIP` the frames to skip, `HEVERY` the frames between pictures, default 2 = 15 per second, `HCOUNT` how many). `docs/demo.gif` was made from them:

```shell
mkdir -p /tmp/f && HFRAMES=/tmp/f/f HDEMO=1 HSKIP=200 HCOUNT=150 tools/hostcheck/run.sh
ffmpeg -framerate 15 -i /tmp/f/f-%04d.pbm -vf "format=rgb24,lutrgb=r='if(gt(val,127),177,49)':g='if(gt(val,127),174,47)':b='if(gt(val,127),167,40)',scale=800:480:flags=neighbor,split[a][b];[a]palettegen=stats_mode=full[p];[b][p]paletteuse=dither=none:diff_mode=rectangle" -loop 0 docs/demo.gif
```
