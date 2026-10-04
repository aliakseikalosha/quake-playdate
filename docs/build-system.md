# Build system

[← Documentation index](README.md) · [Source index](source-index.md)

The project is built with CMake. Three CMake files form the skeleton and the board
directory (`port/boards/playdate`, the only board in the tree) is pulled in by name:

| File | Role |
| --- | --- |
| [`CMakeLists.txt`](../CMakeLists.txt) | Top level. Selects the board, sets global flags, adds `port/` and `winquake/`. |
| [`winquake/CMakeLists.txt`](../winquake/CMakeLists.txt) | Builds the engine as the `winquake` object library. |
| [`port/CMakeLists.txt`](../port/CMakeLists.txt) | Builds the shared platform layer as the `port` object library and adds `boards/<BOARD_NAME>`. |
| [`port/boards/playdate/CMakeLists.txt`](../port/boards/playdate/CMakeLists.txt) | Builds the game: the device executable, or for the Playdate Simulator a shared library. |

## How the pieces fit together

```
                +-------------------------+
                |  port/boards/playdate/  |   executable / shared library
                |  (main, display, fio…)  |
                +------------+------------+
                             | links
              +--------------+--------------+
              |                             |
      +-------v-------+            +--------v--------+
      |  port (OBJECT)|            | winquake (OBJECT)|
      |  sys/vid/in/cd|            | the Quake engine |
      +---------------+            +------------------+
```

`BOARD_NAME` picks the board directory. It has to be passed on the command line (`-DBOARD_NAME=playdate`);
the Playdate is the only board left in the tree (see [Removed boards](#removed-boards)).
Everything outside `port/boards/` is board independent.

## Top-level `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.8)
project(QuakEMBD C ASM)

if(BOARD_NAME STREQUAL "playdate")
	include(${CMAKE_CURRENT_LIST_DIR}/port/boards/playdate/platform.cmake)
endif()

add_compile_options(-fno-common)
add_definitions(-DWINQUAKE_ENABLE_LOGGING -DWINQUAKE_LOGGING_EXTERNAL)

add_subdirectory(port)
add_subdirectory(winquake)
```

- `-DWINQUAKE_LOGGING_EXTERNAL` makes `Sys_Printf` route through
  [`_Sys_Printf` in `port/sys_port.c`](../port/sys_port.c) so each board decides where log text goes.
- `-fno-common` makes duplicate global definitions a link error instead of a silent merge.
- The Playdate board's `platform.cmake` is included *before* `add_subdirectory`, because it
  adds compile definitions (`PD_*`) that every target must see.

## `winquake/CMakeLists.txt`: the engine

The source list is assembled in layers:

```cmake
set(WQ_SRCS chase.c cmd.c common.c console.c ... sv_user.c)   # always

if(PD_NEW_RENDERER)
	list(APPEND WQ_SRCS pdr_main.c pdr_world.c pdr_span.c pdr_light.c
	                    pdr_alias.c pdr_sprite.c pdr_lowres.c)  # Playdate renderer
else()
	list(APPEND WQ_SRCS d_edge.c ... r_vars.c)                  # Quake's original software refresh
endif()

if(CMAKE_SYSTEM_NAME MATCHES "(Darwin|Linux)" AND NOT BOARD_NAME STREQUAL "playdate")
	list(APPEND WQ_SRCS net_dgrm.c net_udp.c net_bsd.c)         # UDP networking (a non-Playdate Darwin/Linux board)
else()
	list(APPEND WQ_SRCS net_none.c)                             # loopback only: what the Playdate gets
endif()

if(PD_ASM_BUILD)
	list(APPEND WQ_ASM_SRCS d_scan_arm.S r_edge_arm.S)          # Thumb-2 hot loops
endif()

add_library(winquake OBJECT ${WQ_SRCS} ${WQ_ASM_SRCS})
```

Things worth knowing:

- **Two renderers, one switch.** `PD_NEW_RENDERER` swaps the whole `r_*`/`d_*` set for the
  `pdr_*` set. See [Original renderer](engine/renderer-original.md) and
  [Playdate renderer](engine/renderer-pdr.md).
- **Warnings.** On GCC/Clang `-Wdouble-promotion` is always on: a stray `double` is a
  multi-microsecond software-float call on the Cortex-M7 (single-precision FPU only).
- **Networking.** Only Darwin/Linux hosts that are *not* the Playdate get the datagram
  stack (`net_dgrm.c`, `net_udp.c`, `net_bsd.c`). Everything else links `net_none.c`.
  `net_loop.c` (the local loopback driver used by single player) is always built.
- **Assembly.** `PD_ASM_BUILD` is only turned on for a device build with the *original*
  renderer (see `platform.cmake` below), so with the default settings no `.S` file is compiled.

## `port/CMakeLists.txt`: shared platform layer

```cmake
add_library(port OBJECT in_port.c sys_port.c vid_port.c)

target_include_directories(port PUBLIC
	${PROJECT_SOURCE_DIR}/include
	${PROJECT_SOURCE_DIR}/winquake)

add_subdirectory(boards/${BOARD_NAME})
```

See [Shared platform layer](port/overview.md) for what these files do.

## Playdate: `platform.cmake`

[`port/boards/playdate/platform.cmake`](../port/boards/playdate/platform.cmake) is where
almost all build options live. It locates the SDK (`PLAYDATE_SDK_PATH`, or the `SDKRoot` line
of `~/.Playdate/config`), picks defaults, and turns options into compile definitions.

### Options

| CMake option | Default | Compile definition | Meaning |
| --- | --- | --- | --- |
| `PD_NEW_RENDERER` | ON | `PD_NEW_RENDERER=1` | Use `pdr_*.c` instead of `r_*.c`/`d_*.c`. |
| `PD_LOWRES_3D` | ON | `PD_LOWRES_3D=1` | Render the 3D view at half resolution; the display layer dithers it directly. |
| `PD_RENDER_WIDTH` / `PD_RENDER_HEIGHT` | 400×240 (320×240 without `PD_LOWRES_3D`) | same | Size of Quake's frame buffer. Menus need ≥ 320×200. |
| `PD_REFRESH_RATE` | 30 | same | Frame rate the system asks for while the game loads (0 = uncapped, max 50). |
| `PD_OPT` | `-O3` | `CMAKE_C_FLAGS_RELEASE` | Optimisation level of the device build. |
| `PD_FAST_EDGES` | ON | `PD_FAST_EDGES=1` | Array-based edge scan (original renderer only). |
| `PD_FAST_ALIAS` | ON | `PD_FAST_ALIAS=1` | Register-based alias triangle rasteriser (original renderer only). |
| `PD_FAST_FACES` | ON | `PD_FAST_FACES=1` | Stack-based world face edge emission (original renderer only). |
| `PD_FAST_SURFACES` | ON | `PD_FAST_SURFACES=1` | Row-order surface cache build (original renderer only). |
| `PD_ASM` | ON | `PD_ASM=1` *(via `PD_ASM_BUILD`)* | Thumb-2 assembly for hot loops (device + original renderer only). |
| `PD_ASM_AB` / `PD_ASM_CHECK` | OFF | same | Profiling aids for the assembly; need `PD_PROFILE`. |
| `PD_STACK` | ON | `PD_STACK=1` | Put scratch buffers on the fast stack when there is room. |
| `PD_STACK_AB` | OFF | same | A/B the stack buffers against static ones (needs `PD_PROFILE`). |
| `PD_PDR_EXP` | 0 | `PD_PDR_AB=1`, `PDR_EXP=n` | Renderer experiment `n` on alternate frames (needs `PD_PROFILE`). |
| `PD_PROFILE` | OFF | `PD_PROFILE=1` | On-device profiler writing `prof.csv`. |
| `PD_PROFILE_FINE` | OFF | `PD_PROFILE_FINE=1` | More detailed sections (adds ≈ 2 ms/frame). |
| `PD_BENCH` | OFF | `PD_BENCH=1` | Play the demos back as `timedemo`s. |
| `PD_BENCH_CMDS` / `PD_BENCH_COUNT` / `PD_BENCH_FIRST` | `""` / 3 / 1 | same | Console commands to run first, how many demos, which demo first. |

Always defined for Playdate builds: `QEMBD_PLAYDATE=1`, `TARGET_EXTENSION=1`,
`DEFAULT_MEM_SIZE=(7*1024*1024)`, `DEFAULT_MIN_MEM_SIZE=(4*1024*1024)` (the Quake heap
backs off in 512 KiB steps from the default to the minimum, see `qembd_init`),
and either `TARGET_PLAYDATE=1` (device) or `TARGET_SIMULATOR=1` (host).

Every C file also gets [`pd_compat.h`](../port/boards/playdate/pd_compat.h)
force-included (`-include`), which redirects stdio to the Playdate file API.

### Device vs. simulator

The `TOOLCHAIN` variable (set by the SDK's `arm.cmake`) decides which one is built:

```shell
# Simulator (host library, becomes quake.pdx)
mkdir build-sim && cd build-sim
cmake -DBOARD_NAME=playdate .. && make

# Device (Cortex-M7, hard-float, becomes quake_DEVICE.pdx)
mkdir build-dev && cd build-dev
cmake -DCMAKE_TOOLCHAIN_FILE=../port/boards/playdate/toolchain.cmake -DBOARD_NAME=playdate .. && make
```

For the device the extra flags are
`-mthumb -mcpu=cortex-m7 -mfloat-abi=hard -mfpu=fpv5-sp-d16 -falign-functions=16
-fomit-frame-pointer -ffunction-sections -fdata-sections -mword-relocations`.

### Passing options

```shell
# Full panel resolution, higher refresh cap, original renderer with all its optimisations
cmake -DCMAKE_TOOLCHAIN_FILE=../port/boards/playdate/toolchain.cmake -DBOARD_NAME=playdate \
      -DPD_RENDER_WIDTH=400 -DPD_RENDER_HEIGHT=240 -DPD_REFRESH_RATE=50 \
      -DPD_NEW_RENDERER=OFF ..

# Profiling build that plays only demo1
cmake -DCMAKE_TOOLCHAIN_FILE=../port/boards/playdate/toolchain.cmake -DBOARD_NAME=playdate \
      -DPD_PROFILE=ON -DPD_BENCH=ON -DPD_BENCH_COUNT=1 ..
```

## Playdate: board `CMakeLists.txt`

[`port/boards/playdate/CMakeLists.txt`](../port/boards/playdate/CMakeLists.txt):

```cmake
set(PD_SOURCES main.c keyqueue.c autofire.c weapons.c display.c fio.c pd_stdio.c snd.c cd_pd.c)
if(PD_PROFILE)
	list(APPEND PD_SOURCES pdprof.c)
endif()

if(TOOLCHAIN STREQUAL "armgcc")
	add_executable(quake_DEVICE ${PD_SOURCES})
	target_link_libraries(quake_DEVICE winquake port)
else()
	add_library(quake SHARED ${PD_SOURCES})
	target_link_libraries(quake winquake port)
endif()

set(PRODUCT_DIR ${CMAKE_BINARY_DIR} CACHE STRING "Built PDX directory" FORCE)
include(${SDK}/C_API/buildsupport/playdate_game.cmake)
```

The SDK's `playdate_game.cmake` runs `pdc` to package `Source/` and the compiled code into a
`.pdx`. Two `POST_BUILD` steps then adjust the `.pdx`'s `pdxinfo`:

- [`pdx_buildnumber.cmake`](../port/boards/playdate/pdx_buildnumber.cmake) gives every build the next
  `buildNumber=`, counted in `port/boards/playdate/.build_number` (git-ignored, shared by all build
  directories). It uses the larger of that counter and the number in `Source/pdxinfo`, so bumping
  `buildNumber` in `Source/pdxinfo` moves the count up.
- [`pdx_rename.cmake`](../port/boards/playdate/pdx_rename.cmake) (profiling builds only) appends tags
  such as ` (profile, demo1, stack A/B)` to the game's `name=`, so a profiling build is not mistaken
  for the real game in the launcher.

```shell
# What the post-build step runs, by hand:
cmake -DPDXINFO=build-dev/quake_DEVICE.pdx/pdxinfo \
      -DSOURCE=port/boards/playdate/Source/pdxinfo \
      -DCOUNTER=port/boards/playdate/.build_number \
      -P port/boards/playdate/pdx_buildnumber.cmake
```

### Release build

```shell
scripts/release-device.sh        # or the VS Code task "Playdate: release build (device)"
```

A clean Release build of the device `.pdx` in `build-release/` (no profiler or benchmark), with a new build number: the number appears in the built `pdxinfo`
and is also written to `Source/pdxinfo`, so the release is recorded in the source tree. Details in [Scripts and tools](tools.md#scriptsrelease-devicesh).

The script configures with `-DPD_RELEASE=ON`, which makes the release ship the shareware data (see [`Source/`](#source) below).
`CMAKE_BUILD_TYPE` cannot decide that: `platform.cmake` defaults it to Release, so an ordinary build is a Release build too.

### `toolchain.cmake`

[`toolchain.cmake`](../port/boards/playdate/toolchain.cmake) only locates the SDK and includes
the SDK's own `C_API/buildsupport/arm.cmake`. Omit it and you get the Simulator build.

### `Source/`

| Path | Content |
| --- | --- |
| `Source/pdxinfo` | Game metadata: `name`, `author`, `bundleID`, `version`, `buildNumber`. |
| `Source/id1/pak0.pak` | Quake game data. **Not in git** and not redistributable; copy your own (the shareware one is fine). |
| `Source/id1/music/Quake02.wav` ... `Quake11.wav` | Optional: the music (CD tracks 2-11), see [`cd_pd.c`](port/playdate.md#cd_pdc). `pdc` turns them into `.pda`; the re-release's ten tracks are about 590 MB as PCM and about 150 MB as ADPCM (use ADPCM). |
| `Source/id1/pak0_demo.pak` | Optional: the shareware `pak0.pak` under another name. With `-DPD_RELEASE=ON` (`scripts/release-device.sh`) the built `.pdx` gets it as `id1/pak0.pak`, replacing `pak0.pak`; in every other build it is left out of the `.pdx`. |
| `Source/pdex.elf` etc. | Build products copied in by `playdate_game.cmake` (git-ignored). |

`DOS-CONFIG.CFG.bak` in the same directory is a stray backup of a DOS-era Quake config file
and is not used by the build.

[`port/boards/playdate/.gitignore`](../port/boards/playdate/.gitignore) keeps the generated and non-redistributable
files out of git: `Source/pdex.*` (build products), `Source/id1/` (the game data) and `.build_number`.

### Which pak0.pak the `.pdx` gets

`pdc` bundles all of `Source/`, so a `.pdx` would carry both `id1/pak0.pak` and `id1/pak0_demo.pak`.
[`pdx_pak.cmake`](../port/boards/playdate/pdx_pak.cmake) runs after `pdc` (a `POST_BUILD` step of the game target, device and simulator)
and works on the built `.pdx` only; `Source/id1/` is never touched:

| Build | `pak0_demo.pak` exists | In the `.pdx` |
| --- | --- | --- |
| `PD_RELEASE=ON` | yes | `id1/pak0.pak` is `pak0_demo.pak`; there is no `pak0_demo.pak` and no `id1/music` |
| `PD_RELEASE=ON` | no | `id1/pak0.pak` and `id1/music` as in `Source/id1/` |
| `PD_RELEASE=OFF` (default) | either | `id1/pak0.pak` as in `Source/id1/`; `pak0_demo.pak` is deleted from the `.pdx` |

```shell
cmake -DPDX=build-release/quake_DEVICE.pdx -DRELEASE=ON -P port/boards/playdate/pdx_pak.cmake   # by hand, on a built .pdx
```

## Removed boards

This project began as [sysprog21/quake-embedded](https://github.com/sysprog21/quake-embedded), which ran Quake on several targets.
The boards that have nothing to do with the Playdate were removed from this tree, together with the files that only they used:

| Removed | What it was |
| --- | --- |
| `port/boards/emulator/` | Desktop window through the MiniFB library |
| `port/boards/rv32emu/` | RISC-V guest for the rv32emu emulator |
| `port/boards/stm32h747i_disco/` | STM32H747I-DISCO discovery board (Cortex-M7, SD card, DSI LCD), with its vendor HAL configuration |
| `lib/minifb` (git submodule) and `.gitmodules` | The windowing library only the desktop board used |
| `port/snd.c` | A sound shim that used RISC-V `scall`s (RISC-V board only) |
| `port/fio/fio_fatfs.c` | `Sys_File*` on FatFs (STM32 board only) |

They are all in the git history. `port/fio/fio_posix.c` stays: the [host check](tools.md#toolshostcheckhostcheckc) links it to run the engine on a computer.
The engine's desktop networking (`net_dgrm.c`, `net_udp.c`, `net_bsd.c`) is Quake engine code and stays in the tree, but no board that selects it is left (see [Networking](engine/network.md)).

## Editor integration

[`.vscode/tasks.json`](../.vscode/tasks.json) wraps the common builds
(`Playdate: build (simulator, debug)`, `Playdate: build (device)`, `Playdate: release build (device)`, `Install on device (USB)`);
[`.vscode/launch.json`](../.vscode/launch.json) launches the Playdate Simulator under CodeLLDB
against `build-sim-debug/quake.pdx`. See [Scripts and tools](tools.md) for `release-device.sh` and `install-device.sh`.
