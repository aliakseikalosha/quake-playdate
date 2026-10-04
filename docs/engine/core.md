# Engine core

[← Documentation index](../README.md) · [Source index](../source-index.md)

The foundation every other engine module stands on: start-up and the frame loop, memory, strings and
files, console variables and commands, math, and the WAD file format. These files are classic id
Software WinQuake (GPL) with the changes this port made noted under each heading. "Original" means
the WinQuake source imported as the first commit of this repository.

| File | Role |
| --- | --- |
| [`quakedef.h`](#quakedefh) | The master header: limits, stats, item bits, include order, host globals |
| [`host.c`](#hostc) | `Host_Init`, `Host_Frame`, errors, client management, `config.cfg` |
| [`host_cmd.c`](#host_cmdc) | Console commands: `map`, `save`, `load`, `kick`, `god`, … |
| [`sys.h`](#sysh) | The OS interface the port implements |
| [`common.h` / `common.c`](#commonh--commonc) | Buffers, message I/O, strings, file system, `.pak` loading |
| [`zone.h` / `zone.c`](#zoneh--zonec) | Hunk, zone and cache memory |
| [`cvar.h` / `cvar.c`](#cvarh--cvarc) | Console variables |
| [`cmd.h` / `cmd.c`](#cmdh--cmdc), [`defaultcfg.h`](#built-in-defaultcfg-defaultcfgh) | Command buffer, tokenizer, aliases; the built-in `default.cfg` |
| [`crc.h` / `crc.c`](#crch--crcc) | 16-bit CRC |
| [`mathlib.h` / `mathlib.c`](#mathlibh--mathlibc) | Vectors, matrices, plane tests |
| [`wad.h` / `wad.c`](#wadh--wadc) | WAD2 archive (`gfx.wad`) |
| [`nonintel.c`](#nonintelc) | Empty stand-ins for x86 surface-patching routines |

---

## `quakedef.h`

The header every `.c` file includes first. It does three jobs.

**1. Constants and limits**

```c
#define GAMENAME        "id1"     // directory the game data lives in
#define MAX_EDICTS      600       // per-level entity limit
#define MAX_MODELS      256       // sent as bytes over the net: cannot grow
#define MAX_SOUNDS      256
#define MAX_LIGHTSTYLES 64
#define MAX_QPATH       64        // game path length
#define MAX_OSPATH      128       // filesystem path length
#define MAX_MSGLEN      8000      // reliable message
#define MAX_DATAGRAM    1024      // unreliable message
#define MINIMUM_MEMORY  0x550000  // 5.3 MB heap needed to run
#define PITCH 0
#define YAW   1
#define ROLL  2
```

Player stats (`STAT_HEALTH`, `STAT_AMMO`, `STAT_ARMOR`, `STAT_SHELLS`, … `STAT_MONSTERS`) are indexes into
`cl.stats[]`; item bits (`IT_SHOTGUN`, `IT_AXE`, `IT_ARMOR1`, `IT_KEY1`, `IT_QUAD`, …) are tested against
`cl.items`. The Rogue and Hipnotic mission-pack bit sets (`RIT_*`, `HIT_*`) are defined too.

**2. Include order.** `quakedef.h` pulls in the whole engine in a fixed order: `common.h`, `bspfile.h`,
`vid.h`, `sys.h`, `zone.h`, `mathlib.h`, `wad.h`, `draw.h`, `cvar.h`, `screen.h`, `net.h`,
`protocol.h`, `cmd.h`, `sbar.h`, `sound.h`, `render.h`, `client.h`, `progs.h`, `server.h`, `model.h`,
`d_iface.h`, `input.h`, `world.h`, `keys.h`, `console.h`, `view.h`, `menu.h`, `crc.h`, `cdaudio.h`.
Most engine files need nothing else.

**3. Host interface**

```c
typedef struct {
	char *basedir;      // directory holding id1/
	char *cachedir;     // development only
	int   argc;  char **argv;
	void *membase;      // the heap the port allocated
	int   memsize;
} quakeparms_t;

void Host_Init (quakeparms_t *parms);
void Host_Frame (float time);
void Host_Shutdown (void);
```

**What this port changed:** the Windows/i386 conditionals and `id386`/`UNALIGNED_OK` are gone;
`<stdbool.h>`/`<stdint.h>` are included; `host_frametime` is a `float`; and two declarations were added,
`host_options_dirty` and `Host_SaveOptions()` (see [`host.c`](#hostc)). `VID_LockBuffer` and
`VID_UnlockBuffer` are empty macros.

---

## `host.c`

Start-up, the per-frame sequence, error handling and disconnecting clients.

### `Host_Init` order

```c
Memory_Init (parms->membase, parms->memsize);   // the hunk
Cbuf_Init ();  Cmd_Init ();                      // commands
V_Init ();  Chase_Init ();                       // view, chase camera
COM_Init (parms->basedir);                       // file system, pak files
Host_InitLocal ();                               // cvars, Host_InitCommands, skill, ...
W_LoadWadFile ("gfx.wad");
Key_Init ();  Con_Init ();  M_Init ();           // keys, console, menus
PR_Init ();  Mod_Init ();  NET_Init ();  SV_Init ();
R_InitTextures ();
host_basepal  = COM_LoadHunkFile ("gfx/palette.lmp");
host_colormap = COM_LoadHunkFile ("gfx/colormap.lmp");
VID_Init (host_basepal);  Draw_Init ();  SCR_Init ();  R_Init ();
CDAudio_Init ();  S_Init ();  Sbar_Init ();  CL_Init ();  IN_Init ();
Cbuf_InsertText ("exec quake.rc\n");
Hunk_AllocName (0, "-HOST_HUNKLEVEL-");   host_hunklevel = Hunk_LowMark ();   // level start mark
host_initialized = true;
```

If the heap is smaller than `MINIMUM_MEMORY`, `Sys_Error` stops the game.

### `Host_Frame(time)` / `_Host_Frame`

`Host_Frame` optionally wraps `_Host_Frame` with the `serverprofile` cvar. One frame is:

```
Host_FilterTime        refuse to run faster than 72 Hz (except in timedemo); sets host_frametime
Sys_SendKeyEvents      keys → Key_Event()
IN_Commands            external controllers add commands
Cbuf_Execute           run console commands
NET_Poll               accept new connections / read packets
CL_SendCmd             (local server) make this frame's user command
Host_GetConsoleCommands
Host_ServerFrame       (if sv.active) SV_RunClients, SV_Physics, SV_SendClientMessages
CL_SendCmd             (remote server, after packets were read)
CL_ReadFromServer      (if connected) apply the server's messages
SCR_UpdateScreen       draw everything
S_Update / CL_DecayLights / CDAudio_Update
host_framecount++
```

Frame timing is added by this port as `PROF_BEGIN(P_INPUT) … PROF_END(...)` pairs (see
[`pdprof.h`](perf-infrastructure.md#pdprofh)); they compile to nothing in a normal build.

### Errors and shutdown

| Function | Behaviour |
| --- | --- |
| `Host_Error(fmt, ...)` | Recoverable: `SCR_EndLoadingPlaque`, `CL_Disconnect`, shut down the local server, `longjmp(host_abortserver)` back to `_Host_Frame`. Calling it twice recursively becomes `Sys_Error`. |
| `Host_EndGame(fmt, ...)` | Like `Host_Error` but plays the demo loop (`CL_NextDemo`) if one is queued. |
| `Host_ShutdownServer(crash)` | Tells clients to disconnect and waits (up to 3 s) for the datagrams to leave. |
| `Host_ClearMemory` | Frees everything above the level mark; called on every map change. |
| `Host_Shutdown` | Saves `config.cfg`, shuts every subsystem down (once). |
| `SV_DropClient(crash)`, `SV_ClientPrintf`, `SV_BroadcastPrintf`, `Host_ClientCommands` | Per-client server utilities live here too. |

### Saved options (`config.cfg`)

`Host_WriteConfiguration` writes `config.cfg` (bindings and archived cvars) into the game directory.
**This port's changes**:

- **`Host_SaveOptions()`** writes the file only if `host_options_dirty` is set (the Options menu sets it
  when a value changes). The Playdate never runs `Host_Shutdown` (the game is stopped from the system menu), so
  the port calls `Host_SaveOptions` when the menu is left and from `eventHandler` on pause, lock and terminate.
- With `QEMBD_PLAYDATE`, **key bindings are not written** (`Key_WriteBindings` is skipped): bindings come
  from `default.cfg` (the pak's, or the [built-in one](#built-in-defaultcfg-defaultcfgh)) and the port's own
  button mapping, and a saved copy would override future defaults.

```c
// port/boards/playdate/main.c
if ((event == kEventPause || event == kEventLock || event == kEventTerminate) && state == ST_RUN)
	Host_SaveOptions ();       // cheap when nothing changed
```

### Other changes from the original

`host_framerate` and all dedicated-server (`-dedicated`) code paths were removed; `host_frametime` is a
`float`; `pd_stack_top` (see [`pd_stack.h`](perf-infrastructure.md#pd_stackh)) is defined here; the
`FPS_20` server stepping variant was removed.

Two cvars are registered in `Host_InitLocal` that nothing reads: `campaign` and `scr_usekfont`. The 2021 re-release's
`progs.dat` sets the first on every level and its `quake.rc` sets the second; unregistered, `Cvar_Set` printed
`Cvar_Set: variable campaign not found` every frame. See [Game data](../port/game-data.md#2-console-spam-cvar_set-variable-campaign-not-found).

```c
static cvar_t	campaign = {"campaign","0"};
static cvar_t	scr_usekfont = {"scr_usekfont","0"};
...
	Cvar_RegisterVariable (&temp1);
	Cvar_RegisterVariable (&campaign);
	Cvar_RegisterVariable (&scr_usekfont);
```

> **Note.** In `Host_FilterTime` the 0.001–0.1 s clamp is applied to `host_frametime` *before* it is
> assigned the new value (`_new_host_frametime`), so it clamps the previous frame's time and the value used for
> the current frame is not clamped. The caller (`qembd_frame`) separately resets its clock after a hitch longer than
> `2 * sys_ticrate`.

---

## `host_cmd.c`

The console commands that belong to the host. All are registered in `Host_InitCommands`.

| Group | Commands |
| --- | --- |
| Starting a game | `map <name>`, `changelevel`, `changelevel2`, `restart`, `connect`, `reconnect`, `begin`, `prespawn`, `spawn`, `kill` |
| Saving | `save <name>`, `load <name>` (`Host_Savegame_f`, `Host_Loadgame_f`, `SaveGamestate`, `LoadGamestate`, `Host_SavegameComment`) |
| Cheats | `god`, `notarget`, `fly`, `noclip`, `give <item> <n>` |
| Messages | `say`, `say_team`, `tell`, `color`, `name`, `kick`, `ping`, `status`, `pause`, `please` |
| Demos | `startdemos`, `demos`, `stopdemo` |
| Model viewer | `viewmodel`, `viewframe`, `viewnext`, `viewprev` (`FindViewthing`, `PrintFrameName`) |
| System | `quit`, `version` |

```
map e1m1            starts a single-player game
save slot0          writes id1/slot0.sav: every global, edict and light style as text
load slot0
give 7 100          100 rockets
```

A save file is plain text written with `fprintf` and read with `fscanf`. On the Playdate those go through
[`pd_stdio.c`](../port/playdate.md#pd_stdioc). **This port's changes:** dedicated-server branches removed;
`Host_Loadgame_f` and `LoadGamestate` use a `static char str[32768]` instead of a stack buffer (the stack is
only ~10 KB on the device); floats are cast to `double` for `fprintf`.

---

## `sys.h`

The platform interface the port implements ([`port/sys_port.c`](../port/overview.md#portsys_portc),
`fio*.c`):

```c
int  Sys_FileOpenRead (char *path, int *hndl);   // returns size, -1 if missing
int  Sys_FileOpenWrite (char *path);
void Sys_FileClose (int handle);
void Sys_FileSeek (int handle, int position);
int  Sys_FileRead (int handle, void *dest, int count);
int  Sys_FileWrite (int handle, void *data, int count);
int  Sys_FileTime (char *path);                  // -1 if missing
void Sys_mkdir (char *path);

void   Sys_Error (char *error, ...);             // never returns
void   Sys_Quit (void);
double Sys_FloatTime (void);
void   Sys_SendKeyEvents (void);
void   Sys_Printf (...);                         // macro → _Sys_Printf
```

**Changes:** `Sys_Printf` became a macro over `_Sys_Printf` so logging can be compiled out or routed
(`WINQUAKE_ENABLE_LOGGING`, `WINQUAKE_LOGGING_EXTERNAL`); all C-library file I/O in the engine was replaced by these calls.

---

## `common.h` / `common.c`

Utilities used by everything.

### Types and buffers

```c
typedef unsigned char byte;
typedef _Bool qboolean;                      // was an enum {false, true}

typedef struct sizebuf_s {                   // growable message buffer
	qboolean allowoverflow, overflowed;
	byte *data;  int maxsize, cursize;
} sizebuf_t;

SZ_Alloc (&buf, 1024);  SZ_Write (&buf, data, len);  SZ_Print (&buf, "text");  SZ_Clear (&buf);
```

`link_t` and `ClearLink` / `RemoveLink` / `InsertLinkBefore` / `InsertLinkAfter` implement the doubly linked
lists used by the world's area nodes; `STRUCT_FROM_LINK` recovers the owning struct.

### Message I/O

Everything on the wire goes through these (little-endian, with Quake's scaled coordinates and angles):

```c
MSG_WriteByte (&sv.reliable_datagram, svc_print);
MSG_WriteString (&sv.reliable_datagram, "hello");
MSG_WriteCoord (sb, origin[0]);                 // short, units of 1/8
MSG_WriteAngle (sb, angle);                     // byte, units of 360/256

MSG_BeginReading ();
int cmd = MSG_ReadByte ();                      // sets msg_badread on overrun
float x = MSG_ReadCoord ();
```

### Byte order

The original used function pointers selected at start-up. **This port** replaced them with compile-time `static inline`
functions chosen from `__BYTE_ORDER__`: `LittleShort/Long/Float` are identity on little-endian targets, `BigShort/…` swap.

```c
int version = LittleLong (header->version);     // free on the Cortex-M7
```

### Strings

`Q_strcpy`, `Q_strncpy`, `Q_strlen`, `Q_strcmp`, `Q_strcasecmp`, `Q_atoi`, `Q_atof` (now `float`), `Q_memcpy`, … are
Quake's own versions. On RISC-V (`__riscv`) they are inline wrappers over the C library (kept from the removed RISC-V board; the Playdate uses Quake's own). `va(fmt, ...)` formats into a
rotating static buffer; `COM_Parse(data)` is Quake's tokenizer (quoted strings, `//` comments).

### Arguments and file system

| Function | Purpose |
| --- | --- |
| `COM_InitArgv`, `COM_CheckParm("-mem")` | Command-line arguments (`com_argc`, `com_argv`). |
| `COM_Init`, `COM_InitFilesystem` | Registers the `registered` cvar, finds the game dir (`id1`), adds `-game` dirs, loads `pak0.pak`, `pak1.pak`, … |
| `COM_AddGameDirectory(dir)` | Add a directory and its numbered pak files to the search path. |
| `COM_FindFile` / `COM_OpenFile` / `COM_FOpenFile` | Look a file up in packs and directories. Return the file size (`com_filesize`), `-1` if missing. |
| `COM_LoadFile(path, usehunk)` | Loads to the hunk, the temp area, the zone or the cache. Wrappers: `COM_LoadHunkFile`, `COM_LoadTempFile`, `COM_LoadStackFile`, `COM_LoadCacheFile`. |
| `COM_WriteFile`, `COM_CreatePath`, `COM_CopyFile` | Writing under the game directory. |
| `COM_SkipPath`, `COM_StripExtension`, `COM_FileExtension`, `COM_DefaultExtension`, `COM_FileBase(in, out, outsize)` | Path helpers (`COM_FileBase` takes a buffer size in this port). |
| `COM_LoadPackFile` | Reads a `.pak` directory (`PACK` header) into `pack_t`. |
| `COM_CheckRegistered` | Detects the registered game; sets `registered` cvar. |

```c
byte *palette = COM_LoadHunkFile ("gfx/palette.lmp");   // from pak0.pak or id1/ on disk
if (!palette) Sys_Error ("Couldn't load gfx/palette.lmp");

COM_FOpenFile ("maps/e1m1.bsp", &f);                     // FILE* positioned at the pak entry
```

`len_for_emu` / `com_filesize`: the Playdate's sound backend reads the size of the last loaded file
(`COM_LoadTempFile`) from `len_for_emu`.

---

## `zone.h` / `zone.c`

Quake's memory is one contiguous block handed in by the port (`qembd_allocmain`). It is divided like this:

```
------ Top of memory -------
high hunk  (video buffer, z buffer, surface/light cache)   ← Hunk_HighAllocName
<-- high hunk reset point (held by vid)
cachable memory (models, sounds, textures: LRU)             ← Cache_Alloc
<-- low hunk used
client and server low hunk (level data)                     ← Hunk_Alloc / Hunk_AllocName
<-- low hunk reset point (host_hunklevel)
startup hunk allocations
zone (≈48 KB, small dynamic strings)                        ← Z_Malloc
----- Bottom of memory -----
```

| API | Use |
| --- | --- |
| `Hunk_Alloc(size)`, `Hunk_AllocName(size, "name")` | Zero-filled, 16-byte-aligned, stack-style allocations at the low end. Freed only by `Hunk_FreeToLowMark`. |
| `Hunk_HighAllocName` | Same, from the top. |
| `Hunk_LowMark`, `Hunk_FreeToLowMark`, `Hunk_HighMark`, `Hunk_FreeToHighMark` | Save and restore the stack pointers. |
| `Hunk_TempAlloc(size)` | Scratch space above everything; valid until the next call. |
| `Z_Malloc`, `Z_Free`, `Z_TagMalloc` | Small dynamic blocks in the zone. |
| `Cache_Alloc(&cu, size, name)`, `Cache_Check(&cu)`, `Cache_Free` | Data that may be evicted and reloaded (LRU). |
| `Hunk_Print`, `Cache_Print`, `Z_Print` | Debug listings (`hunk_print` etc. console commands). |

```c
// load a model file into the cache, reload it if it was evicted
cache_user_t cu;
byte *data = Cache_Check (&cu);
if (!data) {
	COM_LoadCacheFile (path, &cu);
	data = cu.data;
}
```

**This port's changes** (to save time and memory on the device): the zone ID guards (`ZONEID`) and the debug
`Z_CheckHeap()` call in `Z_Malloc` were removed, and `Cache_Report` computes in `float`.

---

## `cvar.h` / `cvar.c`

Console variables: named floats with a string form.

```c
typedef struct cvar_s {
	char *name;  char *string;
	qboolean archive;       // saved to config.cfg
	qboolean server;        // notifies players when changed
	float value;
	struct cvar_s *next;
} cvar_t;
```

```c
cvar_t crank_speed = {"crank_speed", "1.4", true};      // name, default, archive
Cvar_RegisterVariable (&crank_speed);                    // once, in an init function
if (crank_speed.value > 2) { ... }                       // read in place
Cvar_SetValue ("crank_speed", 1.0f);                     // set from C
```

| Function | Purpose |
| --- | --- |
| `Cvar_RegisterVariable(var)` | Add to the list; evaluates `string` into `value`. Refuses names that clash with a command. |
| `Cvar_Set(name, string)` / `Cvar_SetValue(name, float)` | Change a variable; notifies clients for `server` cvars. |
| `Cvar_VariableValue(name)` / `Cvar_VariableString(name)` | Look up by name (0 / `""` if missing). |
| `Cvar_FindVar`, `Cvar_CompleteVariable` | Find, tab-complete. |
| `Cvar_Command()` | Called by `Cmd_ExecuteString` for a line that names a variable: prints or sets it. |
| `Cvar_WriteVariables(FILE *)` | Writes `name "value"` for every archived cvar (used for `config.cfg`). |

At the console: `crank_speed` prints it, `crank_speed 2` sets it. **Changes:** `Cvar_VariableString` returns `""`
directly; `Cvar_SetValue` casts to `double` for `sprintf`.

---

## `cmd.h` / `cmd.c`

The command buffer and the command interpreter.

- **Buffer.** `Cbuf_AddText("text\n")` appends, `Cbuf_InsertText` prepends, `Cbuf_Execute()` runs every complete line
  through `Cmd_ExecuteString`. `wait` (`Cmd_Wait_f`) delays the remaining commands one frame.
- **Dispatch.** `Cmd_ExecuteString(text, src)` tokenises the line (`Cmd_TokenizeString`), then tries, in order: a registered
  command, an alias, a cvar (`Cvar_Command`); otherwise it prints `Unknown command`. `Cmd_Argc()`, `Cmd_Argv(i)`,
  `Cmd_Args()` give the tokens. Commands that belong to the server (`god`, `noclip`, `say`, …) call
  `Cmd_ForwardToServer()` themselves when typed on a client that is not the server.
- **Registering.** `Cmd_AddCommand("name", function)`.
- **Built-in commands.** `exec <file>` (`Cmd_Exec_f`), `echo`, `alias`, `stuffcmds`, `wait`.
- **Port changes to `exec`.** Two, both for the 2021 re-release's `pak0.pak` (see [Game data](../port/game-data.md)):
  `exec default.cfg` falls back to a [built-in copy](#built-in-defaultcfg-defaultcfgh) when the pak has no such file, and
  the file's text is followed by a newline so that a script without a final newline cannot swallow the next queued command.
- **Sources.** `cmd_source` is `src_command` (console or key binding) or `src_client` (a client sent it over the net; commands may refuse).

```c
void Host_Kill_f (void) { /* ... */ }
Cmd_AddCommand ("kill", Host_Kill_f);

Cbuf_AddText ("map e1m1\n");           // queued, runs in Cbuf_Execute this frame
Cbuf_InsertText ("exec quake.rc\n");   // runs before anything already queued
```

`Cmd_Exec_f` as changed by this port:

```c
mark = Hunk_LowMark ();
f = (char *)COM_LoadHunkFile (Cmd_Argv(1));
if (!f)
{
	// the re-release's pak0.pak has no default.cfg (its engine has it built in), and without
	// it nothing is bound: use ours (defaultcfg.h)
	if (!Q_strcmp (Cmd_Argv(1), "default.cfg"))
	{
		Con_Printf ("execing default.cfg (built in)\n");
		Cbuf_InsertText ((char *)default_cfg);
		return;
	}
	Con_Printf ("couldn't exec %s\n",Cmd_Argv(1));
	return;
}
Con_Printf ("execing %s\n",Cmd_Argv(1));

// the file may not end in a newline (the re-release's quake.rc does not), which would glue
// its last command to whatever is queued behind it
Cbuf_InsertText ("\n");
Cbuf_InsertText (f);
Hunk_FreeToLowMark (mark);
```

### Built-in `default.cfg` (`defaultcfg.h`)

A `static const char default_cfg[]` with the same commands as the shareware pak's `default.cfg`, included only by `cmd.c`.
`quake.rc` runs `exec default.cfg` first, then `config.cfg`, so these are the defaults the saved options are applied on top of.

| Group | Content |
| --- | --- |
| Reset | `unbindall` (so *Reset defaults* in the Options menu also clears anything else) |
| Movement and buttons | `UPARROW` `+forward`, `DOWNARROW` `+back`, `LEFTARROW` `+left`, `RIGHTARROW` `+right`, `CTRL` `+attack`, `SPACE` and `ENTER` `+jump`, `,` `+moveleft`, `.` `+moveright`, `ALT` `+strafe`, `SHIFT` `+speed`, look keys |
| Weapons | `1`–`8` and `0` `impulse n`, `/` `impulse 10` |
| Menus and screen | `ESCAPE` `togglemenu`, function keys, `PAUSE`, `~` and `` ` `` `toggleconsole`, `+` `=` `-` size keys |
| Mouse | `MOUSE1` `+attack`, `MOUSE2` `+forward`, `MOUSE3` and `\` `+mlook` |
| Default cvars | `viewsize 100`, `gamma 1.0`, `volume 0.7`, `sensitivity 3` |

On the Playdate the buttons reach these bindings as keys: [`poll_input`](../port/playdate.md#input-mapping) sends `K_UPARROW`,
`K_CTRL`, … and Quake looks the command up in `keybindings[key]`.

---

## `crc.h` / `crc.c`

A 16-bit CRC (CCITT, polynomial 0x1021) over bytes. It is used in two places: `PR_LoadProgs` computes `pr_crc` over
`progs.dat` so a client can check it matches the server's, and `common.c` uses it to derive the per-packet sequence bytes
of the multiplayer anti-tamper check.

```c
unsigned short crc;
CRC_Init (&crc);
for (i = 0; i < len; i++) CRC_ProcessByte (&crc, data[i]);
return CRC_Value (crc);
```

---

## `mathlib.h` / `mathlib.c`

Vector and matrix math. `vec_t` is `float`, `vec3_t` is `float[3]`.

```c
vec3_t a, b, c;
VectorSubtract (b, a, c);            // macros: no call overhead
VectorMA (start, 2048.0f, fwd, end); // end = start + 2048 * fwd
if (DotProduct (dir, fwd) < sv_aim.value) { ... }
VectorNormalize (dir);               // returns the original length
```

| Function / macro | Purpose |
| --- | --- |
| `DotProduct`, `VectorAdd`, `VectorSubtract`, `VectorCopy` | Macros. The function forms (`_DotProduct`, …) exist for use as function pointers. |
| `VectorMA`, `VectorScale`, `VectorInverse`, `VectorCompare`, `VectorNormalize`, `Length`, `CrossProduct` | Vector operations. |
| `AngleVectors(angles, fwd, right, up)` | Euler angles (pitch, yaw, roll) → basis vectors. The Playdate renderer has a faster `PDR_AngleVectors`. |
| `R_ConcatRotations`, `R_ConcatTransforms` | 3×3 and 3×4 matrix multiply. |
| `BoxOnPlaneSide`, `BOX_ON_PLANE_SIDE` | Which side(s) of a plane an axis-aligned box is on (1, 2 or 3). The macro handles axial planes inline. |
| `anglemod(a)` | Wrap an angle to [0, 360) using 16-bit fixed point. |
| `IS_NAN(x)` | Bit test for NaN. |
| `bound(a, b, c)`, `qmin`, `qmax`, `qclamp` | Clamping helpers. |
| `PerpendicularVector`, `ProjectPointOnPlane`, `GreatestCommonDivisor` | Utilities. |

**Changes:** the x86 assembly versions (`math.s`) were dropped; `M_PI` is a `float`.

---

## `wad.h` / `wad.c`

A **WAD2** archive: a header (`wadinfo_t`), then lumps described by `lumpinfo_t` (name, position, size, type, compression).
Quake keeps its 2D pictures (status bar, menu text, console font) in `gfx.wad`.

```c
W_LoadWadFile ("gfx.wad");                           // once, in Host_Init
qpic_t *p = W_GetLumpName ("num_0");                  // a lump by name
byte   *conchars = W_GetLumpName ("conchars");        // the console font
```

`qpic_t` is a width/height pair followed by raw palette-indexed pixels. `W_CleanupName` lower-cases a name and pads it
with zeros to the 16-byte lump-name length, `W_GetLumpinfo` finds a directory entry, `W_GetLumpNum` fetches by index,
and `SwapPic` fixes the byte order of a picture's header.

---

## `nonintel.c`

The original provided `R_Surf8Patch`, `R_Surf16Patch` and `R_SurfacePatch` here for non-x86 builds; they patch
x86 assembly code at run time and are empty on other CPUs. **This port compiles them unconditionally** (the
`#if !id386` guard was removed along with the x86 code), so the linker is satisfied.
