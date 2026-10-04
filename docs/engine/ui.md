# Screen, HUD, console and menus

[← Documentation index](../README.md)

Everything drawn *on top of* (or around) the 3D view: the frame sequence that assembles the screen, the 2D drawing
primitives, the status bar, the console and the menus.

| File | Role |
| --- | --- |
| [`screen.h` / `screen.c`](#screenh--screenc) | `SCR_UpdateScreen`: the frame sequence; centre text, FPS counter, loading plaque |
| [`draw.h` / `draw.c`](#drawh--drawc) | 2D drawing primitives (characters, pictures, fills) |
| [`sbar.h` / `sbar.c`](#sbarh--sbarc) | Status bar, inventory, scoreboard, intermission |
| [`console.h` / `console.c`](#consoleh--consolec) | The drop-down console and notify lines |
| [`menu.h` / `menu.c`](#menuh--menuc) | The menus, including the Playdate Options screen |

```
SCR_UpdateScreen
 ├─ SCR_CalcRefdef (if the view changed)
 ├─ V_RenderView  ──► the 3D view (renderer)
 ├─ Sbar_Draw, SCR_DrawConsole, SCR_DrawFPS, M_Draw    ← 2D overlays through Draw_*
 ├─ V_UpdatePalette                                      ← flash colours
 └─ VID_Update(rects) ──► qembd_fillrect ──► the LCD
```

---

## `screen.h` / `screen.c`

Owns the overall frame and the view rectangle.

### `SCR_UpdateScreen`

Called once per frame by `Host_Frame`. Roughly:

1. Skip if drawing is blocked (`scr_skipupdate`, `block_drawing`, or a load in progress; a load that takes over 60 s gives up and prints `load failed.`).
2. Update the FPS counter if `scr_showfps` is on.
3. Recalculate the view rectangle if the field of view, view size or `lcd_x` changed (`SCR_CalcRefdef` → `R_ViewChanged`).
4. First frames: clear the whole screen (`Draw_TileClear`).
5. `SCR_SetUpToDrawConsole`, `V_RenderView` (the 3D view).
6. Overlays, depending on state: loading plaque; intermission/finale; or the normal HUD (`SCR_DrawRam/Net/Turtle/Pause`, centre print,
   `Sbar_Draw`, `SCR_DrawConsole`, the FPS counter, `M_Draw`).
7. `V_UpdatePalette` for colour flashes.
8. `VID_Update` on one of three rectangles: the whole screen (`scr_copyeverything`), everything above the status bar (`scr_copytop`), or only the 3D view.

### Other functions

| Function | Purpose |
| --- | --- |
| `SCR_Init` | Registers the cvars `fov`, `viewsize`, `scr_conspeed`, `showram`, `showturtle`, `showpause`, `scr_centertime`, `scr_printspeed`, `scr_showfps` and the commands `sizeup`, `sizedown`. |
| `SCR_CalcRefdef` | Chooses the status-bar height (`sb_lines`) and the view rectangle (`scr_vrect`), and calls `R_ViewChanged`. |
| `SCR_CenterPrint(str)` | Text printed in the middle of the screen (door messages, pickups). `SCR_DrawCenterString`, `SCR_CheckDrawCenterString`, `SCR_EraseCenterString`. |
| `SCR_BeginLoadingPlaque` / `SCR_EndLoadingPlaque` / `SCR_DrawLoading` | The "Loading" disc between levels. |
| `SCR_ModalMessage(text)` | A blocking yes/no prompt (e.g. quit). |
| `SCR_BringDownConsole`, `SCR_SetUpToDrawConsole`, `SCR_DrawConsole` | Console slide animation (`scr_con_current`). |
| `SCR_DrawRam`, `SCR_DrawNet`, `SCR_DrawTurtle`, `SCR_DrawPause` | The small icons: cache full, net trouble, slow frame, paused. |
| `SCR_SizeUp_f` / `SCR_SizeDown_f` | `sizeup` / `sizedown`. |
| `CalcFov(fov_x, width, height)` | Vertical field of view from the horizontal one. |

### The FPS counter (port addition)

`scr_showfps` is an archived cvar, default on (`"Show FPS"` in the Options menu). The counter counts frames for half a second and rewrites
a short string (`"28.4"`) at that rate, drawn right-to-left in the top-right corner with `Draw_Character`. Turning it off clears its
state, so it costs nothing when off.

```c
cvar_t scr_showfps = {"scr_showfps", "1", true};

// SCR_UpdateFPS: once per frame
scr_fps_frames++;
if (t - scr_fps_start < 0.5) return;                      // refresh only twice a second
tenths = (int)((float)scr_fps_frames / (float)(t - scr_fps_start) * 10.0f + 0.5f);
snprintf (scr_fps_str, sizeof(scr_fps_str), "%d.%d", tenths / 10, tenths % 10);
```

**Other changes from the original:** the `screenshot` command and the PCX writer (`WritePCXfile`, `SCR_ScreenShot_f`) were removed; `pconupdate`
and all dedicated-server branches were removed; maths calls are single-precision (`tanf`, `atanf`).

---

## `draw.h` / `draw.c`

"The only functions outside the refresh allowed to touch the vid buffer." Everything writes into `vid.buffer` / `vid.conbuffer`.

| Function | Purpose |
| --- | --- |
| `Draw_Init` | Loads the font (`conchars`), the loading disc (`disc`) and the background tile (`backtile`) from `gfx.wad`. |
| `Draw_Character(x, y, num)` | One 8×8 glyph; `num` is the font cell (add 128 for the alternate "gold" set). |
| `Draw_CharacterScaled(x, y, num, scale)` | **(port)** The same glyph with every font pixel drawn as a `scale`×`scale` block. Used by the Options and Save/Load menus at 2×. Glyphs that do not fit the screen entirely are not drawn. |
| `Draw_String(x, y, str)` | A string of 8×8 glyphs. |
| `Draw_Pic`, `Draw_TransPic`, `Draw_TransPicTranslate` | A `qpic_t`: opaque, with colour 255 transparent, and with a colour remap (player colours). |
| `Draw_PicFromWad(name)`, `Draw_CachePic(path)` | Fetch a picture from `gfx.wad` or load it from the pak and cache it. |
| `Draw_Fill(x, y, w, h, colour)`, `Draw_TileClear(x, y, w, h)` | Solid fill; tile the background texture (`backtile`) around a smaller view. |
| `Draw_FadeScreen` | Darken everything (checkerboard of black) behind a menu or dialog. |
| `Draw_ConsoleBackground(lines)` | The console's picture, with the version string. |
| `Draw_BeginDisc` / `Draw_EndDisc` | Disc icon while loading. |
| `Draw_DebugChar` | A debug character on the screen. |

```c
Draw_Character (x, y, *end);                    // the FPS counter
Draw_CharacterScaled (4, 32 + row*16, 12, 2);   // the menu cursor at double size (glyphs 12 and 13 blink)
Draw_Fill (0, 0, vid.width, 8, 0);              // a black bar
```

### `DRAW_TOUCH`: keeping the low-res view in step

When `PD_LOWRES_3D` is on, the 3D view is *not* expanded into `vid.buffer`; the display layer dithers it straight from a half-resolution
buffer ([`pdr_lowres.c`](renderer-pdr.md#pdr_lowresc)). Anything that draws on top of the view must therefore make the rows underneath
real first. **Every primitive that writes `vid.buffer` calls `DRAW_TOUCH(y, h)` with the rows it is about to change:**

```c
#define DRAW_TOUCH(y, h)  do { if (qembd_lowres_active) D_LowresTouch ((y), (y) + (h)); } while (0)

void Draw_Character (int x, int y, int num)
{
	...
	DRAW_TOUCH (y, 8);        // expand the 2 row-pairs under this glyph, once
	... write pixels ...
}
```

Forgetting `DRAW_TOUCH` in a new primitive leaves the picture under it wrong; the host check ([`hostcheck`](../tools.md#the-two-invariants))
exists to catch exactly that.

### Other changes from the original

`Draw_Pic` now **scales to fit** instead of refusing to draw a picture that is larger than the screen (a hack so the 320×200 help screens
fit); the 16-bit paths still exist but are unused; `DRAW_TOUCH` calls were added throughout.

---

## `sbar.h` / `sbar.c`

The status bar (health, armour, ammo, weapon icons, the face) and the overlays that use the same drawing helpers.

| Function | Purpose |
| --- | --- |
| `Sbar_Init` | Loads the number, face, weapon, ammo, armour and rune pictures from `gfx.wad`; registers `+showscores` / `-showscores`. |
| `Sbar_Draw` | The whole HUD for the frame: inventory, status bar, face, ammo counts, frags, deathmatch overlay. Only redraws when `Sbar_Changed` was called (a stat changed). |
| `Sbar_DrawFace`, `Sbar_DrawNum`, `Sbar_DrawInventory`, `Sbar_DrawFrags` | The parts. Numbers can be red (`color` 1) when low. |
| `Sbar_SoloScoreboard` | Level name, kills, secrets, time. |
| `Sbar_DeathmatchOverlay`, `Sbar_MiniDeathmatchOverlay`, `Sbar_UpdateScoreboard`, `Sbar_SortFrags`, `Sbar_ColorForMap` | The multiplayer scoreboard. |
| `Sbar_IntermissionOverlay`, `Sbar_FinaleOverlay`, `Sbar_IntermissionNumber` | The end-of-level and end-of-game screens. |
| `Sbar_ShowScores` / `Sbar_DontShowScores` | The `+showscores` / `-showscores` commands. |

```c
Sbar_DrawNum (136, 0, cl.stats[STAT_HEALTH], 3, cl.stats[STAT_HEALTH] <= 25);   // health, red when ≤ 25
```

Unchanged except an unsigned shift in the rune loop and an unused variable removed.

---

## `console.h` / `console.c`

The drop-down console and the "notify" lines at the top of the screen.

- **Output.** `Con_Printf(fmt, ...)` writes into a scrolling text buffer (`con_text`, `CON_TEXTSIZE`) and echoes to `Sys_Printf`. `Con_DPrintf` prints only if
  `developer` is set; `Con_SafePrintf` is safe during loading; `Con_Print` is the raw version.
- **Display.** `Con_DrawConsole(lines, drawinput)` draws the buffer and the input line; `Con_DrawNotify` the last few lines for a few seconds
  (`con_notifytime`); `Con_DrawInput` the edit line with a blinking cursor.
- **Control.** `Con_ToggleConsole_f` (`toggleconsole`), `Con_MessageMode_f` (`messagemode`, chat), `Con_Clear_f`, `Con_ClearNotify`, `Con_CheckResize`
  (re-wraps when the width changes), `Con_NotifyBox` (a blocking message box).
- **Debug log.** `Con_DebugLog(file, fmt, ...)` appends to a file when `-condebug` is given.

```c
Con_Printf ("%4.1f megabyte heap\n", parms->memsize / (1024*1024.0));
Con_DPrintf ("S_LocalSound: cannot cache %s\n", s);      // only with "developer 1"
```

**Port changes:** the large temporary buffers (`tbuf`, `msg` in the print functions) are `static` instead of on the stack; the debug log uses the stdio shim on the
Playdate (no libc file descriptors); the dedicated-server early-outs were removed; `PROF_STK(K_CON)` is a stack-depth probe. On the Playdate the console cannot be opened
(the Options menu has no "Go to console"), but its output still goes to the system log.

---

## `menu.h` / `menu.c`

All of Quake's menus. A menu is a `m_state` plus three functions: `M_X_Draw`, `M_X_Key`, and an entry point `M_Menu_X_f`. `M_Keydown(key)` routes keys to the current menu,
`M_Draw()` draws it, `M_ToggleMenu_f` (the `togglemenu` command) opens or closes it. With `key_dest = key_menu` the keys go to `M_Keydown` instead of the game.

| Menu | Entry point | Notes |
| --- | --- | --- |
| Main | `M_Menu_Main_f` | Single Player, Multiplayer, Options, Help, Quit. |
| Single player | `M_Menu_SinglePlayer_f`, `M_Menu_Load_f`, `M_Menu_Save_f` | New game, load, save. |
| **Options** | `M_Menu_Options_f`, **`M_Menu_Options_Shortcut`** | Rewritten for the Playdate (below). |
| Help | `M_Menu_Help_f` | The help screens. |
| Quit | `M_Menu_Quit_f` | |
| Multiplayer, LAN, serial, modem, setup, game options, server list, search | `M_Menu_MultiPlayer_f`, `M_Menu_LanConfig_f`, … | Present but only meaningful with a network driver (`net_dgrm.c`). |
| Keys, Video | `M_Menu_Keys_f`, `M_Menu_Video_f` | Not reachable on the Playdate (the Options menu has no "Customize controls" or "Video"). |

```c
M_Menu_Options_f ();            // normal: Escape returns to the main menu
M_Menu_Options_Shortcut ();     // from the Playdate system menu: Escape returns straight to the game
```

### The Playdate Options menu

The original Options menu was replaced by a scrolling list drawn at 2× in screen coordinates (`OPT_SCALE` = 2, `Draw_CharacterScaled`). Rows (`enum` in `menu.c`):

| Row | Control | Cvar / effect |
| --- | --- | --- |
| Reset defaults | Enter | Runs `exec default.cfg`, then sets the port's cvars back to their defaults (see below). |
| Brightness | slider | `gamma` 0.5–1.0 |
| **Crank speed** | slider (0.2 steps, 0.2–3) | `crank_speed` (default 1.4) |
| Music volume, SFX volume | sliders | `bgmvolume`, `volume` |
| Always run | checkbox | `cl_forwardspeed` / `cl_backspeed` 200 ↔ 400 |
| **Autofire** | checkbox | `cl_autofire` |
| **Texture detail** | low / high | `d_mipcap` 1 / 0 |
| **Interlaced** | checkbox | `r_interlace` |
| **Dithering** | patterns / bayer / blue noise / diffusion (wraps) | `pd_dither` 0–3 |
| **Draw distance** | slider over 256, 384, 512, 768, 1024, 1536, 2048, 3072, unlimited | `r_maxdist` |
| **Max framerate** | 30 / 50 / unlimited | `pd_maxfps` |
| **Show FPS** | checkbox | `scr_showfps` |

Removed rows: Customize controls, Go to console, Screen size, Invert mouse, Lookspring, Lookstrafe.

```c
// menu.c: new cvars owned by the menu, registered in M_Init
cvar_t crank_speed = {"crank_speed", "1.4", true};
cvar_t pd_maxfps   = {"pd_maxfps",   "30",  true};
cvar_t pd_dither   = {"pd_dither",   "0",   true};

// every change marks the options dirty so Host_SaveOptions writes config.cfg when the menu is left
void M_AdjustSliders (int dir)
{
	host_options_dirty = true;
	switch (options_cursor) {
	case OPT_DITHER:
		Cvar_SetValue ("pd_dither", (M_DitherMode () + dir + NUM_DITHER_MODES) % NUM_DITHER_MODES);
		break;
	...
```

```c
// "Reset defaults": default.cfg knows nothing about the port's options, so set them here
Cbuf_AddText ("exec default.cfg\n");
Cvar_SetValue ("d_mipcap", 1);      Cvar_SetValue ("r_interlace", 1);
Cvar_SetValue ("pd_dither", 0);     Cvar_SetValue ("r_maxdist", 512);
Cvar_SetValue ("crank_speed", 1.4f); Cvar_SetValue ("pd_maxfps", 30);
Cvar_SetValue ("scr_showfps", 1);
```

Leaving the menu with Escape calls `Host_SaveOptions()`; with `options_to_game` (opened from the system menu) Escape closes the menu via `M_Menu_Close`.

### Load and Save at double size

`M_DrawSlots` draws the slot list at 2×. A slot's 39-character comment (22-character level name plus `kills:%3i/%3i`) does not fit in 400 pixels at that size, so
`M_SlotText` shows the level name cut to 15 characters and the kills as `k/t`.

### Other changes from the original

- `M_Menu_Close()`: one place that leaves the menu (restores the demo loop, calls `CL_NextDemo` on the title screen).
- `qembd_set_relative_mode(false/true)` calls tell a desktop board to release or capture the mouse; a no-op on the Playdate.
- `_M_RealTime4Mod1` / `_M_HostTime10Mod6` replace the cursor and menu-dot animation expressions with single-precision versions (no `double` multiply per frame).
