#!/usr/bin/env python3
"""Generate docs/source-index.md for the quake-embedded repo.

Every file of the repo (git ls-files + untracked, not ignored) must appear in exactly one group below;
the script fails otherwise. {key} in a description becomes a link to that file's detailed section
(key = basename when unique, else the repo-relative path).  Line counts are computed.
"""
import os, re, subprocess, sys
from collections import OrderedDict

ROOT = sys.argv[1]
OUT = os.path.join(ROOT, 'docs', 'source-index.md')

# ---- short labels for the "Docs" column -------------------------------------------------------
PAGES = {
    'build-system.md': 'build', 'tools.md': 'tools', 'README.md': 'docs index',
    'port/overview.md': 'overview', 'port/playdate.md': 'playdate', 'port/game-data.md': 'game-data',
    'engine/core.md': 'core', 'engine/platform-interfaces.md': 'platform-interfaces',
    'engine/client.md': 'client', 'engine/ui.md': 'ui', 'engine/server.md': 'server',
    'engine/quakec.md': 'quakec', 'engine/network.md': 'network', 'engine/models.md': 'models',
    'engine/renderer-pdr.md': 'renderer-pdr', 'engine/renderer-original.md': 'renderer-original',
    'engine/perf-infrastructure.md': 'perf-infrastructure',
}

# ---- groups: (title, intro, base dir shown in the File column, entries) -----------------------
# entry: (path, built, description, doc)   doc = 'page.md#anchor' relative to docs/, '../X' for outside docs, or None
G = []


def group(title, intro, base, entries):
    G.append((title, intro, base, entries))


group('Top level', 'Build entry point, licence and editor configuration.', '', [
    ('CMakeLists.txt', 'build', 'Top-level build: selects the board, sets the global flags, adds {winquake/CMakeLists.txt} and {port/CMakeLists.txt}.', 'build-system.md#top-level-cmakeliststxt'),
    ('README.md', 'doc', 'The project overview for players and developers: controls, building, the renderer, settings, profiling and the host check.', '../README.md'),
    ('gpl-2.0.txt', 'licence', 'The GNU GPL v2 text; Quake\'s source is released under it.', None),
    ('.gitignore', 'config', 'Keeps `build-*/`, `bench-results/` and `tools/hostcheck/out/` out of git.', 'README.md#not-documented'),
    ('.vscode/tasks.json', 'editor', 'VS Code tasks: simulator (debug) build, device build, release build, install over USB.', 'build-system.md#editor-integration'),
    ('.vscode/launch.json', 'editor', 'Launches the Playdate Simulator under CodeLLDB against `build-sim-debug/quake.pdx`.', 'build-system.md#editor-integration'),
])

group('Board interface (`include/`)', 'The contract between the shared platform layer and a board.', 'include', [
    ('include/quakembd.h', 'header', 'The board interface: the `qembd_*` hooks a board implements, the entry points the port provides to it, and the logging macros.', 'port/overview.md#includequakembdh'),
])

group('Shared platform layer (`port/`)', 'Board-independent implementations of the engine\'s `Sys_*`, `VID_*`, `IN_*` and `CDAudio_*` interfaces, written against the `qembd_*` hooks. Overview: [Shared platform layer](port/overview.md).', 'port', [
    ('port/CMakeLists.txt', 'build', 'Builds `in_port.c`, `sys_port.c` and `vid_port.c` as the `port` object library and adds the board directory.', 'build-system.md#portcmakeliststxt-shared-platform-layer'),
    ('port/sys_port.c', 'always', '`Sys_*` on the `qembd_*` hooks (time, errors, quit, key polling), plus start-up (`qembd_init`) and the per-frame `qembd_frame` that wraps `Host_Frame`.', 'port/overview.md#portsys_portc'),
    ('port/vid_port.c', 'always', '`VID_*`: allocates the 8-bit view buffer, z-buffer and surface cache, converts the palette, and hands the dirty rectangles to the board\'s `qembd_fillrect`.', 'port/overview.md#portvid_portc'),
    ('port/in_port.c', 'always', '`IN_*`: applies relative mouse movement to the view angles (keys arrive through `Sys_SendKeyEvents`, not here).', 'port/overview.md#portin_portc'),
    ('port/cd_null.c', 'host check', '`CDAudio_*` as empty functions. Not linked on the Playdate (it has {cd_pd.c}); the host check uses it.', 'port/overview.md#portcd_nullc'),
    ('port/fio/fio_posix.c', 'host check', '`Sys_File*` on POSIX `open/read/write`. The Playdate uses {fio.c} instead; the host check uses this one.', 'port/overview.md#portfiofio_posixc'),
])

group('Playdate board: sources (`port/boards/playdate/`)', 'Everything that touches the Playdate OS and its C API. Overview: [Playdate board](port/playdate.md).', 'port/boards/playdate', [
    ('port/boards/playdate/main.c', 'always', 'The Playdate entry point: `eventHandler`, the update callback and its state machine, input mapping (D-pad, buttons, crank), the system menu, time and heap hooks.', 'port/playdate.md#mainc'),
    ('port/boards/playdate/display.c', 'always', 'Turns the 8-bit paletted frame into the 1-bit LCD frame: luminance, the four dithering modes (`pd_dither`), the half-resolution hand-over and redraw bookkeeping for interlacing.', 'port/playdate.md#displayc'),
    ('port/boards/playdate/bluenoise.h', 'header', 'The generated 32×32 blue-noise threshold tile used by the blue-noise dither mode; made by {gen-bluenoise.py}.', 'port/playdate.md#bluenoiseh'),
    ('port/boards/playdate/fio.c', 'always', '`Sys_File*` on the Playdate file API: reads try the Data folder, then the `.pdx` bundle; writes go to the Data folder.', 'port/playdate.md#fioc'),
    ('port/boards/playdate/pd_stdio.c', 'always', 'The `stdio` subset Quake calls (`fopen`, `fread`, `fprintf`, `fscanf` …) on `playdate->file`, and console logging via `pdq_log_line`.', 'port/playdate.md#pd_stdioc'),
    ('port/boards/playdate/pd_compat.h', 'header', 'Force-included into every C file of the build; redirects `stdio` calls to {pd_stdio.c}.', 'port/playdate.md#pd_compath'),
    ('port/boards/playdate/pd_port.h', 'header', 'Internal header shared by the board\'s files: `qembd_pd`, path helpers, and the suspend and display-invalidate hooks.', 'port/playdate.md#pd_porth'),
    ('port/boards/playdate/keyqueue.c', 'always', 'A 32-entry ring buffer between input polling in {main.c} and `Sys_SendKeyEvents`; also the empty `qembd_set_relative_mode`.', 'port/playdate.md#keyqueuec--keyqueueh'),
    ('port/boards/playdate/keyqueue.h', 'header', '`pdq_push_key`: queue a key press or release.', 'port/playdate.md#keyqueuec--keyqueueh'),
    ('port/boards/playdate/autofire.c', 'always', 'Holds fire while a live monster is in the line of fire, with the crank out and `cl_autofire` on.', 'port/playdate.md#autofirec--autofireh'),
    ('port/boards/playdate/autofire.h', 'header', '`pdq_autofire_update`, called once per frame by {main.c}.', 'port/playdate.md#autofirec--autofireh'),
    ('port/boards/playdate/weapons.c', 'always', 'Lists the weapons you own and have ammo for (the system menu\'s Weapon item) and selects one with an `impulse`.', 'port/playdate.md#weaponsc--weaponsh'),
    ('port/boards/playdate/weapons.h', 'header', '`pdq_weapons_scan`, `pdq_weapon_names`, `pdq_weapon_select`.', 'port/playdate.md#weaponsc--weaponsh'),
    ('port/boards/playdate/snd.c', 'always', 'The sound backend: each effect is decoded once to PCM and played on one of 8 Playdate `SamplePlayer`s; Quake\'s software mixer is not used. Implements {sound.h}.', 'port/playdate.md#sndc'),
    ('port/boards/playdate/cd_pd.c', 'always', 'CD audio: track *N* is `id1/music/QuakeNN`, streamed by a `FilePlayer`; Music on/off (`bgmenabled`), volume, game pause, the system menu, and the `cd` command. Implements {cdaudio.h}.', 'port/playdate.md#cd_pdc'),
    ('port/boards/playdate/pdprof.c', 'profile', 'The on-device profiler: section timings, counters, stack probes and the timedemo benchmark driver, written to `prof.csv`. Macros in {pdprof.h}.', 'port/playdate.md#pdprofc'),
])

group('Playdate board: build files and data', 'How the `.pdx` is configured, built and packaged. Details: [Build system](build-system.md).', 'port/boards/playdate', [
    ('port/boards/playdate/CMakeLists.txt', 'build', 'Defines the game target (device executable or Simulator library), its source list and the post-build steps.', 'build-system.md#playdate-board-cmakeliststxt'),
    ('port/boards/playdate/platform.cmake', 'build', 'Every `PD_*` option, the compile definitions they produce, the force-included {pd_compat.h}, and SDK and toolchain selection.', 'build-system.md#playdate-platformcmake'),
    ('port/boards/playdate/toolchain.cmake', 'build', 'The Arm GCC cross-compilation toolchain for the device build.', 'build-system.md#toolchaincmake'),
    ('port/boards/playdate/pdx_buildnumber.cmake', 'build', 'Post-build step: gives every build the next build number in the built `pdxinfo`.', 'build-system.md#playdate-board-cmakeliststxt'),
    ('port/boards/playdate/pdx_pak.cmake', 'build', 'Post-build step: decides which game data the `.pdx` carries. A release build with a `pak0_demo.pak` ships it as `id1/pak0.pak` and leaves out the music; every other build drops `pak0_demo.pak`.', 'build-system.md#which-pak0pak-the-pdx-gets'),
    ('port/boards/playdate/pdx_rename.cmake', 'build', 'Profiling builds only: appends tags such as "profile, demo1" to the launcher title.', 'build-system.md#playdate-board-cmakeliststxt'),
    ('port/boards/playdate/Source/pdxinfo', 'data', 'Game metadata: `name`, `author`, `bundleID`, `version`, `buildNumber`.', 'build-system.md#source'),
    ('port/boards/playdate/Source/id1/.gitkeep', 'data', 'Keeps the otherwise git-ignored `Source/id1/` (where your `pak0.pak` and music go) in the tree.', 'build-system.md#source'),
    ('port/boards/playdate/.gitignore', 'config', 'Ignores `Source/pdex.*`, `Source/id1/` and `.build_number`.', 'build-system.md#source'),
    ('port/boards/playdate/DOS-CONFIG.CFG.bak', 'unused', 'A stray backup of a DOS-era Quake config; not used by the build.', 'build-system.md#source'),
])

group('Engine core', 'Start-up and the frame loop, memory, strings and files, console variables and commands, math, WADs. Page: [Engine core](engine/core.md).', 'winquake', [
    ('winquake/quakedef.h', 'header', 'The master header every `.c` includes first: limits, `STAT_*` and `IT_*` bits, include order, host globals.', 'engine/core.md#quakedefh'),
    ('winquake/host.c', 'always', '`Host_Init`, `Host_Frame`, errors and shutdown, client management and the `config.cfg` handling (`Host_SaveOptions`).', 'engine/core.md#hostc'),
    ('winquake/host_cmd.c', 'always', 'The console commands: `map`, `changelevel`, `save`, `load`, `kick`, `god`, `give`, `status`, …', 'engine/core.md#host_cmdc'),
    ('winquake/sys.h', 'header', 'The OS interface the port implements (`Sys_*`); see {sys_port.c}.', 'engine/core.md#sysh'),
    ('winquake/common.h', 'header', 'Buffers (`sizebuf_t`), message I/O, byte order, string and file-system declarations.', 'engine/core.md#commonh--commonc'),
    ('winquake/common.c', 'always', 'Message read/write, byte order, strings, argument parsing, the file system and `.pak` loading.', 'engine/core.md#commonh--commonc'),
    ('winquake/zone.h', 'header', 'The hunk, zone and cache allocator interface.', 'engine/core.md#zoneh--zonec'),
    ('winquake/zone.c', 'always', 'The three memory allocators: the hunk (levels, models), the zone (small dynamic blocks) and the cache (discardable data).', 'engine/core.md#zoneh--zonec'),
    ('winquake/cvar.h', 'header', '`cvar_t` and the console-variable API.', 'engine/core.md#cvarh--cvarc'),
    ('winquake/cvar.c', 'always', 'Console variables: registration, lookup, setting, archiving to `config.cfg`.', 'engine/core.md#cvarh--cvarc'),
    ('winquake/cmd.h', 'header', 'The command buffer, tokenizer and command-table API.', 'engine/core.md#cmdh--cmdc'),
    ('winquake/cmd.c', 'always', 'The command buffer, tokenizer, aliases and command table; falls back to {defaultcfg.h} when the pak has no `default.cfg`.', 'engine/core.md#cmdh--cmdc'),
    ('winquake/defaultcfg.h', 'header', 'The built-in `default.cfg` (D-pad and button bindings) for paks that lack one, such as the 2021 re-release\'s.', 'engine/core.md#built-in-defaultcfg-defaultcfgh'),
    ('winquake/crc.h', 'header', '`CRC_Block` and friends.', 'engine/core.md#crch--crcc'),
    ('winquake/crc.c', 'always', 'The 16-bit CRC, used to check `progs.dat` against the compiled-in variable layout.', 'engine/core.md#crch--crcc'),
    ('winquake/mathlib.h', 'header', 'Vector macros and the math function declarations.', 'engine/core.md#mathlibh--mathlibc'),
    ('winquake/mathlib.c', 'always', 'Vectors, matrices, angle conversion, plane tests and box-on-plane-side.', 'engine/core.md#mathlibh--mathlibc'),
    ('winquake/wad.h', 'header', 'The WAD2 archive structures and API.', 'engine/core.md#wadh--wadc'),
    ('winquake/wad.c', 'always', 'Reads WAD2 archives (`gfx.wad`: the 2D graphics).', 'engine/core.md#wadh--wadc'),
    ('winquake/nonintel.c', 'always', 'Empty stand-ins for the x86 surface-patching routines, so the engine links without assembly.', 'engine/core.md#nonintelc'),
])

group('Platform interface headers', 'What the engine expects the platform to provide. Page: [Platform interface headers](engine/platform-interfaces.md).', 'winquake', [
    ('winquake/vid.h', 'header', 'The video contract: the global `viddef_t vid` and `VID_*`, implemented by {vid_port.c}.', 'engine/platform-interfaces.md#vidh'),
    ('winquake/input.h', 'header', 'The input contract (`IN_*`), implemented by {in_port.c}.', 'engine/platform-interfaces.md#inputh'),
    ('winquake/sound.h', 'header', 'The sound API (`S_*`), channel and sample structures; implemented by {snd.c}.', 'engine/platform-interfaces.md#soundh'),
    ('winquake/cdaudio.h', 'header', 'The CD audio contract (`CDAudio_*`), implemented by {cd_pd.c} (and {cd_null.c} in the host check).', 'engine/platform-interfaces.md#cdaudioh'),
])

group('Client', 'Turns player intent into movement commands and server messages into a picture of the world. Page: [Client](engine/client.md).', 'winquake', [
    ('winquake/client.h', 'header', 'Client state (`client_static_t cls`, `client_state_t cl`) and the entity, dynamic-light and light-style structures.', 'engine/client.md#clienth'),
    ('winquake/protocol.h', 'header', 'The network protocol constants: `svc_*`, `clc_*`, `U_*`, `SU_*`, `TE_*`.', 'engine/client.md#protocolh'),
    ('winquake/cl_main.c', 'always', 'Connecting and signon, entity interpolation (`CL_RelinkEntities`), dynamic lights, the client frame.', 'engine/client.md#cl_mainc'),
    ('winquake/cl_parse.c', 'always', 'Decodes every server message (`CL_ParseServerMessage`), precaches models and sounds during signon.', 'engine/client.md#cl_parsec'),
    ('winquake/cl_input.c', 'always', 'Button state (`+forward`, `+attack`, …) and `CL_SendCmd`, which builds the `usercmd_t` sent to the server.', 'engine/client.md#cl_inputc'),
    ('winquake/cl_demo.c', 'always', 'Demo recording and playback, and `timedemo`.', 'engine/client.md#cl_democ'),
    ('winquake/cl_tent.c', 'always', 'Temporary entities: explosions, beams, lightning, sparks.', 'engine/client.md#cl_tentc'),
    ('winquake/view.h', 'header', 'The view interface (`V_RenderView`, `V_UpdatePalette`, …).', 'engine/client.md#viewh--viewc'),
    ('winquake/view.c', 'always', 'The camera: bob, roll, kick, damage and pickup colour shifts, gamma and palette; calls the renderer ({pdr_main.c} or {r_main.c}).', 'engine/client.md#viewh--viewc'),
    ('winquake/chase.c', 'always', 'The third-person chase camera.', 'engine/client.md#chasec'),
    ('winquake/keys.h', 'header', 'Key codes (`K_*`) and the binding API.', 'engine/client.md#keysh--keysc'),
    ('winquake/keys.c', 'always', 'Key bindings and dispatch of key events to the game, console or menu ({menu.c}).', 'engine/client.md#keysh--keysc'),
])

group('Screen, HUD, console and menus', 'Everything drawn on top of or around the 3D view. Page: [Screen, HUD, console and menus](engine/ui.md).', 'winquake', [
    ('winquake/screen.h', 'header', 'Screen and view-rectangle declarations.', 'engine/ui.md#screenh--screenc'),
    ('winquake/screen.c', 'always', '`SCR_UpdateScreen` (the frame sequence), the view rectangle, centre text, the FPS counter and the loading plaque.', 'engine/ui.md#screenh--screenc'),
    ('winquake/draw.h', 'header', 'The 2D drawing interface (`Draw_*`).', 'engine/ui.md#drawh--drawc'),
    ('winquake/draw.c', 'always', '2D primitives (characters, pictures, fills) and the bookkeeping that keeps the low-resolution view in step (`DRAW_TOUCH`).', 'engine/ui.md#drawh--drawc'),
    ('winquake/sbar.h', 'header', 'Status-bar interface.', 'engine/ui.md#sbarh--sbarc'),
    ('winquake/sbar.c', 'always', 'The status bar, inventory, scoreboard and intermission screens.', 'engine/ui.md#sbarh--sbarc'),
    ('winquake/console.h', 'header', 'Console interface (`Con_*`).', 'engine/ui.md#consoleh--consolec'),
    ('winquake/console.c', 'always', 'The drop-down console and notify lines.', 'engine/ui.md#consoleh--consolec'),
    ('winquake/menu.h', 'header', 'Menu entry points (`M_Init`, `M_Draw`, `M_Keydown`, `M_Menu_Options_Shortcut`).', 'engine/ui.md#menuh--menuc'),
    ('winquake/menu.c', 'always', 'All of Quake\'s menus, including the Playdate Options screen (Music, Crank speed, Dithering, Draw distance, Max framerate, Show FPS, …).', 'engine/ui.md#menuh--menuc'),
])

group('Server, world and physics', 'Runs the level through the QuakeC VM, moves entities and tells clients what changed. Page: [Server, world and physics](engine/server.md).', 'winquake', [
    ('winquake/server.h', 'header', '`server_t`, `client_t` and the entity constants (`MOVETYPE_*`, `SOLID_*`, `FL_*`, `EF_*`).', 'engine/server.md#serverh'),
    ('winquake/sv_main.c', 'always', 'Server start-up (`SV_SpawnServer`), client connections, and building the update messages.', 'engine/server.md#sv_mainc'),
    ('winquake/sv_user.c', 'always', 'Reading client input and the player\'s movement and acceleration.', 'engine/server.md#sv_userc'),
    ('winquake/sv_phys.c', 'always', 'Entity physics by movement type: walk, fly, toss, push, step, bounce.', 'engine/server.md#sv_physc'),
    ('winquake/sv_move.c', 'always', 'Monster movement helpers (`SV_movestep`, `walkmove`, `movetogoal`).', 'engine/server.md#sv_movec'),
    ('winquake/world.h', 'header', 'Collision interface: `trace_t`, hulls, `SV_Move`.', 'engine/server.md#worldh--worldc'),
    ('winquake/world.c', 'always', 'Collision: the clip hulls, the area tree, `SV_LinkEdict` and `SV_Move`.', 'engine/server.md#worldh--worldc'),
])

group('QuakeC virtual machine', 'The bytecode interpreter that runs the game rules in `progs.dat`. Page: [QuakeC virtual machine](engine/quakec.md).', 'winquake', [
    ('winquake/pr_comp.h', 'header', 'The bytecode format: opcodes, statements, definitions and functions (shared with the QuakeC compiler).', 'engine/quakec.md#pr_comph'),
    ('winquake/progdefs.h', 'header', 'Includes `progdefs.q1` (or `progdefs.q2` with `QUAKE2`).', 'engine/quakec.md#progdefsh-progdefsq1-progdefsq2'),
    ('winquake/progdefs.q1', 'header', 'The C layout of Quake 1\'s QuakeC globals and entity fields; its CRC (5927) is checked when `progs.dat` loads.', 'engine/quakec.md#progdefsh-progdefsq1-progdefsq2'),
    ('winquake/progdefs.q2', 'unused', 'The same for the Quake 2 variable set; only used when `QUAKE2` is defined.', 'engine/quakec.md#progdefsh-progdefsq1-progdefsq2'),
    ('winquake/progs.h', 'header', 'Edicts, the `eval_t` union, accessor macros and the VM globals.', 'engine/quakec.md#progsh'),
    ('winquake/pr_edict.c', 'always', 'Loading `progs.dat`, edict allocation, parsing the map\'s entity text and saving entities.', 'engine/quakec.md#pr_edictc'),
    ('winquake/pr_exec.c', 'always', 'The bytecode interpreter (`PR_ExecuteProgram`).', 'engine/quakec.md#pr_execc'),
    ('winquake/pr_cmds.c', 'always', 'The ~90 builtin functions (`PF_*`) QuakeC calls: sounds, spawning, tracing, messages.', 'engine/quakec.md#pr_cmdsc'),
])

group('Networking', 'Quake always talks to its server through sockets, even in single player (loopback). Page: [Networking](engine/network.md).', 'winquake', [
    ('winquake/net.h', 'header', 'The socket and driver structures and the connection-protocol constants.', 'engine/network.md#neth'),
    ('winquake/net_main.c', 'always', 'The driver-independent `NET_*` layer: connect, accept, send and receive, the driver tables.', 'engine/network.md#net_mainc'),
    ('winquake/net_loop.h', 'header', 'The loopback driver\'s entry points.', 'engine/network.md#net_loopc'),
    ('winquake/net_loop.c', 'always', 'The in-process loopback driver that connects the local client to the local server (single player).', 'engine/network.md#net_loopc'),
    ('winquake/net_none.c', 'always', 'The driver table for builds without network hardware: loopback only. This is what the Playdate builds.', 'engine/network.md#net_nonec'),
    ('winquake/net_bsd.c', 'not built', 'The driver table for UDP builds: loopback plus the datagram driver.', 'engine/network.md#net_bsdc'),
    ('winquake/net_dgrm.h', 'not built', 'The datagram driver\'s entry points.', 'engine/network.md#net_dgrmc'),
    ('winquake/net_dgrm.c', 'not built', 'The datagram driver: a reliable connection over an unreliable transport, plus the LAN server list.', 'engine/network.md#net_dgrmc'),
    ('winquake/net_udp.h', 'not built', 'The UDP driver\'s entry points.', 'engine/network.md#net_udpc'),
    ('winquake/net_udp.c', 'not built', 'The UDP LAN driver on BSD sockets.', 'engine/network.md#net_udpc'),
    ('winquake/net_vcr.h', 'not built', 'The VCR (record and replay) driver\'s entry points.', 'engine/network.md#net_vcrc-not-built'),
    ('winquake/net_vcr.c', 'not built', 'A driver that records network traffic to a file and plays it back for reproducible multiplayer tests.', 'engine/network.md#net_vcrc-not-built'),
])

group('Models and map formats', 'The BSP, MDL and SPR file formats and their loader. Page: [Models and map formats](engine/models.md).', 'winquake', [
    ('winquake/bspfile.h', 'header', 'On-disk BSP structures (the 15 lumps) and the map limits.', 'engine/models.md#bspfileh'),
    ('winquake/modelgen.h', 'header', 'On-disk alias (MDL) structures.', 'engine/models.md#modelgenh'),
    ('winquake/spritegn.h', 'header', 'On-disk sprite (SPR) structures.', 'engine/models.md#spritegnh'),
    ('winquake/model.h', 'header', 'The in-memory structures (`model_t`, `msurface_t`, `mleaf_t`, `mnode_t`, …).', 'engine/models.md#modelh'),
    ('winquake/model.c', 'always', 'Loads and caches brush, alias and sprite models; point-in-leaf and PVS decompression.', 'engine/models.md#modelc'),
    ('winquake/anorms.h', 'header', 'The 162 precomputed vertex normals of alias models.', 'engine/models.md#anormsh'),
])

group('Playdate renderer (`pdr_*`)', 'The default 3D renderer (`PD_NEW_RENDERER=ON`), written for the Playdate\'s memory system. Page: [The Playdate renderer](engine/renderer-pdr.md).', 'winquake', [
    ('winquake/pdr.h', 'header · new renderer', 'The renderer\'s internal interface: constants, compact brush-data structs, shared globals, the span context.', 'engine/renderer-pdr.md#pdrh'),
    ('winquake/pdr_main.c', 'new renderer', '`R_RenderView` and the per-frame setup; owns the globals the original refresh had; interlacing.', 'engine/renderer-pdr.md#pdr_mainc'),
    ('winquake/pdr_world.c', 'new renderer', 'Builds the compact brush data at map load, walks the BSP front to back, keeps the per-row coverage mask, draws brush entities.', 'engine/renderer-pdr.md#pdr_worldc'),
    ('winquake/pdr_span.c', 'new renderer', 'Draws one span of one face: textured, water and sky spans straight from the mip texture.', 'engine/renderer-pdr.md#pdr_spanc'),
    ('winquake/pdr_light.c', 'new renderer', 'Light blocks in place of the surface cache; light styles, dynamic lights and `R_LightPoint`.', 'engine/renderer-pdr.md#pdr_lightc'),
    ('winquake/pdr_alias.c', 'new renderer', 'Alias models (monsters, items, the weapon): transform, lighting and its own triangle rasterizer.', 'engine/renderer-pdr.md#pdr_aliasc'),
    ('winquake/pdr_sprite.c', 'new renderer', 'Sprites (explosions, bubbles) and particles.', 'engine/renderer-pdr.md#pdr_spritec'),
    ('winquake/pdr_lowres.c', 'new renderer', 'Hands the half-resolution 3D view to the display layer ({display.c}).', 'engine/renderer-pdr.md#pdr_lowresc'),
])

group('Original software renderer: interfaces and set-up', 'Id Software\'s WinQuake renderer, built with `PD_NEW_RENDERER=OFF`. `r_*` decides what to draw, `d_*` draws it. Page: [Original software renderer](engine/renderer-original.md).', 'winquake', [
    ('winquake/render.h', 'header', 'The public refresh interface the engine uses (`R_*`, `entity_t`, `refdef_t`); implemented by {pdr_main.c} or {r_main.c}.', 'engine/renderer-original.md#renderh'),
    ('winquake/r_shared.h', 'header · orig. renderer', 'Types shared between the refresh and the driver (`espan_t`, `surf_t`, `edge_t`).', 'engine/renderer-original.md#r_sharedh'),
    ('winquake/r_local.h', 'header · orig. renderer', 'Refresh-private definitions: lighting, clipped edges, clip planes, debug cvars.', 'engine/renderer-original.md#r_localh'),
    ('winquake/d_iface.h', 'header', 'The refresh-to-driver contract (`WARP_WIDTH`, `particle_t`, the `D_*` entry points); included by {quakedef.h}, so also in the new-renderer build.', 'engine/renderer-original.md#d_ifaceh'),
    ('winquake/d_local.h', 'header · orig. renderer', 'Driver-private definitions: the surface cache node, span parameters, texture gradients.', 'engine/renderer-original.md#d_localh'),
    ('winquake/r_main.c', 'orig. renderer', 'Frame control: `R_Init`, `R_RenderView` and the entity drawing order.', 'engine/renderer-original.md#r_mainc'),
    ('winquake/r_misc.c', 'orig. renderer', 'Per-frame set-up (`R_SetupFrame`, frustum, `R_ViewChanged`) and debug helpers.', 'engine/renderer-original.md#r_miscc'),
    ('winquake/r_vars.c', 'orig. renderer', 'Definitions of the refresh globals, collected in a block to avoid cache conflicts.', 'engine/renderer-original.md#r_varsc--d_varsc'),
    ('winquake/d_vars.c', 'orig. renderer', 'Definitions of the driver globals (gradients, texture adjustments, buffers).', 'engine/renderer-original.md#r_varsc--d_varsc'),
    ('winquake/d_init.c', 'orig. renderer', 'Driver initialisation (`D_Init`) and the per-frame mip scale (`D_SetupFrame`); the `d_mipcap` and `d_subdiv16` cvars.', 'engine/renderer-original.md#d_initc'),
    ('winquake/d_modech.c', 'orig. renderer', '`D_ViewChanged`: recomputes what the driver derives from the view (row tables, aspect, particle clip limits).', 'engine/renderer-original.md#d_modechc'),
])

group('Original software renderer: refresh (`r_*`)', 'World traversal, edge list and scan, surface and model preparation.', 'winquake', [
    ('winquake/r_bsp.c', 'orig. renderer', 'World traversal and brush entities: walks the BSP and marks visible faces.', 'engine/renderer-original.md#r_bspc'),
    ('winquake/r_draw.c', 'orig. renderer', 'Turns a face into clipped, projected edges (`R_RenderFace`, `R_ClipEdge`, `R_EmitEdge`).', 'engine/renderer-original.md#r_drawc'),
    ('winquake/r_edge.c', 'orig. renderer', 'The edge scan: sorts the edge list per scan line and emits the spans of the nearest surface; has an ARM twin ({r_edge_arm.S}).', 'engine/renderer-original.md#r_edgec'),
    ('winquake/r_surf.c', 'orig. renderer', 'Builds the surface cache: the lit texture of a face at a mip level.', 'engine/renderer-original.md#r_surfc'),
    ('winquake/r_light.c', 'orig. renderer', 'Lighting: dynamic lights, `R_LightPoint`, light-style animation.', 'engine/renderer-original.md#r_lightc'),
    ('winquake/r_sky.c', 'orig. renderer', 'Splits the sky texture into its layers and generates the scrolling sky tile.', 'engine/renderer-original.md#r_skyc'),
    ('winquake/r_alias.c', 'orig. renderer', 'Alias models: transform, light and set-up before {d_polyse.c} draws them.', 'engine/renderer-original.md#r_aliasc'),
    ('winquake/r_aclip.c', 'orig. renderer', 'Clips alias triangles that cross the screen edges or the near plane.', 'engine/renderer-original.md#r_aclipc'),
    ('winquake/r_sprite.c', 'orig. renderer', 'Sprites: frame selection, rotation, clipping, hand-off to {d_sprite.c}.', 'engine/renderer-original.md#r_spritec'),
    ('winquake/r_part.c', 'always', 'The particle simulation and effect spawners called by the client; used by both renderers.', 'engine/renderer-original.md#r_partc'),
    ('winquake/r_efrag.c', 'always', 'Entity fragments: which BSP leaves each entity touches; used by both renderers.', 'engine/renderer-original.md#r_efragc'),
])

group('Original software renderer: driver (`d_*`)', 'Drawing the spans, models, sprites and particles that the refresh prepared.', 'winquake', [
    ('winquake/d_edge.c', 'orig. renderer', '`D_DrawSurfaces`: draws each surface\'s spans (sky, water, textured).', 'engine/renderer-original.md#d_edgec'),
    ('winquake/d_surf.c', 'orig. renderer', 'The surface cache allocator and `D_CacheSurface`.', 'engine/renderer-original.md#d_surfc'),
    ('winquake/d_scan.c', 'orig. renderer', 'The span rasterisers (`D_DrawSpans8`, turbulent water, z spans) and the low-resolution upscale; has an ARM twin ({d_scan_arm.S}).', 'engine/renderer-original.md#d_scanc'),
    ('winquake/d_sky.c', 'orig. renderer', 'Draws sky spans from the two scrolling layers.', 'engine/renderer-original.md#d_skyc'),
    ('winquake/d_polyse.c', 'orig. renderer', 'The alias-model triangle rasteriser (Gouraud-lit, affine-textured).', 'engine/renderer-original.md#d_polysec'),
    ('winquake/d_sprite.c', 'orig. renderer', 'Draws sprites as z-tested polygons.', 'engine/renderer-original.md#d_spritec'),
    ('winquake/d_part.c', 'orig. renderer', 'Draws particles as z-tested dots.', 'engine/renderer-original.md#d_partc'),
    ('winquake/d_zpoint.c', 'orig. renderer', '`D_DrawZPoint`: a point with a z test (not called anywhere in this tree).', 'engine/renderer-original.md#d_zpointc'),
    ('winquake/d_fill.c', 'orig. renderer', '`D_FillRect`: clears a rectangle (not called anywhere in this tree).', 'engine/renderer-original.md#d_fillc'),
])

group('Performance infrastructure', 'Profiling hooks, stack budgeting and the hand-written assembly. Page: [Performance infrastructure](engine/perf-infrastructure.md).', 'winquake', [
    ('winquake/pdprof.h', 'header', 'Section timers, counters and stack probes; everything compiles to nothing without `PD_PROFILE`. Implemented by {pdprof.c}.', 'engine/perf-infrastructure.md#pdprofh'),
    ('winquake/pd_stack.h', 'header', '"Is there room on the fast stack for this buffer?": the `PD_STACK` budget check.', 'engine/perf-infrastructure.md#pd_stackh'),
    ('winquake/pd_asm.h', 'header', 'The switches for the assembly and its A/B and check modes (`PD_USE_ASM`, `PD_ASM_AB`, `PD_ASM_CHECK`).', 'engine/perf-infrastructure.md#pd_asmh'),
    ('winquake/asm_draw.h', 'unused', 'Struct byte offsets from the original x86 assembly; kept as a layout reference, nothing includes it.', 'engine/perf-infrastructure.md#asm_drawh'),
    ('winquake/d_scan_arm.S', 'asm', 'Thumb-2 textured span drawer for the Cortex-M7 (C twin: {d_scan.c}).', 'engine/perf-infrastructure.md#d_scan_arms'),
    ('winquake/r_edge_arm.S', 'asm', 'Thumb-2 edge-scan routines for span generation and active-edge upkeep (C twin: {r_edge.c}).', 'engine/perf-infrastructure.md#r_edge_arms'),
])

group('Legacy x86 headers', 'From the original DOS/Windows build; nothing in this tree includes them. Documented together in [Platform interface headers](engine/platform-interfaces.md#legacy-x86-headers-not-used).', 'winquake', [
    ('winquake/quakeasm.h', 'unused', 'GNU-as `.extern` declarations of the renderer globals the x86 assembly read.', 'engine/platform-interfaces.md#legacy-x86-headers-not-used'),
    ('winquake/d_ifacea.h', 'unused', 'Offsets and constants of the `d_iface.h` structs for the x86 driver assembly.', 'engine/platform-interfaces.md#legacy-x86-headers-not-used'),
    ('winquake/block8.h', 'unused', 'x86 assembly macros for the 8-bit surface-cache block draw.', 'engine/platform-interfaces.md#legacy-x86-headers-not-used'),
    ('winquake/block16.h', 'unused', 'x86 assembly macros for the 16-bit surface-cache block draw.', 'engine/platform-interfaces.md#legacy-x86-headers-not-used'),
    ('winquake/vgamodes.h', 'unused', 'VGA mode tables for the DOS port.', 'engine/platform-interfaces.md#legacy-x86-headers-not-used'),
    ('winquake/resource.h', 'unused', 'Windows resource IDs for `winquake.rc`.', 'engine/platform-interfaces.md#legacy-x86-headers-not-used'),
])

group('Engine build file', 'Which engine files each configuration compiles.', 'winquake', [
    ('winquake/CMakeLists.txt', 'build', 'Builds the engine as the `winquake` object library: the core list, `pdr_*.c` or `r_*.c`/`d_*.c` by renderer, the network driver by platform, the ARM files with `PD_ASM`.', 'build-system.md#winquakecmakeliststxt-the-engine'),
])

group('Scripts (`scripts/`)', 'Run on your computer around a device build. Page: [Scripts and tools](tools.md).', 'scripts', [
    ('scripts/release-device.sh', 'tool', 'A clean Release build of the device `.pdx` with a new build number, shipping the shareware pak when one is present.', 'tools.md#scriptsrelease-devicesh'),
    ('scripts/install-device.sh', 'tool', 'Copies the device `.pdx` to a USB-connected Playdate and launches it.', 'tools.md#scriptsinstall-devicesh'),
    ('scripts/pd-bench.sh', 'tool', 'Installs a profiling build, runs it for a set time and fetches `prof.csv`.', 'tools.md#scriptspd-benchsh'),
    ('scripts/pd-report.py', 'tool', 'Summarises and compares `prof.csv` files, including the `--ab` paired comparison.', 'tools.md#scriptspd-reportpy'),
    ('scripts/gen-bluenoise.py', 'tool', 'Generates the blue-noise tile ({bluenoise.h}) with void-and-cluster.', 'tools.md#scriptsgen-bluenoisepy'),
    ('scripts/__pycache__/pd-report.cpython-314.pyc', 'generated', 'Python bytecode cache of `pd-report.py`; not source.', None),
])

group('Host check (`tools/hostcheck/`)', 'Runs the real engine and the real {display.c} on the host to prove an optimisation does not change the picture. Page: [Scripts and tools](tools.md#toolshostcheckhostcheckc).', 'tools/hostcheck', [
    ('tools/hostcheck/hostcheck.c', 'host check', 'The test program: scripted scenes, menu and input tests, golden frame hashes, map tours and screenshot dumps.', 'tools.md#toolshostcheckhostcheckc'),
    ('tools/hostcheck/build.sh', 'tool', 'Builds {hostcheck.c} against a source tree, in either renderer, with switches for the original code paths.', 'tools.md#toolshostcheckbuildsh'),
    ('tools/hostcheck/run.sh', 'tool', 'Builds and runs the host check in its different modes.', 'tools.md#toolshostcheckrunsh'),
    ('tools/hostcheck/golden-compare.py', 'tool', 'Compares two golden hash files frame by frame.', 'tools.md#toolshostcheckgolden-comparepy'),
    ('tools/hostcheck/compare-shots.py', 'tool', 'Measures how many pixels differ between two sets of screenshots and writes side-by-side PNGs.', 'tools.md#toolshostcheckcompare-shotspy'),
])

group('Documentation', 'These pages. Start at the [documentation index](README.md).', 'docs', [
    ('docs/README.md', 'doc', 'The documentation index: reading paths, architecture, the page map and conventions.', 'README.md'),
    ('docs/source-index.md', 'doc', 'This file.', None),
    ('docs/build-system.md', 'doc', 'CMake files, every `PD_*` option, release builds, which `pak0.pak` the `.pdx` gets, editor tasks.', 'build-system.md'),
    ('docs/tools.md', 'doc', 'The scripts and the host check harness.', 'tools.md'),
    ('docs/port/overview.md', 'doc', 'The shared platform layer and the `qembd_*` board interface.', 'port/overview.md'),
    ('docs/port/playdate.md', 'doc', 'The Playdate board: main loop, display and dithering, files, sound, music, profiler.', 'port/playdate.md'),
    ('docs/port/game-data.md', 'doc', 'The shareware and the 2021 re-release `pak0.pak`: what the engine needs from them.', 'port/game-data.md'),
    ('docs/engine/core.md', 'doc', 'Engine core: start-up, memory, strings, files, cvars, commands, math, WADs.', 'engine/core.md'),
    ('docs/engine/platform-interfaces.md', 'doc', 'The video, input, sound and CD audio headers and the legacy x86 headers.', 'engine/platform-interfaces.md'),
    ('docs/engine/client.md', 'doc', 'The client: connection, parsing, input, demos, the camera, keys.', 'engine/client.md'),
    ('docs/engine/ui.md', 'doc', 'Screen, HUD, console and menus (including the Options screen).', 'engine/ui.md'),
    ('docs/engine/server.md', 'doc', 'The server, world collision and physics.', 'engine/server.md'),
    ('docs/engine/quakec.md', 'doc', 'The QuakeC virtual machine and its builtins.', 'engine/quakec.md'),
    ('docs/engine/network.md', 'doc', 'The network layer and drivers.', 'engine/network.md'),
    ('docs/engine/models.md', 'doc', 'BSP, MDL and SPR formats and the model loader.', 'engine/models.md'),
    ('docs/engine/renderer-pdr.md', 'doc', 'The Playdate renderer.', 'engine/renderer-pdr.md'),
    ('docs/engine/renderer-original.md', 'doc', 'The original software renderer.', 'engine/renderer-original.md'),
    ('docs/engine/perf-infrastructure.md', 'doc', 'Profiling, stack budgeting and the assembly.', 'engine/perf-infrastructure.md'),
    ('docs/demo.gif', 'doc', 'Demo 1 as the Playdate screen shows it (captured from the host check; used by the README).', '../README.md'),
    ('note/release/RELEASE_NOTES_0.3.0.md', 'doc', 'Release notes for version 0.3.0.', '../note/release/RELEASE_NOTES_0.3.0.md'),
])

# ---- model ------------------------------------------------------------------------------------
def repo_files():
    out = subprocess.check_output(['git', 'ls-files'], cwd=ROOT, text=True).split('\n')
    out += subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard'], cwd=ROOT, text=True).split('\n')
    return sorted({f for f in out if f and os.path.exists(os.path.join(ROOT, f)) and f != '.DS_Store'})

files = repo_files()
entries = OrderedDict()
for gi, (_, _, _, es) in enumerate(G):
    for path, built, desc, doc in es:
        assert path not in entries, f'duplicate entry {path}'
        assert os.path.exists(os.path.join(ROOT, path)), f'no such file {path}'
        entries[path] = dict(built=built, desc=desc, doc=doc, group=gi)

missing = [f for f in files if f not in entries]
extra = [p for p in entries if p not in files]
if missing or extra:
    sys.exit(f'index and repo disagree:\n  not indexed: {missing}\n  not in repo: {extra}')

base_count = {}
for p in entries:
    base_count[os.path.basename(p)] = base_count.get(os.path.basename(p), 0) + 1


def key_of(p):
    b = os.path.basename(p)
    return b if base_count[b] == 1 else p


by_key = {key_of(p): p for p in entries}


def doc_link(doc):
    """doc is relative to docs/; '../x' stays as is; source-index.md is this page."""
    return doc


def lines_of(p):
    full = os.path.join(ROOT, p)
    if p.endswith(('.gif', '.pyc')) or p == 'docs/source-index.md':
        return '—'
    with open(full, 'rb') as f:
        return str(f.read().count(b'\n'))


def md_escape(s):
    return s.replace('|', '\\|')


def ref(key):
    p = by_key.get(key)
    if p is None:
        sys.exit(f'unknown reference {{{key}}}')
    e = entries[p]
    name = key_of(p)    # the path when the basename is not unique
    target = e['doc'] if e['doc'] else '../' + p
    return f'[`{name}`]({target})'


def render(desc):
    return re.sub(r'\{([^}]+)\}', lambda m: ref(m.group(1)), desc)


def docs_cell(e):
    if not e['doc']:
        return '—'
    page = e['doc'].split('#')[0]
    if page.startswith('../'):
        label = 'README' if page == '../README.md' else os.path.basename(page)
    else:
        label = PAGES.get(page, page)
    return f'[{label}]({e["doc"]})'


out = []
w = out.append
w('# Source code index')
w('')
w('[← Documentation index](README.md)')
w('')
total = len(entries)
code = sum(1 for p in entries if p.endswith(('.c', '.h', '.S', '.q1', '.q2')))
w(f'Every file in the repository ({total} files, {code} of them C, header or assembly source), with what it does, whether it is built, and a link to its detailed description. '
  'The **File** column opens the source; the **Docs** column opens the section that describes it. '
  'Names in the descriptions are links to the related files\' sections, so you can follow a call from one file to the next.')
w('')
w('- Looking for a file by name? Jump to [Find a file by name](#find-a-file-by-name).')
w('- Looking for what to read first? See [Start here](README.md#start-here) in the index.')
w('- Which files make up a given build? See [Which files each build compiles](#which-files-each-build-compiles).')
w('')
w('**Built** column:')
w('')
w('| Value | Meaning |')
w('| --- | --- |')
w('| always | Compiled into every Playdate build (device and Simulator) |')
w('| header | A header, included by the files that need it |')
w('| new renderer | Only with `PD_NEW_RENDERER=ON` (the default) |')
w('| orig. renderer | Only with `PD_NEW_RENDERER=OFF` |')
w('| asm | Only with `PD_ASM=ON` on the device, with the original renderer |')
w('| profile | Only with `PD_PROFILE=ON` |')
w('| host check | Only built by [`tools/hostcheck`](tools.md#toolshostcheckbuildsh), not by the game |')
w('| not built | In the tree, but no configuration here compiles it |')
w('| unused | Nothing includes it; kept for reference |')
w('| build, tool, data, config, editor, doc, licence, generated | Not engine code: build file, script, game data, config, editor setup, documentation |')
w('')

w('## Repository layout')
w('')
w('```')
w('quake-embedded/')
w('├── CMakeLists.txt        top-level build')
w('├── README.md             player and developer overview')
w('├── include/              quakembd.h: the board interface')
w('├── port/                 shared platform layer: sys_port.c vid_port.c in_port.c ...')
w('│   └── boards/playdate/  the Playdate board: main.c display.c fio.c snd.c cd_pd.c ... + Source/ (the .pdx contents)')
w('├── winquake/             the engine: Quake, the Playdate renderer (pdr_*), the original renderer (r_*, d_*)')
w('├── scripts/              release, install, bench, report, blue-noise generator')
w('├── tools/hostcheck/      host-side picture and behaviour checks')
w('├── docs/                 this documentation')
w('└── note/release/         release notes')
w('```')
w('')
w('| Directory | Section |')
w('| --- | --- |')
w('| [`include/`](../include/) | [Board interface](#board-interface-include) |')
w('| [`port/`](../port/) | [Shared platform layer](#shared-platform-layer-port) |')
w('| [`port/boards/playdate/`](../port/boards/playdate/) | [Sources](#playdate-board-sources-portboardsplaydate), [build files and data](#playdate-board-build-files-and-data) |')
w('| [`winquake/`](../winquake/) | [Engine core](#engine-core), [platform headers](#platform-interface-headers), [client](#client), [UI](#screen-hud-console-and-menus), [server](#server-world-and-physics), [QuakeC](#quakec-virtual-machine), [network](#networking), [models](#models-and-map-formats), [Playdate renderer](#playdate-renderer-pdr_), [original renderer](#original-software-renderer-interfaces-and-set-up), [performance](#performance-infrastructure), [legacy](#legacy-x86-headers) |')
w('| [`scripts/`](../scripts/) | [Scripts](#scripts-scripts) |')
w('| [`tools/hostcheck/`](../tools/hostcheck/) | [Host check](#host-check-toolshostcheck) |')
w('| [`docs/`](.), [`note/`](../note/) | [Documentation](#documentation) |')
w('')

w('## Which files each build compiles')
w('')
w('| Build | Files |')
w('| --- | --- |')
w(f'| **Playdate, default** (device or Simulator) | the [engine core](#engine-core), [client](#client), [UI](#screen-hud-console-and-menus), [server](#server-world-and-physics), [QuakeC](#quakec-virtual-machine) and [model](#models-and-map-formats) files, {ref("net_main.c")}, {ref("net_loop.c")}, {ref("net_none.c")}, the [Playdate renderer](#playdate-renderer-pdr_) (`pdr_*.c`), {ref("r_part.c")}, {ref("r_efrag.c")}; the [shared platform layer](#shared-platform-layer-port) (`sys_port.c`, `vid_port.c`, `in_port.c`); the [board sources](#playdate-board-sources-portboardsplaydate) except {ref("pdprof.c")} |')
w(f'| `-DPD_NEW_RENDERER=OFF` | the `pdr_*.c` files are replaced by the [original renderer](#original-software-renderer-interfaces-and-set-up) (`r_*.c` and `d_*.c`); with `PD_ASM=ON` on the device also {ref("d_scan_arm.S")} and {ref("r_edge_arm.S")} |')
w(f'| `-DPD_PROFILE=ON` | adds {ref("pdprof.c")} (and `PD_BENCH` plays the demos as timedemos) |')
w(f'| **Host check** ([`build.sh`](tools.md#toolshostcheckbuildsh)) | the same engine files with {ref("cd_null.c")} and {ref("fio_posix.c")} instead of {ref("cd_pd.c")} and {ref("fio.c")}, the real {ref("display.c")}, and {ref("hostcheck.c")} as `main` |')
w(f'| **Not built by any configuration** | {ref("net_bsd.c")}, {ref("net_dgrm.c")}, {ref("net_udp.c")}, {ref("net_vcr.c")} (and their headers), {ref("progdefs.q2")}, the [legacy x86 headers](#legacy-x86-headers), {ref("asm_draw.h")} |')
w('')
w('Details of the options: [Build system](build-system.md#options).')
w('')

for title, intro, base, es in G:
    w(f'## {title}')
    w('')
    w(render(intro))
    w('')
    w('| File | Lines | Built | What it does | Docs |')
    w('| --- | ---: | --- | --- | --- |')
    for path, built, desc, doc in es:
        shown = path[len(base) + 1:] if base and path.startswith(base + '/') else path
        w(f'| [`{shown}`](../{path}) | {lines_of(path)} | {md_escape(built)} | {md_escape(render(desc))} | {docs_cell(entries[path])} |')
    w('')

# ---- A-Z ----------------------------------------------------------------------------------------
w('## Find a file by name')
w('')
w('Every source and build file by name; each link opens the section that describes it (files without a section link to the file). Documentation pages are listed in [Documentation](#documentation).')
w('')
az = OrderedDict()
for p, e in entries.items():
    if p.startswith(('docs/', 'note/')) or p.endswith('.pyc'):
        continue
    name = key_of(p)
    letter = os.path.basename(p).lstrip('.')[0].upper()
    az.setdefault(letter, []).append((os.path.basename(p).lstrip('.').lower(), p))
for letter in sorted(az):
    items = []
    for _, p in sorted(az[letter]):
        e = entries[p]
        target = e['doc'] if e['doc'] else '../' + p
        label = p if base_count[os.path.basename(p)] > 1 else os.path.basename(p)
        items.append(f'[`{label}`]({target})')
    w(f'**{letter}** · ' + ' · '.join(items))
    w('')

w('## What this index does not list')
w('')
w('- Generated or local directories, all git-ignored: `build-*/`, `bench-results/`, `tools/hostcheck/out/`, `port/boards/playdate/.build_number`, `port/boards/playdate/Source/pdex.*`.')
w('- `port/boards/playdate/Source/id1/` (your `pak0.pak`, the optional `pak0_demo.pak` and the music): game data, not part of the source tree. What the engine needs from the pak: [Game data](port/game-data.md).')
w('- The upstream desktop, RISC-V and STM32 boards and `lib/minifb`, removed from the tree: [Removed boards](build-system.md#removed-boards).')
w('')

with open(OUT, 'w', encoding='utf-8') as f:
    f.write('\n'.join(out))
print(f'wrote {OUT}: {total} files in {len(G)} groups')
