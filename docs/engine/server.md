# Server, world and physics

[← Documentation index](../README.md)

The server owns the game: it loads a level, runs the entities (through the [QuakeC VM](quakec.md)), moves them with
Quake's physics, and tells every client what changed. In single player there is one local client, connected through the
[loopback network driver](network.md#net_loopc); the server runs *inside the same process* every frame
(`Host_ServerFrame`).

| File | Role |
| --- | --- |
| [`server.h`](#serverh) | `server_t`, `client_t`, entity constants (`MOVETYPE_*`, `SOLID_*`, `FL_*`, `EF_*`) |
| [`sv_main.c`](#sv_mainc) | Server start-up, client connections, building update messages |
| [`sv_user.c`](#sv_userc) | Reading client input and player movement |
| [`sv_phys.c`](#sv_physc) | Entity physics by movement type |
| [`sv_move.c`](#sv_movec) | Monster movement helpers (`walkmove`, `movetogoal`) |
| [`world.h` / `world.c`](#worldh--worldc) | Collision: the area tree, hulls and `SV_Move` |

```
Host_ServerFrame
 ├─ SV_RunClients      for each client: read its clc_move / commands, SV_ClientThink (apply the command to velocity)
 ├─ SV_Physics         QuakeC StartFrame, then every entity by movetype and its think function (QuakeC);
 │                     players: PlayerPreThink → move → PlayerPostThink (SV_Physics_Client)
 └─ SV_SendClientMessages
       └─ SV_WriteEntitiesToClient (PVS culling) + SV_WriteClientdataToMessage per client
```

---

## `server.h`

```c
typedef struct {                // lives for the whole session
	int maxclients, maxclientslimit;
	struct client_s *clients;    // [maxclients]
	int serverflags;             // episode completion
	qboolean changelevel_issued;
} server_static_t;   extern server_static_t svs;

typedef struct {                // wiped for every level
	qboolean active, paused, loadgame;
	double   time;               // the server's clock
	char     name[64];           // map name, e.g. "e1m1"
	struct model_s *worldmodel;
	char    *model_precache[MAX_MODELS];   char *sound_precache[MAX_SOUNDS];
	char    *lightstyles[MAX_LIGHTSTYLES];
	int      num_edicts, max_edicts;
	edict_t *edicts;             // variable-sized records: use EDICT_NUM(n), NEXT_EDICT(e)
	sizebuf_t datagram, reliable_datagram, signon;
	...
} server_t;   extern server_t sv;
```

`client_t` is one connected player: `active`, `spawned`, `netconnection`, the last `usercmd_t`, a reliable message buffer, `edict`, `name`,
`colors`, `ping_times[16]`, and `spawn_parms[16]` (what the player carries between levels).

Constant groups used by both C and QuakeC:

| Group | Values |
| --- | --- |
| `MOVETYPE_*` | `NONE`, `ANGLENOCLIP`, `ANGLECLIP`, `WALK`, `STEP`, `FLY`, `TOSS`, `PUSH`, `NOCLIP`, `FLYMISSILE`, `BOUNCE` |
| `SOLID_*` | `NOT`, `TRIGGER`, `BBOX`, `SLIDEBOX`, `BSP` |
| `FL_*` flags | `FLY`, `SWIM`, `CLIENT`, `INWATER`, `MONSTER`, `GODMODE`, `NOTARGET`, `ITEM`, `ONGROUND`, `PARTIALGROUND`, `WATERJUMP`, `JUMPRELEASED` |
| `EF_*` effects | `BRIGHTFIELD`, `MUZZLEFLASH`, `BRIGHTLIGHT`, `DIMLIGHT` |
| `DEAD_*`, `DAMAGE_*`, `SPAWNFLAG_NOT_*` | state and difficulty flags |

Cvars declared here: `teamplay`, `skill`, `deathmatch`, `coop`, `fraglimit`, `timelimit`.

```c
// autofire.c reads the live server directly:
if (!sv.active || !sv_player) return 0;
if ((int)e->v.flags & FL_MONSTER && e->v.takedamage && e->v.health > 0) { /* a live monster */ }
for (i = 1, check = NEXT_EDICT (sv.edicts); i < sv.num_edicts; i++, check = NEXT_EDICT (check)) { ... }
```

---

## `sv_main.c`

| Function | Purpose |
| --- | --- |
| `SV_Init` | Registers the server cvars (`sv_maxvelocity`, `sv_gravity`, `sv_friction`, `sv_edgefriction`, `sv_stopspeed`, `sv_maxspeed`, `sv_accelerate`, `sv_idealpitchscale`, `sv_aim`, `sv_nostep`) and builds the `localmodels` names `*1`, `*2`, … used for the brush submodels of a map. |
| `SV_SpawnServer(name)` | Starts a level: clears `sv`, loads the map (`Mod_ForName`), allocates edicts, runs the map's entities (`ED_LoadFromFile`), and runs two frames of physics so things settle. |
| `SV_CheckForNewClients` | Polls `NET_CheckNewConnections` and connects each new client (`SV_ConnectClient`). |
| `SV_SendServerinfo` | Sends protocol, level name, the model/sound precache lists, then the signon baselines. |
| `SV_CreateBaseline` | Records each entity's initial state, so later updates are deltas. |
| `SV_FatPVS(org)` / `SV_AddToFatPVS` | The set of leaves visible from a point, widened by neighbouring leaves. |
| `SV_WriteEntitiesToClient(clent, msg)` | For each entity in the client's PVS, writes only the fields that changed since the baseline (`U_*` bits). |
| `SV_WriteClientdataToMessage` | The player's own stats, view height, punch angle, velocity, weapon (`SU_*` bits). |
| `SV_SendClientDatagram`, `SV_SendClientMessages` | Assemble and send one packet per client each frame. |
| `SV_UpdateToReliableMessages` | Copies changed names, frags and colours to everyone. |
| `SV_StartSound(entity, channel, sample, volume, attenuation)` | Queues a sound in the datagram (called by QuakeC `sound()`). |
| `SV_StartParticle(org, dir, color, count)` | Queues a particle burst. |
| `SV_ModelIndex(name)` | Index of a precached model. |
| `SV_SaveSpawnparms`, `SV_SendReconnect`, `SV_SendNop`, `SV_CleanupEnts`, `SV_ClearDatagram` | Level change and per-frame housekeeping. |

```c
// QuakeC calls  sound (self, CHAN_WEAPON, "weapons/rocket1i.wav", 1, ATTN_NORM);  which ends up here:
SV_StartSound (entity, channel, sample, volume, attenuation);
// ... the datagram reaches the client as svc_sound, then S_StartSound → port/boards/playdate/snd.c
```

**What this port changed:** `SV_SendServerinfo` uses a `static` message buffer; `SV_CreateBaseline` checks the model string with
`G_VALID_STRING` (a QuakeC entity may carry an invalid string offset); the dedicated-server branch in `SV_SendReconnect` was dropped; float literals.

---

## `sv_user.c`

Player input and movement.

- **`SV_RunClients`** (each frame): read this client's messages (`SV_ReadClientMessage`), then `SV_ClientThink`; also `SV_Physics_Client` calls QuakeC `PlayerPreThink` / `PlayerPostThink`.
- **`SV_ReadClientMessage`**: handles `clc_move` (`SV_ReadClientMove` → `host_client->cmd`), `clc_stringcmd` (checked against a whitelist: `status`, `god`, `notarget`, `fly`, `name`, `noclip`, `say`, `say_team`, `tell`, `color`, `kill`, `pause`, `spawn`, `begin`, `prespawn`, `kick`, `ping`, `give`, `ban`; anything else is ignored with a print), `clc_disconnect`.
- **`SV_ClientThink`**: view angles from the command, then one of three moves depending on state:

| Function | When |
| --- | --- |
| `SV_AirMove` | Walking and falling: friction (`SV_UserFriction`), then ground (`SV_Accelerate`) or air (`SV_AirAccelerate`) acceleration towards the wished direction. |
| `SV_WaterMove` | Swimming (`waterlevel` ≥ 2). |
| `SV_WaterJump` | Hopping out of water onto a ledge. |

- `DropPunchAngle` fades the view kick; `SV_SetIdealPitch` computes the slope-following pitch from floor traces.

```c
// SV_Accelerate: Quake's movement model, the same on every platform
currentspeed = DotProduct (velocity, wishdir);
addspeed = wishspeed - currentspeed;
if (addspeed <= 0) return;
accelspeed = sv_accelerate.value * wishspeed * host_frametime;
if (accelspeed > addspeed) accelspeed = addspeed;
for (i = 0; i < 3; i++) velocity[i] += accelspeed * wishdir[i];
```

---

## `sv_phys.c`

Per-entity physics. `SV_Physics()` first runs QuakeC `StartFrame`, then loops over every active edict. Client entities go to `SV_Physics_Client`
(`PlayerPreThink`, the move, `SV_LinkEdict`, `PlayerPostThink`); everything else is dispatched on `movetype`, and any other value is a `Sys_Error`.

| Movetype | Handler |
| --- | --- |
| `MOVETYPE_PUSH` | `SV_Physics_Pusher`: doors, platforms; `SV_PushMove` / `SV_PushRotate` carry things that stand on them |
| `MOVETYPE_NONE` | `SV_Physics_None` (just run its think) |
| `MOVETYPE_NOCLIP` | `SV_Physics_Noclip` |
| `MOVETYPE_STEP` | `SV_Physics_Step`: monsters, with gravity |
| `MOVETYPE_TOSS`, `BOUNCE`, `FLY`, `FLYMISSILE` | `SV_Physics_Toss`: projectiles, items, gibs |

For a player, `SV_Physics_Client` handles `NONE`, `WALK` (gravity, `SV_CheckStuck`, then `SV_WalkMove`, which steps up stairs), `TOSS`/`BOUNCE`, `FLY` and `NOCLIP`.

Building blocks: `SV_FlyMove` (slide along up to four planes), `ClipVelocity`, `SV_PushEntity`, `SV_Impact` (calls the entities' `touch` functions), `SV_CheckVelocity`,
`SV_AddGravity`, `SV_CheckWater`, `SV_CheckStuck`/`SV_TryUnstick`, `SV_RunThink` (calls QuakeC `think` when `nextthink` is due), `SV_WallFriction`.
Tunables: `sv_gravity` 800, `sv_friction` 4, `sv_stopspeed` 100, `sv_maxvelocity` 2000, `sv_nostep`.

```c
qboolean SV_RunThink (edict_t *ent)
{
	float thinktime = ent->v.nextthink;
	if (thinktime <= 0.0F || thinktime > (_sv_time + host_frametime)) return true;   // not yet
	if (thinktime < _sv_time) thinktime = _sv_time;                                  // never in the past
	ent->v.nextthink = 0.0F;
	pr_global_struct->time = thinktime;  pr_global_struct->self = EDICT_TO_PROG (ent);
	PR_ExecuteProgram (ent->v.think);                                                // QuakeC runs here
	return !ent->free;
}
```

**Port changes:** the 600-entry `moved_edict` / `moved_from` arrays in the pusher code are `static` (they would be ~10 KB of stack otherwise); the time is cached as a `float`;
math is single precision.

---

## `sv_move.c`

Helpers for monsters, exposed to QuakeC as builtins (`walkmove`, `movetogoal`, `droptofloor`, `checkbottom`).

| Function | Purpose |
| --- | --- |
| `SV_movestep(ent, move, relink)` | One step in a direction: tries to climb stairs (`STEPSIZE` 18), checks the floor underneath, handles flying and swimming monsters. |
| `SV_CheckBottom(ent)` | Is there floor under all four corners? (Stops monsters walking off ledges.) |
| `SV_StepDirection(ent, yaw, dist)` | Step in a compass direction and turn to face it. |
| `SV_NewChaseDir(actor, enemy, dist)` | Pick the next direction when chasing: straight toward, then the two axes, then a random one. |
| `SV_MoveToGoal` | The `movetogoal` builtin: step toward `goalentity`, or pick a new direction. |
| `SV_CloseEnough(ent, goal, dist)` | Within range of the goal. |
| `SV_FixCheckBottom` | Marks a monster as partially on ground after `SV_movestep`. |

---

## `world.h` / `world.c`

Collision against the world and between entities.

### Hulls

Quake does not clip against triangles but against **hulls**: copies of the BSP tree expanded to fixed box sizes. A brush model has
three (point, player-sized, and large-monster-sized). `SV_HullForEntity(ent, mins, maxs, &offset)` picks the hull of a `SOLID_BSP` entity's model by the size of the
thing being moved; any other entity is clipped as a plain box (`SV_HullForBox`).
`SV_HullPointContents(hull, num, p)` finds the `CONTENTS_*` at a point; `SV_RecursiveHullCheck` traces a line through the hull tree and fills in a `trace_t`.

```c
typedef struct {
	qboolean allsolid, startsolid, inopen, inwater;
	float    fraction;       // 1.0 = didn't hit anything
	vec3_t   endpos;         // where the move stopped
	plane_t  plane;          // surface hit
	edict_t *ent;            // entity hit
} trace_t;
```

### Area tree

Entities are stored in a binary tree of areas (`areanode_t`, depth 4) so that tests only visit nearby ones. `SV_ClearWorld` builds it once per level,
`SV_LinkEdict(ent, touch_triggers)` (re)inserts an entity whenever it moves and fires its trigger touches, `SV_UnlinkEdict` removes it,
`SV_TouchLinks` finds triggers an entity overlaps, `SV_FindTouchedLeafs` records the BSP leaves an entity is in (used by the client for visibility).

### Moves

```c
trace_t tr = SV_Move (start, mins, maxs, end, MOVE_NORMAL, sv_player);   // sweep a box
if (tr.fraction < 1.0 && tr.ent && is_live_monster (tr.ent)) ...         // what autofire.c does
```

`SV_Move` sweeps a box from `start` to `end`: first through the world hull (`SV_ClipMoveToEntity` for entity 0), then `SV_ClipToLinks` over entities along the path.
`type` is `MOVE_NORMAL`, `MOVE_NOMONSTERS` (line of sight tests) or `MOVE_MISSILE` (extra margin). `passedict` is excluded from clipping.
`SV_PointContents(p)` and `SV_TruePointContents(p)` give the world contents at a point (water currents map to plain water in the first),
`SV_TestEntityPosition(ent)` returns the entity it is stuck in, if any.

**Changes:** `SV_RecursiveHullCheck` was added to `world.h` (the chase camera and autofire use it through `SV_Move`/`TraceLine`); the x86 conditionals were removed.
