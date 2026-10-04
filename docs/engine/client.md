# Client

[← Documentation index](../README.md) · [Source index](../source-index.md)

The client turns *player intent* (keys, the crank) into movement commands, turns *server messages* into
a picture of the world, and plays demos. In single player the "server" is in the same program, connected through a
loopback driver ([`net_loop.c`](network.md#net_loopc)), so the same message path is used as for a network game.

```
keys / crank ─► Key_Event ─► +forward / +attack ... (kbutton_t) ─► CL_SendCmd ─► usercmd_t ─┐
                                                                                            ▼
   screen ◄─ V_RenderView ◄─ CL_RelinkEntities ◄─ CL_ParseServerMessage ◄─ net_message ◄─ server
```

| File | Role |
| --- | --- |
| [`client.h`](#clienth) | Client state structs and shared declarations |
| [`protocol.h`](#protocolh) | Network protocol constants (`svc_*`, `clc_*`, `U_*`, `SU_*`, `TE_*`) |
| [`cl_main.c`](#cl_mainc) | Connection, signon, entity interpolation, dynamic lights |
| [`cl_parse.c`](#cl_parsec) | Decodes server messages |
| [`cl_input.c`](#cl_inputc) | Button state, `+`/`-` commands, building `usercmd_t` |
| [`cl_demo.c`](#cl_democ) | Demo recording, playback and `timedemo` |
| [`cl_tent.c`](#cl_tentc) | Temporary entities (explosions, beams, sparks) |
| [`view.h` / `view.c`](#viewh--viewc) | The camera: bob, roll, kick, colour shifts, gamma |
| [`chase.c`](#chasec) | Third-person chase camera |
| [`keys.h` / `keys.c`](#keysh--keysc) | Key codes, bindings, key dispatch |

---

## `client.h`

The two big structs and the shared declarations.

- **`client_static_t cls`** persists across connections: connection `state` (`ca_disconnected`,
  `ca_connected`), the `signon` stage (0 to `SIGNONS` = 4), the demo loop (`demonum`, `demos[]`, `demoplayback`,
  `timedemo`, `demofile`), the `netcon` socket, and `message` (the buffer being written to the server).
- **`client_state_t cl`** is wiped at every signon: `stats[]`, `items` (bit flags), `viewangles`, `velocity`,
  `punchangle`, the colour shifts (`cshifts[NUM_CSHIFTS]`), time (`time`, `oldtime`, `mtime[2]`), the precache tables
  (`model_precache[]`, `sound_precache[]`), `worldmodel`, `viewentity`, `viewent` (the weapon), `scores`.
- Entity arrays: `cl_entities[MAX_EDICTS]`, `cl_static_entities[128]`, `cl_temp_entities[64]`, `cl_dlights[32]`,
  `cl_lightstyle[64]`, `cl_beams[24]`, `cl_efrags[640]`; `cl_visedicts[]` is rebuilt every frame.
- `usercmd_t` is the movement command: view angles plus `forwardmove`, `sidemove`, `upmove`.
- `dlight_t` is a dynamic light: `origin`, `radius`, `die` (time to stop), `decay`, `minlight`, `key` (so an entity reuses its light).

```c
// reading the client's world from other code (autofire.c does this)
AngleVectors (cl.viewangles, fwd, right, up);
if (cl.items & IT_AXE) { ... }
int health = cl.stats[STAT_HEALTH];
```

The port adds one cvar here: `cl_autofire` (declared in `client.h`, defined in `cl_input.c`).

---

## `protocol.h`

Constants only. The protocol is Quake's `PROTOCOL_VERSION` 15.

- **Server → client** (`svc_*`): `svc_serverinfo`, `svc_setview`, `svc_time`, `svc_print`, `svc_stufftext`, `svc_setangle`,
  `svc_lightstyle`, `svc_updatename/frags/colors`, `svc_clientdata`, `svc_sound`, `svc_particle`, `svc_damage`,
  `svc_spawnstatic`, `svc_spawnbaseline`, `svc_temp_entity`, `svc_setpause`, `svc_signonnum`, `svc_centerprint`,
  `svc_killedmonster`, `svc_foundsecret`, `svc_intermission`, `svc_finale`, `svc_cdtrack`, …
- **Client → server** (`clc_*`): `clc_nop`, `clc_disconnect`, `clc_move`, `clc_stringcmd`.
- **Entity update bits** (`U_ORIGIN1`, `U_ANGLE2`, `U_FRAME`, `U_MODEL`, `U_EFFECTS`, …): which fields of an entity changed.
  A byte with the high bit set is a "fast update": the low 7 bits are the first flags.
- **Player update bits** (`SU_VIEWHEIGHT`, `SU_PUNCH1`, `SU_VELOCITY1`, `SU_ITEMS`, `SU_WEAPON`, …) for `svc_clientdata`.
- **Temp entities** (`TE_SPIKE`, `TE_EXPLOSION`, `TE_LIGHTNING1…3`, `TE_TELEPORT`, `TE_BEAM`, …).

```c
// a server message is a stream of [command byte][payload]; fast updates reuse the high bit
cmd = MSG_ReadByte ();
if (cmd & U_SIGNAL)  { CL_ParseUpdate (cmd & 127); continue; }     // entity delta
switch (cmd) { case svc_print: Con_Printf ("%s", MSG_ReadString ()); break; /* ... */ }
```

---

## `cl_main.c`

Connection management and the per-frame client update.

| Function | Purpose |
| --- | --- |
| `CL_Init` | Registers the client cvars (`_cl_name`, `_cl_color`, `cl_shownet`, `cl_nolerp`, `lookspring`, `lookstrafe`, `sensitivity`, `m_pitch`, `m_yaw`, `m_forward`, `m_side`, `cl_autofire`) and commands (`entities`, `disconnect`, `record`, `stop`, `playdemo`, `timedemo`); calls `CL_InitInput` and `CL_InitTEnts`. |
| `CL_EstablishConnection(host)` | Opens a socket (`NET_Connect`); `"local"` connects to the in-process server. |
| `CL_SignonReply` | Answers each signon stage: `prespawn` → `name` / `color` → `spawn` → `begin`. |
| `CL_Disconnect`, `CL_ClearState` | Close the connection and wipe `cl`. |
| `CL_ReadFromServer` | Advances `cl.time` by `host_frametime`; reads messages (`CL_GetMessage` → `CL_ParseServerMessage`) until none are left (`Host_Error` if the connection is lost); then `CL_RelinkEntities`, `CL_UpdateTEnts`. |
| `CL_RelinkEntities` | Interpolates every entity between the last two server updates (`CL_LerpPoint`), turns muzzle-flash and brightlight effects into dynamic lights, spawns rocket/grenade/blood trails (`R_RocketTrail`), and builds `cl_visedicts[]` for the renderer. |
| `CL_LerpPoint` | How far between the last two server packets this frame is (0…1). |
| `CL_AllocDlight(key)` / `CL_DecayLights` | Reuse or allocate a light by key; shrink lights over time. |
| `CL_SendCmd` | Build a `usercmd_t` (`CL_BaseMove`, `IN_Move`) and send it with `CL_SendMove`. |
| `CL_NextDemo` | Start the next demo of the title-screen loop. |
| `CL_PrintEntities_f` | `entities` console command. |

```c
// every frame, Host_Frame ends up here (host.c):
if (cls.state == ca_connected)
	CL_ReadFromServer ();       // apply what the server said, interpolate, relink

// a light for a muzzle flash: reuse the entity's light slot, it fades out in 0.1 s
dlight_t *dl = CL_AllocDlight (i);
VectorCopy (ent->origin, dl->origin);
dl->radius = 200 + (rand() & 31);
dl->die = cl.time + 0.1f;
```

**What this port changed:** `CL_SignonReply` uses a `static` 8 KB buffer (stack space is scarce); constants became `float`
literals; the dedicated-server check was dropped; `cl_autofire` is registered here.

---

## `cl_parse.c`

Decodes one server message at a time (`net_message`).

| Function | Purpose |
| --- | --- |
| `CL_ParseServerMessage` | The main loop: reads a command byte and dispatches (`svc_*`), handling fast entity updates. |
| `CL_ParseServerInfo` | Protocol check, maximum clients, level name; **loads every model and sound** the server lists (`Mod_ForName`, `S_PrecacheSound`) and calls `R_NewMap`. |
| `CL_ParseUpdate(bits)` | An entity delta: position, angles, model, frame, colormap, skin, effects; sets up the interpolation (`msg_origins`). |
| `CL_ParseBaseline(ent)` | The initial state of an entity. |
| `CL_ParseClientdata(bits)` | The player's own stats, view height, punch, velocity, items, weapon. |
| `CL_ParseStatic` / `CL_ParseStaticSound` | Torches and flames (`cl_static_entities`), ambient sounds. |
| `CL_ParseStartSoundPacket` | Decodes a sound event and calls `S_StartSound`. |
| `CL_EntityNum(n)` | The entity slot, growing `cl.num_entities` as needed. |
| `CL_NewTranslation(slot)` | Re-maps a player's shirt and pants colours into a per-player palette. |
| `CL_KeepaliveMessage` | Sends a nop while a long load blocks the net. |

```c
case svc_updatestat:                     // [byte index][long value]
	i = MSG_ReadByte ();
	cl.stats[i] = MSG_ReadLong ();
	break;
case svc_lightstyle:                     // [byte style][string pattern like "mmnmmommommnonmmonqnmmo"]
	i = MSG_ReadByte ();
	Q_strcpy (cl_lightstyle[i].map, MSG_ReadString ());
	break;
```

**Changes:** large local arrays (`olddata`, `model_precache`, `sound_precache`) became `static`, again to spare the stack.

---

## `cl_input.c`

Turns key state into movement.

- **Buttons.** A `kbutton_t` records which keys hold a button down (`down[2]`) and its state bits. Every `+name`/`-name` command pair
  (`+forward`, `-forward`, `+attack`, `+jump`, `+left`, `+moveleft`, `+speed`, `+strafe`, `+lookup`, …) calls `KeyDown` /
  `KeyUp` on the matching button; `impulse N` sets `in_impulse`.
- **`CL_KeyState(key)`** returns the fraction of the frame the button counts as held: `1.0` held the whole frame, `0.5` pressed this
  frame and still held, `0.75` released and pressed again this frame, `0.25` pressed and released this frame, `0` otherwise. So a quick tap still
  moves the player a little.
- **`CL_AdjustAngles`** turns the view with `+left` / `+right` / `+lookup` / `+lookdown` (speed `cl_yawspeed` / `cl_pitchspeed`, faster with `+speed`).
- **`CL_BaseMove(cmd)`** sets `forwardmove`, `sidemove`, `upmove` from the buttons and speed cvars (`cl_forwardspeed`, `cl_backspeed`, `cl_sidespeed`, `cl_upspeed`; ×`cl_movespeedkey` while running).
- **`CL_SendMove(cmd)`** builds a `clc_move` message: the client's last server timestamp (`cl.mtime[0]`, so the server can measure ping), the three
  view angles, `forwardmove`/`sidemove`/`upmove` as shorts, a button byte (bit 0 attack, bit 1 jump) and the impulse byte. It is sent unreliably through
  `NET_SendUnreliableMessage`; the first two messages after connecting are dropped because they may hold leftover input from the last level.

```c
// how the Playdate port moves the player without touching this file:
//   main.c:     pdq_push_key(K_UPARROW, 1)  →  Key_Event  →  binding "+forward"  →  IN_ForwardDown  →  in_forward.state |= 1
//   CL_BaseMove: cmd->forwardmove += cl_forwardspeed.value * CL_KeyState (&in_forward);
```

```c
// port: cl_autofire is an archived cvar; autofire.c reads it
cvar_t cl_autofire = {"cl_autofire", "1", true};   // fire when an enemy is in the crosshair
```

The port also sets `cl_forwardspeed` and `cl_backspeed` to 400 at start-up, so the player always runs
(`apply_run()` in [`main.c`](../port/playdate.md#mainc)). `CL_KeyState` and `CL_AdjustAngles` were touched only to add braces and
cache `host_frametime` in a local.

---

## `cl_demo.c`

Demo (`.dem`) recording and playback, and the benchmark mode.

- **File format:** the first line is the forced CD track (`-1`), then for every frame: message length (`int`), view angles (3 `float`s),
  then the raw server message.
- `CL_PlayDemo_f` opens a demo from the pak (`COM_FOpenFile`) and sets `cls.demoplayback`; `CL_GetMessage` reads the next frame
  instead of from the network (pacing it to `cl.time` unless `timedemo`); `CL_StopPlayback` ends it.
- `CL_Record_f` / `CL_Stop_f` / `CL_WriteDemoMessage` record. `CL_TimeDemo_f` plays as fast as possible, rendering every frame, and
  `CL_FinishTimeDemo` prints `N frames T seconds F fps`.

```
playdemo demo1        play id1's pak demo
timedemo demo1        benchmark; at the end prints "<N> frames <T> seconds <F> fps"
record mydemo e1m1    record a session
```

`timedemo` is what the profiler's `-DPD_BENCH=ON` automates ([`pdprof.c`](../port/playdate.md#pdprofc)). Demo files go through
stdio (`fopen`, `fread`, `fwrite`), which the Playdate board redirects to its file API ([`pd_stdio.c`](../port/playdate.md#pd_stdioc)).

---

## `cl_tent.c`

Temporary entities: effects that live for a short time and are not entities on the server.

| Function | Purpose |
| --- | --- |
| `CL_InitTEnts` | Precache the lightning and beam models (`progs/bolt.mdl`, `bolt2.mdl`, `bolt3.mdl`, `beam.mdl`) and the sounds. |
| `CL_ParseTEnt` | Handles `svc_temp_entity`: spikes, gunshots, explosions (particles, dynamic light, sound), teleport, lava splash, lightning (`CL_ParseBeam`). |
| `CL_ParseBeam(model)` | Adds or updates a beam in `cl_beams[]` keyed by its entity. |
| `CL_NewTempEntity` | Takes a slot from `cl_temp_entities[]` and adds it to `cl_visedicts`. |
| `CL_UpdateTEnts` | Each frame: expire beams, and lay out the lightning bolt model segments along each beam (`atan2f` for yaw and pitch). |

```c
case TE_EXPLOSION:                         // rocket explosion
	pos[0] = MSG_ReadCoord (); pos[1] = MSG_ReadCoord (); pos[2] = MSG_ReadCoord ();
	R_ParticleExplosion (pos);             // r_part.c
	dl = CL_AllocDlight (0);
	VectorCopy (pos, dl->origin);
	dl->radius = 350;  dl->die = cl.time + 0.5f;  dl->decay = 300;
	S_StartSound (-1, 0, cl_sfx_r_exp3, pos, 1, 1);
	break;
```

---

## `view.h` / `view.c`

The camera and the colour effects on the whole screen.

- **`V_RenderView`** is the frame's entry point to rendering: it calls `V_CalcRefdef` to set `r_refdef` (view origin and angles), `R_PushDlights`, `R_RenderView` and (if enabled) the crosshair.
- **`V_CalcRefdef`** puts the eye at `viewheight`, adds `V_CalcBob` (walking bob, `cl_bob`, `cl_bobcycle`), `V_CalcRoll` (lean into strafing, `cl_rollangle`), `V_AddIdle` (idle sway), the damage kick (`v_kickpitch/roll`), and positions the weapon model (`CalcGunAngle`).
- **Pitch drift.** `V_StartPitchDrift` / `V_StopPitchDrift` / `V_DriftPitch` slowly recentre the view when the player is walking (`lookspring`, `v_centermove`, `v_centerspeed`).
- **Colour shifts.** `cl.cshifts[]` (contents, damage, bonus, powerup) blend into a full-screen tint: `V_ParseDamage` (`svc_damage`), `V_BonusFlash_f`, `V_cshift_f`, `V_CalcPowerupCshift`, `V_SetContentsColor`, `V_CalcBlend` → `V_UpdatePalette` builds a new palette through the gamma table and calls `VID_SetPalette`.
- **Gamma.** `BuildGammaTable(g)`, `V_CheckGamma`; cvar `gamma` (`v_gamma`).
- Cvars: `crosshair`, `cl_crossx/y`, `scr_ofsx/y/z`, `cl_bob*`, `v_kick*`, `v_idlescale`, `lcd_x`, `lcd_yaw`.

```c
// V_UpdatePalette: blend each active shift into the 256-colour palette, then hand it to the video driver
for (j = 0; j < NUM_CSHIFTS; j++) {
	r += (cl.cshifts[j].percent * (cl.cshifts[j].destcolor[0] - r)) >> 8;
	/* g, b likewise */
}
VID_SetPalette (pal);
```

**What this port changed:**

- Under water, slime and lava Quake blends the whole palette 50–60 % toward the liquid's colour. On the 1-bit display only the luminance survives, which would squeeze the picture into the midtones, so with `QEMBD_PLAYDATE` the contents shift is **skipped** (`if (j == CSHIFT_CONTENTS) continue;`). The underwater warp and the damage, pickup and powerup flashes are unchanged.
- `VID_ShiftPalette` was replaced by `VID_SetPalette`; math calls became single-precision (`fabsf`, `sqrtf`, `sinf`, `powf`); `host_frametime` is cached in a local.
- `gl_cshiftpercent` is only registered for `GLQUAKE`.

---

## `chase.c`

A third-person camera, off by default.

```c
cvar_t chase_back   = {"chase_back", "100"};   // distance behind the player
cvar_t chase_up     = {"chase_up", "16"};
cvar_t chase_right  = {"chase_right", "0"};
cvar_t chase_active = {"chase_active", "0"};   // set to 1 to enable
```

`Chase_Update` traces from the player toward a point `chase_back` behind them (`TraceLine`, which wraps
`SV_RecursiveHullCheck` to stop at walls) and aims the camera at the player's aim point. `Chase_Init` registers the cvars; `Chase_Reset` is an empty hook
for respawning and teleporting.

---

## `keys.h` / `keys.c`

Everything between "a key was pressed" and the right consumer.

- **Key codes.** `K_TAB` 9, `K_ENTER` 13, `K_ESCAPE` 27, `K_SPACE` 32, then `K_UPARROW`…`K_RIGHTARROW` (128–131), `K_ALT`, `K_CTRL`, `K_SHIFT`, `K_F1`…`K_F12`,
  `K_INS`…`K_END`, `K_PAUSE`, mouse buttons (`K_MOUSE1…3`), joystick and aux keys, wheel up/down. Printable keys are passed as lower-case ASCII.
- **Destination.** `key_dest` is `key_game`, `key_console`, `key_message` (typing a chat line) or `key_menu`. `Key_Event(key, down)` sends the key to
  the console (`Key_Console`), chat (`Key_Message`), the menu (`M_Keydown`) or the binding for the key.
- **Bindings.** `keybindings[256]` holds a command string per key. `Key_SetBinding(keynum, "cmd")`, the `bind`/`unbind`/`unbindall` commands
  (`Key_Bind_f` …), `Key_KeynumToString` / `Key_StringToKeynum` (e.g. `"CTRL"`, `"UPARROW"`), `Key_WriteBindings(FILE *)` for `config.cfg`.
  A binding that starts with `+` generates `+cmd <keynum>` on press and `-cmd <keynum>` on release.
- `Key_ClearStates` releases everything (e.g. when the console opens).

```c
Key_SetBinding (',', "+moveleft");        // used by the Playdate port for crank-out strafing
Key_SetBinding ('.', "+moveright");

// button presses from the board arrive here (via Sys_SendKeyEvents):
Key_Event (K_CTRL, true);                 // "+attack 133" if CTRL is bound to +attack
```

Playdate buttons send different keys in the game and in menus ([`main.c`](../port/playdate.md#mainc)): A is `K_CTRL` in the game and `K_ENTER` in a menu.
This file is unmodified from the original except that `Key_WriteBindings` is not called by `Host_WriteConfiguration` on the Playdate
([`host.c`](core.md#hostc)).
