# Quake for Playdate

A WinQuake port for the [Playdate](https://play.date/), running on the device and in the Playdate Simulator.

Based on the original [Quake GPL source](https://github.com/id-Software/Quake), through [sysprog21/quake-embedded](https://github.com/sysprog21/quake-embedded).

Source documentation is in [docs/](docs/README.md): a [source code index](docs/source-index.md) of every file with links to a detailed description of each.

![Quake demo 1 on the Playdate screen](docs/demo.gif)

*Demo 1 as the Playdate screen shows it. Captured on a computer from the port's own 1-bit display output (see [Checking that an optimisation does not change the picture](docs/picture-check.md)), not filmed on a device, so it says nothing about device speed.*

## Controls

| Input | In the game | In Quake's menus |
| --- | --- | --- |
| D-pad up / down | Walk forward / back | Move the cursor |
| D-pad left / right | Turn left / right (crank out: strafe) | Change the highlighted value |
| A | Fire | Select |
| B | Jump | Back |
| Crank | Turn left / right (clockwise = right) | - |

You run by default; the Options menu's "Always Run" turns that off. While the title-screen demo plays, A or B opens Quake's menu.

**Crank out.** Pulling the crank out of the body changes two things:

- The crank turns the view, so D-pad left / right strafe instead of turning.
- Autofire is active (below).

**Autofire.** With the crank out and "Autofire" on in the Options menu (it is on by default), the game holds fire for you while a live monster is in your line of fire: on the crosshair, or inside the range Quake's own auto-aim would turn toward. The axe only swings at what is within reach, and grenade and rocket launchers fire once every 2 seconds instead of being held. Hold A to fire yourself at any time.

**System menu** (the Playdate's menu button):

- **Game Menu** opens Quake's own menu (new game, save and load, Options).
- **Options** opens Quake's Options screen directly (see [Settings](docs/settings.md)). B leaves it and returns straight to the game; the same screen reached through Game Menu goes back to Quake's main menu first.
- **Weapon** picks a weapon you own and have ammo for. It appears during play once you can choose between two or more. There is no weapon-cycling button, so this is how you switch.

## Mods and mission packs

Copy a mod's or mission pack's folder (`hipnotic`, `rogue`, `mymod`, ...) next to `id1` in the game's Data folder, then pick it in Quake's main menu under **Mods**: the game restarts in it and keeps starting there until you pick Quake again. Hold B while the game starts to start plain Quake. Scourge of Armagon and Dissolution of Eternity get their own status bar, New Game episodes and Weapon menu entries. See [Mods and mission packs](docs/port/mods.md).

## Building

Requires the [Playdate SDK](https://play.date/dev/) and your own `pak0.pak` (the shareware one is fine). The steps, where the game data and music go, and the CMake options are in **[Building](docs/building.md)**.

## Documentation

- [Building](docs/building.md): Simulator and device builds, game data, music, performance options.
- [Renderer](docs/renderer.md): how the Playdate renderer differs from the original, and device timings.
- [Settings](docs/settings.md): every Options menu setting, the dithering modes and their cost.
- [Mods and mission packs](docs/port/mods.md): installing them, the Mods menu, what was tested.
- [Profiling on the device](docs/profiling.md): the on-device profiler and what it showed about the hardware.
- [Checking that an optimisation does not change the picture](docs/picture-check.md): the host check harness.
- [Source documentation](docs/README.md): every source file, starting from the [source code index](docs/source-index.md).
