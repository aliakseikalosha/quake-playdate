# Checking that an optimisation does not change the picture

[← Documentation index](README.md) · [Source index](source-index.md)

The host check harness is described file by file in [Scripts and tools](tools.md).

`tools/hostcheck` runs the real engine and `display.c` on the host. `tools/hostcheck/run.sh` checks the low-res upscale invariants over scripted scenes (walking, console, menus, HUD, view sizes, demos, switching the dithering mode while playing; `HCMD="pd_dither 2"` runs it all in another dithering mode); `HGOLD=file tools/hostcheck/run.sh` writes a hash of every LCD frame of the three demos plus a hash of the 8-bit render buffer and z buffer (`NO_FAST_ALIAS=1` / `NO_FAST_FACES=1` / `NO_FAST_SURFACES=1` build the original code paths, `EXTRA_DEFS="-DFOO=1"` adds compiler flags), and `tools/hostcheck/golden-compare.py a b` compares two such files (build a reference checkout with `TREE=/path OUT=ref NO_LAZY_CHECK=1` if it predates the lazy upscale). Needs clang and the Playdate SDK headers.

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
