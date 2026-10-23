/*
 * Weapon list for the system menu: which weapons can be selected right now.
 *
 * The mission packs add weapons and Dissolution of Eternity moves the axe's item bit, so each
 * game has its own list (common.c sets hipnotic / rogue).
 */

#include <quakedef.h>
#include "weapons.h"

/* Dissolution of Eternity keeps the count of each ammo type in a QuakeC field of its own
 * (ammo_nails1, ammo_lava_nails, ...): the ammo the client sees is only the current weapon's.
 * Those fields are read from the player (the single-player server is local). */
typedef struct
{
	const char *name;
	int item;		  /* bit in .items that says the weapon is owned; also its .weapon value */
	int impulse;	  /* selects it */
	const char *ammo; /* QuakeC field with its ammo, NULL: the client's stat */
	int stat;		  /* STAT_* with its ammo, -1: none */
	int ammo_min;	  /* ammo needed to select it (the QuakeC refuses otherwise) */
	int kind;		  /* WEAPON_* (autofire) */
	int toggle;		  /* the weapon whose key (impulse) toggles to this one, 0: none */
} weapon_t;

/* Impulse order (impulse 1 = axe ... 8 = thunderbolt), each pack's weapons after the one they
 * share a key with. The impulses are the ones both the packs' 1997 QuakeC and the 2021
 * re-release's (github.com/id-Software/quake-rerelease-qc) handle. */
static const weapon_t quake_weapons[] = {
	{"Axe", IT_AXE, 1, NULL, -1, 0, WEAPON_MELEE},
	{"Shotgun", IT_SHOTGUN, 2, NULL, STAT_SHELLS, 1, 0},
	{"Dbl Shotgun", IT_SUPER_SHOTGUN, 3, NULL, STAT_SHELLS, 2, 0},
	{"Nailgun", IT_NAILGUN, 4, NULL, STAT_NAILS, 1, 0},
	{"Super Nailgun", IT_SUPER_NAILGUN, 5, NULL, STAT_NAILS, 2, 0},
	{"Grenade", IT_GRENADE_LAUNCHER, 6, NULL, STAT_ROCKETS, 1, WEAPON_HEAVY},
	{"Rocket", IT_ROCKET_LAUNCHER, 7, NULL, STAT_ROCKETS, 1, WEAPON_HEAVY},
	{"Lightning", IT_LIGHTNING, 8, NULL, STAT_CELLS, 1, 0},
};

/* Scourge of Armagon: 225 laser cannon, 226 Mjolnir; the proximity gun has no impulse of its own
 * in the 1997 QuakeC (the re-release added 227): 6 toggles to it from the grenade launcher.
 * 8 toggles to the laser cannon the same way, which the menu never asks for (see pdq_weapon_select) */
static const weapon_t hipnotic_weapons[] = {
	{"Axe", IT_AXE, 1, NULL, -1, 0, WEAPON_MELEE},
	{"Mjolnir", HIT_MJOLNIR, 226, NULL, -1, 0, WEAPON_MELEE},
	{"Shotgun", IT_SHOTGUN, 2, NULL, STAT_SHELLS, 1, 0},
	{"Dbl Shotgun", IT_SUPER_SHOTGUN, 3, NULL, STAT_SHELLS, 2, 0},
	{"Nailgun", IT_NAILGUN, 4, NULL, STAT_NAILS, 1, 0},
	{"Super Nailgun", IT_SUPER_NAILGUN, 5, NULL, STAT_NAILS, 2, 0},
	{"Grenade", IT_GRENADE_LAUNCHER, 6, NULL, STAT_ROCKETS, 1, WEAPON_HEAVY},
	{"Proximity Gun", HIT_PROXIMITY_GUN, 6, NULL, STAT_ROCKETS, 1, WEAPON_HEAVY, IT_GRENADE_LAUNCHER},
	{"Rocket", IT_ROCKET_LAUNCHER, 7, NULL, STAT_ROCKETS, 1, WEAPON_HEAVY},
	{"Lightning", IT_LIGHTNING, 8, NULL, STAT_CELLS, 1, 0},
	{"Laser Cannon", HIT_LASER_CANNON, 225, NULL, STAT_CELLS, 1, 0},
};

/* Dissolution of Eternity: the axe is RIT_AXE; 4-8 pick the plain weapon unless already holding
 * it (then they toggle to the powered-up one), 60-64 the powered-up ones */
static const weapon_t rogue_weapons[] = {
	{"Axe", RIT_AXE, 1, NULL, -1, 0, WEAPON_MELEE},
	{"Shotgun", IT_SHOTGUN, 2, "ammo_shells1", STAT_SHELLS, 1, 0},
	{"Dbl Shotgun", IT_SUPER_SHOTGUN, 3, "ammo_shells1", STAT_SHELLS, 2, 0},
	{"Nailgun", IT_NAILGUN, 4, "ammo_nails1", STAT_NAILS, 1, 0},
	{"Lava Nailgun", RIT_LAVA_NAILGUN, 60, "ammo_lava_nails", -1, 1, 0},
	{"Super Nailgun", IT_SUPER_NAILGUN, 5, "ammo_nails1", STAT_NAILS, 2, 0},
	{"Lava S.Nailgun", RIT_LAVA_SUPER_NAILGUN, 61, "ammo_lava_nails", -1, 1, 0},
	{"Grenade", IT_GRENADE_LAUNCHER, 6, "ammo_rockets1", STAT_ROCKETS, 1, WEAPON_HEAVY},
	{"Multi Grenade", RIT_MULTI_GRENADE, 62, "ammo_multi_rockets", -1, 1, WEAPON_HEAVY},
	{"Rocket", IT_ROCKET_LAUNCHER, 7, "ammo_rockets1", STAT_ROCKETS, 1, WEAPON_HEAVY},
	{"Multi Rocket", RIT_MULTI_ROCKET, 63, "ammo_multi_rockets", -1, 1, WEAPON_HEAVY},
	{"Lightning", IT_LIGHTNING, 8, "ammo_cells1", STAT_CELLS, 1, 0},
	{"Plasma Gun", RIT_PLASMA_GUN, 64, "ammo_plasma", -1, 1, 0},
};

#define COUNT(a) (int)(sizeof(a) / sizeof(a[0]))
#define MAX_WEAPONS COUNT(rogue_weapons)

static const weapon_t *game_weapons(int *count)
{
	if (rogue)
	{
		*count = COUNT(rogue_weapons);
		return rogue_weapons;
	}
	if (hipnotic)
	{
		*count = COUNT(hipnotic_weapons);
		return hipnotic_weapons;
	}
	*count = COUNT(quake_weapons);
	return quake_weapons;
}

/* The player's ammo for w; -1: unknown (the QuakeC decides) */
static int ammo(const weapon_t *w)
{
	ddef_t *def;

	if (w->ammo && sv.active && sv_player && (def = ED_FindField((char *)w->ammo)))
		return (int)((float *)&sv_player->v)[def->ofs];
	return w->stat >= 0 ? cl.stats[w->stat] : -1;
}

/* The weapon in hand: the local server's player (single player). The client's stat is a byte in
 * Quake's protocol, so the axe (IT_AXE, 4096) arrives as 0 there. */
static int active_weapon(void)
{
	return sv.active && sv_player ? (int)sv_player->v.weapon : cl.stats[STAT_ACTIVEWEAPON];
}

/* avail[menu value] = weapon */
const char *pdq_weapon_names[MAX_WEAPONS];
static const weapon_t *avail[MAX_WEAPONS];
static int num_avail;

int pdq_weapons_scan(int *current)
{
	int count, active = active_weapon();
	const weapon_t *weapons = game_weapons(&count);

	*current = 0;
	num_avail = 0;
	for (int i = 0; i < count; i++)
	{
		const weapon_t *w = &weapons[i];

		if (!(cl.items & w->item))
			continue;
		if (w->ammo_min && ammo(w) >= 0 && ammo(w) < w->ammo_min)
			continue;
		if (active == w->item)
			*current = num_avail;
		pdq_weapon_names[num_avail] = w->name;
		avail[num_avail] = w;
		num_avail++;
	}
	return num_avail;
}

void pdq_weapon_select(int index)
{
	const weapon_t *w;
	int active = active_weapon();

	/* not for the weapon in hand: in the mission packs its key would toggle to another one */
	if (index < 0 || index >= num_avail || avail[index]->item == active)
		return;
	w = avail[index];
	/* a weapon reached by toggling: its key once for the weapon it toggles from, and again in the
	 * next frame (one impulse per frame reaches the server) */
	if (w->toggle && active != w->toggle && (cl.items & w->toggle))
		Cbuf_AddText(va("impulse %d\nwait\nimpulse %d\n", w->impulse, w->impulse));
	else
		Cbuf_AddText(va("impulse %d\n", w->impulse));
}

int pdq_weapon_kind(int weapon)
{
	int count;
	const weapon_t *weapons = game_weapons(&count);

	for (int i = 0; i < count; i++)
		if (weapons[i].item == weapon)
			return weapons[i].kind;
	return 0;
}
