# Platform interface headers

[← Documentation index](../README.md)

These headers declare the functions the engine *expects the platform to provide*: video, input, sound and CD
audio. In this repository the implementations live under [`port/`](../port/overview.md) (see the table at the
end of each section). The page also lists a few **legacy headers** from the original x86 DOS/Windows build that
nothing in this tree includes any more.

| Header | Implemented by |
| --- | --- |
| [`vid.h`](#vidh) | [`port/vid_port.c`](../port/overview.md#portvid_portc) |
| [`input.h`](#inputh) | [`port/in_port.c`](../port/overview.md#portin_portc) |
| [`sound.h`](#soundh) | [`port/boards/playdate/snd.c`](../port/playdate.md#sndc) |
| [`cdaudio.h`](#cdaudioh) | [`port/cd_null.c`](../port/overview.md#portcd_nullc) |
| [Legacy headers](#legacy-x86-headers-not-used) | nothing (unused) |

---

## `vid.h`

The video driver contract and the global video state.

```c
typedef byte pixel_t;                    // one byte per pixel (8-bit paletted)

typedef struct vrect_s {                 // a rectangle, chained into lists
	int x, y, width, height;
	struct vrect_s *pnext;
} vrect_t;

typedef struct {
	pixel_t  *buffer;                    // the frame being drawn
	pixel_t  *colormap;                  // 256 * VID_GRADES (64) lighting table
	int       fullbright;                // first fullbright palette index
	unsigned  rowbytes, width, height;
	float     aspect;
	int       numpages;                  // 1: no page flipping
	int       recalc_refdef;
	pixel_t  *conbuffer;                 // where the console is drawn
	int       conrowbytes;
	unsigned  conwidth, conheight;
	int       maxwarpwidth, maxwarpheight;   // underwater warp buffer size
} viddef_t;

extern viddef_t vid;                     // the one global
```

Functions the platform implements:

| Function | Purpose |
| --- | --- |
| `VID_Init(palette)` | Allocate the buffers, fill in `vid`. |
| `VID_SetPalette(palette)` | The 768-byte RGB palette changed (start-up, gamma, damage/pickup flashes). |
| `VID_Update(rects)` | Flush the dirty rectangles from `vid.buffer` to the screen. |
| `VID_Shutdown()` | Release the screen. |
| `VID_HandlePause`, `VID_SetMode` | Win32-only hooks; not used here. |

The `d_8to16table` / `d_8to24table` arrays hold the palette converted for 16- and 24-bit displays. This port defines them
in `vid_port.c`, though only the 8-bit path is exercised on the Playdate. **Change from the original:** `VID_ShiftPalette`
was removed (palette flashes call `VID_SetPalette` directly).

```c
// vid_port.c: what VID_Init sets up (abridged)
vid.width  = vid.conwidth  = width;      vid.height = vid.conheight = height;
vid.buffer = vid.conbuffer = vid_buffer; vid.rowbytes = vid.conrowbytes = width;
vid.colormap   = host_colormap;
vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));
```

## `input.h`

```c
void IN_Init (void);               // start devices
void IN_Shutdown (void);
void IN_Commands (void);           // devices may append console commands (called each frame)
void IN_Move (usercmd_t *cmd);     // add movement on top of the keyboard cmd
void IN_ClearStates (void);        // reset all button/position states
```

Keyboard-style input does *not* go through here; boards queue key events and the engine pulls them with
`Sys_SendKeyEvents` → `Key_Event`. `IN_Move` is only the mouse-look path, which is idle on the Playdate (the crank is
handled in [`main.c`](../port/playdate.md#mainc) by writing `cl.viewangles` directly).

## `sound.h`

The full Quake sound API: sound effects (`sfx_t`), cached decoded data (`sfxcache_t`), mixer channels (`channel_t`),
the DMA ring (`dma_t`) and the functions the client calls.

```c
sfx_t *S_PrecacheSound (char *sample);                          // register a sound at load
void   S_StartSound (int entnum, int entchannel, sfx_t *sfx,
                     vec3_t origin, float fvol, float attenuation);   // play it from a position
void   S_StopSound (int entnum, int entchannel);
void   S_StopAllSounds (qboolean clear);
void   S_Update (vec3_t origin, vec3_t fwd, vec3_t right, vec3_t up);  // per frame: listener
void   S_BeginPrecaching (void);  void S_EndPrecaching (void);
void   S_LocalSound (char *s);                                  // menu/UI sounds
```

The original also exposes the software mixer (`S_PaintChannels`, `SND_PickChannel`, `SNDDMA_*`, `channels[128]`,
`paintedtime`). **This port does not use that half:** the Playdate backend hands decoded samples to the system's own
mixer. Only the cvars `volume`, `bgmvolume` and the variables `sound_nominal_clip_dist`, `listener_origin`,
`listener_right` are shared with the implementation.

**Changes from the original:** `sfx_t` gained `cache_data_size` and `sfxcache_t` carries a `void *data` pointer
instead of a trailing byte array (the backends keep the decoded data in their own memory).

```c
// a sound as the Playdate backend registers it (port/boards/playdate/snd.c): the name is the pak path
sfx_t *s = S_PrecacheSound ("weapons/rocket1i.wav");   // → "sound/weapons/rocket1i.wav"
S_StartSound (cl.viewentity, 0, s, vec3_origin, 1.0f, 1.0f);
```

## `cdaudio.h`

`CDAudio_Init`, `Play`, `Stop`, `Pause`, `Resume`, `Update`, `Shutdown`. All are empty in
[`cd_null.c`](../port/overview.md#portcd_nullc); there is no CD music.

---

## Legacy x86 headers (not used)

These come from id Software's DOS/Windows source, where critical loops were hand-written in 32-bit x86 assembly (`x86/*.s`
in the original). **No source or CMake file in this tree includes them**, and the assembly (`x86/`) and a few related
headers (`adivtab.h`, `anorm_dots.h`, `asm_i386.h`) were deleted. The remaining headers are kept as documentation of the
layouts the assembly assumed.

| Header | What it is |
| --- | --- |
| [`quakeasm.h`](../../winquake/quakeasm.h) | GNU-as `.extern C(...)` declarations of the renderer globals the x86 assembly read. |
| [`d_ifacea.h`](../../winquake/d_ifacea.h) | Offsets and constants of the `d_iface.h` structs for the assembly driver (`ALIAS_ONSEAM`, `TURB_TEX_SIZE`, …), with "must match the C structs" warnings. |
| [`asm_draw.h`](perf-infrastructure.md#asm_drawh) | Offsets of `espan_t`, `edge_t`, `surf_t`, `spanpackage_t` for the assembly. |
| [`block8.h`](../../winquake/block8.h), [`block16.h`](../../winquake/block16.h) | x86 AT&T assembly macros for the 8- and 16-bit surface-cache block draw (`LEnter16_8`, with self-modifying patch points `LBPatch0…`). |
| [`vgamodes.h`](../../winquake/vgamodes.h) | VGA mode tables (`VGA_InitMode`, `VGA_SwapBuffers`, `VGA_SetPalette`, base mode descriptors) for the DOS port. |
| [`resource.h`](../../winquake/resource.h) | Windows resource IDs (`IDS_STRING1`, `IDI_ICON2`, `IDD_PROGRESS`, …) generated by Microsoft Developer Studio for `winquake.rc`. |

For the Playdate's own assembly see [Performance infrastructure](perf-infrastructure.md).
