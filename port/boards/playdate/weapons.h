#ifndef PD_WEAPONS_H
#define PD_WEAPONS_H

/*
 * List the weapons that can be selected right now (owned, with enough ammo)
 * and return how many. Fills the arrays below and *current with the index of
 * the active weapon.
 */
int pdq_weapons_scan(int *current);

/* Names of the weapons found by the last scan, indexed 0..count-1 */
extern const char *pdq_weapon_names[];

/* Send the "impulse" that selects weapon number index of the last scan */
void pdq_weapon_select(int index);

/* What autofire needs to know about a weapon (a .weapon value) of the game running */
#define WEAPON_MELEE 1 /* hits only what is within reach */
#define WEAPON_HEAVY 2 /* fired one shot at a time (grenades, rockets) */
int pdq_weapon_kind(int weapon);

#endif /* PD_WEAPONS_H */
