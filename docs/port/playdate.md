# Playdate board

[← Documentation index](../README.md) · [Source index](../source-index.md) · Directory: [`port/boards/playdate/`](../../port/boards/playdate/)

The Playdate is the project's only target: a 168 MHz Cortex-M7 with a single-precision FPU,
a 400×240 1-bit LCD, a D-pad, A/B buttons and a crank. The game runs as a **C pure-game** `.pdx`:
the system calls an *update callback* once per frame, so unlike a desktop program the port
never owns the main loop.

```
Playdate OS ──eventHandler()──► main.c ──update()──► poll_input() ─► qembd_frame() ─► Host_Frame()
                                                                                          │
   LCD (1-bit) ◄── display.c: qembd_fillrect() ◄── VID_Update() ◄── renderer ◄────────────┘
```

| File | Role |
| --- | --- |
| [`main.c`](#mainc) | Event handler, update callback, input mapping, system menu, time, heap |
| [`display.c`](#displayc) | 8-bit paletted frame → 1-bit LCD, four dithering modes |
| [`bluenoise.h`](#bluenoiseh) | 32×32 blue-noise threshold tile (generated) |
| [`fio.c`](#fioc) | `Sys_File*` on the Playdate file API |
| [`pd_stdio.c`](#pd_stdioc) / [`pd_compat.h`](#pd_compath) | stdio shim so unmodified Quake code can `fopen`/`printf` |
| [`pd_port.h`](#pd_porth) | Internal header shared by the board's files |
| [`keyqueue.c/.h`](#keyqueuec--keyqueueh) | Key event ring buffer |
| [`autofire.c/.h`](#autofirec--autofireh) | Autofire when the crank is out |
| [`weapons.c/.h`](#weaponsc--weaponsh) | Weapon list for the system menu |
| [`snd.c`](#sndc) | Sound through Playdate `SamplePlayer`s |
| [`cd_pd.c`](#cd_pdc) | CD music: `id1/music/QuakeNN` streamed by a `FilePlayer` |
| [`pdprof.c`](#pdprofc) | On-device profiler (only with `PD_PROFILE`) |
| `platform.cmake`, `CMakeLists.txt`, `toolchain.cmake`, `pdx_*.cmake` | See [Build system](../build-system.md) |
| `Source/pdxinfo` | Game metadata (`name`, `bundleID`, `buildNumber`, …) |

---

## `main.c`

Entry point, frame loop and input. Everything the Playdate OS touches goes through
`eventHandler`, registered by name in the `.pdx`.

### State machine

`update()` is the callback registered with `setUpdateCallback`. It runs a four-state machine
(`ST_SPLASH`, `ST_INIT`, `ST_RUN`, `ST_STOPPED`) because loading `pak0.pak` takes a while and the
user must see something first:

```c
static int update(void *ud)
{
	switch (state) {
	case ST_SPLASH:                       // frame 1: draw "Loading..." and return so it shows
		show_message("Quake", "Loading...");
		state = ST_INIT;
		return 1;

	case ST_INIT:                         // frame 2: the long part
		if (setjmp(frame_jmp)) return 1;  // Sys_Error longjmps back here
		in_frame = 1;
		if (qembd_init(1, argv) != 0) { /* "Quake failed to start" */ }
		apply_run();                      // cl_forwardspeed / cl_backspeed = 400
		Key_SetBinding(',', "+moveleft");
		Key_SetBinding('.', "+moveright");
		state = ST_RUN;
		return 1;

	case ST_RUN:                          // every later frame
		if (setjmp(frame_jmp)) { pd_stack_top = 0; return 1; }
		in_frame = 1;
		pd_stack_top = PD_StackPointer(); // see winquake/pd_stack.h
		poll_input();
		qembd_frame();                    // one Host_Frame
		in_frame = 0;
		apply_refresh_rate();             // follow pd_maxfps
		return 1;
	}
}
```

**Error handling.** Quake's `Sys_Error` never returns, but a Playdate callback must. So
`qembd_fatal` (called by `Sys_Error`) stores a message on screen, sets `ST_STOPPED`, and `longjmp`s
back to the `setjmp` in `update()`. `qembd_quit` (called by `Sys_Quit`) does the same with the text
"Quake has exited.".

### Time and memory hooks

```c
uint64_t qembd_get_us_time()           // getCurrentTimeMilliseconds() * 1000, relative to first call
void     qembd_udelay(uint32_t us)     // busy loop
void    *qembd_allocmain(size_t size)  // device: system->realloc(NULL, size)
                                       // simulator: a static 8 MiB pool (64-bit hosts would break
                                       //   QuakeC's 32-bit string offsets with a malloc'd heap)
```

### Input mapping

Buttons map to different Quake keys in the game and in menus:

| Button | In game | In menus (`key_dest != key_game`) |
| --- | --- | --- |
| Up / Down / Left / Right | arrows | arrows |
| A | `K_CTRL` (+attack) | `K_ENTER` |
| B | `K_SPACE` (+jump) | `K_ESCAPE` |

The buttons only produce *keys*; what a key does is whatever is **bound** to it. The bindings come from `default.cfg`
(`quake.rc` runs it at start-up), and when the pak has none (the 2021 re-release's) from the
[built-in copy](../engine/core.md#built-in-defaultcfg-defaultcfgh); without them the D-pad would do nothing.
`main.c` itself only binds the two strafe keys:

```c
// after qembd_init()
Key_SetBinding(',', "+moveleft");
Key_SetBinding('.', "+moveright");
```

Rules in `poll_input()`:

- The key *sent on press* is remembered and *released with the same key*, so switching between
  game and menu while a button is held cannot leave a key stuck.
- **Crank out** (`!isCrankDocked()`) while playing: D-pad left/right send `,` and `.`, bound to
  `+moveleft`/`+moveright` (strafe) instead of turning.
- **Title demo**: while a demo plays, A and B send `K_ESCAPE` to open the menu.
- **Crank turns the player**: `cl.viewangles[YAW] -= getCrankChange() * crank_speed.value`
  while connected, in `key_game`, and not in a demo.
- The system-menu requests (`menu_requested`, `options_requested`) set by menu callbacks are turned
  into an Escape keypress or `M_Menu_Options_Shortcut()` here, on the game thread.

```c
// Autofire runs every frame; A owns the fire key whenever it is held
pdq_autofire_update((cur & kButtonA) != 0, playing && !qembd_pd->system->isCrankDocked());
```

### System menu and events

`eventHandler` (the one exported symbol) reacts to Playdate system events:

| Event | Action |
| --- | --- |
| `kEventInit` | Save the `PlaydateAPI*` in `qembd_pd`, set the refresh rate, add the **Game Menu** and **Options** menu items, register `update`. |
| `kEventPause` | Rebuild the **Weapon** menu item (`rebuild_weapon_item`) so it lists only usable weapons. |
| `kEventPause` / `kEventLock` / `kEventTerminate` | `Host_SaveOptions()` (writes `config.cfg`). The system can stop the game without `Host_Shutdown` ever running. |
| `kEventPause` / `kEventLock` | `qembd_cd_suspend(1)`: the music pauses while the system menu or lock screen has the device. |
| `kEventResume` / `kEventUnlock` | `qembd_cd_suspend(0)` (the music carries on where it stopped) and `qembd_display_invalidate()`: the system drew over the LCD buffer, so redraw every row. |

The Weapon item is an options menu item whose choices are produced by
[`weapons.c`](#weaponsc--weaponsh):

```c
num_avail = pdq_weapons_scan(&current);
if (num_avail < 2) return;                    // nothing to choose between
weapon_item = qembd_pd->system->addOptionsMenuItem("Weapon", pdq_weapon_names, num_avail,
                                                   menu_weapon, NULL);
qembd_pd->system->setMenuItemValue(weapon_item, current);
```

### Frame rate

`apply_refresh_rate()` copies `pd_maxfps` (0 = unlimited, clamped to 50) to
`display->setRefreshRate` whenever it changes, so the Options menu, `config.cfg` or the console
all take effect on the next frame. `PD_REFRESH_RATE` only applies while the game loads.

---

## `display.c`

Turns Quake's 8-bit paletted picture into the 1-bit LCD frame buffer. It is implemented as the
`qembd_*` display hooks (`qembd_get_width/height`, `qembd_vidinit`, `qembd_fillrect`,
`qembd_refresh`) plus `qembd_display_invalidate`.

### Geometry

Quake renders `PD_RENDER_WIDTH × PD_RENDER_HEIGHT` pixels (400×240 with `PD_LOWRES_3D`, 320×240
otherwise) drawn 1:1, centred on the panel. Compile-time asserts keep the width and the centring
offset byte aligned, because the LCD packs 8 pixels per byte (MSB = leftmost, 1 = white).

### Luminance

Every palette entry becomes a luminance, contrast-stretched (Quake is dark; a 1-bit panel loses
shadow detail):

```c
#define BLACK_POINT 4.0f
#define WHITE_POINT 120.0f
#define GAMMA 0.8f

float v = (i - BLACK_POINT) / (WHITE_POINT - BLACK_POINT);     // stretch
v = v < 0 ? 0 : (v > 1 ? 1 : v);
gamma_lut[i] = (uint8_t)(255.0f * powf(v, GAMMA) + 0.5f);      // mild gamma

uint32_t l = (77 * R + 151 * G + 28 * B) >> 8;                 // Rec.601-style weights
lum[i] = gamma_lut[l];
```

The table is rebuilt only when the palette changes (`memcmp` against the previous one).

### Dithering modes (`pd_dither`, 0–3)

The Options menu stores the mode in the `pd_dither` cvar. [`VID_Update`](overview.md#portvid_portc)
copies it into `qembd_dither_mode`, and `qembd_fillrect` switches tile when it changes.

| Mode | Constant | Tile | 3D view |
| --- | --- | --- | --- |
| 0 `patterns` | `DITHER_PATTERNS` | 4×4 Bayer | Each uniform 2×2 block of the half-resolution view becomes one of 5 fixed patterns. |
| 1 `bayer` | `DITHER_BAYER` | 4×4 Bayer | Every pixel against the Bayer matrix. |
| 2 `blue noise` | `DITHER_NOISE` | 32×32 blue noise | Every pixel against the noise tile. |
| 3 `diffusion` | `DITHER_DIFFUSE` | Bayer (for non-3D) | Floyd–Steinberg with a hashed random threshold and a scene-median tone curve. |

A pixel is white when `lum[pixel] > threshold`; the threshold comes from `tile[(y & tile_rmask) *
tile_stride + (x & (tile_stride-1))]`. `dither8` / `dither8t` pack 8 comparisons into one byte:

```c
static inline uint8_t dither8(const uint8_t *sp, unsigned t0, unsigned t1, unsigned t2, unsigned t3)
{
	return (uint8_t)((lum[sp[0]] > t0) << 7 | (lum[sp[1]] > t1) << 6 |
	                 (lum[sp[2]] > t2) << 5 | (lum[sp[3]] > t3) << 4 |
	                 (lum[sp[4]] > t0) << 3 | (lum[sp[5]] > t1) << 2 |
	                 (lum[sp[6]] > t2) << 1 | (lum[sp[7]] > t3));
}
```

**Pattern mode.** `var_top`/`var_bot` list equally bright 2×2 variants for grey levels 1–3
(e.g. level 1: the one white pixel in any of 4 corners). One variant is chosen per palette colour
ramp (`index >> 4`) so adjacent surfaces of the same brightness stay distinguishable. `pat2[256]`
packs top and bottom row patterns so one lookup serves both LCD rows.

**Diffusion mode.** Each LCD row *pair* is diffused independently (top row into bottom row,
alternating scan direction per pair), starting from zero error. That makes every pair a pure
function of its own source rows, so interlaced frames can keep rows and nothing flickers.
Random thresholds are *hashed from the pixel position* (`diff_noise`) rather than drawn from a
generator. The scene's median luminance (`diff_sample_median`, smoothed with hysteresis in
`diff_follow_median`) drives a piecewise-linear tone curve (`diff_build_curve`) that stretches the
median to mid-grey.

### The low-resolution hand-over

With `PD_LOWRES_3D` the engine does not expand the half-resolution view into the 8-bit buffer.
Instead `pdr_lowres.c` (or `d_scan.c`) publishes it, and `qembd_fillrect` dithers straight from it:

```c
extern int qembd_lowres_rect[4];         // view rectangle, full-res pixels
extern int qembd_lowres_active;          // a half-res view is waiting
extern const uint8_t *qembd_lowres_src;  // its pixels
extern int qembd_lowres_stride;
extern uint8_t qembd_lowres_pending[];   // per row pair: 1 = drawn this frame, 2 = kept (interlaced)
extern uint8_t qembd_lowres_shown[];     // per row pair: LCD already shows this row's current content
```

Per row pair `qy`: if pending == 2 and already shown the LCD row is left alone (interlaced
frame); if pending is set, `lowres_direct` dithers from the half-resolution row; otherwise
`lowres_pair` dithers the expanded 8-bit rows (something drew over the view). Rows outside the
view use `dither_span`. See [the Playdate renderer](../engine/renderer-pdr.md#pdr_lowresc).

### Hooks

```c
qembd_vidinit();          // build gamma_lut/bayer_tile, select patterns, clear the LCD frame
qembd_fillrect(...);      // dither a dirty rectangle into getFrame()
qembd_refresh();          // markUpdatedRows(0, LCD_ROWS - 1)
qembd_display_invalidate(); // forget which rows the LCD shows; called after system screens
```

---

## `bluenoise.h`

```c
/* Generated by scripts/gen-bluenoise.py (void-and-cluster, 32x32, sigma 1.5, seed 20261003): do not edit. */
#define BLUENOISE_SIZE 32
static const uint8_t bluenoise[BLUENOISE_SIZE * BLUENOISE_SIZE] = { 152,81,183,103, ... };
```

A 1024-byte threshold tile (values 0–254) produced by [`scripts/gen-bluenoise.py`](../tools.md#scriptsgen-bluenoisepy).
Regenerate with `scripts/gen-bluenoise.py > port/boards/playdate/bluenoise.h`.

---

## `fio.c`

Implements `Sys_FileOpenRead` … `Sys_File_gets` on `playdate->file`. Handles are indexes into a
16-entry table of `SDFile*`.

- **Reads** try the game's *Data* folder first, then the `.pdx` bundle itself
  (`kFileRead | kFileReadData`). So `pak0.pak` can ship in `Source/` or be copied into
  `Data/<bundle>/id1/` later.
- **Writes** always go to the Data folder and create missing directories first (`pdq_mkdirs`).
- The size returned by `Sys_FileOpenRead` is measured by seeking to the end, because `stat` only sees
  Data-folder files.
- `pdq_path` normalises the paths Quake builds (`".//id1/pak0.pak"` → `"id1/pak0.pak"`).

```c
const char *pdq_path(const char *path, char *out, size_t size);   // normalise
void pdq_mkdirs(const char *path);                                // mkdir -p for the file's folders

char norm[256];
SDFile *f = qembd_pd->file->open(pdq_path(".//id1/pak0.pak", norm, sizeof norm),
                                 kFileRead | kFileReadData);
```

## `pd_stdio.c`

Quake still uses `<stdio.h>` directly for `config.cfg`, saved games and demos. The Playdate has no
usable libc file I/O, so this file implements the subset Quake calls on top of `playdate->file`:
`pdq_fopen`, `pdq_fclose`, `pdq_fread`, `pdq_fwrite`, `pdq_fseek`, `pdq_fgetc`, `pdq_feof`,
`pdq_fflush`, `pdq_fprintf`, `pdq_fscanf`, `pdq_printf`, `pdq_unlink`, and `pdq_log_line`.

- A `FILE *` is really a `pdq_file_t` with a 512-byte read buffer, an unget slot and an EOF flag.
- `pdq_fscanf` understands only `%i %d %f %s` (with width) and whitespace: exactly what the
  save/load code uses.
- `pdq_fprintf(stdout|stderr, …)` and `pdq_printf` go to the Playdate console through
  `pdq_log_line`, which strips ANSI colour codes and CR/LF and replaces bytes ≥ 128 with `?`.
- `pdq_log_line` also forwards each line to `pdprof_note` (a no-op without `PD_PROFILE`).

## `pd_compat.h`

Force-included (`-include`) into every C file of a Playdate build, so no call site needs patching:

```c
#define fopen   pdq_fopen
#define fclose  pdq_fclose
#define fread   pdq_fread
#define fprintf pdq_fprintf
#define printf  pdq_printf
#define unlink  pdq_unlink
/* ... fwrite fseek fgetc getc feof fflush fscanf */

#define QEMBD_PRINTF pdq_printf      // quakembd.h logging goes to the Playdate console
```

## `pd_port.h`

```c
extern PlaydateAPI *qembd_pd;                 // set once in eventHandler(kEventInit)
void pdq_log_line(const char *text);          // console output
const char *pdq_path(const char *, char *, size_t);
void pdq_mkdirs(const char *path);
void qembd_display_invalidate(void);          // display.c
```

## `keyqueue.c` / `keyqueue.h`

A 32-entry ring buffer between `poll_input` and `Sys_SendKeyEvents`. If it is full the new event
is dropped.

```c
pdq_push_key(K_ESCAPE, 1);     // press   (producer: main.c, autofire.c)
pdq_push_key(K_ESCAPE, 0);     // release
// consumer, called from Sys_SendKeyEvents():
key_event_t e;
while (qembd_dequeue_key_event(&e) == 0)
	Key_Event(e.keycode, e.state == 1);
```

The same file defines `qembd_get_mouse_movement` (always zero movement) and an empty
`qembd_set_relative_mode`, which `menu.c` calls (mouse capture is a desktop concept, left over from the removed desktop board).

## `autofire.c` / `autofire.h`

```c
void pdq_autofire_update(int a_held, int allowed);
```

Called every frame. With the crank out and `cl_autofire` on it holds `K_CTRL` (+attack) while a
live monster is in the line of fire:

- `enemy_in_sight()` reads the local server's edicts directly (single player). It casts a straight
  ray 2048 units ahead and then mirrors `PF_aim`: any monster inside the `sv_aim` cone and visible
  also counts. The axe only counts what is within 64 units straight ahead.
- Grenade and rocket launchers are tapped once per `AUTOFIRE_HEAVY_COOLDOWN` (2 s) rather than held.
- If A is held, autofire stands down; A owns the key.

```c
want = cl_autofire.value && allowed && enemy_in_sight();
if (want != auto_down) {
	pdq_push_key(K_CTRL, want);      // synthesize press/release of the fire key
	auto_down = want;
}
```

## `weapons.c` / `weapons.h`

```c
int  pdq_weapons_scan(int *current);      // list usable weapons; returns count, *current = active index
extern const char *pdq_weapon_names[];    // names for the system-menu options item
void pdq_weapon_select(int index);        // queue "impulse N"
```

A static table (`Axe` … `Lightning`, in impulse order) says which `IT_*` item bit and `STAT_*`
ammo counter each weapon needs. A weapon is listed only if `cl.items` has it and the ammo is at
least its `ammo_min`. Selecting one queues the impulse the original game would send:

```c
Cbuf_AddText(va("impulse %d\n", avail_impulse[index]));
```

---

## `snd.c`

A sound backend written for the Playdate. Quake's software mixer is not used. Each effect (an
8-bit WAV in the pak) is decoded once to 16-bit mono PCM and handed to one of
`NUM_PLAYERS` (8) `SamplePlayer`s, which the system mixes.

| Part | Behaviour |
| --- | --- |
| `decode_wav` | Parses the RIFF chunks (`fmt `, `data`), converts 8-bit unsigned or 16-bit PCM, any channel count, to 16-bit mono (first channel). |
| `snd_entry_t` / `entries[512]` | One per precached sound: `sfx_t` (must be first, so `sfx_t*` ↔ `snd_entry_t*`), the `AudioSample`, PCM, byte count, LRU stamp. |
| PCM cache | Budget `PCM_BUDGET` = 1.5 MiB. `trim_cache` evicts the least-recently-used sample no voice is using. |
| `S_EndPrecaching` | Loads sounds up front while the level loads, up to ⅔ of the budget, to avoid a hitch the first time a weapon fires. |
| `S_StartSound` | Volume and left/right pan from the emitter's position relative to the listener (`listener_origin`, `listener_right`); attenuated by distance (`sound_nominal_clip_dist` = 1000). Picks a voice: same entity channel replaces its sound, else a free player, else the oldest. |
| `S_Update` | Tracks the listener and releases finished voices. |

```c
float scale = 1.0f - dist * (attenuation / sound_nominal_clip_dist);
if (scale <= 0) return;                               // too far to hear
float dot = DotProduct(delta, listener_right) / dist; // -1 (left) .. +1 (right)
left  = vol * scale * (dot > 0 ? 1.0f - dot : 1.0f);
right = vol * scale * (dot < 0 ? 1.0f + dot : 1.0f);
```

Not supported (empty functions): ambient and looping sounds (`S_StaticSound`, `S_AmbientOn/Off`),
the DMA mixer. CD music is [`cd_pd.c`](#cd_pdc). `snd.c` registers the `volume` and `bgmvolume` cvars
(`bgmvolume` is the music volume, used by `cd_pd.c`).

---

## `cd_pd.c`

Quake's music is CD tracks 2-11. The engine asks for them through `CDAudio_Play` (the `svc_cdtrack` message that
every level sends, from the level's `sounds` key; the `cd` console command), `CDAudio_Pause`/`Resume` (the game pause)
and `CDAudio_Update` (every frame). There is no CD, so track *N* is the file `id1/music/QuakeNN`, the names the
2021 re-release's music has: `Quake02.wav` ... `Quake11.wav` in `Source/id1/music/`.

`pdc` compiles a `.wav` to `.pda` and keeps its format (a PCM WAV stays PCM, an ADPCM WAV stays ADPCM), so the built `.pdx`
holds `id1/music/Quake02.pda` etc. A Playdate `FilePlayer` streams the file from flash, so only the player's buffer is in RAM.
`load_track` tries `.pda`, then `.mp3`, first in the game directory (`com_gamedir`, so a mod can bring its own music) and
then in `id1`. Without the files every call is a quiet no-op (`Con_DPrintf`, so `developer 1` shows it).

| Function | Behaviour |
| --- | --- |
| `CDAudio_Init` | Allocates the `FilePlayer`, registers the `cd` command. |
| `CDAudio_Play(track, loop)` | Remembers the request (`req_track`), and, if the music is on, loads the file and starts it; a level's music loops (`play(player, 0)`), `cd play` plays once. The track that is already playing is left alone, so a new level with the same music does not restart it. |
| `CDAudio_Stop` / `Pause` / `Resume` | Stop unloads and forgets the request; pause and resume keep the position. |
| `CDAudio_Update` | Notices the **Music** option changing (`sync_enabled`), applies `bgmvolume` (0 pauses the stream instead of playing silence) and notices a `cd play` track that ended. |
| `qembd_cd_suspend(on)` | Called from `eventHandler` for the system menu and lock screen (see [events](#system-menu-and-events)). |
| `cd` command | `on` and `off` set `bgmenabled` (so they are saved like the menu option); `reset`, `play N`, `loop N`, `stop`, `pause`, `resume`, `info`. `remap`, `eject` and `close` do nothing without a drive. |

**The Music option.** Options > Music is the archived cvar `bgmenabled` (default on, defined in `menu.c`; Reset defaults turns it
on). Switching it off unloads the track but keeps the last request, so switching it on again starts the level's music, not
silence until the next level; a request that arrives while it is off (a new level) is kept the same way. `cd stop`, a
`cd play` track that ended and a missing file clear or never set the request, so nothing restarts by itself.

Everything funnels through one function, `apply()`, which compares what is wanted (a track, not paused by the game, not
suspended by the system, `bgmvolume` > 0) with what the player is doing and issues the `play`/`pause`/`setVolume` calls:

```c
int want = play_track && !paused && !suspended && vol > 0;
if (!want) { if (started) { fp->pause(player); started = 0; } return; }
if (vol != volume_set) { fp->setVolume(player, vol, vol); volume_set = vol; }
if (started && fp->isPlaying(player)) return;
if (started && !looping) { play_track = 0; started = 0; return; }   // a "cd play" track reached its end
fp->play(player, looping ? 0 : 1);                                   // first start, resume, or a looped track that stopped
```

**File size.** The re-release's tracks are 44.1 kHz 16-bit stereo PCM, about 590 MB for the ten of them, and `pdc` keeps
them that way. The `FilePlayer` also plays IMA ADPCM, a quarter of the size (about 150 MB), which `pdc` keeps as well.
Converted this way the ten tracks keep their length and sample rate and measure 27-54 dB signal-to-noise against the PCM
(lowest on `Quake02`, the busiest):

```shell
cd port/boards/playdate/Source/id1/music
mkdir adpcm && for f in Quake*.wav; do ffmpeg -i "$f" -acodec adpcm_ima_wav "adpcm/$f"; done
# keep the PCM originals somewhere outside Source/ (pdc bundles everything in it), then move adpcm/*.wav here
```

**Release builds** (`-DPD_RELEASE=ON` with a `pak0_demo.pak`) leave `id1/music` out of the `.pdx`, like the full `pak0.pak`
([which `pak0.pak` the `.pdx` gets](../build-system.md#which-pak0pak-the-pdx-gets)).

> **Not measured on the device.** The Simulator ran the real game (the title demo's track message starts track 2) and a
> test driver that played PCM and ADPCM tracks, paused, resumed, looped, ended a once-only track, silenced the volume and
> suspended; all behaved as above. The audio itself was not listened to, and neither the memory the player's buffer takes
> nor an underrun during a level load were looked at on a Playdate.

---

## `pdprof.c`

Only compiled with `-DPD_PROFILE=ON`. Header and macros live in
[`winquake/pdprof.h`](../../winquake/pdprof.h) ([documented here](../engine/perf-infrastructure.md#pdprofh)).

Per-frame section timings, counters and scene statistics are written to `prof.csv` in the game's
Data folder (the USB serial console does not carry `logToConsole`). Game code is unprivileged, so
it uses `system->getElapsedTime()`, not the DWT cycle counter; a tick is 1/168 µs
(`TICKS_PER_US` = 168).

| Function | Purpose |
| --- | --- |
| `pdprof_open()` / `pdprof_stage(text)` | Open `prof.csv`; write a `STAGE` line (start-up milestones). |
| `pdprof_init()` | Measure the timer's own cost (`CAL` line), write the column header (`H,…`), run the CPU/memory micro-benchmarks (`MICRO` lines). |
| `pdprof_frame_begin()` / `pdprof_frame_end()` | Bracket one `Host_Frame`; the end writes a `P,…` row of section times (µs) and counters, batched into 3 KiB writes. |
| `pdprof_note(text)` | Mirror a console line as an `L,…` row. |
| `pdprof_count_spans(list)` | Count spans and pixels of an `espan_t` list (original renderer). |
| `pdprof_bi_add(builtin, ticks)` | Per-QuakeC-builtin cumulative cost, written as `BI` lines. |
| `pd_asm_mismatch(...)` | `PD_ASM_CHECK`: log the first 40 differing pixels as `ASMBAD` lines. |

With `PD_BENCH`, `bench_start`/`bench_tick` play `demo1`…`demo3` with `timedemo` and write
`BENCH,<ms>,start|done|all-done,<demo>` markers; `PD_BENCH_CMDS` is run first. `micro_*` functions
measure store/load/ALU/stack costs (the numbers behind the [device memory model](../engine/perf-infrastructure.md)).

```shell
# end to end: build, install, run 105 s, fetch prof.csv, summarise
scripts/pd-bench.sh build-prof demo1 105
scripts/pd-report.py bench-results/demo1.csv
```

See [Scripts and tools](../tools.md).
