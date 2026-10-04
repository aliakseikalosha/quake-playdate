# Quake for Playdate: source documentation

This folder describes **every source file** of the project with its purpose, its main functions and types, and code
examples taken from (or written against) the real code. The project is a [WinQuake](https://github.com/id-Software/Quake) port for the
[Playdate](https://play.date/) handheld, derived from [sysprog21/quake-embedded](https://github.com/sysprog21/quake-embedded).
For *using* the game (controls, options, building, benchmark results) see the project [README](../README.md).
The **[Source code index](source-index.md)** lists every file of the repository in one place, with what it does, whether it is built, and a link to the section that describes it in detail.

> **What "port changes" means here.** Where a page says *"Port changes"* it describes how the file differs from the **original WinQuake source**
> (the first commit of this repository, `5288eee Imported original WinQuake GPL source`), found by diffing against it. Files with no such note differ from the original only in small ways
> (formatting, `float` literals, minor type fixes).

## Start here

| If you want to… | Read |
| --- | --- |
| Find out what a file does, whether it is built, and where it is described | [Source code index](source-index.md) |
| Build the game, understand the CMake options | [Build system](build-system.md) |
| Use the shareware or the 2021 re-release `pak0.pak`, or find out why the controls or levels misbehave with one | [Game data](port/game-data.md) |
| See how the engine is wired to the Playdate | [Shared platform layer](port/overview.md), then [Playdate board](port/playdate.md) |
| Understand how a frame is drawn and shown on the 1-bit screen | [Playdate renderer](engine/renderer-pdr.md), [Playdate board → display.c](port/playdate.md#displayc) |
| Understand the optimisation work | [Performance infrastructure](engine/perf-infrastructure.md), [Scripts and tools](tools.md) |
| Learn Quake's own architecture | [Engine core](engine/core.md), [Client](engine/client.md), [Server](engine/server.md), [QuakeC](engine/quakec.md) |
| Port to another board | [Shared platform layer → skeleton](port/overview.md#minimal-board-skeleton) (the Playdate is the only board in the tree) |

## Architecture in one picture

```
                         ┌───────────────────────────────────────────────────────────────┐
  Playdate OS ──events──►│ port/boards/playdate: main.c display.c fio.c snd.c cd_pd.c    │
                         └───────────────┬───────────────────────────────▲───────────────┘
                                         │ qembd_* hooks (include/quakembd.h)│
                         ┌───────────────▼───────────────────────────────┴───────────────┐
                         │ port/: sys_port.c  vid_port.c  in_port.c                      │
                         └───────────────┬───────────────────────────────────────────────┘
                                         │ Sys_*  VID_*  IN_*  CDAudio_*  S_*
┌────────────────────────────────────────▼─────────────────────────────────────────────────┐
│ winquake/ (the engine)                                                                     │
│                                                                                            │
│  host ─► client ─► view ─► screen/sbar/menu/console ─► draw ─► renderer ─► VID_Update      │
│    │        ▲                                          (pdr_* or r_*/d_*)                  │
│    └──► server ─► physics/world ─► QuakeC VM (pr_*) ─► builtins                            │
│            ▲                                                                               │
│            └─ network (loopback in single player)                                          │
│  support: common, zone (hunk), cvar, cmd, mathlib, wad, model (BSP/MDL/SPR)                │
└────────────────────────────────────────────────────────────────────────────────────────────┘
```

One frame (single player), in `Host_Frame`: read buttons → run console commands → client makes a movement command → the local server runs physics and QuakeC
→ the client applies the server's reply → `SCR_UpdateScreen` renders the 3D view and the HUD into an 8-bit buffer → `VID_Update` hands dirty rectangles to the board's `qembd_fillrect`
→ on the Playdate, `display.c` dithers them into the 1-bit LCD frame.

## Documentation map

### Index

| Page | Covers |
| --- | --- |
| [Source code index](source-index.md) | Every file of the repository, grouped by directory and subsystem: what it does, whether it is built (and in which configuration), its size, and a link to its detailed section; an A-Z lookup |

### Build, platform and tools

| Page | Covers |
| --- | --- |
| [Build system](build-system.md) | `CMakeLists.txt` (root, `winquake/`, `port/`), `platform.cmake` and every `PD_*` option, the Playdate board's CMake files (`pdx_*.cmake`, `toolchain.cmake`), VS Code tasks |
| [Shared platform layer](port/overview.md) | `include/quakembd.h`, `port/sys_port.c`, `vid_port.c`, `in_port.c`, `cd_null.c` (host check only), `fio/fio_posix.c` |
| [Game data](port/game-data.md) | The shareware and the re-release `pak0.pak`: the built-in `default.cfg`, unused cvars, `MOVETYPE_BOUNCEMISSILE`, the alias setup cache fix |
| [Playdate board](port/playdate.md) | `main.c`, `display.c`, `bluenoise.h`, `fio.c`, `pd_stdio.c`, `pd_compat.h`, `pd_port.h`, `keyqueue.*`, `autofire.*`, `weapons.*`, `snd.c`, `cd_pd.c`, `pdprof.c`, `.gitignore` |
| [Scripts and tools](tools.md) | `scripts/*` (release build, install, bench, report, blue noise) and `tools/hostcheck/*` |

### The engine (`winquake/`)

| Page | Files |
| --- | --- |
| [Engine core](engine/core.md) | `quakedef.h`, `host.c`, `host_cmd.c`, `sys.h`, `common.h/.c`, `zone.h/.c`, `cvar.h/.c`, `cmd.h/.c`, `defaultcfg.h`, `crc.h/.c`, `mathlib.h/.c`, `wad.h/.c`, `nonintel.c` |
| [Platform interface headers](engine/platform-interfaces.md) | `vid.h`, `input.h`, `sound.h`, `cdaudio.h`; unused legacy headers `quakeasm.h`, `d_ifacea.h`, `block8.h`, `block16.h`, `vgamodes.h`, `resource.h` |
| [Client](engine/client.md) | `client.h`, `protocol.h`, `cl_main.c`, `cl_parse.c`, `cl_input.c`, `cl_demo.c`, `cl_tent.c`, `view.h/.c`, `chase.c`, `keys.h/.c` |
| [Screen, HUD, console and menus](engine/ui.md) | `screen.h/.c`, `draw.h/.c`, `sbar.h/.c`, `console.h/.c`, `menu.h/.c` |
| [Server, world and physics](engine/server.md) | `server.h`, `sv_main.c`, `sv_user.c`, `sv_phys.c`, `sv_move.c`, `world.h/.c` |
| [QuakeC virtual machine](engine/quakec.md) | `pr_comp.h`, `progdefs.h/.q1/.q2`, `progs.h`, `pr_edict.c`, `pr_exec.c`, `pr_cmds.c` |
| [Networking](engine/network.md) | `net.h`, `net_main.c`, `net_loop.h/.c`, `net_none.c`, `net_bsd.c`, `net_dgrm.h/.c`, `net_udp.h/.c`, `net_vcr.h/.c` |
| [Models and map formats](engine/models.md) | `bspfile.h`, `modelgen.h`, `spritegn.h`, `model.h/.c`, `anorms.h` |
| [Playdate renderer](engine/renderer-pdr.md) | `pdr.h`, `pdr_main.c`, `pdr_world.c`, `pdr_span.c`, `pdr_light.c`, `pdr_alias.c`, `pdr_sprite.c`, `pdr_lowres.c` |
| [Original software renderer](engine/renderer-original.md) | `render.h`, `r_shared.h`, `r_local.h`, `d_iface.h`, `d_local.h`, `r_*.c`, `d_*.c` |
| [Performance infrastructure](engine/perf-infrastructure.md) | `pdprof.h`, `pd_stack.h`, `pd_asm.h`, `asm_draw.h`, `d_scan_arm.S`, `r_edge_arm.S` |

`winquake/CMakeLists.txt` is covered in the [build system](build-system.md#winquakecmakeliststxt-the-engine) page.

### Not documented

- **Removed boards:** the upstream desktop, RISC-V and STM32 boards, their `lib/minifb` submodule and the files only they used were removed from the tree (see [Removed boards](build-system.md#removed-boards)); they are in the git history.
- Generated or local directories: `build-*/`, `bench-results/`, `tools/hostcheck/out/` (all git-ignored).
- `port/boards/playdate/Source/id1/pak0.pak`: the game data, not part of the source tree (what the engine needs from it: [Game data](port/game-data.md)).

## Which renderer is built?

Most of the engine is the same in every configuration; the **renderer** is the one big switch (`PD_NEW_RENDERER`, default ON):

| | Playdate renderer (default) | Original renderer |
| --- | --- | --- |
| Files | `pdr_*.c` | `r_*.c`, `d_*.c` (+ optional `*_arm.S`) |
| Hidden surfaces | BSP walk front→back with a per-row coverage mask | Edge list, scan, span sorting |
| Lighting | 8-bit light blocks per face, sampled bilinearly | Surface cache (the lit texture of every visible face) |
| Page | [renderer-pdr.md](engine/renderer-pdr.md) | [renderer-original.md](engine/renderer-original.md) |

Both render at half resolution into a buffer the Playdate display layer dithers directly (`PD_LOWRES_3D`), and both use the same cvars and Options menu.

## Conventions used in these pages

- **Links** to source files are relative paths; `file.c:` section anchors link into other pages.
- **Code blocks** are either real excerpts (usually abridged with `...`) or, when marked as such, short illustrations written against the real API.
- **Cvars** are shown as `name` (default; *archived* means saved in `config.cfg`).
- Function and type names are exactly as in the source; hundreds of tiny functions (for example every `PF_*` builtin) are summarised in groups rather than listed one by one.
- The pages describe the tree as of this writing, **including the staged, not-yet-committed changes** (the blue-noise and diffusion dithering modes, the *Show FPS* default, CD music and the *Music* option, the built-in `default.cfg`).
- The [Source code index](source-index.md) lists every file of the repository; when a file is added, removed or renamed, add, remove or rename its row there as well as its section on the page that covers it.
