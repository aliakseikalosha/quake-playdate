# Scripts and tools

[← Documentation index](README.md) · [Source index](source-index.md)

Two directories hold the helper programs:

- [`scripts/`](../scripts/): things you run on your computer: around a device build (release build, install, benchmark, report), to generate data (the blue-noise tile) and to generate the [source code index](source-index.md).
- [`tools/hostcheck/`](../tools/hostcheck/): a harness that runs the real engine and the real Playdate display code *on the host* to prove an optimisation did not change the picture.

| File | Purpose |
| --- | --- |
| [`scripts/release-device.sh`](#scriptsrelease-devicesh) | Clean Release build of the device `.pdx` with a new build number |
| [`scripts/install-device.sh`](#scriptsinstall-devicesh) | Copy the device `.pdx` to a USB-connected Playdate and launch it |
| [`scripts/pd-bench.sh`](#scriptspd-benchsh) | Run a profiling build on the device and fetch `prof.csv` |
| [`scripts/pd-report.py`](#scriptspd-reportpy) | Summarise and compare `prof.csv` files |
| [`scripts/gen-launcher-assets.py`](#scriptsgen-launcher-assetspy) | Generate the launcher card, icon, launch image and launch sound |
| [`scripts/gen-bluenoise.py`](#scriptsgen-bluenoisepy) | Generate the blue-noise dither tile |
| [`scripts/gen-source-index.py`](#scriptsgen-source-indexpy) | Generate the [source code index](source-index.md) and check the docs' links |
| [`tools/hostcheck/hostcheck.c`](#toolshostcheckhostcheckc) | The host-side test program |
| [`tools/hostcheck/build.sh`](#toolshostcheckbuildsh) | Build it against a source tree |
| [`tools/hostcheck/run.sh`](#toolshostcheckrunsh) | Build and run it |
| [`tools/hostcheck/golden-compare.py`](#toolshostcheckgolden-comparepy) | Compare two golden hash files |
| [`tools/hostcheck/compare-shots.py`](#toolshostcheckcompare-shotspy) | Measure how much two renderers' pictures differ |

---

## `scripts/release-device.sh`

A clean **Release** build of the game for the device, with a new build number. Also available as the VS Code task **Playdate: release build (device)**.

```shell
scripts/release-device.sh                    # → build-release/quake_DEVICE.pdx
BUILD_DIR=build-foo scripts/release-device.sh
```

Steps:

1. Removes the build directory (`build-release`, git-ignored) and the stale `Source/pdex.*` files that `pdc` would bundle, so everything is rebuilt and the post-build step that hands out the build number always runs.
2. Configures a Release device build (`-DCMAKE_BUILD_TYPE=Release -DPD_RELEASE=ON -DPD_PROFILE=OFF -DPD_BENCH=OFF`, with the SDK's ARM toolchain) and builds it with all cores.
3. Ships the shareware data: when `port/boards/playdate/Source/id1/pak0_demo.pak` exists, the `.pdx` gets it as `id1/pak0.pak` instead of `pak0.pak` (`-DPD_RELEASE=ON`, [`pdx_pak.cmake`](build-system.md#which-pak0pak-the-pdx-gets); every other build leaves `pak0_demo.pak` out of its `.pdx`). The script checks the two files are identical and stops if not. Without a `pak0_demo.pak` the release keeps `pak0.pak`.
4. Reads the build number the build wrote into the `.pdx`'s `pdxinfo` ([`pdx_buildnumber.cmake`](build-system.md#playdate-board-cmakeliststxt): one more than the larger of `.build_number` and the `buildNumber` in `Source/pdxinfo`) and checks it went up. If it did not, the script stops with an error.
5. Writes that number into `port/boards/playdate/Source/pdxinfo`, so the release's number is recorded in the source tree (a tracked file: commit it with the release) and the next build counts on from it. Nothing else in `Source/pdxinfo` is changed (to change the `version=` line, edit it by hand).

```
Release build done: build-release/quake_DEVICE.pdx
  version 0.3, build number 88 -> 89 (also written to port/boards/playdate/Source/pdxinfo)
  game data: id1/pak0.pak is pak0_demo.pak
```

Run twice, it goes 89 → 90: every release gets its own number. The built `pdxinfo` and `Source/pdxinfo` then agree; an ordinary build (`build-dev`, simulator) only changes the built `pdxinfo`, never `Source/pdxinfo`.
The script does not install the result; [`install-device.sh`](#scriptsinstall-devicesh) installs `build-dev/quake_DEVICE.pdx`.

## `scripts/install-device.sh`

Copies `build-dev/quake_DEVICE.pdx` to a Playdate over USB and starts it. The SDK's `pdutil` has no
"install" command, so the script mounts the device's data disk, copies into `Games/`, ejects, and
runs the game.

```shell
cmake --build build-dev -j 8     # make the device build first
./scripts/install-device.sh
```

What it does, in order:

1. Finds the serial port `/dev/cu.usbmodemPD*` (the device must be unlocked and connected).
2. `pdutil <port> datadisk` mounts `/Volumes/PLAYDATE`.
3. Waits for `/Volumes/PLAYDATE/Games`, then `cp -RX`es the `.pdx` (with `pak0.pak`, so this takes a while). It retries up to five times; the volume can appear before it is writable. It uses `cp`, not `rsync`, because macOS `rsync` cannot `mkdir` on the FAT data disk.
4. A `cleanup` trap removes the AppleDouble stub (`._quake_DEVICE.pdx`), runs `sync`, and ejects the disk, also when a step fails.
5. Waits for the serial port to return and runs `pdutil <port> run /Games/quake_DEVICE.pdx`.

If macOS says "Operation not permitted", allow the app running the script under
*System Settings → Privacy & Security → Files and Folders → Removable Volumes*.

## `scripts/pd-bench.sh`

Runs a profiling build (`-DPD_PROFILE=ON`, usually with `-DPD_BENCH=ON`) on a connected Playdate and
fetches the resulting `prof.csv`.

```shell
scripts/pd-bench.sh <build-dir> <label> <seconds>

scripts/pd-bench.sh build-prof demo1-before 105
scripts/pd-report.py bench-results/demo1-before.csv
```

- Installs the build as `Games/quake_PROF.pdx` (your `quake_DEVICE.pdx` is untouched). The first run
  copies the whole pak; later runs only replace `pdex.bin` and `pdxinfo`.
- Launches it, waits `<seconds>`, mounts the data disk again, copies
  `Data/<bundleID>/prof.csv` to `bench-results/<label>.csv`.
- Nothing is deleted from the device.
- The Playdate sleeps after a few minutes and disconnects USB. Keep the run short (a timedemo of one
  demo takes ≈ 80–100 s) or set *Auto-lock* to *Never*.

`bench-results/` is git-ignored.

## `scripts/pd-report.py`

Summarises `prof.csv` files written by [`pdprof.c`](port/playdate.md#pdprofc).

```shell
scripts/pd-report.py bench-results/a.csv             # per-demo summary
scripts/pd-report.py bench-results/a.csv b.csv       # … and b compared with a
scripts/pd-report.py --ab bench-results/a.csv        # A/B build: switch off vs on
```

- Times are per frame in milliseconds of *game work*. The "wall" fps includes what the system does between frames.
- Sections nest: `scan` contains `dsurf`, which contains `cache`/`spans`/`zspan`/`other`. Finer sections need `-DPD_PROFILE_FINE=ON`.
- `load()` understands the line types `H` (header), `P` (per-frame row), `BENCH` (demo start/stop markers) and `L` (a mirrored console line, shown as "game says: …").
- `--ab` works on builds with `PD_ASM_AB`, `PD_STACK_AB` or `PD_PDR_EXP`. It prints the frames with the switch off against those with it on, plus a **paired** column that compares each frame with its neighbour. Trust the paired column; the plain means of the two halves can differ by up to ≈ 1 ms in sections the switch does not touch. The paired difference is *on minus off*; with `PD_PDR_EXP` the experiment runs on the **off** frames, so a positive number means the experiment is faster.

```python
# the core of load(): rows are dicts keyed by the column names from the H line
hdr = line.split(",")[1:]            # "H,ms,period,frame,input,..."
v   = line.split(",")[1:]            # "P,12345,33012,31200,..."
rows.append(dict(zip(hdr, map(int, v))))
```

## `scripts/gen-launcher-assets.py`

Makes the assets the Playdate launcher shows for the game ([`pdxinfo`](build-system.md#source)'s `imagePath=launcher` and `launchSoundPath=launcher/launch`) out of the game's own data.

```shell
scripts/gen-launcher-assets.py                       # Source/id1/pak0_demo.pak (else pak0.pak) -> Source/launcher/
scripts/gen-launcher-assets.py <pak0.pak> <out dir>
```

| File | Size | What |
| --- | --- | --- |
| `card.png` | 350×155 | Q logo, "QUAKE" and "PLAYDATE" on dark stone: the card in the launcher's cards view |
| `card-pressed.png` | 350×155 | The card inverted, while A is held |
| `icon.png` | 32×32 | The Q logo, for the list view |
| `launchImage.png` | 400×240 | Large Q logo and "LOADING", shown while the game loads |
| `wrapping-pattern.png` | 400×240 | Rows of small Q logos: wrapping paper of a newly downloaded copy |
| `launch.wav` | 0.3 s | The ammo pickup sound (`sound/weapons/lock4.wav`) as 16-bit PCM, played during the launch animation (`pdc` makes it a `.pda`) |

- Pure Python, standard library only, deterministic; about 2 s.
- The Q logo is cut out of `gfx/conback.lmp` (the console background, where it is a dark embossed shape): brightness blurred over 5×5, thresholded, the largest connected shape kept, its edges rounded, then resampled with 4×4 supersampling. The text is the console font (`CONCHARS` in `gfx.wad`, its lit face only), the stone is `BACKTILE` ordered-dithered to at most 25 % white. Every image is plain black and white, so `pdc` does not dither it again.
- The output is derived from the game data and is not in git (`Source/*/` is ignored). The CMake configure step runs the script when `Source/launcher/card.png` is missing; after changing the script, run it by hand. A release build keeps `launcher/`: [`pdx_pak.cmake`](build-system.md#which-pak0pak-the-pdx-gets) only drops folders that hold game data.
- To use another sound, change `SOUND` at the top of the script (any `.wav` in the pak).

## `scripts/gen-bluenoise.py`

Generates the 32×32 blue-noise threshold tile used by the *blue noise* dithering mode, with
Ulichney's **void-and-cluster** method on a torus.

```shell
scripts/gen-bluenoise.py > port/boards/playdate/bluenoise.h
```

- Pure Python, deterministic (`SEED = 20261003`, `SIGMA = 1.5`, `N = 32`).
- Every pixel gets a distinct rank 0…1023 so that for any threshold the pixels below it are spread as evenly as possible. Ranks map to thresholds `0…254`, so the display's test `luminance > threshold` gives all black for 0 and all white for 255.
- Three phases: remove ones from the tightest clusters (ranks `ones-1 … 0`); add ones into the largest voids (ranks `ones … n²/2-1`); then fill the rest by the tightest clusters of zeros.

The output is a C header: `#define BLUENOISE_SIZE 32` and `static const uint8_t bluenoise[...]`.

---

## `scripts/gen-source-index.py`

Generates [`docs/source-index.md`](source-index.md): every file of the repository with what it does, whether it is built, its size, and a link to the section of these docs that describes it.

```shell
scripts/gen-source-index.py            # write docs/source-index.md
scripts/gen-source-index.py --check    # change nothing; exit 1 if the index is out of date or a link is broken
```

Python 3, standard library only. It asks `git` for the tracked files and for the untracked ones that are not ignored, so it runs from a git checkout.

**Where the text comes from.** The script holds the index's content: a list of `group(title, intro, base, [(path, built, description, doc), ...])` calls, one row per file. `doc` is the page and `#anchor` of the file's section, and `{name}` inside a description becomes a link to that file's section (`name` is the basename, or the path when two files share a basename, as with the four `CMakeLists.txt`). Computed: each file's line count, the **Docs** column's labels, the build table and the lookup by name.

**What it stops on.** When the list and the repository disagree it prints the files and exits 1, writing nothing:

```text
scripts/gen-source-index.py: the file list in this script and the repository disagree.
  in the repository but not listed (add a row to the right group):
    winquake/new_file.c
```

After writing, it checks every relative link and `#anchor` in `README.md` and `docs/**/*.md` (fenced code blocks skipped, anchors as GitHub makes them from the headings) and exits 1 listing the broken ones, so a renamed heading is found here and not by a reader.

**When to run it.** After adding, removing or renaming a file, or renaming a heading that a row points at: change the row, run the script, commit both. `--check` is for a pre-commit hook or CI. A change to the script itself changes its own line count in the index, so run it once more afterwards.

---

## `tools/hostcheck/hostcheck.c`

Host-side checks for the Playdate port. It links the real engine (`winquake/*.c`), the real
`port/sys_port.c` / `vid_port.c` and the real [`display.c`](port/playdate.md#displayc), with a fake
`PlaydateAPI` (just `graphics->getFrame`, `markUpdatedRows`), sound stubs, and a fake clock
(`qembd_udelay` advances a counter). It is built twice into one binary: `display.c` as `tree_*`
(the code under test) and, for the lazy-upscale check, a modified copy as `old_*`.

### Modes (selected by environment variables)

| Variable | Mode | What it does |
| --- | --- | --- |
| *(none)* | default | Scripted play: load e1m1, walk, FPS counter, notify lines, console, every menu, view sizes, pause, FOV, weapons, scoreboard, crosshair, then demo1–3, then switch the dithering mode while playing. On every screen update it checks two invariants (below). Exit status 1 if either fails. |
| `HGOLD=<file>` | golden | Plays demo1–3 with `timedemo` and writes one hash of the 1-bit LCD image per frame, plus hashes of the 8-bit render buffer and z buffer. |
| `HMENU=1` | menu | Sends keys through the Options menu (crank speed, music, texture detail, light levels, interlaced, dithering, draw distance, max framerate, show FPS) and checks what it does and saves. The row numbers are hard-coded in `menu_test`: update them when a row is added. |
| `HMODS=1` | mods | Makes fake game folders in the run directory (`hipnotic/pak0.pak`, `zmod/maps/`, and `notamod/` which is not a game), opens the main menu's Mods screen and checks the list, its order, the confirmation and the folder handed to `qembd_restart` (a stub here). Expects id1 to be running. |
| `HINPUT=1` | input | Checks the key bindings the port relies on (arrow keys, ctrl, space, `,` and `.`) exist and move, turn, fire and jump the player in e1m1, and survive `exec default.cfg`. Fails if the pak has no `default.cfg` and the built-in one (`winquake/defaultcfg.h`) is missing. |
| `HFRAMES=<prefix>` | frames | Plays a demo in real time and writes the LCD as `<prefix>-NNNN.pbm`. `HDEMO`, `HSKIP`, `HEVERY` (default 2), `HCOUNT` tune it. Used to make `docs/demo.gif`. |
| `HSHOTS=<dir>` | shots | Plays the demos and writes every `HEVERY`-th (default 25) frame's 3D view (`dD_NNNN.ppm`) and LCD picture. `HDEMOS="1 2 3"` picks demos. |
| `HMAPS=1` | maps | Loads every shareware map (`start`, `e1m1`–`e1m8`) and turns, walks and fires in each: a crash and limits check. `HMAPS="hip1m1 hip1m2 …"` loads those maps instead (a mod's, with `HARGS="-game …"`). |
| `HWEAPONS=<map>` | weapons | On that map gives every weapon (`impulse 9`) and picks each one the Weapon system menu lists through `port/boards/playdate/weapons.c` (compiled in), first to last and back, checking the weapon in hand. With `HARGS="-game hipnotic"` etc. it checks the mission packs' impulses against their QuakeC. |
| `HINTERLACE=1` | modifier | Any mode above with `r_interlace 1`. |
| `HCMD="…"` | modifier | Console commands before any mode (e.g. `HCMD="pd_dither 2"`). Every mode except `HMENU` first sets `d_mipcap 0`, `r_lightlevels 64`, `r_interlace 0`, `r_maxdist 0` and `scr_showfps 0`, so pictures stay comparable with the original renderer; use `HCMD` to test the defaults instead (e.g. `HCMD="r_lightlevels 16"`). |
| `HLOG=1` | modifier | Print engine log lines. |
| `HARGS="…"` | modifier | Quake's command line for any mode, as the Playdate's launch arguments give it (e.g. `HARGS="-game mymod"`, `-mem 9` for a heap up to 16 MB). |

The first argument is the frame count per scenario (default 150).

### The two invariants

The Playdate display layer dithers the half-resolution view *directly*, skipping an expensive
expansion into the 8-bit buffer. That is only valid if every draw primitive that writes into the
view calls `DRAW_TOUCH` first. hostcheck verifies this on every `qembd_fillrect`:

1. **No hook violation.** No row pair that is still "pending" may differ from the snapshot taken when the half-resolution view was handed over. If it does, something wrote into the view without calling `DRAW_TOUCH`:
   `HOOK VIOLATION scn=… pending pair N was written without DRAW_TOUCH`
2. **No LCD mismatch.** The LCD image produced by dithering pending rows straight from the half-resolution buffer must equal the one produced the old way (expand the view into the frame buffer, then dither):
   `LCD MISMATCH scn=… frame N byte M …`

```c
// the heart of the check: two copies of display.c, one image each
cur_frame = frame_new;  tree_fillrect(src, clut, x, y, xs, ys);   // new path: dither from half-res
if (qembd_lowres_active) D_LowresTouch(0, H);                      // expand every pending row
cur_frame = frame_old;  old_fillrect(src, clut, x, y, xs, ys);     // old path
if (memcmp(frame_new, frame_old, FRAME_BYTES)) { /* report first differing byte */ }
```

## `tools/hostcheck/build.sh`

Builds `hostcheck.c` against a source tree with `clang`. Needs the Playdate SDK only for `pd_api.h`.

```shell
tools/hostcheck/build.sh                                      # this repo → tools/hostcheck/out/hostcheck
TREE=/path/to/other/checkout OUT=ref tools/hostcheck/build.sh # a reference checkout → out/ref
NEW=1 OUT=hnew tools/hostcheck/build.sh                       # the new renderer (pdr_*.c)
NO_FAST_ALIAS=1 NO_FAST_FACES=1 NO_FAST_SURFACES=1 tools/hostcheck/build.sh   # original code paths
NO_STACK=1 …        # scratch buffers in static memory
NO_LAZY_CHECK=1 …   # for trees that predate the lazy low-res upscale
EXTRA_DEFS="-DFOO=1" …                                        # extra compiler flags
```

It compiles the engine file lists (the `pdr_*` set with `NEW=1`, the `r_*`/`d_*` set otherwise),
the four `port/*_port.c` files, `fio_posix.c`, both copies of `display.c` (renaming the `qembd_*`
symbols with `-D` so they can coexist, and generating `display_old.c` with a small Python patch),
and links everything with `-lm`. Defaults: `-O1 -g`, 400×240, `PD_LOWRES_3D`, and a generous
`PD_STACK_BUDGET` (64 KiB) because the host's stack frames are larger.

### The input test (`HINPUT=1`)

`input_test()` checks that the keys the port sends are bound, and that they move the player. It presses the same keys
[`poll_input`](port/playdate.md#input-mapping) pushes (`Key_Event`), so it also fails when the pak has no `default.cfg` and the
[built-in one](engine/core.md#built-in-defaultcfg-defaultcfgh) is missing:

```c
run(3);		/* quake.rc (and so default.cfg) is executed by the first frames */
MCHECK(!strcmp(binding(K_UPARROW), "+forward"), "UPARROW is +forward");
MCHECK(!strcmp(binding(K_CTRL),    "+attack"),  "CTRL is +attack");
...
cmd("map e1m1\n");
run(80);
AngleVectors(cl.viewangles, fwd, right, up);
VectorCopy(sv_player->v.origin, from);
Key_Event(K_UPARROW, true);  run(20);  Key_Event(K_UPARROW, false);  run(10);
MCHECK(moved(from, fwd) > 50, "holding UP walks forward");   /* DOWN, ',' and '.' likewise (< -50, strafe) */

yaw = cl.viewangles[YAW];
Key_Event(K_LEFTARROW, true);  run(10);  Key_Event(K_LEFTARROW, false);  run(2);
MCHECK(cl.viewangles[YAW] > yaw + 10, "holding LEFT turns left");

Key_Event(K_CTRL, true);  run(3);
MCHECK(sv_player->v.button0 != 0, "A (ctrl) fires");           /* K_SPACE: v.button2, "B jumps" */

/* "Reset defaults" runs default.cfg again: the bindings must survive it */
cmd("unbindall\nexec default.cfg\n");
```

Against the original engine and the re-release pak every check fails; with the built-in `default.cfg` all pass, with the
re-release and with the shareware pak. It exits 1 on any failure and prints `input test passed` otherwise.
To look at everything the console prints with a given pak (the way the re-release's `campaign` spam was found):
`cd tools/hostcheck/out/run && HLOG=1 ../hostcheck 200 | sort | uniq -c | sort -rn | head`.
See [Game data](port/game-data.md#checking-a-pak).

## `tools/hostcheck/run.sh`

```shell
tools/hostcheck/run.sh [frames]               # build, set up the run directory, run default mode
HGOLD=file tools/hostcheck/run.sh             # golden mode
HMENU=1 tools/hostcheck/run.sh                # menu test
HINPUT=1 tools/hostcheck/run.sh               # key bindings and movement test
HMODS=1 tools/hostcheck/run.sh                # Mods screen test
HINTERLACE=1 tools/hostcheck/run.sh           # any of the above with interlaced rendering
NEW=1 tools/hostcheck/run.sh                  # …with the new renderer
```

It calls `build.sh`, creates `out/run/id1/`, symlinks `port/boards/playdate/Source/id1/pak0.pak`
into it (the run fails with a message if the pak is missing) and starts the binary there.

## `tools/hostcheck/golden-compare.py`

Compares two golden files (`HGOLD=`) frame by frame; equal hashes mean identical pictures.

```shell
HGOLD=/tmp/before.txt tools/hostcheck/run.sh
# … make an optimisation …
HGOLD=/tmp/after.txt  tools/hostcheck/run.sh
tools/hostcheck/golden-compare.py /tmp/before.txt /tmp/after.txt
#  IDENTICAL: <N> frames                         (exit 0)
#  DIFFERENT: <k> of <N> frames differ; first: …  (exit 1)
```

The first 8 frames of demo1 are skipped: they show start-up state that differs between builds of identical source.

## `tools/hostcheck/compare-shots.py`

Compares the pictures two hostcheck builds wrote in `HSHOTS` mode. Pure Python, no libraries.

```shell
tools/hostcheck/build.sh && NEW=1 OUT=hnew tools/hostcheck/build.sh
mkdir -p /tmp/old /tmp/new && cd tools/hostcheck/out/run
HSHOTS=/tmp/old ../hostcheck && HSHOTS=/tmp/new ../hnew && cd -
tools/hostcheck/compare-shots.py /tmp/old /tmp/new /tmp/sbs
```

For every `dD_NNNN.ppm` present in both directories it prints the share of pixels whose brightness
differs noticeably. With an output directory it also writes `old | new` side by side at 2×
(`dD_NNNN.png`) and the LCD pictures (`dD_NNNN-lcd.png`). It decodes PPM (P6) and PBM (P4) itself
and writes PNG with `zlib`/`struct`.
