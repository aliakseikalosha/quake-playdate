# Mods and mission packs

[← Documentation index](../README.md) · [Source index](../source-index.md)

Quake runs mods and the mission packs from game folders next to `id1`, as on a PC (`quake -game mymod`, `-hipnotic`, `-rogue`).
The Playdate has no command line, so Quake's main menu has a **Mods** item that lists the folders and starts the game again in the one you pick.

## Installing one

Copy the mod's folder (the one with its `pak0.pak`, `progs.dat` or `maps/` in it) next to `id1`:

- on the device or in the Simulator, into the game's **Data** folder: `Data/<bundle id>/<folder>/` (the bundle id is in `Source/pdxinfo`, e.g. `com.aliakseikalosha.quake`; on the device, connect it over USB as a data disk, e.g. with `pdutil <port> datadisk`);
- or before building, into `port/boards/playdate/Source/<folder>/`: `pdc` bundles it into the `.pdx`. A shareware release build leaves it out (below).

A folder is listed if it holds a `.pak` file, a `progs.dat`, or a `maps/`, `progs/`, `gfx/` or `sound/` folder. The game that is running is always listed.

| Folder | Shown as | Notes |
| --- | --- | --- |
| `id1` | Quake | Always first. |
| `hipnotic` | Scourge of Armagon | Mission pack 1: status bar, menus and weapons of the pack (below). |
| `rogue` | Dissolution of Eternity | Mission pack 2: the same. |
| `dopa` | Dimension of the Past | Re-release add-on; the standard status bar. |
| `mg1` | Dimension of the Machine | Re-release add-on; the standard status bar. |
| `impel` | Abyss of Pandemonium | Impel Development Team's 1998 episode (its own `progs.dat` next to the pak). |
| `as` | Aftershock | Head Games' 1997 level pack: maps and demos only, played with Quake's own QuakeC. |
| anything else | its folder name | Sorted by name after Quake. |

Most mods need the registered game (`pak1.pak` in `id1`): with only the shareware `pak0.pak` the engine stops with
*"You must have the registered version to use modified games"*, as the original does.

## The packs in this tree

`port/boards/playdate/Source/` holds four add-ons (not in git, see `port/boards/playdate/.gitignore`), extracted on 2026-10-22 from the 1997-98 CDs
in `../ModesFolder/`:

| Folder | From | Game data | Music (`music/QuakeNN.wav`, IMA ADPCM as `id1/music`) |
| --- | --- | --- | --- |
| `hipnotic` | `Quake - scourge of armagon.nrg` (Nero image: data track from byte 307200, audio tracks 2-9) | `pak0.pak` (34 MB: 17 levels, `hipdm1`, 4 demos) | tracks 2-9, 64 MB |
| `rogue` | `DISSOLUTION.bin/.cue` (`MODE1/2352` data track, audio 2-9) | `pak0.pak` (36 MB: 16 levels, `ctf1`, 3 demos) | tracks 2-9, 64 MB |
| `impel` | `abyss.bin/.cue` (data track, audio 2-11) | `pak0.pak` + `progs.dat` (31 MB: 10 levels, 6 deathmatch maps, 3 demos) | tracks 2-11, 133 MB |
| `as` | `Aftershock for Quake.ISO` | `maps/` (start, `e1m1`-`e3m5`, `dmas01`-`dmas13`) and `demo1-3.dem` from the disc's `AS/` | none (no audio tracks) |

File names were lowercased; the installers, editors and the CDs' other files were left out, and so was Aftershock's `SINGLES/` (about 80 single
levels by different authors, which nothing in the game's menus could start). The CD audio was cut from the images by their track tables
(little-endian, checked by comparing both byte orders) and encoded with `ffmpeg -f s16le -ar 44100 -ac 2 -i - -acodec adpcm_ima_wav`. With the
music the four folders are about 450 MB; `pdc` copies all of `Source/` into every `.pdx`, so the install over USB takes that much longer.

### Memory

On the device, with a 10 MB heap and every level of each pack loaded in turn (2026-10-23, the test build below):

| Game | Levels | Heap used at the peak | Level | Longest update while loading | Frame time walking |
| --- | --- | --- | --- | --- | --- |
| Scourge of Armagon | 18 of 18 | 7.1 MB | `hip2m3` | 5.4 s | 35-108 ms |
| Dissolution of Eternity | 17 of 17 | 7.4 MB | `r2m7` | 5.1 s | 34-66 ms |
| Abyss of Pandemonium | 11 of 11 | 7.3 MB | `aop1m3` | 5.3 s | 34-44 ms |

"Heap used" is Quake's hunk (low + high marks); the space between them holds the model cache. With a 7 MB heap the packs stop with
`Hunk_Alloc: failed` or `Cache_TryAlloc` (seen on the host, whose 64-bit pointers make it need about 1.5 MB more than the device), so
`DEFAULT_MEM_SIZE` is now 10 MB (`port/boards/playdate/platform.cmake`; it still backs off in 512 KiB steps if the system has less). The device gave
14.8 MB in one block before Quake started; with the 10 MB heap, the sounds and the music the system kept 2.7-3.0 MB free throughout. A 12 MB
heap left it under 1 MB, too little.

The packs also overflowed the engine's table of model names (`MAX_MOD_KNOWN` in `winquake/model.c`, never freed in a session: every map, model and
brush submodel `*N` loaded). It was 256; Scourge of Armagon needs up to ~312 (`hip1m4` alone has 187 submodels). It is 384 now (+50 KB of static memory).

### The stack overflow

The Playdate gives a game only about 10 KB of stack below its update callback (measured on the device by recursing in 512-byte steps until it
failed: about 9.5 KB). Overflowing it does not stop the game at once: it overwrites the system's memory below the stack, and the device fails
later, at the next system allocation (a fault inside the firmware's allocator, `pc 240309d4`, or the system's "run loop stalled" message).

Scourge of Armagon's `hip2m3` did that while it loaded: a monster's first `walkmove` checked the floor with a trace whose
`SV_RecursiveHullCheck` recursion went 107 calls deep (80 bytes each on the device) through the map's clip hull, under the `map` command's
`Cbuf_Execute`, which kept a 1 KB line buffer on the stack: about 10.7 KB. The 1997 maps' BSP trees are much deeper than id's (the deepest
`SV_FindTouchedLeafs` recursion was 141 calls in Scourge, 116 in Dissolution, 85 in Quake). Fixed by turning the tail calls of
`SV_RecursiveHullCheck`, `SV_FindTouchedLeafs` and `R_SplitEntityOnNode` into loops (only a node the line or box crosses still recurses) and
making `Cbuf_Execute`'s line buffer static. The results are unchanged: 630 000 traces and 321 000 entity links on 93 levels of the five games
compared byte for byte with the original functions, and demos 1-3 render identically. On the device the deepest stack during the three packs is
now 3.6 KB below the update (it was 11.1 KB).

How it was found, for next time: the host check built with `-finstrument-functions` and a shadow call stack recorded the deepest chain, and
`arm-none-eabi-gcc -fstack-usage` gave each function's frame size on the device.

### Loading a level in under 10 s

The system stops a game whose update callback runs for more than 10 s ("run loop stalled"), and Quake loads a level in one frame. `map` now
spawns the server in one frame and connects the client in the next (`Host_Map_f`, `wait` before `connect local`): the packs' slowest levels took
5-5.5 s and 1-2.6 s instead of up to 8 s in one frame (Abyss of Pandemonium's `aop1m4`).

(Before the stack overflow was found, the crash looked like the music player hanging: starting the next level's track was the first system
allocation after the overflow, so that is where the damage showed. With the overflow fixed the music starts in the loading frame as before.)

## Using the Mods menu

Main menu → **Mods** (the item under Quit). The list shows the running game in white. A on another one asks first
(*Restart the game in …? Progress not saved is lost.*); A again saves the options and restarts the game in it, B goes back.
A on the running game, or B, returns to the main menu.

The choice is remembered in `Data/<bundle id>/game.txt` (one line, the folder name), so the next launches start there too, until
Quake is picked again. If the folder has since been deleted, the game starts as Quake and forgets the choice.

**Hold B while the game starts** to start plain Quake and forget the choice: a mod that stops with an error, or crashes the device,
cannot lock the game out. A mod that stops with an error says so on the error screen.

Each game folder keeps its own `config.cfg` and saved games (Quake writes them into the running game's folder), and its music (`QuakeNN.pda` / `.mp3`) is looked for
in `<folder>/music/` first, then in `id1/music/` (see [`cd_pd.c`](playdate.md#cd_pdc)).

### Launch arguments

The Playdate's launch arguments (the Simulator's command line, `run` from the device's serial console) are passed to Quake as its command line,
e.g. `-game mymod`, `-hipnotic`, `+map e1m1`. When there are any, `game.txt` is not used.

## How it works

| Piece | Where | What |
| --- | --- | --- |
| Game folder at start-up | `port/boards/playdate/main.c`, `build_args` | Launch arguments if there are any, else `-game <game.txt>`; B held: neither. The result is Quake's `argv` for `qembd_init`. |
| Search path | `winquake/common.c`, `COM_InitFilesystem` | Unchanged: `-game` puts the folder and its `pakN.pak` files in front of `id1`. New: `-game hipnotic` / `-game rogue` also set `hipnotic` / `rogue` (and clear `standard_quake`), as `-hipnotic` / `-rogue` do. |
| Names | `common.c`, `COM_GameDirName`, `COM_GameTitle` | The running folder's name; a folder's title (the table above). |
| Listing folders | `Sys_ListDir` in `port/boards/playdate/fio.c` (`playdate->file->listfiles`) and `port/fio/fio_posix.c` (`opendir`) | `listfiles` shows the `.pdx` and the Data folder as one, so a folder in either is found, and `id1` once. |
| The menu | `winquake/menu.c`, `M_Menu_Mods_f` (`menu_mods` command), `M_ScanMods` | The main menu's sixth item; drawn at the Options menu's double size. |
| Restart | `qembd_restart(gamedir)` in `main.c` | Writes or deletes `game.txt`, then `playdate->system->restartGame("")`; the system sends `kEventTerminate` (which saves the options) and starts the game again. |

### Mission packs

The 1997 engine code for the packs was already in the tree and is used unchanged: `sbar.c` (the packs' weapons, items, keys and
Dissolution of Eternity's powered-up weapon bar, from the packs' own `gfx.wad`), `menu.c` (their episodes and levels in New Game) and `host_cmd.c` (`give`).
It is switched on by `hipnotic` / `rogue`, which the Mods menu now sets through the folder name.

The port's own code had to learn the packs:

- **Weapon** in the system menu ([`weapons.c`](playdate.md#weaponsc--weaponsh)) has one list per game. Scourge of Armagon adds Mjolnir, the
  proximity gun and the laser cannon; Dissolution of Eternity the lava nailguns, the multi grenade and rocket launchers and the plasma gun,
  and keeps each ammo type's count in a QuakeC field of its own (`ammo_lava_nails`, ...), which the menu reads from the player to decide what can be selected.
  The impulses are the ones the packs' 1997 QuakeC handles (read from their `progs.dat`'s `W_ChangeWeapon`) and the 2021 re-release's
  does too ([id-Software/quake-rerelease-qc](https://github.com/id-Software/quake-rerelease-qc)): 1-8 for the plain weapons, Scourge 225 laser
  cannon and 226 Mjolnir, Dissolution 60-64. Scourge's proximity gun has no impulse of its own in 1997 (the re-release added 227): 6 toggles to it from
  the grenade launcher, so the menu sends 6, and 6 again a frame later unless the grenade launcher is in hand.
  The weapon in hand is read from the local server's player: Quake's protocol sends it as a byte, so the axe (4096) arrived as 0 and was never recognised.
- **Autofire** ([`autofire.c`](playdate.md#autofirec--autofireh)) asks `pdq_weapon_kind` instead of comparing with `IT_AXE`: Dissolution of
  Eternity's axe has another item bit (`RIT_AXE`, and `IT_AXE`'s bit is its lava nailgun there), Mjolnir is a melee weapon too, and the
  proximity gun and multi launchers fire one shot per cooldown like the grenade and rocket launchers.

## Release build

A shareware release (`scripts/release-device.sh` with a `Source/id1/pak0_demo.pak`) leaves every game folder other than `id1` out of the `.pdx`,
as it does the music: the shareware does not run mods, and the mission packs are not redistributable. See
[which pak0.pak the `.pdx` gets](../build-system.md#which-pak0pak-the-pdx-gets).

## What was checked, and what was not

- **The packs on the host** (2026-10-22, with the data above; `tools/hostcheck/out/run-packs/` holds real folders of symlinks to `Source/`, so nothing
  the engine writes lands in `Source/`): every single-player level of the four packs loaded and played (`HMAPS="…"`, see the memory table);
  `HWEAPONS=<map>` gave every weapon and picked each through `weapons.c` both ways in all five games: Quake 8, Scourge 11, Dissolution 13,
  Abyss 8, Aftershock 8 weapons, each one in hand afterwards.

  ```shell
  cd tools/hostcheck/out/run-packs
  HARGS="-game hipnotic" HMAPS="start hip1m1 hip1m2" ../hostcheck
  HARGS="-game rogue" HWEAPONS=r1m1 ../hostcheck
  ```

- **Host check** (`NEW=1 HMODS=1 tools/hostcheck/run.sh`, see [Scripts and tools](../tools.md)): the Mods screen with fake folders lists Quake,
  a mission pack and a mod, leaves out a folder that is not a game, sorts by title, asks before restarting, and restarts into the folder picked.
  `HARGS="-game testmod"` with a folder holding only an `autoexec.cfg` puts it in front of `id1` and runs that `autoexec.cfg`.
  `HARGS="-game hipnotic"` turns on the Scourge of Armagon status bar (it then asks `gfx.wad` for `inv_laser`, which only that pack has).
  The existing scenario, menu and input checks still pass.
- **Simulator** (a scratch build with its own bundle id): Mods found mods in the Data folder through `listfiles`, picking one wrote
  `game.txt` and restarted, the second start ran with the mod in front of `id1`; a later launch started in it again; with the folder
  deleted the game started as Quake and dropped `game.txt`.
- **Device** (2026-10-22/23, a test build installed as `Games/quake_MEMTEST.pdx` with its own bundle id, the game's code plus a hook that
  reads a list of levels from its Data folder, loads each, walks for 90 frames and logs memory, the stack's low-water mark and frame times): every
  level of the three packs (18 + 17 + 11, the table above) with their music playing, Aftershock's 16 and 11 levels of Quake (`id1` with the
  registered `pak1.pak`), all with the 10 MB heap and no crash. The stack never went deeper than 3.6 KB below the update. The memory numbers,
  the stack overflow and the 10 s limit were found on the device in the same way.
- **Not checked:** playing through a pack with the buttons (the test loads levels and walks, it does not fight), the packs' status bar on the
  1-bit screen, listening to the extracted music, the weapon menu's mission pack entries on the device (they were checked on the host against the
  packs' QuakeC), the re-release's versions of the packs (only the 1997 ones were at hand), saving and loading in a pack, and holding B at start-up.
