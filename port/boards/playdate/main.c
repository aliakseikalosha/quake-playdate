/*
 * Playdate entry point: event handler, frame loop, input and timing.
 *
 * Controls
 *   D-pad up/down   walk forward / back      (menus: move)
 *   D-pad left/right turn                    (menus: change value)
 *   A               fire                     (menus: select)
 *   B               jump                     (menus: back)
 *   Crank           turn left / right
 *                   (crank out: D-pad left/right strafe instead of turning)
 *   Crank out       autofire while an enemy is under the crosshair
 *   System menu     "Game Menu" opens Quake's own menu; "Options" opens its
 *                   Options screen directly (leaving it returns to the game;
 *                   autofire is toggled there); "Weapon" lists the weapons you
 *                   can use now.
 * While a demo is playing (title screen) A and B open Quake's menu.
 *
 * Mods and mission packs: Quake's main menu lists the game folders next to id1 (Mods) and
 * restarts the game in the one picked; Data/game.txt remembers it for the next launches.
 * Launch arguments (Simulator command line, `run` on the device console) are Quake's own command
 * line instead, e.g. "-game mymod" or "-hipnotic". Holding B while the game starts ignores both.
 */

#include <setjmp.h>
#include <string.h>
#include <quakembd.h>
#include <quakedef.h>
#include "pd_port.h"
#include "autofire.h"
#include "keyqueue.h"
#include "weapons.h"
#include "pdprof.h"
#include "pd_stack.h"

PlaydateAPI *qembd_pd;

enum
{
	ST_SPLASH,
	ST_INIT,
	ST_RUN,
	ST_STOPPED
};

#ifndef PD_REFRESH_RATE
#define PD_REFRESH_RATE 30 /* frames per second the system calls update at; 0 = as fast as possible */
#endif

extern cvar_t pd_maxfps; /* "Max framerate" in the options menu: 30, 50 or 0 = unlimited */
extern cvar_t crank_speed; /* degrees of view turn per degree of crank ("Crank speed" in the options menu) */
#define RUN_SPEED 400

static int state = ST_SPLASH;
static jmp_buf frame_jmp;
static int in_frame;

static int menu_requested;
static int options_requested;
static PDMenuItem *weapon_item;

/* ---------------------------------------------------------------- time */

uint64_t qembd_get_us_time()
{
	static unsigned int base;
	static int have_base;
	unsigned int ms = qembd_pd->system->getCurrentTimeMilliseconds();

	if (!have_base)
	{
		base = ms;
		have_base = 1;
	}
	return (uint64_t)(ms - base) * 1000;
}

void qembd_udelay(uint32_t us)
{
	uint64_t end = qembd_get_us_time() + us;

	while (qembd_get_us_time() < end)
		;
}

void *qembd_allocmain(size_t size)
{
#ifdef TARGET_SIMULATOR
	/*
	 * QuakeC strings are 32-bit offsets from pr_strings, and Quake computes
	 * them from pointers into static buffers (sv.name, pr_string_temp, ...).
	 * On a 64-bit host a malloc'd heap can be >2GB away from static data,
	 * which truncates those offsets, so keep the heap in static storage.
	 * (The device is 32-bit and allocates from the system heap below.)
	 */
	static byte pool[12 * 1024 * 1024] __attribute__((aligned(16)));	/* DEFAULT_MEM_SIZE fits */

	return size <= sizeof(pool) ? pool : NULL;
#else
	return qembd_pd->system->realloc(NULL, size);
#endif
}

/* ---------------------------------------------------------- game folder */

#define GAME_FILE "game.txt" /* in Data: the game folder the Mods menu picked; absent = id1 only */
#define MAX_ARGS 16

static char game_dir[MAX_QPATH]; /* the folder Quake started in, "" = id1 only */

static void no_entry(const char *name, void *ud)
{
	(void)name;
	(void)ud;
}

static int folder_exists(const char *dir)
{
	return qembd_pd->file->listfiles(dir, no_entry, NULL, 0) == 0;
}

/* The folder in GAME_FILE, or "" */
static void read_game_file(char *out, int size)
{
	SDFile *f = qembd_pd->file->open(GAME_FILE, kFileReadData);
	int n = 0;

	if (f)
	{
		n = qembd_pd->file->read(f, out, size - 1);
		qembd_pd->file->close(f);
	}
	out[n < 0 ? 0 : n] = '\0';
	out[strcspn(out, "\r\n")] = '\0';
}

/* The Mods menu's choice: remember it and start again (kEventTerminate saves the options first) */
void qembd_restart(const char *gamedir)
{
	SDFile *f;

	if (gamedir[0] && (f = qembd_pd->file->open(GAME_FILE, kFileWrite)))
	{
		qembd_pd->file->write(f, gamedir, strlen(gamedir));
		qembd_pd->file->close(f);
	}
	else
		qembd_pd->file->unlink(GAME_FILE, 0);
	qembd_pd->system->restartGame("");
}

/*
 * Quake's command line: the launch arguments if there are any, else "-game <folder>" from
 * GAME_FILE. B held down at start-up skips both and forgets GAME_FILE, so a mod that cannot
 * start (or crashes the device) does not lock the game out.
 */
static int build_args(char **argv)
{
	static char arg0[] = "quake", game_opt[] = "-game";
	static char buf[256];
	PDButtons held;
	const char *launch = qembd_pd->system->getLaunchArgs(NULL);
	int argc = 0;

	argv[argc++] = arg0;
	game_dir[0] = '\0';

	qembd_pd->system->getButtonState(&held, NULL, NULL);
	if (held & kButtonB)
	{
		qembd_pd->file->unlink(GAME_FILE, 0);
		argv[argc] = NULL;
		return argc;
	}

	if (launch && launch[0])
	{
		char *p = buf;

		strncpy(buf, launch, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';
		while (argc < MAX_ARGS)
		{
			p += strspn(p, " \t");
			if (!*p)
				break;
			argv[argc++] = p;
			p += strcspn(p, " \t");
			if (*p)
				*p++ = '\0';
		}
		for (int i = 1; i < argc; i++)
		{
			if (!strcmp(argv[i], "-game") && i + 1 < argc)
				strncpy(game_dir, argv[i + 1], sizeof(game_dir) - 1);
			else if (!strcmp(argv[i], "-hipnotic") || !strcmp(argv[i], "-rogue"))
				strncpy(game_dir, argv[i] + 1, sizeof(game_dir) - 1);
		}
	}
	else
	{
		read_game_file(buf, sizeof(buf));
		if (buf[0] && strlen(buf) < sizeof(game_dir) && folder_exists(buf))
		{
			strcpy(game_dir, buf);
			argv[argc++] = game_opt;
			argv[argc++] = game_dir;
		}
		else if (buf[0])
			qembd_pd->file->unlink(GAME_FILE, 0); /* the folder is gone */
	}
	argv[argc] = NULL;
	return argc;
}

/* ------------------------------------------------------------- messages */

static void show_message(const char *title, const char *msg)
{
	PlaydateAPI *pd = qembd_pd;

	pd->graphics->clear(kColorWhite);
	pd->graphics->drawText(title, strlen(title), kUTF8Encoding, 20, 20);
	pd->graphics->drawTextInRect(msg, strlen(msg), kUTF8Encoding, 20, 50, 360, 170,
								 kWrapWord, kAlignTextLeft);
}

/* The launcher's launch image (pdxinfo imagePath, scripts/gen-launcher-assets.py), so loading looks
 * like the launch went on; the plain message without it */
static void show_splash(void)
{
	PlaydateAPI *pd = qembd_pd;
	const char *err = NULL;
	LCDBitmap *image = pd->graphics->loadBitmap("launcher/launchImage", &err);

	if (!image)
	{
		show_message("Quake", "Loading...");
		return;
	}
	pd->graphics->drawBitmap(image, 0, 0, kBitmapUnflipped);
	pd->graphics->freeBitmap(image); /* drawn into the frame buffer: not needed after */
}

static void stop_with_message(const char *title, const char *msg)
{
	state = ST_STOPPED;
	show_message(title, msg);
	if (in_frame)
	{
		in_frame = 0;
		longjmp(frame_jmp, 1);
	}
}

void qembd_log(const char *text)
{
	pdq_log_line(text);
}

/* Sys_Error: report on screen (and console) and stop running Quake */
void qembd_fatal(const char *msg)
{
	static char buf[512];

	qembd_pd->system->logToConsole("Quake error: %s", msg);
	if (game_dir[0])
	{
		snprintf(buf, sizeof(buf), "%s\n\nHold B while starting the game to play Quake without %s.",
				 msg, COM_GameTitle(game_dir));
		msg = buf;
	}
	stop_with_message("Quake stopped:", msg);
	/* Not reached from inside a frame, but Sys_Error must not return */
	for (;;)
		;
}

/* Sys_Quit: there is no process to exit, so just park */
void qembd_quit(void)
{
	stop_with_message("Quake has exited.", "Open the system menu to leave the game.");
	for (;;)
		;
}

/* ---------------------------------------------------------------- input */

static const struct
{
	PDButtons mask;
	int game_key;
	int ui_key;
} buttons[] = {
	{kButtonUp, K_UPARROW, K_UPARROW},
	{kButtonDown, K_DOWNARROW, K_DOWNARROW},
	{kButtonLeft, K_LEFTARROW, K_LEFTARROW},
	{kButtonRight, K_RIGHTARROW, K_RIGHTARROW},
	{kButtonA, K_CTRL, K_ENTER},   /* +attack; select */
	{kButtonB, K_SPACE, K_ESCAPE}, /* +jump; back */
};
#define NUM_BUTTONS (int)(sizeof(buttons) / sizeof(buttons[0]))

static void poll_input(void)
{
	static PDButtons prev;
	static int sent[NUM_BUTTONS]; /* key sent on press, released with the same */
	PDButtons cur;
	int ui = key_dest != key_game;
	int playing = !ui && !cls.demoplayback;

	qembd_pd->system->getButtonState(&cur, NULL, NULL);

	for (int i = 0; i < NUM_BUTTONS; i++)
	{
		int down = (cur & buttons[i].mask) != 0;
		int was = (prev & buttons[i].mask) != 0;

		if (down && !was)
		{
			int key = ui ? buttons[i].ui_key : buttons[i].game_key;

			/* With the crank out the crank turns, so left/right strafe */
			if (playing && !qembd_pd->system->isCrankDocked())
			{
				if (buttons[i].mask == kButtonLeft)
					key = ',';
				else if (buttons[i].mask == kButtonRight)
					key = '.';
			}

			/* Title-screen demo: any button opens the menu */
			if (!ui && cls.demoplayback && i >= 4)
				key = K_ESCAPE;
			sent[i] = key;
			pdq_push_key(key, 1);
		}
		else if (!down && was)
		{
			pdq_push_key(sent[i], 0);
		}
	}
	prev = cur;

	/* Crank out: hold fire while an enemy is under the crosshair */
	pdq_autofire_update((cur & kButtonA) != 0, playing && !qembd_pd->system->isCrankDocked());

	if (menu_requested)
	{
		menu_requested = 0;
		pdq_push_key(K_ESCAPE, 1);
		pdq_push_key(K_ESCAPE, 0);
	}
	if (options_requested)
	{
		options_requested = 0;
		M_Menu_Options_Shortcut();
	}

	/* Crank turns the player while playing (clockwise = right) */
	if (key_dest == key_game && cls.state == ca_connected && !cls.demoplayback)
		cl.viewangles[YAW] -= qembd_pd->system->getCrankChange() * crank_speed.value;
}

static void apply_run(void)
{
	Cvar_SetValue("cl_forwardspeed", RUN_SPEED);
	Cvar_SetValue("cl_backspeed", RUN_SPEED);
}

static void menu_quake(void *ud)
{
	menu_requested = 1;
}

static void menu_options(void *ud)
{
	options_requested = 1;
}

static void menu_weapon(void *ud)
{
	if (state == ST_RUN)
		pdq_weapon_select(qembd_pd->system->getMenuItemValue(weapon_item));
}

/*
 * Options can't change once a menu item exists, so rebuild it each time the
 * system menu opens, listing only the weapons that can be selected right now.
 */
static void rebuild_weapon_item(void)
{
	int current, num_avail;

	if (weapon_item)
	{
		qembd_pd->system->removeMenuItem(weapon_item);
		weapon_item = NULL;
	}
	if (state != ST_RUN || cls.state != ca_connected || cls.demoplayback)
		return;

	num_avail = pdq_weapons_scan(&current);
	if (num_avail < 2)
		return; /* nothing to choose between */

	weapon_item = qembd_pd->system->addOptionsMenuItem("Weapon", pdq_weapon_names, num_avail,
													   menu_weapon, NULL);
	qembd_pd->system->setMenuItemValue(weapon_item, current);
}

/* --------------------------------------------------------------- update */

/* Follow the frame rate cap (pd_maxfps) whenever it changes: the menu, config.cfg, the console */
static void apply_refresh_rate(void)
{
	static int applied = -1;
	int rate = (int)pd_maxfps.value;

	if (rate < 0)
		rate = 0;
	if (rate > 50)
		rate = 50; /* the display's maximum */
	if (rate != applied)
	{
		qembd_pd->display->setRefreshRate((float)rate);
		applied = rate;
	}
}

static int update(void *ud)
{
	switch (state)
	{
	case ST_SPLASH:
		/* Draw first: loading the pak takes a while */
		pdprof_stage("splash");
		show_splash();
		state = ST_INIT;
		return 1;

	case ST_INIT:
	{
		static char *argv[MAX_ARGS + 1];
		int argc = build_args(argv);

		if (setjmp(frame_jmp))
			return 1;
		in_frame = 1;
		pdprof_stage("qembd_init begin");
		if (qembd_init(argc, argv) != 0)
		{
			in_frame = 0;
			stop_with_message("Quake failed to start",
							  "Not enough memory. Try a smaller PD_RENDER_WIDTH/HEIGHT.");
			return 1;
		}
		pdprof_stage("qembd_init done");
		apply_run();
		pdprof_init();
		Key_SetBinding(',', "+moveleft");
		Key_SetBinding('.', "+moveright");
		in_frame = 0;
		state = ST_RUN;
		return 1;
	}

	case ST_RUN:
		if (setjmp(frame_jmp))
		{
#ifdef PD_STACK
			pd_stack_top = 0;
#endif
			return 1;
		}
		in_frame = 1;
#ifdef PD_STACK
		pd_stack_top = PD_StackPointer();
#endif
		poll_input();
		qembd_frame();
#ifdef PD_STACK
		pd_stack_top = 0;
#endif
		in_frame = 0;
		apply_refresh_rate();
		return 1;

	default:
		return 1;
	}
}

#ifdef _WINDLL
__declspec(dllexport)
#endif
int eventHandler(PlaydateAPI *playdate, PDSystemEvent event, uint32_t arg)
{
	(void)arg;

	if (event == kEventPause)
		rebuild_weapon_item();

	/* Music is a system-side stream: nothing stops it while the game does not run */
	if (event == kEventPause || event == kEventLock)
		qembd_cd_suspend(1);
	if (event == kEventResume || event == kEventUnlock)
		qembd_cd_suspend(0);

	/* Interlaced rendering leaves unchanged rows alone: redraw them all after the system screens */
	if (event == kEventResume || event == kEventUnlock)
		qembd_display_invalidate();

	/* The game is stopped from the system menu without Host_Shutdown ever running, so write
	 * the options (config.cfg) whenever the system takes over; a no-op if none changed. */
	if ((event == kEventPause || event == kEventLock || event == kEventTerminate) && state == ST_RUN)
		Host_SaveOptions();

	if (event == kEventInit)
	{
		qembd_pd = playdate;
		pdprof_stage("kEventInit");
		playdate->display->setRefreshRate(PD_REFRESH_RATE);
		playdate->system->addMenuItem("Game Menu", menu_quake, NULL);
		playdate->system->addMenuItem("Options", menu_options, NULL);
		/* Setting an update callback tells the system this is a pure C game */
		playdate->system->setUpdateCallback(update, NULL);
	}
	return 0;
}
