# Quake for Playdate 0.3.0

## TLDR

 - a new 3D renderer written for the Playdate
 - Quake runs at 25-30 fps
 - a rebuilt Options menu with a set of new settings
 - and four ways to dither the picture onto the 1-bit screen.

Game data is not included. Copy your `pak0.pak` (the shareware one is fine) into the game's Data folder at `id1/`.

## Highlights

- **New renderer, on by default.** The 3D view is drawn by a renderer written for the Playdate's memory system instead of Quake's original software refresh. Against the already-optimised original renderer, with default settings, measured on a device:

  |                                   | original           | new                | change |
  | --------------------------------- | ------------------ | ------------------ | ------ |
  | demo1 (ms per frame)              | 36.8               | 28.2               | -23.5% |
  | demo2                             | 36.1               | 29.1               | -19.2% |
  | demo3                             | 41.2               | 31.2               | -24.3% |
  | demo1 / 2 / 3 (frames per second) | 24.2 / 24.7 / 22.2 | 27.9 / 27.8 / 27.2 |        |

- **Rebuilt Options menu** with settings made for the Playdate: crank speed, texture detail, interlaced rendering, draw distance, max framerate, dithering and show FPS. All of them are saved.
- **Four dithering modes:** patterns, Bayer, blue noise and error diffusion.
- **Options in the system menu.** The new "Options" item opens the Options screen directly, and B returns straight to the game.
- **Underwater view fixed.** Water, slime and lava no longer squeeze the picture into mid-grey.

## Changes you will notice when upgrading from 0.1.0

- **New defaults:** texture detail **low** (was high), interlaced **on**, draw distance **512**, crank speed **1.4** (it was fixed at 1.0), max framerate **30**, show FPS **on**. A `config.cfg` you already have keeps whatever it says; "Reset defaults" in the Options menu goes back to the new defaults.
- **Show FPS moved** from the system menu to the Options menu, and is now remembered between launches.
- **The console can no longer be opened on the device.** These Options rows are gone: Customize controls, Go to console, Screen size, Invert mouse, Lookspring and Lookstrafe. Their values keep whatever `config.cfg` says. Mouse Speed became **Crank speed**.
- **The Load and Save menus** use a double-size font. A slot shows the level name cut to 15 characters and the kills as `killed/total`.
- **This repository now targets the Playdate only.** The desktop emulator, rv32emu and STM32H747I-Discovery ports are removed, along with the `minifb` submodule and their support code.

## What's new

### Rendering

- **New renderer** (`PD_NEW_RENDERER`, default on; `winquake/pdr_*.c`).
  - It walks the BSP front to back and draws each face straight into the rows it covers, with no edge list or span sorting.
  - There is no surface cache. Faces are textured directly and lit from small per-face light blocks, so dynamic lights and light styles only rebuild a few hundred bytes.
  - It draws the same things as before: world, sky, water, doors and platforms, monsters, sprites, particles, dynamic lights, light styles, underwater warp, interlacing and draw distance. It uses the same settings.
  - Build with `-DPD_NEW_RENDERER=OFF` to get Quake's original renderer back.
- **Interlaced rendering** (Options > Interlaced): each frame draws only every other row of the 3D view, so moving edges comb slightly. demo1 went from 44.5 to 39.2 ms per frame (-12%).
- **Texture detail** (Options > Texture detail): `low` skips the sharpest mip level, about 1.6 ms per frame faster (about 3%) with softer textures.
- **Draw distance** (Options > Draw distance): 256 to 3072 or unlimited. Far world geometry, models, doors and particles are culled and drawn black, and the sky still shows behind them. Open areas gain the most.
- **Max framerate** (Options > Max framerate): 30, 50 or unlimited. Above 30 the battery drains faster.
- **Faster original renderer** (used with `-DPD_NEW_RENDERER=OFF`), same picture as 0.1.0:
  - Hand-written ARM assembly for the hottest loops: 4-5 ms per frame faster (7-9%).
  - Scratch data moved onto the fast stack: 2.6-3.6 ms per frame faster (5-7%).
- **Underwater.** The liquid colour blend is left out of the palette, so the view keeps its full black-to-white range. The warp and the damage, pickup and powerup flashes are unchanged.
- The screen is fully redrawn after the system menu or lock screen, so interlaced frames never leave stale rows behind.

### Dithering

Options > Dithering (`pd_dither`, left/right or A cycles and wraps):

| Mode                   | What it does                                                                                                                                                        |
| ---------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **patterns** (default) | The look from earlier releases. Cheapest and cleanest, but only five grey levels in the 3D view, so dim areas can drop to black.                                    |
| **bayer**              | A 4x4 Bayer matrix everywhere, 17 grey levels. A regular grid pattern, and dim surfaces stay visible.                                                               |
| **blue noise**         | A 32x32 blue-noise tile everywhere, 256 grey levels. An even grain with no grid, but a rougher HUD and menus.                                                       |
| **diffusion**          | Error diffusion for the 3D view, with a tone curve that follows the scene's brightness. Dark rooms keep their detail, at the price of a brighter, grainier picture. |

The CPU cost of the new modes is an estimate, not a device measurement: about 1-1.5 ms per frame for `bayer` and `blue noise`, and 4-6 ms for `diffusion` (about half of that interlaced).

### Controls and menus

- **Crank speed** slider (0.2 to 3, default 1.4), replacing the Mouse Speed slider.
- **System menu:** Game Menu, **Options** (new) and Weapon.
- **Options screen** drawn with a double-size font, scrolling when it does not fit.
- Settings are saved to `config.cfg` when you leave the Options menu and when the system pauses, locks or closes the game.

### Build and tooling

- Every build gets its own **build number** in the built `.pdx`'s `pdxinfo` (`buildNumber=`). `version` is now 0.3.
- Profiling builds show up in the launcher as "Quake Playdate (profile, ...)" so they are not mistaken for the game.
- Build options: `PD_NEW_RENDERER`, `PD_ASM` and `PD_STACK`. `PD_ASM`, `PD_STACK` and the `PD_FAST_*` options only apply to the original renderer.
- New A/B and check modes for the profiler: assembly vs C, stack vs static buffers, and renderer experiments (`PD_ASM_AB`, `PD_ASM_CHECK`, `PD_STACK_AB`, `PD_PDR_EXP`, and `scripts/pd-report.py --ab`).
- `tools/hostcheck` can build and test the new renderer on a computer (`NEW=1`). It also tours every shareware map as a crash check (`HMAPS=1`), compares screenshots of two builds (`HSHOTS` and `compare-shots.py`) and exercises each dithering mode (`HCMD`).
- `scripts/gen-bluenoise.py` generates the blue-noise tile.

### Documentation

- A new `docs/` guide covering the engine, both renderers, the Playdate port, the build system and the tools. Start at `docs/README.md`.
- The README now documents the new renderer, its measurements, every setting and the checking tools.

## Known limitations

- **The new renderer's picture is not bit-identical to the original.** Lighting is interpolated over 8-pixel runs, edges are rounded differently, and small models (torch flames, distant monsters) come out a pixel thinner. Over the three demos, 0.3% of the 3D view's pixels differ by more than a tenth of the brightness range.
- **It uses about 150-250 KB more memory per map** than the original's surface cache.
- Dither mode timings other than `patterns` are estimates (see above).

**Full changelog:** https://github.com/aliakseikalosha/quake-playdate/compare/0.1.0...0.3.0
