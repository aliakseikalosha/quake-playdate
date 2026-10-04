# Source code index

[← Documentation index](README.md)

Every file in the repository (201 files, 153 of them C, header or assembly source), with what it does, whether it is built, and a link to its detailed description. The **File** column opens the source; the **Docs** column opens the section that describes it. Names in the descriptions are links to the related files' sections, so you can follow a call from one file to the next.

- Looking for a file by name? Jump to [Find a file by name](#find-a-file-by-name).
- Looking for what to read first? See [Start here](README.md#start-here) in the index.
- Which files make up a given build? See [Which files each build compiles](#which-files-each-build-compiles).

**Built** column:

| Value | Meaning |
| --- | --- |
| always | Compiled into every Playdate build (device and Simulator) |
| header | A header, included by the files that need it |
| new renderer | Only with `PD_NEW_RENDERER=ON` (the default) |
| orig. renderer | Only with `PD_NEW_RENDERER=OFF` |
| asm | Only with `PD_ASM=ON` on the device, with the original renderer |
| profile | Only with `PD_PROFILE=ON` |
| host check | Only built by [`tools/hostcheck`](tools.md#toolshostcheckbuildsh), not by the game |
| not built | In the tree, but no configuration here compiles it |
| unused | Nothing includes it; kept for reference |
| build, tool, data, config, editor, doc, licence, generated | Not engine code: build file, script, game data, config, editor setup, documentation |

## Repository layout

```
quake-embedded/
├── CMakeLists.txt        top-level build
├── README.md             player and developer overview
├── include/              quakembd.h: the board interface
├── port/                 shared platform layer: sys_port.c vid_port.c in_port.c ...
│   └── boards/playdate/  the Playdate board: main.c display.c fio.c snd.c cd_pd.c ... + Source/ (the .pdx contents)
├── winquake/             the engine: Quake, the Playdate renderer (pdr_*), the original renderer (r_*, d_*)
├── scripts/              release, install, bench, report, blue-noise generator
├── tools/hostcheck/      host-side picture and behaviour checks
├── docs/                 this documentation
└── note/release/         release notes
```

| Directory | Section |
| --- | --- |
| [`include/`](../include/) | [Board interface](#board-interface-include) |
| [`port/`](../port/) | [Shared platform layer](#shared-platform-layer-port) |
| [`port/boards/playdate/`](../port/boards/playdate/) | [Sources](#playdate-board-sources-portboardsplaydate), [build files and data](#playdate-board-build-files-and-data) |
| [`winquake/`](../winquake/) | [Engine core](#engine-core), [platform headers](#platform-interface-headers), [client](#client), [UI](#screen-hud-console-and-menus), [server](#server-world-and-physics), [QuakeC](#quakec-virtual-machine), [network](#networking), [models](#models-and-map-formats), [Playdate renderer](#playdate-renderer-pdr_), [original renderer](#original-software-renderer-interfaces-and-set-up), [performance](#performance-infrastructure), [legacy](#legacy-x86-headers) |
| [`scripts/`](../scripts/) | [Scripts](#scripts-scripts) |
| [`tools/hostcheck/`](../tools/hostcheck/) | [Host check](#host-check-toolshostcheck) |
| [`docs/`](.), [`note/`](../note/) | [Documentation](#documentation) |

## Which files each build compiles

| Build | Files |
| --- | --- |
| **Playdate, default** (device or Simulator) | the [engine core](#engine-core), [client](#client), [UI](#screen-hud-console-and-menus), [server](#server-world-and-physics), [QuakeC](#quakec-virtual-machine) and [model](#models-and-map-formats) files, [`net_main.c`](engine/network.md#net_mainc), [`net_loop.c`](engine/network.md#net_loopc), [`net_none.c`](engine/network.md#net_nonec), the [Playdate renderer](#playdate-renderer-pdr_) (`pdr_*.c`), [`r_part.c`](engine/renderer-original.md#r_partc), [`r_efrag.c`](engine/renderer-original.md#r_efragc); the [shared platform layer](#shared-platform-layer-port) (`sys_port.c`, `vid_port.c`, `in_port.c`); the [board sources](#playdate-board-sources-portboardsplaydate) except [`pdprof.c`](port/playdate.md#pdprofc) |
| `-DPD_NEW_RENDERER=OFF` | the `pdr_*.c` files are replaced by the [original renderer](#original-software-renderer-interfaces-and-set-up) (`r_*.c` and `d_*.c`); with `PD_ASM=ON` on the device also [`d_scan_arm.S`](engine/perf-infrastructure.md#d_scan_arms) and [`r_edge_arm.S`](engine/perf-infrastructure.md#r_edge_arms) |
| `-DPD_PROFILE=ON` | adds [`pdprof.c`](port/playdate.md#pdprofc) (and `PD_BENCH` plays the demos as timedemos) |
| **Host check** ([`build.sh`](tools.md#toolshostcheckbuildsh)) | the same engine files with [`cd_null.c`](port/overview.md#portcd_nullc) and [`fio_posix.c`](port/overview.md#portfiofio_posixc) instead of [`cd_pd.c`](port/playdate.md#cd_pdc) and [`fio.c`](port/playdate.md#fioc), the real [`display.c`](port/playdate.md#displayc), and [`hostcheck.c`](tools.md#toolshostcheckhostcheckc) as `main` |
| **Not built by any configuration** | [`net_bsd.c`](engine/network.md#net_bsdc), [`net_dgrm.c`](engine/network.md#net_dgrmc), [`net_udp.c`](engine/network.md#net_udpc), [`net_vcr.c`](engine/network.md#net_vcrc-not-built) (and their headers), [`progdefs.q2`](engine/quakec.md#progdefsh-progdefsq1-progdefsq2), the [legacy x86 headers](#legacy-x86-headers), [`asm_draw.h`](engine/perf-infrastructure.md#asm_drawh) |

Details of the options: [Build system](build-system.md#options).

## Top level

Build entry point, licence and editor configuration.

| File | Lines | Built | What it does | Docs |
| --- | ---: | --- | --- | --- |
| [`CMakeLists.txt`](../CMakeLists.txt) | 11 | build | Top-level build: selects the board, sets the global flags, adds [`winquake/CMakeLists.txt`](build-system.md#winquakecmakeliststxt-the-engine) and [`port/CMakeLists.txt`](build-system.md#portcmakeliststxt-shared-platform-layer). | [build](build-system.md#top-level-cmakeliststxt) |
| [`README.md`](../README.md) | 207 | doc | The project overview for players and developers: controls, building, the renderer, settings, profiling and the host check. | [README](../README.md) |
| [`gpl-2.0.txt`](../gpl-2.0.txt) | 339 | licence | The GNU GPL v2 text; Quake's source is released under it. | — |
| [`.gitignore`](../.gitignore) | 8 | config | Keeps `build-*/`, `bench-results/` and `tools/hostcheck/out/` out of git. | [docs index](README.md#not-documented) |
| [`.vscode/tasks.json`](../.vscode/tasks.json) | 56 | editor | VS Code tasks: simulator (debug) build, device build, release build, install over USB. | [build](build-system.md#editor-integration) |
| [`.vscode/launch.json`](../.vscode/launch.json) | 19 | editor | Launches the Playdate Simulator under CodeLLDB against `build-sim-debug/quake.pdx`. | [build](build-system.md#editor-integration) |

## Board interface (`include/`)

The contract between the shared platform layer and a board.

| File | Lines | Built | What it does | Docs |
| --- | ---: | --- | --- | --- |
| [`quakembd.h`](../include/quakembd.h) | 87 | header | The board interface: the `qembd_*` hooks a board implements, the entry points the port provides to it, and the logging macros. | [overview](port/overview.md#includequakembdh) |

## Shared platform layer (`port/`)

Board-independent implementations of the engine's `Sys_*`, `VID_*`, `IN_*` and `CDAudio_*` interfaces, written against the `qembd_*` hooks. Overview: [Shared platform layer](port/overview.md).

| File | Lines | Built | What it does | Docs |
| --- | ---: | --- | --- | --- |
| [`CMakeLists.txt`](../port/CMakeLists.txt) | 12 | build | Builds `in_port.c`, `sys_port.c` and `vid_port.c` as the `port` object library and adds the board directory. | [build](build-system.md#portcmakeliststxt-shared-platform-layer) |
| [`sys_port.c`](../port/sys_port.c) | 213 | always | `Sys_*` on the `qembd_*` hooks (time, errors, quit, key polling), plus start-up (`qembd_init`) and the per-frame `qembd_frame` that wraps `Host_Frame`. | [overview](port/overview.md#portsys_portc) |
| [`vid_port.c`](../port/vid_port.c) | 107 | always | `VID_*`: allocates the 8-bit view buffer, z-buffer and surface cache, converts the palette, and hands the dirty rectangles to the board's `qembd_fillrect`. | [overview](port/overview.md#portvid_portc) |
| [`in_port.c`](../port/in_port.c) | 53 | always | `IN_*`: applies relative mouse movement to the view angles (keys arrive through `Sys_SendKeyEvents`, not here). | [overview](port/overview.md#portin_portc) |
| [`cd_null.c`](../port/cd_null.c) | 54 | host check | `CDAudio_*` as empty functions. Not linked on the Playdate (it has [`cd_pd.c`](port/playdate.md#cd_pdc)); the host check uses it. | [overview](port/overview.md#portcd_nullc) |
| [`fio/fio_posix.c`](../port/fio/fio_posix.c) | 101 | host check | `Sys_File*` on POSIX `open/read/write`. The Playdate uses [`fio.c`](port/playdate.md#fioc) instead; the host check uses this one. | [overview](port/overview.md#portfiofio_posixc) |

## Playdate board: sources (`port/boards/playdate/`)

Everything that touches the Playdate OS and its C API. Overview: [Playdate board](port/playdate.md).

| File | Lines | Built | What it does | Docs |
| --- | ---: | --- | --- | --- |
| [`main.c`](../port/boards/playdate/main.c) | 387 | always | The Playdate entry point: `eventHandler`, the update callback and its state machine, input mapping (D-pad, buttons, crank), the system menu, time and heap hooks. | [playdate](port/playdate.md#mainc) |
| [`display.c`](../port/boards/playdate/display.c) | 769 | always | Turns the 8-bit paletted frame into the 1-bit LCD frame: luminance, the four dithering modes (`pd_dither`), the half-resolution hand-over and redraw bookkeeping for interlacing. | [playdate](port/playdate.md#displayc) |
| [`bluenoise.h`](../port/boards/playdate/bluenoise.h) | 37 | header | The generated 32×32 blue-noise threshold tile used by the blue-noise dither mode; made by [`gen-bluenoise.py`](tools.md#scriptsgen-bluenoisepy). | [playdate](port/playdate.md#bluenoiseh) |
| [`fio.c`](../port/boards/playdate/fio.c) | 179 | always | `Sys_File*` on the Playdate file API: reads try the Data folder, then the `.pdx` bundle; writes go to the Data folder. | [playdate](port/playdate.md#fioc) |
| [`pd_stdio.c`](../port/boards/playdate/pd_stdio.c) | 293 | always | The `stdio` subset Quake calls (`fopen`, `fread`, `fprintf`, `fscanf` …) on `playdate->file`, and console logging via `pdq_log_line`. | [playdate](port/playdate.md#pd_stdioc) |
| [`pd_compat.h`](../port/boards/playdate/pd_compat.h) | 48 | header | Force-included into every C file of the build; redirects `stdio` calls to [`pd_stdio.c`](port/playdate.md#pd_stdioc). | [playdate](port/playdate.md#pd_compath) |
| [`pd_port.h`](../port/boards/playdate/pd_port.h) | 26 | header | Internal header shared by the board's files: `qembd_pd`, path helpers, and the suspend and display-invalidate hooks. | [playdate](port/playdate.md#pd_porth) |
| [`keyqueue.c`](../port/boards/playdate/keyqueue.c) | 44 | always | A 32-entry ring buffer between input polling in [`main.c`](port/playdate.md#mainc) and `Sys_SendKeyEvents`; also the empty `qembd_set_relative_mode`. | [playdate](port/playdate.md#keyqueuec--keyqueueh) |
| [`keyqueue.h`](../port/boards/playdate/keyqueue.h) | 9 | header | `pdq_push_key`: queue a key press or release. | [playdate](port/playdate.md#keyqueuec--keyqueueh) |
| [`autofire.c`](../port/boards/playdate/autofire.c) | 107 | always | Holds fire while a live monster is in the line of fire, with the crank out and `cl_autofire` on. | [playdate](port/playdate.md#autofirec--autofireh) |
| [`autofire.h`](../port/boards/playdate/autofire.h) | 11 | header | `pdq_autofire_update`, called once per frame by [`main.c`](port/playdate.md#mainc). | [playdate](port/playdate.md#autofirec--autofireh) |
| [`weapons.c`](../port/boards/playdate/weapons.c) | 55 | always | Lists the weapons you own and have ammo for (the system menu's Weapon item) and selects one with an `impulse`. | [playdate](port/playdate.md#weaponsc--weaponsh) |
| [`weapons.h`](../port/boards/playdate/weapons.h) | 17 | header | `pdq_weapons_scan`, `pdq_weapon_names`, `pdq_weapon_select`. | [playdate](port/playdate.md#weaponsc--weaponsh) |
| [`snd.c`](../port/boards/playdate/snd.c) | 369 | always | The sound backend: each effect is decoded once to PCM and played on one of 8 Playdate `SamplePlayer`s; Quake's software mixer is not used. Implements [`sound.h`](engine/platform-interfaces.md#soundh). | [playdate](port/playdate.md#sndc) |
| [`cd_pd.c`](../port/boards/playdate/cd_pd.c) | 258 | always | CD audio: track *N* is `id1/music/QuakeNN`, streamed by a `FilePlayer`; Music on/off (`bgmenabled`), volume, game pause, the system menu, and the `cd` command. Implements [`cdaudio.h`](engine/platform-interfaces.md#cdaudioh). | [playdate](port/playdate.md#cd_pdc) |
| [`pdprof.c`](../port/boards/playdate/pdprof.c) | 1156 | profile | The on-device profiler: section timings, counters, stack probes and the timedemo benchmark driver, written to `prof.csv`. Macros in [`pdprof.h`](engine/perf-infrastructure.md#pdprofh). | [playdate](port/playdate.md#pdprofc) |

## Playdate board: build files and data

How the `.pdx` is configured, built and packaged. Details: [Build system](build-system.md).

| File | Lines | Built | What it does | Docs |
| --- | ---: | --- | --- | --- |
| [`CMakeLists.txt`](../port/boards/playdate/CMakeLists.txt) | 88 | build | Defines the game target (device executable or Simulator library), its source list and the post-build steps. | [build](build-system.md#playdate-board-cmakeliststxt) |
| [`platform.cmake`](../port/boards/playdate/platform.cmake) | 138 | build | Every `PD_*` option, the compile definitions they produce, the force-included [`pd_compat.h`](port/playdate.md#pd_compath), and SDK and toolchain selection. | [build](build-system.md#playdate-platformcmake) |
| [`toolchain.cmake`](../port/boards/playdate/toolchain.cmake) | 17 | build | The Arm GCC cross-compilation toolchain for the device build. | [build](build-system.md#toolchaincmake) |
| [`pdx_buildnumber.cmake`](../port/boards/playdate/pdx_buildnumber.cmake) | 37 | build | Post-build step: gives every build the next build number in the built `pdxinfo`. | [build](build-system.md#playdate-board-cmakeliststxt) |
| [`pdx_pak.cmake`](../port/boards/playdate/pdx_pak.cmake) | 36 | build | Post-build step: decides which game data the `.pdx` carries. A release build with a `pak0_demo.pak` ships it as `id1/pak0.pak` and leaves out the music; every other build drops `pak0_demo.pak`. | [build](build-system.md#which-pak0pak-the-pdx-gets) |
| [`pdx_rename.cmake`](../port/boards/playdate/pdx_rename.cmake) | 16 | build | Profiling builds only: appends tags such as "profile, demo1" to the launcher title. | [build](build-system.md#playdate-board-cmakeliststxt) |
| [`Source/pdxinfo`](../port/boards/playdate/Source/pdxinfo) | 7 | data | Game metadata: `name`, `author`, `bundleID`, `version`, `buildNumber`. | [build](build-system.md#source) |
| [`Source/id1/.gitkeep`](../port/boards/playdate/Source/id1/.gitkeep) | 0 | data | Keeps the otherwise git-ignored `Source/id1/` (where your `pak0.pak` and music go) in the tree. | [build](build-system.md#source) |
| [`.gitignore`](../port/boards/playdate/.gitignore) | 6 | config | Ignores `Source/pdex.*`, `Source/id1/` and `.build_number`. | [build](build-system.md#source) |
| [`DOS-CONFIG.CFG.bak`](../port/boards/playdate/DOS-CONFIG.CFG.bak) | 78 | unused | A stray backup of a DOS-era Quake config; not used by the build. | [build](build-system.md#source) |

## Engine core

Start-up and the frame loop, memory, strings and files, console variables and commands, math, WADs. Page: [Engine core](engine/core.md).

| File | Lines | Built | What it does | Docs |
| --- | ---: | --- | --- | --- |
| [`quakedef.h`](../winquake/quakedef.h) | 315 | header | The master header every `.c` includes first: limits, `STAT_*` and `IT_*` bits, include order, host globals. | [core](engine/core.md#quakedefh) |
| [`host.c`](../winquake/host.c) | 872 | always | `Host_Init`, `Host_Frame`, errors and shutdown, client management and the `config.cfg` handling (`Host_SaveOptions`). | [core](engine/core.md#hostc) |
| [`host_cmd.c`](../winquake/host_cmd.c) | 1899 | always | The console commands: `map`, `changelevel`, `save`, `load`, `kick`, `god`, `give`, `status`, … | [core](engine/core.md#host_cmdc) |
| [`sys.h`](../winquake/sys.h) | 80 | header | The OS interface the port implements (`Sys_*`); see [`sys_port.c`](port/overview.md#portsys_portc). | [core](engine/core.md#sysh) |
| [`common.h`](../winquake/common.h) | 251 | header | Buffers (`sizebuf_t`), message I/O, byte order, string and file-system declarations. | [core](engine/core.md#commonh--commonc) |
| [`common.c`](../winquake/common.c) | 1695 | always | Message read/write, byte order, strings, argument parsing, the file system and `.pak` loading. | [core](engine/core.md#commonh--commonc) |
| [`zone.h`](../winquake/zone.h) | 131 | header | The hunk, zone and cache allocator interface. | [core](engine/core.md#zoneh--zonec) |
| [`zone.c`](../winquake/zone.c) | 923 | always | The three memory allocators: the hunk (levels, models), the zone (small dynamic blocks) and the cache (discardable data). | [core](engine/core.md#zoneh--zonec) |
| [`cvar.h`](../winquake/cvar.h) | 97 | header | `cvar_t` and the console-variable API. | [core](engine/core.md#cvarh--cvarc) |
| [`cvar.c`](../winquake/cvar.c) | 221 | always | Console variables: registration, lookup, setting, archiving to `config.cfg`. | [core](engine/core.md#cvarh--cvarc) |
| [`cmd.h`](../winquake/cmd.h) | 121 | header | The command buffer, tokenizer and command-table API. | [core](engine/core.md#cmdh--cmdc) |
| [`cmd.c`](../winquake/cmd.c) | 717 | always | The command buffer, tokenizer, aliases and command table; falls back to [`defaultcfg.h`](engine/core.md#built-in-defaultcfg-defaultcfgh) when the pak has no `default.cfg`. | [core](engine/core.md#cmdh--cmdc) |
| [`defaultcfg.h`](../winquake/defaultcfg.h) | 81 | header | The built-in `default.cfg` (D-pad and button bindings) for paks that lack one, such as the 2021 re-release's. | [core](engine/core.md#built-in-defaultcfg-defaultcfgh) |
| [`crc.h`](../winquake/crc.h) | 24 | header | `CRC_Block` and friends. | [core](engine/core.md#crch--crcc) |
| [`crc.c`](../winquake/crc.c) | 80 | always | The 16-bit CRC, used to check `progs.dat` against the compiled-in variable layout. | [core](engine/core.md#crch--crcc) |
| [`mathlib.h`](../winquake/mathlib.h) | 108 | header | Vector macros and the math function declarations. | [core](engine/core.md#mathlibh--mathlibc) |
| [`mathlib.c`](../winquake/mathlib.c) | 435 | always | Vectors, matrices, angle conversion, plane tests and box-on-plane-side. | [core](engine/core.md#mathlibh--mathlibc) |
| [`wad.h`](../winquake/wad.h) | 75 | header | The WAD2 archive structures and API. | [core](engine/core.md#wadh--wadc) |
| [`wad.c`](../winquake/wad.c) | 158 | always | Reads WAD2 archives (`gfx.wad`: the 2D graphics). | [core](engine/core.md#wadh--wadc) |
| [`nonintel.c`](../winquake/nonintel.c) | 58 | always | Empty stand-ins for the x86 surface-patching routines, so the engine links without assembly. | [core](engine/core.md#nonintelc) |

## Platform interface headers

What the engine expects the platform to provide. Page: [Platform interface headers](engine/platform-interfaces.md).

| File | Lines | Built | What it does | Docs |
| --- | ---: | --- | --- | --- |
| [`vid.h`](../winquake/vid.h) | 82 | header | The video contract: the global `viddef_t vid` and `VID_*`, implemented by [`vid_port.c`](port/overview.md#portvid_portc). | [platform-interfaces](engine/platform-interfaces.md#vidh) |
| [`input.h`](../winquake/input.h) | 34 | header | The input contract (`IN_*`), implemented by [`in_port.c`](port/overview.md#portin_portc). | [platform-interfaces](engine/platform-interfaces.md#inputh) |
| [`sound.h`](../winquake/sound.h) | 175 | header | The sound API (`S_*`), channel and sample structures; implemented by [`snd.c`](port/playdate.md#sndc). | [platform-interfaces](engine/platform-interfaces.md#soundh) |
| [`cdaudio.h`](../winquake/cdaudio.h) | 27 | header | The CD audio contract (`CDAudio_*`), implemented by [`cd_pd.c`](port/playdate.md#cd_pdc) (and [`cd_null.c`](port/overview.md#portcd_nullc) in the host check). | [platform-interfaces](engine/platform-interfaces.md#cdaudioh) |

## Client

Turns player intent into movement commands and server messages into a picture of the world. Page: [Client](engine/client.md).

| File | Lines | Built | What it does | Docs |
| --- | ---: | --- | --- | --- |
| [`client.h`](../winquake/client.h) | 377 | header | Client state (`client_static_t cls`, `client_state_t cl`) and the entity, dynamic-light and light-style structures. | [client](engine/client.md#clienth) |
| [`protocol.h`](../winquake/protocol.h) | 167 | header | The network protocol constants: `svc_*`, `clc_*`, `U_*`, `SU_*`, `TE_*`. | [client](engine/client.md#protocolh) |
| [`cl_main.c`](../winquake/cl_main.c) | 754 | always | Connecting and signon, entity interpolation (`CL_RelinkEntities`), dynamic lights, the client frame. | [client](engine/client.md#cl_mainc) |
| [`cl_parse.c`](../winquake/cl_parse.c) | 963 | always | Decodes every server message (`CL_ParseServerMessage`), precaches models and sounds during signon. | [client](engine/client.md#cl_parsec) |
| [`cl_input.c`](../winquake/cl_input.c) | 455 | always | Button state (`+forward`, `+attack`, …) and `CL_SendCmd`, which builds the `usercmd_t` sent to the server. | [client](engine/client.md#cl_inputc) |
| [`cl_demo.c`](../winquake/cl_demo.c) | 367 | always | Demo recording and playback, and `timedemo`. | [client](engine/client.md#cl_democ) |
| [`cl_tent.c`](../winquake/cl_tent.c) | 394 | always | Temporary entities: explosions, beams, lightning, sparks. | [client](engine/client.md#cl_tentc) |
| [`view.h`](../winquake/view.h) | 35 | header | The view interface (`V_RenderView`, `V_UpdatePalette`, …). | [client](engine/client.md#viewh--viewc) |
| [`view.c`](../winquake/view.c) | 1127 | always | The camera: bob, roll, kick, damage and pickup colour shifts, gamma and palette; calls the renderer ([`pdr_main.c`](engine/renderer-pdr.md#pdr_mainc) or [`r_main.c`](engine/renderer-original.md#r_mainc)). | [client](engine/client.md#viewh--viewc) |
| [`chase.c`](../winquake/chase.c) | 94 | always | The third-person chase camera. | [client](engine/client.md#chasec) |
| [`keys.h`](../winquake/keys.h) | 133 | header | Key codes (`K_*`) and the binding API. | [client](engine/client.md#keysh--keysc) |
| [`keys.c`](../winquake/keys.c) | 759 | always | Key bindings and dispatch of key events to the game, console or menu ([`menu.c`](engine/ui.md#menuh--menuc)). | [client](engine/client.md#keysh--keysc) |

## Screen, HUD, console and menus

Everything drawn on top of or around the 3D view. Page: [Screen, HUD, console and menus](engine/ui.md).

| File | Lines | Built | What it does | Docs |
| --- | ---: | --- | --- | --- |
| [`screen.h`](../winquake/screen.h) | 58 | header | Screen and view-rectangle declarations. | [ui](engine/ui.md#screenh--screenc) |
| [`screen.c`](../winquake/screen.c) | 932 | always | `SCR_UpdateScreen` (the frame sequence), the view rectangle, centre text, the FPS counter and the loading plaque. | [ui](engine/ui.md#screenh--screenc) |
| [`draw.h`](../winquake/draw.h) | 52 | header | The 2D drawing interface (`Draw_*`). | [ui](engine/ui.md#drawh--drawc) |
| [`draw.c`](../winquake/draw.c) | 1046 | always | 2D primitives (characters, pictures, fills) and the bookkeeping that keeps the low-resolution view in step (`DRAW_TOUCH`). | [ui](engine/ui.md#drawh--drawc) |
| [`sbar.h`](../winquake/sbar.h) | 39 | header | Status-bar interface. | [ui](engine/ui.md#sbarh--sbarc) |
| [`sbar.c`](../winquake/sbar.c) | 1321 | always | The status bar, inventory, scoreboard and intermission screens. | [ui](engine/ui.md#sbarh--sbarc) |
| [`console.h`](../winquake/console.h) | 46 | header | Console interface (`Con_*`). | [ui](engine/ui.md#consoleh--consolec) |
| [`console.c`](../winquake/console.c) | 655 | always | The drop-down console and notify lines. | [ui](engine/ui.md#consoleh--consolec) |
| [`menu.h`](../winquake/menu.h) | 39 | header | Menu entry points (`M_Init`, `M_Draw`, `M_Keydown`, `M_Menu_Options_Shortcut`). | [ui](engine/ui.md#menuh--menuc) |
| [`menu.c`](../winquake/menu.c) | 3483 | always | All of Quake's menus, including the Playdate Options screen (Music, Crank speed, Dithering, Draw distance, Max framerate, Show FPS, …). | [ui](engine/ui.md#menuh--menuc) |

## Server, world and physics

Runs the level through the QuakeC VM, moves entities and tells clients what changed. Page: [Server, world and physics](engine/server.md).

| File | Lines | Built | What it does | Docs |
| --- | ---: | --- | --- | --- |
| [`server.h`](../winquake/server.h) | 257 | header | `server_t`, `client_t` and the entity constants (`MOVETYPE_*`, `SOLID_*`, `FL_*`, `EF_*`). | [server](engine/server.md#serverh) |
| [`sv_main.c`](../winquake/sv_main.c) | 1206 | always | Server start-up (`SV_SpawnServer`), client connections, and building the update messages. | [server](engine/server.md#sv_mainc) |
| [`sv_user.c`](../winquake/sv_user.c) | 630 | always | Reading client input and the player's movement and acceleration. | [server](engine/server.md#sv_userc) |
| [`sv_phys.c`](../winquake/sv_phys.c) | 1613 | always | Entity physics by movement type: walk, fly, toss, push, step, bounce. | [server](engine/server.md#sv_physc) |
| [`sv_move.c`](../winquake/sv_move.c) | 427 | always | Monster movement helpers (`SV_movestep`, `walkmove`, `movetogoal`). | [server](engine/server.md#sv_movec) |
| [`world.h`](../winquake/world.h) | 80 | header | Collision interface: `trace_t`, hulls, `SV_Move`. | [server](engine/server.md#worldh--worldc) |
| [`world.c`](../winquake/world.c) | 959 | always | Collision: the clip hulls, the area tree, `SV_LinkEdict` and `SV_Move`. | [server](engine/server.md#worldh--worldc) |

## QuakeC virtual machine

The bytecode interpreter that runs the game rules in `progs.dat`. Page: [QuakeC virtual machine](engine/quakec.md).

| File | Lines | Built | What it does | Docs |
| --- | ---: | --- | --- | --- |
| [`pr_comp.h`](../winquake/pr_comp.h) | 180 | header | The bytecode format: opcodes, statements, definitions and functions (shared with the QuakeC compiler). | [quakec](engine/quakec.md#pr_comph) |
| [`progdefs.h`](../winquake/progdefs.h) | 24 | header | Includes `progdefs.q1` (or `progdefs.q2` with `QUAKE2`). | [quakec](engine/quakec.md#progdefsh-progdefsq1-progdefsq2) |
| [`progdefs.q1`](../winquake/progdefs.q1) | 143 | header | The C layout of Quake 1's QuakeC globals and entity fields; its CRC (5927) is checked when `progs.dat` loads. | [quakec](engine/quakec.md#progdefsh-progdefsq1-progdefsq2) |
| [`progdefs.q2`](../winquake/progdefs.q2) | 158 | unused | The same for the Quake 2 variable set; only used when `QUAKE2` is defined. | [quakec](engine/quakec.md#progdefsh-progdefsq1-progdefsq2) |
| [`progs.h`](../winquake/progs.h) | 139 | header | Edicts, the `eval_t` union, accessor macros and the VM globals. | [quakec](engine/quakec.md#progsh) |
| [`pr_edict.c`](../winquake/pr_edict.c) | 1108 | always | Loading `progs.dat`, edict allocation, parsing the map's entity text and saving entities. | [quakec](engine/quakec.md#pr_edictc) |
| [`pr_exec.c`](../winquake/pr_exec.c) | 696 | always | The bytecode interpreter (`PR_ExecuteProgram`). | [quakec](engine/quakec.md#pr_execc) |
| [`pr_cmds.c`](../winquake/pr_cmds.c) | 1933 | always | The ~90 builtin functions (`PF_*`) QuakeC calls: sounds, spawning, tracing, messages. | [quakec](engine/quakec.md#pr_cmdsc) |

## Networking

Quake always talks to its server through sockets, even in single player (loopback). Page: [Networking](engine/network.md).

| File | Lines | Built | What it does | Docs |
| --- | ---: | --- | --- | --- |
| [`net.h`](../winquake/net.h) | 337 | header | The socket and driver structures and the connection-protocol constants. | [network](engine/network.md#neth) |
| [`net_main.c`](../winquake/net_main.c) | 890 | always | The driver-independent `NET_*` layer: connect, accept, send and receive, the driver tables. | [network](engine/network.md#net_mainc) |
| [`net_loop.h`](../winquake/net_loop.h) | 33 | header | The loopback driver's entry points. | [network](engine/network.md#net_loopc) |
| [`net_loop.c`](../winquake/net_loop.c) | 243 | always | The in-process loopback driver that connects the local client to the local server (single player). | [network](engine/network.md#net_loopc) |
| [`net_none.c`](../winquake/net_none.c) | 46 | always | The driver table for builds without network hardware: loopback only. This is what the Playdate builds. | [network](engine/network.md#net_nonec) |
| [`net_bsd.c`](../winquake/net_bsd.c) | 93 | not built | The driver table for UDP builds: loopback plus the datagram driver. | [network](engine/network.md#net_bsdc) |
| [`net_dgrm.h`](../winquake/net_dgrm.h) | 34 | not built | The datagram driver's entry points. | [network](engine/network.md#net_dgrmc) |
| [`net_dgrm.c`](../winquake/net_dgrm.c) | 1387 | not built | The datagram driver: a reliable connection over an unreliable transport, plus the LAN server list. | [network](engine/network.md#net_dgrmc) |
| [`net_udp.h`](../winquake/net_udp.h) | 39 | not built | The UDP driver's entry points. | [network](engine/network.md#net_udpc) |
| [`net_udp.c`](../winquake/net_udp.c) | 414 | not built | The UDP LAN driver on BSD sockets. | [network](engine/network.md#net_udpc) |
| [`net_vcr.h`](../winquake/net_vcr.h) | 37 | not built | The VCR (record and replay) driver's entry points. | [network](engine/network.md#net_vcrc-not-built) |
| [`net_vcr.c`](../winquake/net_vcr.c) | 167 | not built | A driver that records network traffic to a file and plays it back for reproducible multiplayer tests. | [network](engine/network.md#net_vcrc-not-built) |

## Models and map formats

The BSP, MDL and SPR file formats and their loader. Page: [Models and map formats](engine/models.md).

| File | Lines | Built | What it does | Docs |
| --- | ---: | --- | --- | --- |
| [`bspfile.h`](../winquake/bspfile.h) | 323 | header | On-disk BSP structures (the 15 lumps) and the map limits. | [models](engine/models.md#bspfileh) |
| [`modelgen.h`](../winquake/modelgen.h) | 140 | header | On-disk alias (MDL) structures. | [models](engine/models.md#modelgenh) |
| [`spritegn.h`](../winquake/spritegn.h) | 114 | header | On-disk sprite (SPR) structures. | [models](engine/models.md#spritegnh) |
| [`model.h`](../winquake/model.h) | 389 | header | The in-memory structures (`model_t`, `msurface_t`, `mleaf_t`, `mnode_t`, …). | [models](engine/models.md#modelh) |
| [`model.c`](../winquake/model.c) | 1849 | always | Loads and caches brush, alias and sprite models; point-in-leaf and PVS decompression. | [models](engine/models.md#modelc) |
| [`anorms.h`](../winquake/anorms.h) | 181 | header | The 162 precomputed vertex normals of alias models. | [models](engine/models.md#anormsh) |

## Playdate renderer (`pdr_*`)

The default 3D renderer (`PD_NEW_RENDERER=ON`), written for the Playdate's memory system. Page: [The Playdate renderer](engine/renderer-pdr.md).

| File | Lines | Built | What it does | Docs |
| --- | ---: | --- | --- | --- |
| [`pdr.h`](../winquake/pdr.h) | 269 | header · new renderer | The renderer's internal interface: constants, compact brush-data structs, shared globals, the span context. | [renderer-pdr](engine/renderer-pdr.md#pdrh) |
| [`pdr_main.c`](../winquake/pdr_main.c) | 912 | new renderer | `R_RenderView` and the per-frame setup; owns the globals the original refresh had; interlacing. | [renderer-pdr](engine/renderer-pdr.md#pdr_mainc) |
| [`pdr_world.c`](../winquake/pdr_world.c) | 1734 | new renderer | Builds the compact brush data at map load, walks the BSP front to back, keeps the per-row coverage mask, draws brush entities. | [renderer-pdr](engine/renderer-pdr.md#pdr_worldc) |
| [`pdr_span.c`](../winquake/pdr_span.c) | 418 | new renderer | Draws one span of one face: textured, water and sky spans straight from the mip texture. | [renderer-pdr](engine/renderer-pdr.md#pdr_spanc) |
| [`pdr_light.c`](../winquake/pdr_light.c) | 542 | new renderer | Light blocks in place of the surface cache; light styles, dynamic lights and `R_LightPoint`. | [renderer-pdr](engine/renderer-pdr.md#pdr_lightc) |
| [`pdr_alias.c`](../winquake/pdr_alias.c) | 861 | new renderer | Alias models (monsters, items, the weapon): transform, lighting and its own triangle rasterizer. | [renderer-pdr](engine/renderer-pdr.md#pdr_aliasc) |
| [`pdr_sprite.c`](../winquake/pdr_sprite.c) | 535 | new renderer | Sprites (explosions, bubbles) and particles. | [renderer-pdr](engine/renderer-pdr.md#pdr_spritec) |
| [`pdr_lowres.c`](../winquake/pdr_lowres.c) | 111 | new renderer | Hands the half-resolution 3D view to the display layer ([`display.c`](port/playdate.md#displayc)). | [renderer-pdr](engine/renderer-pdr.md#pdr_lowresc) |

## Original software renderer: interfaces and set-up

Id Software's WinQuake renderer, built with `PD_NEW_RENDERER=OFF`. `r_*` decides what to draw, `d_*` draws it. Page: [Original software renderer](engine/renderer-original.md).

| File | Lines | Built | What it does | Docs |
| --- | ---: | --- | --- | --- |
| [`render.h`](../winquake/render.h) | 158 | header | The public refresh interface the engine uses (`R_*`, `entity_t`, `refdef_t`); implemented by [`pdr_main.c`](engine/renderer-pdr.md#pdr_mainc) or [`r_main.c`](engine/renderer-original.md#r_mainc). | [renderer-original](engine/renderer-original.md#renderh) |
| [`r_shared.h`](../winquake/r_shared.h) | 173 | header · orig. renderer | Types shared between the refresh and the driver (`espan_t`, `surf_t`, `edge_t`). | [renderer-original](engine/renderer-original.md#r_sharedh) |
| [`r_local.h`](../winquake/r_local.h) | 325 | header · orig. renderer | Refresh-private definitions: lighting, clipped edges, clip planes, debug cvars. | [renderer-original](engine/renderer-original.md#r_localh) |
| [`d_iface.h`](../winquake/d_iface.h) | 244 | header | The refresh-to-driver contract (`WARP_WIDTH`, `particle_t`, the `D_*` entry points); included by [`quakedef.h`](engine/core.md#quakedefh), so also in the new-renderer build. | [renderer-original](engine/renderer-original.md#d_ifaceh) |
| [`d_local.h`](../winquake/d_local.h) | 123 | header · orig. renderer | Driver-private definitions: the surface cache node, span parameters, texture gradients. | [renderer-original](engine/renderer-original.md#d_localh) |
| [`r_main.c`](../winquake/r_main.c) | 1169 | orig. renderer | Frame control: `R_Init`, `R_RenderView` and the entity drawing order. | [renderer-original](engine/renderer-original.md#r_mainc) |
| [`r_misc.c`](../winquake/r_misc.c) | 542 | orig. renderer | Per-frame set-up (`R_SetupFrame`, frustum, `R_ViewChanged`) and debug helpers. | [renderer-original](engine/renderer-original.md#r_miscc) |
| [`r_vars.c`](../winquake/r_vars.c) | 34 | orig. renderer | Definitions of the refresh globals, collected in a block to avoid cache conflicts. | [renderer-original](engine/renderer-original.md#r_varsc--d_varsc) |
| [`d_vars.c`](../winquake/d_vars.c) | 45 | orig. renderer | Definitions of the driver globals (gradients, texture adjustments, buffers). | [renderer-original](engine/renderer-original.md#r_varsc--d_varsc) |
| [`d_init.c`](../winquake/d_init.c) | 131 | orig. renderer | Driver initialisation (`D_Init`) and the per-frame mip scale (`D_SetupFrame`); the `d_mipcap` and `d_subdiv16` cvars. | [renderer-original](engine/renderer-original.md#d_initc) |
| [`d_modech.c`](../winquake/d_modech.c) | 83 | orig. renderer | `D_ViewChanged`: recomputes what the driver derives from the view (row tables, aspect, particle clip limits). | [renderer-original](engine/renderer-original.md#d_modechc) |

## Original software renderer: refresh (`r_*`)

World traversal, edge list and scan, surface and model preparation.

| File | Lines | Built | What it does | Docs |
| --- | ---: | --- | --- | --- |
| [`r_bsp.c`](../winquake/r_bsp.c) | 1029 | orig. renderer | World traversal and brush entities: walks the BSP and marks visible faces. | [renderer-original](engine/renderer-original.md#r_bspc) |
| [`r_draw.c`](../winquake/r_draw.c) | 1538 | orig. renderer | Turns a face into clipped, projected edges (`R_RenderFace`, `R_ClipEdge`, `R_EmitEdge`). | [renderer-original](engine/renderer-original.md#r_drawc) |
| [`r_edge.c`](../winquake/r_edge.c) | 1489 | orig. renderer | The edge scan: sorts the edge list per scan line and emits the spans of the nearest surface; has an ARM twin ([`r_edge_arm.S`](engine/perf-infrastructure.md#r_edge_arms)). | [renderer-original](engine/renderer-original.md#r_edgec) |
| [`r_surf.c`](../winquake/r_surf.c) | 936 | orig. renderer | Builds the surface cache: the lit texture of a face at a mip level. | [renderer-original](engine/renderer-original.md#r_surfc) |
| [`r_light.c`](../winquake/r_light.c) | 453 | orig. renderer | Lighting: dynamic lights, `R_LightPoint`, light-style animation. | [renderer-original](engine/renderer-original.md#r_lightc) |
| [`r_sky.c`](../winquake/r_sky.c) | 241 | orig. renderer | Splits the sky texture into its layers and generates the scrolling sky tile. | [renderer-original](engine/renderer-original.md#r_skyc) |
| [`r_alias.c`](../winquake/r_alias.c) | 891 | orig. renderer | Alias models: transform, light and set-up before [`d_polyse.c`](engine/renderer-original.md#d_polysec) draws them. | [renderer-original](engine/renderer-original.md#r_aliasc) |
| [`r_aclip.c`](../winquake/r_aclip.c) | 369 | orig. renderer | Clips alias triangles that cross the screen edges or the near plane. | [renderer-original](engine/renderer-original.md#r_aclipc) |
| [`r_sprite.c`](../winquake/r_sprite.c) | 401 | orig. renderer | Sprites: frame selection, rotation, clipping, hand-off to [`d_sprite.c`](engine/renderer-original.md#d_spritec). | [renderer-original](engine/renderer-original.md#r_spritec) |
| [`r_part.c`](../winquake/r_part.c) | 811 | always | The particle simulation and effect spawners called by the client; used by both renderers. | [renderer-original](engine/renderer-original.md#r_partc) |
| [`r_efrag.c`](../winquake/r_efrag.c) | 276 | always | Entity fragments: which BSP leaves each entity touches; used by both renderers. | [renderer-original](engine/renderer-original.md#r_efragc) |

## Original software renderer: driver (`d_*`)

Drawing the spans, models, sprites and particles that the refresh prepared.

| File | Lines | Built | What it does | Docs |
| --- | ---: | --- | --- | --- |
| [`d_edge.c`](../winquake/d_edge.c) | 431 | orig. renderer | `D_DrawSurfaces`: draws each surface's spans (sky, water, textured). | [renderer-original](engine/renderer-original.md#d_edgec) |
| [`d_surf.c`](../winquake/d_surf.c) | 386 | orig. renderer | The surface cache allocator and `D_CacheSurface`. | [renderer-original](engine/renderer-original.md#d_surfc) |
| [`d_scan.c`](../winquake/d_scan.c) | 736 | orig. renderer | The span rasterisers (`D_DrawSpans8`, turbulent water, z spans) and the low-resolution upscale; has an ARM twin ([`d_scan_arm.S`](engine/perf-infrastructure.md#d_scan_arms)). | [renderer-original](engine/renderer-original.md#d_scanc) |
| [`d_sky.c`](../winquake/d_sky.c) | 143 | orig. renderer | Draws sky spans from the two scrolling layers. | [renderer-original](engine/renderer-original.md#d_skyc) |
| [`d_polyse.c`](../winquake/d_polyse.c) | 1523 | orig. renderer | The alias-model triangle rasteriser (Gouraud-lit, affine-textured). | [renderer-original](engine/renderer-original.md#d_polysec) |
| [`d_sprite.c`](../winquake/d_sprite.c) | 437 | orig. renderer | Draws sprites as z-tested polygons. | [renderer-original](engine/renderer-original.md#d_spritec) |
| [`d_part.c`](../winquake/d_part.c) | 226 | orig. renderer | Draws particles as z-tested dots. | [renderer-original](engine/renderer-original.md#d_partc) |
| [`d_zpoint.c`](../winquake/d_zpoint.c) | 47 | orig. renderer | `D_DrawZPoint`: a point with a z test (not called anywhere in this tree). | [renderer-original](engine/renderer-original.md#d_zpointc) |
| [`d_fill.c`](../winquake/d_fill.c) | 88 | orig. renderer | `D_FillRect`: clears a rectangle (not called anywhere in this tree). | [renderer-original](engine/renderer-original.md#d_fillc) |

## Performance infrastructure

Profiling hooks, stack budgeting and the hand-written assembly. Page: [Performance infrastructure](engine/perf-infrastructure.md).

| File | Lines | Built | What it does | Docs |
| --- | ---: | --- | --- | --- |
| [`pdprof.h`](../winquake/pdprof.h) | 94 | header | Section timers, counters and stack probes; everything compiles to nothing without `PD_PROFILE`. Implemented by [`pdprof.c`](port/playdate.md#pdprofc). | [perf-infrastructure](engine/perf-infrastructure.md#pdprofh) |
| [`pd_stack.h`](../winquake/pd_stack.h) | 59 | header | "Is there room on the fast stack for this buffer?": the `PD_STACK` budget check. | [perf-infrastructure](engine/perf-infrastructure.md#pd_stackh) |
| [`pd_asm.h`](../winquake/pd_asm.h) | 35 | header | The switches for the assembly and its A/B and check modes (`PD_USE_ASM`, `PD_ASM_AB`, `PD_ASM_CHECK`). | [perf-infrastructure](engine/perf-infrastructure.md#pd_asmh) |
| [`asm_draw.h`](../winquake/asm_draw.h) | 151 | unused | Struct byte offsets from the original x86 assembly; kept as a layout reference, nothing includes it. | [perf-infrastructure](engine/perf-infrastructure.md#asm_drawh) |
| [`d_scan_arm.S`](../winquake/d_scan_arm.S) | 307 | asm | Thumb-2 textured span drawer for the Cortex-M7 (C twin: [`d_scan.c`](engine/renderer-original.md#d_scanc)). | [perf-infrastructure](engine/perf-infrastructure.md#d_scan_arms) |
| [`r_edge_arm.S`](../winquake/r_edge_arm.S) | 458 | asm | Thumb-2 edge-scan routines for span generation and active-edge upkeep (C twin: [`r_edge.c`](engine/renderer-original.md#r_edgec)). | [perf-infrastructure](engine/perf-infrastructure.md#r_edge_arms) |

## Legacy x86 headers

From the original DOS/Windows build; nothing in this tree includes them. Documented together in [Platform interface headers](engine/platform-interfaces.md#legacy-x86-headers-not-used).

| File | Lines | Built | What it does | Docs |
| --- | ---: | --- | --- | --- |
| [`quakeasm.h`](../winquake/quakeasm.h) | 263 | unused | GNU-as `.extern` declarations of the renderer globals the x86 assembly read. | [platform-interfaces](engine/platform-interfaces.md#legacy-x86-headers-not-used) |
| [`d_ifacea.h`](../winquake/d_ifacea.h) | 96 | unused | Offsets and constants of the `d_iface.h` structs for the x86 driver assembly. | [platform-interfaces](engine/platform-interfaces.md#legacy-x86-headers-not-used) |
| [`block8.h`](../winquake/block8.h) | 143 | unused | x86 assembly macros for the 8-bit surface-cache block draw. | [platform-interfaces](engine/platform-interfaces.md#legacy-x86-headers-not-used) |
| [`block16.h`](../winquake/block16.h) | 142 | unused | x86 assembly macros for the 16-bit surface-cache block draw. | [platform-interfaces](engine/platform-interfaces.md#legacy-x86-headers-not-used) |
| [`vgamodes.h`](../winquake/vgamodes.h) | 599 | unused | VGA mode tables for the DOS port. | [platform-interfaces](engine/platform-interfaces.md#legacy-x86-headers-not-used) |
| [`resource.h`](../winquake/resource.h) | 20 | unused | Windows resource IDs for `winquake.rc`. | [platform-interfaces](engine/platform-interfaces.md#legacy-x86-headers-not-used) |

## Engine build file

Which engine files each configuration compiles.

| File | Lines | Built | What it does | Docs |
| --- | ---: | --- | --- | --- |
| [`CMakeLists.txt`](../winquake/CMakeLists.txt) | 112 | build | Builds the engine as the `winquake` object library: the core list, `pdr_*.c` or `r_*.c`/`d_*.c` by renderer, the network driver by platform, the ARM files with `PD_ASM`. | [build](build-system.md#winquakecmakeliststxt-the-engine) |

## Scripts (`scripts/`)

Run on your computer around a device build. Page: [Scripts and tools](tools.md).

| File | Lines | Built | What it does | Docs |
| --- | ---: | --- | --- | --- |
| [`release-device.sh`](../scripts/release-device.sh) | 80 | tool | A clean Release build of the device `.pdx` with a new build number, shipping the shareware pak when one is present. | [tools](tools.md#scriptsrelease-devicesh) |
| [`install-device.sh`](../scripts/install-device.sh) | 68 | tool | Copies the device `.pdx` to a USB-connected Playdate and launches it. | [tools](tools.md#scriptsinstall-devicesh) |
| [`pd-bench.sh`](../scripts/pd-bench.sh) | 55 | tool | Installs a profiling build, runs it for a set time and fetches `prof.csv`. | [tools](tools.md#scriptspd-benchsh) |
| [`pd-report.py`](../scripts/pd-report.py) | 134 | tool | Summarises and compares `prof.csv` files, including the `--ab` paired comparison. | [tools](tools.md#scriptspd-reportpy) |
| [`gen-bluenoise.py`](../scripts/gen-bluenoise.py) | 102 | tool | Generates the blue-noise tile ([`bluenoise.h`](port/playdate.md#bluenoiseh)) with void-and-cluster. | [tools](tools.md#scriptsgen-bluenoisepy) |
| [`__pycache__/pd-report.cpython-314.pyc`](../scripts/__pycache__/pd-report.cpython-314.pyc) | — | generated | Python bytecode cache of `pd-report.py`; not source. | — |

## Host check (`tools/hostcheck/`)

Runs the real engine and the real [`display.c`](port/playdate.md#displayc) on the host to prove an optimisation does not change the picture. Page: [Scripts and tools](tools.md#toolshostcheckhostcheckc).

| File | Lines | Built | What it does | Docs |
| --- | ---: | --- | --- | --- |
| [`hostcheck.c`](../tools/hostcheck/hostcheck.c) | 944 | host check | The test program: scripted scenes, menu and input tests, golden frame hashes, map tours and screenshot dumps. | [tools](tools.md#toolshostcheckhostcheckc) |
| [`build.sh`](../tools/hostcheck/build.sh) | 76 | tool | Builds [`hostcheck.c`](tools.md#toolshostcheckhostcheckc) against a source tree, in either renderer, with switches for the original code paths. | [tools](tools.md#toolshostcheckbuildsh) |
| [`run.sh`](../tools/hostcheck/run.sh) | 15 | tool | Builds and runs the host check in its different modes. | [tools](tools.md#toolshostcheckrunsh) |
| [`golden-compare.py`](../tools/hostcheck/golden-compare.py) | 24 | tool | Compares two golden hash files frame by frame. | [tools](tools.md#toolshostcheckgolden-comparepy) |
| [`compare-shots.py`](../tools/hostcheck/compare-shots.py) | 97 | tool | Measures how many pixels differ between two sets of screenshots and writes side-by-side PNGs. | [tools](tools.md#toolshostcheckcompare-shotspy) |

## Documentation

These pages. Start at the [documentation index](README.md).

| File | Lines | Built | What it does | Docs |
| --- | ---: | --- | --- | --- |
| [`README.md`](../docs/README.md) | 115 | doc | The documentation index: reading paths, architecture, the page map and conventions. | [docs index](README.md) |
| [`source-index.md`](../docs/source-index.md) | — | doc | This file. | — |
| [`build-system.md`](../docs/build-system.md) | 292 | doc | CMake files, every `PD_*` option, release builds, which `pak0.pak` the `.pdx` gets, editor tasks. | [build](build-system.md) |
| [`tools.md`](../docs/tools.md) | 276 | doc | The scripts and the host check harness. | [tools](tools.md) |
| [`port/overview.md`](../docs/port/overview.md) | 285 | doc | The shared platform layer and the `qembd_*` board interface. | [overview](port/overview.md) |
| [`port/playdate.md`](../docs/port/playdate.md) | 513 | doc | The Playdate board: main loop, display and dithering, files, sound, music, profiler. | [playdate](port/playdate.md) |
| [`port/game-data.md`](../docs/port/game-data.md) | 249 | doc | The shareware and the 2021 re-release `pak0.pak`: what the engine needs from them. | [game-data](port/game-data.md) |
| [`engine/core.md`](../docs/engine/core.md) | 531 | doc | Engine core: start-up, memory, strings, files, cvars, commands, math, WADs. | [core](engine/core.md) |
| [`engine/platform-interfaces.md`](../docs/engine/platform-interfaces.md) | 139 | doc | The video, input, sound and CD audio headers and the legacy x86 headers. | [platform-interfaces](engine/platform-interfaces.md) |
| [`engine/client.md`](../docs/engine/client.md) | 294 | doc | The client: connection, parsing, input, demos, the camera, keys. | [client](engine/client.md) |
| [`engine/ui.md`](../docs/engine/ui.md) | 257 | doc | Screen, HUD, console and menus (including the Options screen). | [ui](engine/ui.md) |
| [`engine/server.md`](../docs/engine/server.md) | 244 | doc | The server, world collision and physics. | [server](engine/server.md) |
| [`engine/quakec.md`](../docs/engine/quakec.md) | 258 | doc | The QuakeC virtual machine and its builtins. | [quakec](engine/quakec.md) |
| [`engine/network.md`](../docs/engine/network.md) | 192 | doc | The network layer and drivers. | [network](engine/network.md) |
| [`engine/models.md`](../docs/engine/models.md) | 171 | doc | BSP, MDL and SPR formats and the model loader. | [models](engine/models.md) |
| [`engine/renderer-pdr.md`](../docs/engine/renderer-pdr.md) | 472 | doc | The Playdate renderer. | [renderer-pdr](engine/renderer-pdr.md) |
| [`engine/renderer-original.md`](../docs/engine/renderer-original.md) | 391 | doc | The original software renderer. | [renderer-original](engine/renderer-original.md) |
| [`engine/perf-infrastructure.md`](../docs/engine/perf-infrastructure.md) | 249 | doc | Profiling, stack budgeting and the assembly. | [perf-infrastructure](engine/perf-infrastructure.md) |
| [`demo.gif`](../docs/demo.gif) | — | doc | Demo 1 as the Playdate screen shows it (captured from the host check; used by the README). | [README](../README.md) |
| [`note/release/RELEASE_NOTES_0.3.0.md`](../note/release/RELEASE_NOTES_0.3.0.md) | 95 | doc | Release notes for version 0.3.0. | [RELEASE_NOTES_0.3.0.md](../note/release/RELEASE_NOTES_0.3.0.md) |

## Find a file by name

Every source and build file by name; each link opens the section that describes it (files without a section link to the file). Documentation pages are listed in [Documentation](#documentation).

**A** · [`anorms.h`](engine/models.md#anormsh) · [`asm_draw.h`](engine/perf-infrastructure.md#asm_drawh) · [`autofire.c`](port/playdate.md#autofirec--autofireh) · [`autofire.h`](port/playdate.md#autofirec--autofireh)

**B** · [`block16.h`](engine/platform-interfaces.md#legacy-x86-headers-not-used) · [`block8.h`](engine/platform-interfaces.md#legacy-x86-headers-not-used) · [`bluenoise.h`](port/playdate.md#bluenoiseh) · [`bspfile.h`](engine/models.md#bspfileh) · [`build.sh`](tools.md#toolshostcheckbuildsh)

**C** · [`cd_null.c`](port/overview.md#portcd_nullc) · [`cd_pd.c`](port/playdate.md#cd_pdc) · [`cdaudio.h`](engine/platform-interfaces.md#cdaudioh) · [`chase.c`](engine/client.md#chasec) · [`cl_demo.c`](engine/client.md#cl_democ) · [`cl_input.c`](engine/client.md#cl_inputc) · [`cl_main.c`](engine/client.md#cl_mainc) · [`cl_parse.c`](engine/client.md#cl_parsec) · [`cl_tent.c`](engine/client.md#cl_tentc) · [`client.h`](engine/client.md#clienth) · [`CMakeLists.txt`](build-system.md#top-level-cmakeliststxt) · [`port/CMakeLists.txt`](build-system.md#portcmakeliststxt-shared-platform-layer) · [`port/boards/playdate/CMakeLists.txt`](build-system.md#playdate-board-cmakeliststxt) · [`winquake/CMakeLists.txt`](build-system.md#winquakecmakeliststxt-the-engine) · [`cmd.c`](engine/core.md#cmdh--cmdc) · [`cmd.h`](engine/core.md#cmdh--cmdc) · [`common.c`](engine/core.md#commonh--commonc) · [`common.h`](engine/core.md#commonh--commonc) · [`compare-shots.py`](tools.md#toolshostcheckcompare-shotspy) · [`console.c`](engine/ui.md#consoleh--consolec) · [`console.h`](engine/ui.md#consoleh--consolec) · [`crc.c`](engine/core.md#crch--crcc) · [`crc.h`](engine/core.md#crch--crcc) · [`cvar.c`](engine/core.md#cvarh--cvarc) · [`cvar.h`](engine/core.md#cvarh--cvarc)

**D** · [`d_edge.c`](engine/renderer-original.md#d_edgec) · [`d_fill.c`](engine/renderer-original.md#d_fillc) · [`d_iface.h`](engine/renderer-original.md#d_ifaceh) · [`d_ifacea.h`](engine/platform-interfaces.md#legacy-x86-headers-not-used) · [`d_init.c`](engine/renderer-original.md#d_initc) · [`d_local.h`](engine/renderer-original.md#d_localh) · [`d_modech.c`](engine/renderer-original.md#d_modechc) · [`d_part.c`](engine/renderer-original.md#d_partc) · [`d_polyse.c`](engine/renderer-original.md#d_polysec) · [`d_scan.c`](engine/renderer-original.md#d_scanc) · [`d_scan_arm.S`](engine/perf-infrastructure.md#d_scan_arms) · [`d_sky.c`](engine/renderer-original.md#d_skyc) · [`d_sprite.c`](engine/renderer-original.md#d_spritec) · [`d_surf.c`](engine/renderer-original.md#d_surfc) · [`d_vars.c`](engine/renderer-original.md#r_varsc--d_varsc) · [`d_zpoint.c`](engine/renderer-original.md#d_zpointc) · [`defaultcfg.h`](engine/core.md#built-in-defaultcfg-defaultcfgh) · [`display.c`](port/playdate.md#displayc) · [`DOS-CONFIG.CFG.bak`](build-system.md#source) · [`draw.c`](engine/ui.md#drawh--drawc) · [`draw.h`](engine/ui.md#drawh--drawc)

**F** · [`fio.c`](port/playdate.md#fioc) · [`fio_posix.c`](port/overview.md#portfiofio_posixc)

**G** · [`gen-bluenoise.py`](tools.md#scriptsgen-bluenoisepy) · [`.gitignore`](README.md#not-documented) · [`port/boards/playdate/.gitignore`](build-system.md#source) · [`.gitkeep`](build-system.md#source) · [`golden-compare.py`](tools.md#toolshostcheckgolden-comparepy) · [`gpl-2.0.txt`](../gpl-2.0.txt)

**H** · [`host.c`](engine/core.md#hostc) · [`host_cmd.c`](engine/core.md#host_cmdc) · [`hostcheck.c`](tools.md#toolshostcheckhostcheckc)

**I** · [`in_port.c`](port/overview.md#portin_portc) · [`input.h`](engine/platform-interfaces.md#inputh) · [`install-device.sh`](tools.md#scriptsinstall-devicesh)

**K** · [`keyqueue.c`](port/playdate.md#keyqueuec--keyqueueh) · [`keyqueue.h`](port/playdate.md#keyqueuec--keyqueueh) · [`keys.c`](engine/client.md#keysh--keysc) · [`keys.h`](engine/client.md#keysh--keysc)

**L** · [`launch.json`](build-system.md#editor-integration)

**M** · [`main.c`](port/playdate.md#mainc) · [`mathlib.c`](engine/core.md#mathlibh--mathlibc) · [`mathlib.h`](engine/core.md#mathlibh--mathlibc) · [`menu.c`](engine/ui.md#menuh--menuc) · [`menu.h`](engine/ui.md#menuh--menuc) · [`model.c`](engine/models.md#modelc) · [`model.h`](engine/models.md#modelh) · [`modelgen.h`](engine/models.md#modelgenh)

**N** · [`net.h`](engine/network.md#neth) · [`net_bsd.c`](engine/network.md#net_bsdc) · [`net_dgrm.c`](engine/network.md#net_dgrmc) · [`net_dgrm.h`](engine/network.md#net_dgrmc) · [`net_loop.c`](engine/network.md#net_loopc) · [`net_loop.h`](engine/network.md#net_loopc) · [`net_main.c`](engine/network.md#net_mainc) · [`net_none.c`](engine/network.md#net_nonec) · [`net_udp.c`](engine/network.md#net_udpc) · [`net_udp.h`](engine/network.md#net_udpc) · [`net_vcr.c`](engine/network.md#net_vcrc-not-built) · [`net_vcr.h`](engine/network.md#net_vcrc-not-built) · [`nonintel.c`](engine/core.md#nonintelc)

**P** · [`pd-bench.sh`](tools.md#scriptspd-benchsh) · [`pd-report.py`](tools.md#scriptspd-reportpy) · [`pd_asm.h`](engine/perf-infrastructure.md#pd_asmh) · [`pd_compat.h`](port/playdate.md#pd_compath) · [`pd_port.h`](port/playdate.md#pd_porth) · [`pd_stack.h`](engine/perf-infrastructure.md#pd_stackh) · [`pd_stdio.c`](port/playdate.md#pd_stdioc) · [`pdprof.c`](port/playdate.md#pdprofc) · [`pdprof.h`](engine/perf-infrastructure.md#pdprofh) · [`pdr.h`](engine/renderer-pdr.md#pdrh) · [`pdr_alias.c`](engine/renderer-pdr.md#pdr_aliasc) · [`pdr_light.c`](engine/renderer-pdr.md#pdr_lightc) · [`pdr_lowres.c`](engine/renderer-pdr.md#pdr_lowresc) · [`pdr_main.c`](engine/renderer-pdr.md#pdr_mainc) · [`pdr_span.c`](engine/renderer-pdr.md#pdr_spanc) · [`pdr_sprite.c`](engine/renderer-pdr.md#pdr_spritec) · [`pdr_world.c`](engine/renderer-pdr.md#pdr_worldc) · [`pdx_buildnumber.cmake`](build-system.md#playdate-board-cmakeliststxt) · [`pdx_pak.cmake`](build-system.md#which-pak0pak-the-pdx-gets) · [`pdx_rename.cmake`](build-system.md#playdate-board-cmakeliststxt) · [`pdxinfo`](build-system.md#source) · [`platform.cmake`](build-system.md#playdate-platformcmake) · [`pr_cmds.c`](engine/quakec.md#pr_cmdsc) · [`pr_comp.h`](engine/quakec.md#pr_comph) · [`pr_edict.c`](engine/quakec.md#pr_edictc) · [`pr_exec.c`](engine/quakec.md#pr_execc) · [`progdefs.h`](engine/quakec.md#progdefsh-progdefsq1-progdefsq2) · [`progdefs.q1`](engine/quakec.md#progdefsh-progdefsq1-progdefsq2) · [`progdefs.q2`](engine/quakec.md#progdefsh-progdefsq1-progdefsq2) · [`progs.h`](engine/quakec.md#progsh) · [`protocol.h`](engine/client.md#protocolh)

**Q** · [`quakeasm.h`](engine/platform-interfaces.md#legacy-x86-headers-not-used) · [`quakedef.h`](engine/core.md#quakedefh) · [`quakembd.h`](port/overview.md#includequakembdh)

**R** · [`r_aclip.c`](engine/renderer-original.md#r_aclipc) · [`r_alias.c`](engine/renderer-original.md#r_aliasc) · [`r_bsp.c`](engine/renderer-original.md#r_bspc) · [`r_draw.c`](engine/renderer-original.md#r_drawc) · [`r_edge.c`](engine/renderer-original.md#r_edgec) · [`r_edge_arm.S`](engine/perf-infrastructure.md#r_edge_arms) · [`r_efrag.c`](engine/renderer-original.md#r_efragc) · [`r_light.c`](engine/renderer-original.md#r_lightc) · [`r_local.h`](engine/renderer-original.md#r_localh) · [`r_main.c`](engine/renderer-original.md#r_mainc) · [`r_misc.c`](engine/renderer-original.md#r_miscc) · [`r_part.c`](engine/renderer-original.md#r_partc) · [`r_shared.h`](engine/renderer-original.md#r_sharedh) · [`r_sky.c`](engine/renderer-original.md#r_skyc) · [`r_sprite.c`](engine/renderer-original.md#r_spritec) · [`r_surf.c`](engine/renderer-original.md#r_surfc) · [`r_vars.c`](engine/renderer-original.md#r_varsc--d_varsc) · [`README.md`](../README.md) · [`release-device.sh`](tools.md#scriptsrelease-devicesh) · [`render.h`](engine/renderer-original.md#renderh) · [`resource.h`](engine/platform-interfaces.md#legacy-x86-headers-not-used) · [`run.sh`](tools.md#toolshostcheckrunsh)

**S** · [`sbar.c`](engine/ui.md#sbarh--sbarc) · [`sbar.h`](engine/ui.md#sbarh--sbarc) · [`screen.c`](engine/ui.md#screenh--screenc) · [`screen.h`](engine/ui.md#screenh--screenc) · [`server.h`](engine/server.md#serverh) · [`snd.c`](port/playdate.md#sndc) · [`sound.h`](engine/platform-interfaces.md#soundh) · [`spritegn.h`](engine/models.md#spritegnh) · [`sv_main.c`](engine/server.md#sv_mainc) · [`sv_move.c`](engine/server.md#sv_movec) · [`sv_phys.c`](engine/server.md#sv_physc) · [`sv_user.c`](engine/server.md#sv_userc) · [`sys.h`](engine/core.md#sysh) · [`sys_port.c`](port/overview.md#portsys_portc)

**T** · [`tasks.json`](build-system.md#editor-integration) · [`toolchain.cmake`](build-system.md#toolchaincmake)

**V** · [`vgamodes.h`](engine/platform-interfaces.md#legacy-x86-headers-not-used) · [`vid.h`](engine/platform-interfaces.md#vidh) · [`vid_port.c`](port/overview.md#portvid_portc) · [`view.c`](engine/client.md#viewh--viewc) · [`view.h`](engine/client.md#viewh--viewc)

**W** · [`wad.c`](engine/core.md#wadh--wadc) · [`wad.h`](engine/core.md#wadh--wadc) · [`weapons.c`](port/playdate.md#weaponsc--weaponsh) · [`weapons.h`](port/playdate.md#weaponsc--weaponsh) · [`world.c`](engine/server.md#worldh--worldc) · [`world.h`](engine/server.md#worldh--worldc)

**Z** · [`zone.c`](engine/core.md#zoneh--zonec) · [`zone.h`](engine/core.md#zoneh--zonec)

## What this index does not list

- Generated or local directories, all git-ignored: `build-*/`, `bench-results/`, `tools/hostcheck/out/`, `port/boards/playdate/.build_number`, `port/boards/playdate/Source/pdex.*`.
- `port/boards/playdate/Source/id1/` (your `pak0.pak`, the optional `pak0_demo.pak` and the music): game data, not part of the source tree. What the engine needs from the pak: [Game data](port/game-data.md).
- The upstream desktop, RISC-V and STM32 boards and `lib/minifb`, removed from the tree: [Removed boards](build-system.md#removed-boards).
