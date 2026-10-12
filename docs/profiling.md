# Profiling on the device

[← Documentation index](README.md) · [Source index](source-index.md)

How to build and run the on-device profiler, and what the measurements showed about the hardware. The profiler's code is described in [Performance infrastructure](engine/perf-infrastructure.md).

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
- Floating point is not slower than integer maths (`MICRO,int-vs-float`, single precision): a dependent multiply-add chain is ~24 ns per step either way (VFMA or MLA), a multiply 7.5 ns (VMUL) vs 10 ns (MUL), a divide ~110-130 ns (VDIV) vs ~80-90 ns (SDIV/UDIV), an int -> float -> int round trip ~70 ns. One perspective step of a textured span (1/z, s, t) is 216 ns in float and 259 ns done in integers (CLZ, UDIV reciprocal, 64-bit multiplies). Rewriting the renderer's frustum box tests, alias triangle rows and span perspective divide in integers changed the frame by less than ±0.15 ms each (the span divide got slower), so fixed point is not worth it on this CPU.
- Span drawing (~0.67 us per pixel, some 90 cycles) is mostly cache misses, and the 16 KB colormap is a large part of them: the light level picks one of its 64 rows of 256 bytes for every pixel. Using only 16 of the rows saved ~0.7 ms in the world, as much as dropping the per-8-pixel perspective divide (~0.8 ms); together they are the "Light levels" option (see [Settings](settings.md)).
- `sinf`/`cosf` ~1 us, `sqrtf` ~0.7 us, `floorf` ~0.5 us, any `double` mul/div ~2-5 us and `sqrt(double)` 6.5 us (software floating point).
- Cost of a store run is per 32-byte line touched (~0.7 us), almost regardless of how many bytes it writes: whole rows cost 24 ns/byte, 16-byte segments at a row stride 41-49, an unaligned 18-byte span ~76 (1.4 us per span).
- An early load (a "prefetch by touch") overlaps with ALU work (~73% of a ~1.75 us miss hidden) and with stores (~78%), but not with loads, even cache hits (~1% hidden), so it only helps ahead of load-free work such as FP set-up.
- Surface cache: 73% of the builds in demo1 were dynamic-light rebuilds (muzzle flashes, projectiles), the rest first-time builds of newly visible surfaces; light styles and animated textures are negligible. The cache never thrashes (693 KB, ~4 MB of hunk is free). `d_mipcap 1` (coarser textures, changes the picture) is worth ~1.6 ms of scan: it is the "Texture detail" row of the Options menu (high by default).
- The deepest stack use is the world traversal recursion (~5-6 KB below the frame entry, 64 bytes per BSP level) and, with `PD_STACK`, the drawing of a large alias model; the probes (`PROF_STK`, logged as `STK` lines) show them.
