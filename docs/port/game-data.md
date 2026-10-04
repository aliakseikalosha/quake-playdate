# Game data: the shareware and the re-release `pak0.pak`

[← Documentation index](../README.md) · [Source index](../source-index.md)

The game data is not part of the source tree: `port/boards/playdate/Source/id1/pak0.pak` (and, for the
release build, `pak0_demo.pak` next to it; see [which `pak0.pak` the `.pdx` gets](../build-system.md#which-pak0pak-the-pdx-gets)).
Two kinds of data work:

| | Shareware (`pak0_demo.pak`) | 2021 re-release (`pak0.pak`) |
| --- | --- | --- |
| Size | 18.7 MB, 339 files | 179.6 MB, 1071 files (plus the music, `id1/music/*`: 83 MB of `.ogg`, which is not used, or `QuakeNN.wav`, see [`cd_pd.c`](playdate.md#cd_pdc)) |
| Maps | `start`, `e1m1`–`e1m8` | `start`, `e1m1`–`e4m8`, `end`, `dm1`–`dm8`, `base32b`, `death32c`, `test/*` |
| `default.cfg` | **in the pak** | **not in the pak** (the re-release engine has it built in) |
| `quake.rc` | `exec default.cfg`, …, `startdemos` | the same, **no final newline**, plus `scr_usekfont 1` and `alias quickswitch_*` |
| `progs.dat` | original QuakeC (CRC 5927) | same CRC, but sets a `campaign` cvar and gives gibs `MOVETYPE_BOUNCEMISSILE` |

The engine was first written and measured against the shareware data. The re-release data is accepted by the
same `COM_LoadPackFile` (it is a normal `PACK` file; the extra `bots/`, `tactile/` files are never opened, and the music is not in the pak but in `id1/music/`),
but it needed five changes before the game was playable. This page lists them, each as symptom → cause → fix.

## 1. The D-pad did nothing (missing `default.cfg`)

**Symptom.** With the re-release pak: D-pad up/down did not walk, and with the crank docked (D-pad left/right turn)
the D-pad did nothing at all. The crank still turned the view, and with the crank out the D-pad still strafed.

**Cause.** Every Playdate button is turned into a Quake key in [`poll_input`](playdate.md#input-mapping) (`K_UPARROW`,
`K_CTRL`, `K_SPACE`, …), and Quake runs the command *bound* to that key. The bindings come from `default.cfg`, which `quake.rc`
executes. The re-release pak has no such file, so `exec default.cfg` printed `couldn't exec default.cfg` and nothing was bound.
What still worked was bound some other way:

```c
// main.c, after qembd_init(): the only two bindings the port makes itself, which is why strafing worked
Key_SetBinding(',', "+moveleft");
Key_SetBinding('.', "+moveright");

// main.c, poll_input(): the crank bypasses bindings, it edits the view angle directly
cl.viewangles[YAW] -= qembd_pd->system->getCrankChange() * crank_speed.value;
```

**Fix.** [`winquake/defaultcfg.h`](../../winquake/defaultcfg.h) holds a built-in copy of the shareware `default.cfg`
(character controls, weapon impulses, menus, mouse buttons and the default cvars), and `Cmd_Exec_f` uses it
when the pak has no `default.cfg`:

```c
// winquake/defaultcfg.h (abridged)
static const char default_cfg[] =
	"unbindall\n"
	"bind , +moveleft\n"
	"bind . +moveright\n"
	"bind CTRL +attack\n"
	"bind UPARROW +forward\n"
	"bind DOWNARROW +back\n"
	"bind LEFTARROW +left\n"
	"bind RIGHTARROW +right\n"
	"bind SPACE +jump\n"
	"bind ESCAPE togglemenu\n"
	/* ... weapon impulses, function keys, mouse buttons ... */
	"viewsize 100\n"
	"gamma 1.0\n"
	"volume 0.7\n"
	"sensitivity 3\n";

// winquake/cmd.c, Cmd_Exec_f
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
```

A pak that *has* a `default.cfg` (the shareware one) still wins, so that data behaves exactly as before. The Options menu's
*Reset defaults* runs `exec default.cfg` too and so gets the same fallback.
Key bindings are never saved to `config.cfg` on the Playdate (see [Engine core](../engine/core.md#saved-options-configcfg)),
so no stale bindings from an earlier run can hide the fix.

## 2. Console spam: `Cvar_Set: variable campaign not found`

**Symptom.** The console (and `logToConsole`) filled with `Cvar_Set: variable campaign not found`, about 140 lines per
second of play in the host test, and `Unknown command "scr_usekfont"` once at start-up.

**Cause.** The re-release's QuakeC calls `cvar_set("campaign", ...)` while running and its `quake.rc` sets `scr_usekfont 1`
(the re-release's Unicode font switch). Both are cvars of the *re-release engine*. `Cvar_Set` complains when a cvar is not
registered, and every line costs a `logToConsole` call.

**Fix.** Register them in `Host_InitLocal` as cvars that nothing reads:

```c
// winquake/host.c
// Set by the 2021 re-release's pak0.pak (campaign by its progs.dat on every level, scr_usekfont by its
// quake.rc) and used by nothing here; registered so that they are not reported again and again
// ("Cvar_Set: variable campaign not found" every frame, "Unknown command").
static cvar_t	campaign = {"campaign","0"};
static cvar_t	scr_usekfont = {"scr_usekfont","0"};

void Host_InitLocal (void)
{
	...
	Cvar_RegisterVariable (&temp1);
	Cvar_RegisterVariable (&campaign);
	Cvar_RegisterVariable (&scr_usekfont);
```

Two other messages remain and are harmless: `'fog' is not a field` and `'property 1' is not a field` while a map's entities are
parsed (the re-release maps carry keys the original `progs.dat` has no field for; `ED_ParseEpair` skips them).

## 3. `quake.rc` has no final newline

**Symptom.** A command queued *before* the first frame (the host test did `map e1m1`) never ran.

**Cause.** `Cmd_Exec_f` inserts the file's text at the front of the command buffer. The re-release `quake.rc` ends with
`alias quickswitch_left "switchweapon 6 7"` and no `\n`, so the next queued command was glued onto that line:

```
alias quickswitch_left "switchweapon 6 7"map e1m1      <- one line; map e1m1 is swallowed by the alias
```

The game itself is not affected (nothing is queued ahead of `exec quake.rc` on the device), but any queued command would be.

**Fix.** `exec` inserts a newline first, so the file's text always ends at a line boundary:

```c
// winquake/cmd.c, Cmd_Exec_f
Con_Printf ("execing %s\n",Cmd_Argv(1));

// the file may not end in a newline (the re-release's quake.rc does not), which would glue
// its last command to whatever is queued behind it
Cbuf_InsertText ("\n");
Cbuf_InsertText (f);
```

`Cbuf_InsertText` puts text at the front, so the order in the buffer is `file text`, `\n`, `what was queued`.

## 4. `SV_Physics: bad movetype 11` (gibs)

**Symptom.** The game stopped with *Quake stopped: SV_Physics: bad movetype 11*, for example at the start of `e4m8`
(`The Nameless City`) and whenever a monster is gibbed.

**Cause.** The re-release QuakeC throws gibs (`progs/gib1.mdl`) with `MOVETYPE_BOUNCEMISSILE` (11, "bounce without gravity").
`server.h` defines that constant and `SV_Physics_Toss` handles it, but only inside `#ifdef QUAKE2`, which this port does not build.
Anything with an unknown movetype is a `Sys_Error`.

**Fix.** Define the constant and handle it as the `QUAKE2` code does, without switching the rest of `QUAKE2` on:

```c
// winquake/server.h
#define	MOVETYPE_BOUNCE			10
#define MOVETYPE_BOUNCEMISSILE	11		// bounce w/o gravity; the re-release's progs.dat uses it (gibs)
#ifdef QUAKE2
#define MOVETYPE_FOLLOW			12		// track movement of aiment
#endif

// winquake/sv_phys.c, SV_Physics: dispatch it to SV_Physics_Toss
else if (ent->v.movetype == MOVETYPE_TOSS
|| ent->v.movetype == MOVETYPE_BOUNCE
|| ent->v.movetype == MOVETYPE_BOUNCEMISSILE
|| ent->v.movetype == MOVETYPE_FLY
|| ent->v.movetype == MOVETYPE_FLYMISSILE)
	SV_Physics_Toss (ent);

// SV_Physics_Toss: no gravity, a stronger bounce, and it keeps bouncing off floors like BOUNCE
if (ent->v.movetype != MOVETYPE_FLY
&& ent->v.movetype != MOVETYPE_BOUNCEMISSILE
&& ent->v.movetype != MOVETYPE_FLYMISSILE)
	SV_AddGravity (ent);
...
if (ent->v.movetype == MOVETYPE_BOUNCE)
	backoff = 1.5f;
else if (ent->v.movetype == MOVETYPE_BOUNCEMISSILE)
	backoff = 2.0f;
else
	backoff = 1;
...
if (ent->v.velocity[2] < 60 || (ent->v.movetype != MOVETYPE_BOUNCE && ent->v.movetype != MOVETYPE_BOUNCEMISSILE))
	/* come to rest: FL_ONGROUND, velocity 0 */
```

## 5. Crash in the Playdate renderer on large levels (`SetupModelCached`)

**Symptom.** A segmentation fault in `SetupSkin` (`pdr_alias.c`) in `e2m6` (`The Dismal Oubliette`); the original
renderer ran the same level fine. The shareware data has no such level.

**Cause.** Alias models live in Quake's *cache*, memory that may be evicted when something else is loaded.
[`PDR_AliasRect`](../engine/renderer-pdr.md#pdr_aliasc) sets up an entity early in the frame and saves the result (including the
`aliashdr_t *` into the cache) for `PDR_DrawAliasModel` to reuse later. In a level with many kinds of monsters and a 7 MB heap
(the device's size), setting up a *different* model in between can evict the first model's cache entry, and the saved pointer then
points at memory another model now owns. The original renderer is not affected because it asks `Mod_Extradata` at the moment
it draws.

**Fix.** Before reusing a saved setup, ask `Mod_Extradata` again (a cheap `Cache_Check` that also reloads an evicted model) and
use the saved setup only if the model is still where it was:

```c
// winquake/pdr_alias.c
static void SetupModelCached (entity_t *e, asetup_t *a)
{
	int		i;

	for (i=0 ; i<numasetups ; i++)
	{
		if (asetup_ent[i] == e)
		{
			// The saved setup points into the model's cache entry, and loading another model
			// since (a full cache, as in the larger retail levels) may have evicted it:
			// Mod_Extradata gives the model back, wherever it is now.
			if (Mod_Extradata (e->model) == (void *)asetup_saved[i].hdr)
			{
				*a = asetup_saved[i];
				return;
			}
			break;
		}
	}
	SetupModel (e, a);
}
```

If the model was evicted and reloaded elsewhere the pointers differ and `SetupModel` redoes the setup; if it is still in place
the saved setup is used exactly as before, so the picture does not change.

## Checking a pak

`tools/hostcheck/run.sh` links whatever pak is in `Source/id1/pak0.pak` (see [Scripts and tools](../tools.md#toolshostcheckhostcheckc)):

```shell
NEW=1 HINPUT=1 tools/hostcheck/run.sh      # bindings exist, D-pad / crank-out strafe / A / B act on the player
NEW=1 HMENU=1  tools/hostcheck/run.sh      # Options menu, config.cfg, save and load
NEW=1 HMAPS=1  tools/hostcheck/run.sh      # start, e1m1-e1m8: crash and limits check
cd tools/hostcheck/out/run && HLOG=1 ../hostcheck 200 | sort | uniq -c | sort -rn | head
                                           # every distinct console message and how often it came
```

The last command is how problems 1 and 2 were found: with the re-release pak it listed `couldn't exec default.cfg`, `Unknown command
"scr_usekfont"` and 941 × `Cvar_Set: variable campaign not found` in 200 frames.
`HMAPS` only loads the shareware map names; to try the re-release levels edit the `maps[]` list in `maps_mode` (a throwaway copy of
`hostcheck.c` is enough). With the changes above `e2m1`, `e2m3`, `e2m6`, `e2m7`, `e3m1`, `e3m4`, `e3m7`, `e4m1`, `e4m5`, `e4m8`,
`dm1`, `dm4`, `dm6` and `end` load and run (turn, walk, fire) without an error. `base32b`, a large deathmatch map only the re-release has,
does not fit the 7 MB heap (`Hunk_Alloc: failed`); it is not part of the single-player game.

> **Not measured on the device.** The checks run on the host with the same 7 MB heap and the same key events the port pushes
> (`Key_Event`), but not on a Playdate.
