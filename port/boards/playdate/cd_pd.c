/*
 * CD audio for the Playdate.
 *
 * Quake's music is CD tracks 2-11, played by svc_cdtrack (the level's "sounds" key), the "cd"
 * console command and the Options menu's "Music volume". There is no CD, so track N is the file
 * id1/music/QuakeNN, as the re-release ships them: Source/id1/music/Quake02.wav ... Quake11.wav.
 * pdc compiles a .wav to .pda (a PCM or ADPCM WAV keeps its format), so the .pdx holds
 * id1/music/QuakeNN.pda; an .mp3 with that name works as well. A Playdate FilePlayer streams the
 * file from flash, so only its small buffer is in RAM, and loops it for the level music.
 *
 * Without the files (the shareware release has none) every call is a quiet no-op.
 */

#include "quakedef.h"
#include "pd_port.h"

#define MUSIC_DIR	"music"
#define BASE_GAMEDIR	"id1"	/* where mods without their own music look */

extern cvar_t bgmenabled;	/* "Music" in the Options menu (menu.c); the volume is bgmvolume */

static FilePlayer *player;
static int ready;
static int was_enabled = 1;	/* bgmenabled as of the last sync_enabled() */
static int req_track;		/* the track the game last asked for, kept while the music is off; 0 = none */
static int req_loop;
static int play_track;		/* track loaded in the player; 0 = none */
static int looping;
static int started;		/* play() issued since the file was loaded or last paused */
static int paused;		/* the game is paused (svc_setpause, "cd pause") */
static int suspended;		/* the system menu or lock screen has the device */
static float volume_set = -1;	/* last volume handed to the player */

static float music_volume(void)
{
	float v = bgmvolume.value;

	return v < 0 ? 0 : v > 1 ? 1 : v;
}

/* Loads id1/music/Quake<track>.pda (or .mp3) from the game directory, then from id1 */
static int load_track(int track)
{
	static const char *const exts[] = { ".pda", ".mp3" };
	char dir[2][MAX_OSPATH], path[MAX_OSPATH], norm[MAX_OSPATH];

	pdq_path(com_gamedir, dir[0], sizeof(dir[0]));
	pdq_path(BASE_GAMEDIR, dir[1], sizeof(dir[1]));

	for (int d = 0; d < 2; d++) {
		if (d == 1 && !strcmp(dir[0], dir[1]))
			break;
		for (int e = 0; e < 2; e++) {
			snprintf(path, sizeof(path), "%s/" MUSIC_DIR "/Quake%02d%s",
			         dir[d], track, exts[e]);
			pdq_path(path, norm, sizeof(norm));
			if (qembd_pd->sound->fileplayer->loadIntoPlayer(player, norm))
				return 1;
		}
	}
	return 0;
}

/* Bring the player in line with what is wanted: playing, or paused for any of several reasons */
static void apply(void)
{
	const struct playdate_sound_fileplayer *fp;
	float vol = music_volume();
	int want = play_track && !paused && !suspended && vol > 0;

	if (!ready)
		return;
	fp = qembd_pd->sound->fileplayer;

	if (!want) {
		if (started) {
			fp->pause(player);
			started = 0;
		}
		return;
	}

	if (vol != volume_set) {
		fp->setVolume(player, vol, vol);
		volume_set = vol;
	}

	if (started && fp->isPlaying(player))
		return;
	if (started && !looping) {	/* a "cd play" track reached its end */
		play_track = 0;
		req_track = 0;
		started = 0;
		return;
	}
	/* first start, resume after a pause, or a looped track that stopped for some reason */
	if (!fp->play(player, looping ? 0 : 1)) {
		Con_Printf("CDAudio: could not start track %d (out of memory?)\n", play_track);
		play_track = 0;
		started = 0;
		return;
	}
	started = 1;
}

static void unload(void)
{
	if (started || play_track)
		qembd_pd->sound->fileplayer->stop(player);
	play_track = 0;
	started = 0;
}

static void begin(int track, int loop)
{
	if (play_track) {
		if (play_track == track)
			return;
		unload();
	}
	if (!track)
		return;

	if (!load_track(track)) {
		Con_DPrintf("CDAudio: no music file for track %d\n", track);
		return;
	}
	Con_DPrintf("CDAudio: %s track %d\n", loop ? "looping" : "playing", track);
	play_track = track;
	looping = loop;
	started = 0;
	volume_set = -1;
	apply();
}

/* The music was switched off or on (the Options menu, "cd off"/"cd on"): off unloads the track but
 * remembers it, on starts what the game asked for last, so the level's music comes back */
static void sync_enabled(void)
{
	int on = bgmenabled.value != 0;

	if (!ready || on == was_enabled)
		return;
	was_enabled = on;
	if (!on)
		unload();
	else if (req_track)
		begin(req_track, req_loop);
}

void CDAudio_Play(byte track, qboolean loop)
{
	if (!ready)
		return;

	req_track = track;
	req_loop = loop;
	sync_enabled();
	if (bgmenabled.value)
		begin(track, loop);
}

void CDAudio_Stop(void)
{
	if (!ready)
		return;
	req_track = 0;
	unload();
}

void CDAudio_Pause(void)
{
	paused = 1;
	apply();
}

void CDAudio_Resume(void)
{
	paused = 0;
	apply();
}

void CDAudio_Update(void)
{
	sync_enabled();	/* the Music option */
	apply();	/* the volume slider, a finished track */
}

/* The system menu or the lock screen took over (eventHandler): music must not play on its own */
void qembd_cd_suspend(int suspend)
{
	suspended = suspend;
	apply();
}

static void CD_f(void)
{
	char *command;

	if (Cmd_Argc() < 2)
		return;
	command = Cmd_Argv(1);

	if (!Q_strcasecmp(command, "on")) {
		Cvar_SetValue("bgmenabled", 1);
		host_options_dirty = true;
		sync_enabled();
	} else if (!Q_strcasecmp(command, "off")) {
		Cvar_SetValue("bgmenabled", 0);
		host_options_dirty = true;
		sync_enabled();
	} else if (!Q_strcasecmp(command, "reset")) {
		Cvar_SetValue("bgmenabled", 1);
		host_options_dirty = true;
		sync_enabled();
		CDAudio_Stop();
	} else if (!Q_strcasecmp(command, "play")) {
		CDAudio_Play((byte) Q_atoi(Cmd_Argv(2)), false);
	} else if (!Q_strcasecmp(command, "loop")) {
		CDAudio_Play((byte) Q_atoi(Cmd_Argv(2)), true);
	} else if (!Q_strcasecmp(command, "stop")) {
		CDAudio_Stop();
	} else if (!Q_strcasecmp(command, "pause")) {
		CDAudio_Pause();
	} else if (!Q_strcasecmp(command, "resume")) {
		CDAudio_Resume();
	} else if (!Q_strcasecmp(command, "info")) {
		if (play_track)
			Con_Printf("Currently %s track %d\n", looping ? "looping" : "playing", play_track);
		else
			Con_Printf("Not playing\n");
		Con_Printf("Volume is %.2f\n", (double) bgmvolume.value);
	}
	/* remap, eject and close mean nothing without a drive */
}

int CDAudio_Init(void)
{
	player = qembd_pd->sound->fileplayer->newPlayer();
	if (!player) {
		Con_Printf("CDAudio: could not allocate a file player\n");
		return -1;
	}
	ready = 1;
	Cmd_AddCommand("cd", CD_f);
	Con_Printf("CD audio initialized\n");
	return 0;
}

void CDAudio_Shutdown(void)
{
	if (!ready)
		return;
	unload();
	ready = 0;
	qembd_pd->sound->fileplayer->freePlayer(player);
	player = NULL;
}
