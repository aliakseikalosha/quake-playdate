/*
 * Host-side checks for the Playdate port: the real engine and the real
 * port/boards/playdate/display.c, run headless on the development machine.
 *
 *  golden mode (HGOLD=<file>):  plays demo1..3 back with `timedemo` and writes one hash of the
 *      1-bit LCD image per frame, plus one of the 8-bit render buffer and z buffer. Build the tree before and after an optimisation and compare
 *      the two files (tools/hostcheck/golden-compare.py): equal hashes = identical pictures.
 *
 *  menu mode (HMENU=1):  keys through the options menu, see menu_test().
 *
 *  frames mode (HFRAMES=<prefix>):  writes the LCD of a demo playing in real time as PBM pictures,
 *      see frames_mode(). Used to make docs/demo.gif.
 *
 *  default mode:  scripted play (walking, console, menus, HUD, view sizes, demos) that, on every
 *      screen update, also checks the lazy low-res upscale (see D_UpscaleScreen in d_scan.c):
 *        1. no view row pair that is still "pending" may differ from the snapshot taken when the
 *           half-resolution view was handed over, i.e. no draw primitive wrote into it without
 *           calling DRAW_TOUCH first;
 *        2. the LCD image produced by dithering pending rows straight from the half-resolution
 *           buffer is byte-identical to the one produced the old way (expand the view into the
 *           frame buffer, then dither).
 *      Built with -DNO_LAZY_CHECK for trees that predate the lazy upscale (golden mode only).
 */
#include <stdio.h>
#include "pd_api.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <quakedef.h>
#include <quakembd.h>
#include <d_local.h>
#ifdef PD_STACK
#include <pd_stack.h>
#endif

#define W 400
#define H 240
#define FRAME_BYTES (LCD_ROWSIZE * LCD_ROWS)

PlaydateAPI *qembd_pd;
static struct playdate_graphics gfx;
static PlaydateAPI api;
static uint8_t frame_new[FRAME_BYTES], frame_old[FRAME_BYTES];
static uint8_t *cur_frame;
static long frame_no;
static int cur_scn;
static const char *phase = "";
long checks, pending_pairs_checked, flushed_pairs, mismatches, hook_violations;

static uint8_t *get_frame(void) { return cur_frame; }
static void mark_rows(int a, int b) { (void)a; (void)b; }

/* display.c is compiled twice, as tree_* (the tree under test) and old_* (expand-then-dither) */
void tree_fillrect(uint8_t *, uint32_t *, uint16_t, uint16_t, uint16_t, uint16_t);
void tree_vidinit(void);
int tree_get_width(void);
int tree_get_height(void);

int qembd_get_width(void) { return tree_get_width(); }
int qembd_get_height(void) { return tree_get_height(); }
void qembd_refresh(void) {}

#ifndef NO_LAZY_CHECK
void old_fillrect(uint8_t *, uint32_t *, uint16_t, uint16_t, uint16_t, uint16_t);
void old_vidinit(void);
void real_D_UpscaleScreen(void);
extern int qembd_lowres_rect[4];
extern int qembd_lowres_active;
extern uint8_t qembd_lowres_pending[];
static uint8_t snapshot[W * H];
static int snap_valid;

void D_UpscaleScreen(void)
{
	real_D_UpscaleScreen();
	memcpy(snapshot, vid.buffer, W * H);
	snap_valid = 1;
}

void qembd_vidinit(void)
{
	cur_frame = frame_new;
	tree_vidinit();
	cur_frame = frame_old;
	old_vidinit();
}

void qembd_fillrect(uint8_t *src, uint32_t *clut, uint16_t x, uint16_t y, uint16_t xs, uint16_t ys)
{
	checks++;
	if (qembd_lowres_active && snap_valid) {
		int p, ry0 = qembd_lowres_rect[1], ry1 = ry0 + qembd_lowres_rect[3], rx0 = qembd_lowres_rect[0], rw = qembd_lowres_rect[2];

		for (p = ry0 >> 1; p < ry1 >> 1; p++) {
			if (!qembd_lowres_pending[p]) {
				flushed_pairs++;
				continue;
			}
			pending_pairs_checked++;
			if (memcmp(vid.buffer + 2 * p * W + rx0, snapshot + 2 * p * W + rx0, rw) ||
				memcmp(vid.buffer + (2 * p + 1) * W + rx0, snapshot + (2 * p + 1) * W + rx0, rw)) {
				if (++hook_violations < 6)
					printf("HOOK VIOLATION scn=%d %s frame %ld: pending pair %d was written without DRAW_TOUCH\n", cur_scn, phase, frame_no, p);
			}
		}
	}

	/* new path: pending rows dithered straight from the half-resolution buffer */
	cur_frame = frame_new;
	tree_fillrect(src, clut, x, y, xs, ys);

	/* old path: expand every still-pending row pair into the buffer, then dither it */
	if (qembd_lowres_active)
		D_LowresTouch(0, H);
	cur_frame = frame_old;
	old_fillrect(src, clut, x, y, xs, ys);

	if (memcmp(frame_new, frame_old, FRAME_BYTES)) {
		int i;

		for (i = 0; i < FRAME_BYTES && frame_new[i] == frame_old[i]; i++)
			;
		if (++mismatches < 6)
			printf("LCD MISMATCH scn=%d %s frame %ld byte %d (row %d col %d): new %02x old %02x\n", cur_scn, phase, frame_no, i,
				   i / LCD_ROWSIZE, i % LCD_ROWSIZE, frame_new[i], frame_old[i]);
		memcpy(frame_old, frame_new, FRAME_BYTES); /* resync so one bug is reported once */
	}
}
#else
void qembd_vidinit(void)
{
	cur_frame = frame_new;
	tree_vidinit();
}

void qembd_fillrect(uint8_t *src, uint32_t *clut, uint16_t x, uint16_t y, uint16_t xs, uint16_t ys)
{
	cur_frame = frame_new;
	tree_fillrect(src, clut, x, y, xs, ys);
}
#endif

/* ------------------------------------------------------------------ stubs */
static uint64_t fake_us;
uint64_t qembd_get_us_time(void) { return fake_us; }
void qembd_udelay(uint32_t us) { fake_us += us; }
void *qembd_allocmain(size_t size)
{
	static byte pool[8 * 1024 * 1024] __attribute__((aligned(16)));
	return size <= sizeof(pool) ? pool : NULL;
}
void qembd_log(const char *t) { if (getenv("HLOG")) fputs(t, stdout); }
void qembd_fatal(const char *m) { printf("FATAL: %s\n", m); exit(2); }
void qembd_quit(void) { exit(0); }
int qembd_dequeue_key_event(key_event_t *e) { (void)e; return -1; }
int qembd_get_mouse_movement(mouse_movement_t *m) { (void)m; return -1; }
void qembd_set_relative_mode(bool e) { (void)e; }

cvar_t bgmvolume = {"bgmvolume", "1", true};
cvar_t volume = {"volume", "0.7", true};
vec_t sound_nominal_clip_dist = 1000.0;
void S_Init(void) {}
void S_Startup(void) {}
void S_Shutdown(void) {}
void S_StartSound(int a, int b, sfx_t *s, vec3_t o, float v, float at) {}
void S_StaticSound(sfx_t *s, vec3_t o, float v, float a) {}
void S_StopSound(int a, int b) {}
void S_StopAllSounds(qboolean c) {}
void S_ClearBuffer(void) {}
void S_Update(vec3_t o, vec3_t f, vec3_t r, vec3_t u) {}
void S_ExtraUpdate(void) {}
sfx_t *S_PrecacheSound(char *s) { return NULL; }
void S_TouchSound(char *s) {}
void S_ClearPrecache(void) {}
void S_BeginPrecaching(void) {}
void S_EndPrecaching(void) {}
void S_LocalSound(char *s) {}
void S_AmbientOff(void) {}
void S_AmbientOn(void) {}

/* ------------------------------------------------------------------ driver */
static void cmd(const char *s) { Cbuf_AddText((char *)s); }

static void run(int n)
{
	while (n-- > 0) {
		fake_us += 33000;
#ifdef PD_STACK
		pd_stack_top = PD_StackPointer();	/* as port/boards/playdate/main.c does */
#endif
		qembd_frame();
#ifdef PD_STACK
		pd_stack_top = 0;
#endif
		frame_no++;
	}
}

static void step(const char *name, const char *c, int n)
{
	phase = name;
	if (c)
		cmd(c);
	run(n);
}

static uint32_t hash_frame(void)
{
	uint32_t h = 2166136261u;
	int k;

	for (k = 0; k < FRAME_BYTES; k++)
		h = (h ^ frame_new[k]) * 16777619u;
	return h;
}

/* the 8-bit render buffer and the z buffer, which see differences the 1-bit dither hides */
static uint32_t hash_render(void)
{
	uint32_t h = 2166136261u;
	const uint8_t *z = (const uint8_t *)d_pzbuffer;
	int k, n = vid.width * vid.height * 2;

	for (k = 0; k < W * H; k++)
		h = (h ^ vid.buffer[k]) * 16777619u;
	for (k = 0; k < n; k++)
		h = (h ^ z[k]) * 16777619u;
	return h;
}


static int golden(const char *path)
{
	FILE *g = fopen(path, "w");
	int d;

	for (d = 1; d <= 3; d++) {
		int started = 0, i;
		char c[32];

		snprintf(c, sizeof(c), "timedemo demo%d\n", d);
		cmd(c);
		for (i = 0; i < 5000; i++) {
			run(1);
			if (cls.timedemo) {
				started = 1;
				key_dest = key_game; /* keep the console off the view */
			} else if (started)
				break;
			fprintf(g, "demo%d %d %08x %08x\n", d, i, hash_frame(), hash_render());
		}
		fprintf(g, "demo%d done after %d frames\n", d, i);
	}
	fclose(g);
	printf("golden: %ld frames written to %s\n", frame_no, path);
	return 0;
}

/*
 * HMENU=1: the options menu. Presses real keys (Key_Event) and checks navigation, the Crank speed
 * (crank_speed), Texture detail (d_mipcap), Interlaced (r_interlace) and Draw distance (r_maxdist)
 * rows, that leaving the menu writes config.cfg with the archived cvars but no key
 * bindings, that the file reloads, and that "Reset to defaults" puts the option back.
 */
extern int options_cursor;
extern cvar_t d_mipcap;
extern cvar_t r_interlace;
extern cvar_t r_maxdist;
extern cvar_t crank_speed;
extern cvar_t pd_maxfps;

static int menu_fail;
#define MCHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); menu_fail++; } else printf("ok:   %s\n", what); } while (0)

static void press(int key)
{
	Key_Event(key, true);
	Key_Event(key, false);
	run(2);
}

static char *slurp(const char *path)
{
	static char buf[16384];
	FILE *f = fopen(path, "r");
	size_t n;

	if (!f)
		return NULL;
	n = fread(buf, 1, sizeof(buf) - 1, f);
	buf[n] = 0;
	fclose(f);
	return buf;
}

/* the 1-bit LCD as a PBM (viewable / convertible) */
static void write_pbm(const char *path)
{
	FILE *f = fopen(path, "wb");
	int y, x;

	if (!f)
		return;
	fprintf(f, "P4\n%d %d\n", W, H);
	for (y = 0; y < H; y++)
		for (x = 0; x < W; x += 8) {
			uint8_t b = frame_new[y * LCD_ROWSIZE + x / 8];

			fputc((uint8_t)~b, f);	/* LCD: 1 = white; PBM: 1 = black */
		}
	fclose(f);
}

/* HMENUPIC=<prefix> also dumps the LCD as <prefix>-<name>.pbm */
static void dump_lcd(const char *name)
{
	const char *prefix = getenv("HMENUPIC");
	char path[512];

	if (!prefix)
		return;
	snprintf(path, sizeof(path), "%s-%s.pbm", prefix, name);
	write_pbm(path);
}

/*
 * HFRAMES=<prefix>: plays demo HDEMO (default 1) in real time on the fake 33 ms clock, as the device
 * does, and writes the LCD as <prefix>-NNNN.pbm every HEVERY frames (default 2, so 15 pictures per
 * second) after the first HSKIP frames (default 0), HCOUNT pictures in all (default 100).
 */
static int frames_mode(const char *prefix)
{
	int demo = getenv("HDEMO") ? atoi(getenv("HDEMO")) : 1;
	int skip = getenv("HSKIP") ? atoi(getenv("HSKIP")) : 0;
	int every = getenv("HEVERY") ? atoi(getenv("HEVERY")) : 2;
	int count = getenv("HCOUNT") ? atoi(getenv("HCOUNT")) : 100;
	int i, saved = 0;
	char path[512];

	snprintf(path, sizeof(path), "playdemo demo%d\n", demo);
	cmd(path);
	for (i = 0; saved < count && i < 20000; i++) {
		run(1);
		if (cls.demoplayback)
			key_dest = key_game; /* keep the console off the view */
		if (i >= skip && (i - skip) % every == 0) {
			snprintf(path, sizeof(path), "%s-%04d.pbm", prefix, saved++);
			write_pbm(path);
		}
	}
	printf("frames: %d pictures from %d frames -> %s-NNNN.pbm\n", saved, i, prefix);
	return 0;
}

/* the half-resolution 3D view (before the 1-bit dither) as a PPM, with the base palette */
static void write_view_ppm(const char *path)
{
	extern const byte *qembd_lowres_src;
	extern int qembd_lowres_stride;
	FILE *f = fopen(path, "wb");
	int x, y, x0 = qembd_lowres_rect[0] / 2, y0 = qembd_lowres_rect[1] / 2;
	int w = qembd_lowres_rect[2] / 2, h = qembd_lowres_rect[3] / 2;

	if (!f || !qembd_lowres_src)
		return;
	fprintf(f, "P6\n%d %d\n255\n", w, h);
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			byte c = qembd_lowres_src[(y0 + y) * qembd_lowres_stride + x0 + x];

			fputc(host_basepal[c * 3], f);
			fputc(host_basepal[c * 3 + 1], f);
			fputc(host_basepal[c * 3 + 2], f);
		}
	fclose(f);
}

/*
 * HSHOTS=<dir>: plays the demos in HDEMOS (default "1 2 3") as timedemos on the fake 33 ms clock and
 * writes, every HEVERY frames (default 25), the 3D view as <dir>/dD_NNNN.ppm and the LCD as
 * <dir>/dD_NNNN.pbm. Two builds (the old and the new renderer) give comparable pictures.
 */
static int shots_mode(const char *dir)
{
	const char *demos = getenv("HDEMOS") ? getenv("HDEMOS") : "1 2 3";
	int every = getenv("HEVERY") ? atoi(getenv("HEVERY")) : 25;
	const char *p;
	char path[512];
	int total = 0;

	for (p = demos; *p; p++) {
		int d, started = 0, i;
		char c[32];
#ifdef PD_NEW_RENDERER
		extern int pdr_c_nodes, pdr_c_leafs, pdr_c_faces, pdr_c_drawn, pdr_c_occl, pdr_c_spans, pdr_c_pixels;
		extern int pdr_c_lbuild, pdr_c_aliasmodels, pdr_c_atris, pdr_c_averts;
		double acc[11] = {0}, acc2[6] = {0};
		int nacc = 0;
#endif

		if (*p < '1' || *p > '9')
			continue;
		d = *p - '0';
		snprintf(c, sizeof(c), "timedemo demo%d\n", d);
		cmd(c);
		for (i = 0; i < 5000; i++) {
			run(1);
			if (cls.timedemo) {
				started = 1;
				key_dest = key_game;
			} else if (started)
				break;
#ifdef PD_NEW_RENDERER
			if (started) {
				int k = 0;

				acc[k++] += pdr_c_nodes; acc[k++] += pdr_c_leafs; acc[k++] += pdr_c_faces; acc[k++] += pdr_c_drawn;
				acc[k++] += pdr_c_occl; acc[k++] += pdr_c_spans; acc[k++] += pdr_c_pixels; acc[k++] += pdr_c_lbuild;
				acc[k++] += pdr_c_aliasmodels; acc[k++] += pdr_c_atris; acc[k++] += pdr_c_averts;
				{
					extern int pdr_c_calls, pdr_c_andrej, pdr_c_cliprej, pdr_c_norows, pdr_c_rows, pdr_c_verts;
					acc2[0] += pdr_c_calls; acc2[1] += pdr_c_andrej; acc2[2] += pdr_c_cliprej; acc2[3] += pdr_c_norows;
					acc2[4] += pdr_c_rows; acc2[5] += pdr_c_verts;
					pdr_c_calls = pdr_c_andrej = pdr_c_cliprej = pdr_c_norows = pdr_c_rows = pdr_c_verts = 0;
				}
				nacc++;
			}
#endif
			if (started && i % every == 0) {
				snprintf(path, sizeof(path), "%s/d%d_%04d.ppm", dir, d, i);
				write_view_ppm(path);
				snprintf(path, sizeof(path), "%s/d%d_%04d.pbm", dir, d, i);
				write_pbm(path);
				total++;
			}
		}
#ifdef PD_NEW_RENDERER
		if (nacc)
			printf("demo%d per frame: nodes %.0f leafs %.0f faces %.0f drawn %.0f occluded %.0f spans %.0f pixels %.0f "
				   "lightbuilds %.1f amodels %.1f atris %.0f averts %.0f\n", d, acc[0] / nacc, acc[1] / nacc, acc[2] / nacc,
				   acc[3] / nacc, acc[4] / nacc, acc[5] / nacc, acc[6] / nacc, acc[7] / nacc, acc[8] / nacc, acc[9] / nacc,
				   acc[10] / nacc);
		if (nacc)
			printf("   raster: calls %.0f (verts %.0f) all-outside %.0f clipped-away %.0f no-rows %.0f rows %.0f\n",
				   acc2[0] / nacc, acc2[5] / nacc, acc2[1] / nacc, acc2[2] / nacc, acc2[3] / nacc, acc2[4] / nacc);
#endif
	}
	printf("shots: %d pictures in %s\n", total, dir);
	return 0;
}

/* HMAPS=1: every map of the pak, turning and walking for a while in each (crash and limit checks) */
static int maps_mode(void)
{
	static const char *const maps[] = {"start", "e1m1", "e1m2", "e1m3", "e1m4", "e1m5", "e1m6", "e1m7", "e1m8"};
	int i;
	char c[64];

	for (i = 0; i < (int)(sizeof(maps) / sizeof(maps[0])); i++) {
		snprintf(c, sizeof(c), "map %s\n", maps[i]);
		cmd(c);
		run(40);
		cmd("+left\n");
		run(120);
		cmd("-left\n+forward\n+right\n");
		run(200);
		cmd("-forward\n-right\nimpulse 9\n+attack\n");
		run(60);
		cmd("-attack\n");
		run(10);
		printf("map %s: ok (frame %ld)\n", maps[i], frame_no);
	}
	return 0;
}

static int menu_test(void)
{
	char path[256];
	char *cfg;
	int i;

	snprintf(path, sizeof(path), "%s/config.cfg", com_gamedir);
	remove(path);
	cmd("map e1m1\n");
	run(30);
	cmd("menu_options\n");
	run(3);
	MCHECK(key_dest == key_menu, "options menu opens");
	MCHECK(options_cursor == 0, "cursor starts on the first row");

	dump_lcd("options-top");
	MCHECK(!host_options_dirty, "nothing changed yet");
	for (i = 0; i < 2; i++)
		press(K_DOWNARROW);
	MCHECK(options_cursor == 2, "2 x down reaches the Crank speed row");
	MCHECK(crank_speed.value == 1.4f, "crank speed starts at 1.4");
	press(K_RIGHTARROW);
	MCHECK(crank_speed.value == 1.6f, "right: 1.6");
	press(K_LEFTARROW);
	press(K_LEFTARROW);
	MCHECK(crank_speed.value == 1.2f, "left twice: 1.2");
	for (i = 0; i < 30; i++)
		press(K_LEFTARROW);
	MCHECK(crank_speed.value == 0.2f, "crank speed stops at 0.2");
	for (i = 0; i < 30; i++)
		press(K_RIGHTARROW);
	MCHECK(crank_speed.value == 3, "crank speed stops at 3");
	press(K_LEFTARROW);
	press(K_LEFTARROW);
	MCHECK(crank_speed.value == 2.6f, "left twice: 2.6");

	for (i = 0; i < 5; i++)
		press(K_DOWNARROW);
	MCHECK(options_cursor == 7, "5 more down reach the Texture detail row");
	MCHECK(d_mipcap.value == 1, "texture detail starts on low (d_mipcap 1)");

	dump_lcd("low");
	press(K_RIGHTARROW);
	dump_lcd("high");
	MCHECK(d_mipcap.value == 0, "right: high (d_mipcap 0)");
	MCHECK(host_options_dirty, "changes mark the options dirty");
	press(K_LEFTARROW);
	MCHECK(d_mipcap.value == 1, "left: low again");
	press(K_ENTER);
	MCHECK(d_mipcap.value == 0, "enter toggles too");

	press(K_DOWNARROW);
	MCHECK(options_cursor == 8, "down reaches the Interlaced row");
	MCHECK(r_interlace.value == 1, "interlaced starts on");
	dump_lcd("interlace-on");
	press(K_RIGHTARROW);
	dump_lcd("interlace-off");
	MCHECK(r_interlace.value == 0, "right: interlaced off");
	press(K_LEFTARROW);
	MCHECK(r_interlace.value == 1, "left: on again");
	press(K_ENTER);
	MCHECK(r_interlace.value == 0, "enter toggles too");

	press(K_DOWNARROW);
	MCHECK(options_cursor == 9, "down reaches the Draw distance row");
	dump_lcd("options-bottom");
	MCHECK(r_maxdist.value == 512, "draw distance starts at 512");
	press(K_RIGHTARROW);
	MCHECK(r_maxdist.value == 768, "right: 768");
	for (i = 0; i < 20; i++)
		press(K_RIGHTARROW);
	MCHECK(r_maxdist.value == 0, "the right end is unlimited (r_maxdist 0)");
	press(K_LEFTARROW);
	MCHECK(r_maxdist.value == 3072, "left: 3072");
	for (i = 0; i < 20; i++)
		press(K_LEFTARROW);
	MCHECK(r_maxdist.value == 256, "stops at 256");
	press(K_RIGHTARROW);
	press(K_RIGHTARROW);
	press(K_RIGHTARROW);
	MCHECK(r_maxdist.value == 768, "right x3: 768");

	press(K_DOWNARROW);
	MCHECK(options_cursor == 10, "down reaches the Max framerate row");
	MCHECK(pd_maxfps.value == 30, "max framerate starts at 30");
	press(K_LEFTARROW);
	MCHECK(pd_maxfps.value == 30, "left at 30 stays 30");
	press(K_RIGHTARROW);
	MCHECK(pd_maxfps.value == 50, "right: 50");
	press(K_RIGHTARROW);
	MCHECK(pd_maxfps.value == 0, "right: unlimited (0)");
	dump_lcd("options-maxfps");
	press(K_RIGHTARROW);
	MCHECK(pd_maxfps.value == 0, "right at unlimited stays unlimited");
	press(K_LEFTARROW);
	MCHECK(pd_maxfps.value == 50, "left: 50");

	press(K_DOWNARROW);
	MCHECK(options_cursor == 0, "down from the last row wraps to the first (no Video Options row here)");
	press(K_UPARROW);
	MCHECK(options_cursor == 10, "up from the first row lands on Max framerate");

	MCHECK(slurp(path) == NULL, "no config.cfg before leaving the menu");
	press(K_ESCAPE);
	MCHECK(!host_options_dirty, "leaving the menu clears the dirty flag");
	cfg = slurp(path);
	MCHECK(cfg != NULL, "leaving the menu writes config.cfg");
	MCHECK(cfg && strstr(cfg, "d_mipcap \"0"), "config.cfg holds d_mipcap 0");
	MCHECK(cfg && strstr(cfg, "r_interlace \"0"), "config.cfg holds r_interlace 0");
	MCHECK(cfg && strstr(cfg, "r_maxdist \"768"), "config.cfg holds r_maxdist 768");
	MCHECK(cfg && strstr(cfg, "crank_speed \"2.6"), "config.cfg holds crank_speed 2.6");
	MCHECK(cfg && strstr(cfg, "pd_maxfps \"50"), "config.cfg holds pd_maxfps 50");
	MCHECK(cfg && strstr(cfg, "cl_autofire"), "config.cfg holds the other archived options");
	MCHECK(cfg && !strstr(cfg, "bind "), "config.cfg holds no key bindings");

	Cvar_SetValue("d_mipcap", 1);
	Cvar_SetValue("r_interlace", 1);
	Cvar_SetValue("r_maxdist", 0);
	Cvar_SetValue("crank_speed", 1);
	Cvar_SetValue("pd_maxfps", 30);
	cmd("exec config.cfg\n");
	run(3);
	MCHECK(d_mipcap.value == 0, "config.cfg restores d_mipcap 0 (what the next launch does)");
	MCHECK(r_interlace.value == 0, "config.cfg restores r_interlace 0");
	MCHECK(r_maxdist.value == 768, "config.cfg restores r_maxdist 768");
	MCHECK(crank_speed.value == 2.6f, "config.cfg restores crank_speed 2.6");
	MCHECK(pd_maxfps.value == 50, "config.cfg restores pd_maxfps 50");

	/* Reset to defaults puts it back */
	cmd("menu_options\n");
	run(3);
	for (i = 0; i < 20 && options_cursor != 0; i++)	/* the cursor keeps its row when the menu reopens */
		press(K_DOWNARROW);
	MCHECK(options_cursor == 0, "cursor on Reset defaults");
	press(K_ENTER);
	run(3);
	MCHECK(d_mipcap.value == 1, "reset to defaults sets texture detail back to low");
	MCHECK(r_interlace.value == 1, "reset to defaults turns interlaced on");
	MCHECK(r_maxdist.value == 512, "reset to defaults sets the draw distance back to 512");
	MCHECK(crank_speed.value == 1.4f, "reset to defaults sets crank speed back to 1.4");
	MCHECK(pd_maxfps.value == 30, "reset to defaults sets max framerate back to 30");
	press(K_ESCAPE);
	cfg = slurp(path);
	MCHECK(cfg && strstr(cfg, "d_mipcap \"1"), "and the saved file says so");

	remove(path);

	/* the load menu (double-size font): a saved slot and the unused ones */
	cmd("save s11\n");
	run(3);
	cmd("menu_load\n");
	run(3);
	MCHECK(key_dest == key_menu, "load menu opens");
	dump_lcd("load");
	press(K_ESCAPE);
	snprintf(path, sizeof(path), "%s/s11.sav", com_gamedir);
	MCHECK(remove(path) == 0, "save to slot 11 wrote s11.sav");

	printf("%s\n", menu_fail ? "MENU TEST FAILED" : "menu test passed");
	return menu_fail ? 1 : 0;
}

int main(int argc, char **argv)
{
	static char a0[] = "quake";
	static char *av[] = {a0, NULL};
	int frames = argc > 1 ? atoi(argv[1]) : 150;

	gfx.getFrame = get_frame;
	gfx.markUpdatedRows = mark_rows;
	api.graphics = &gfx;
	qembd_pd = &api;
	cur_frame = frame_new;
	if (qembd_init(1, av) != 0)
		return 1;

	/* The port's picture options default to low texture detail, interlaced and a draw distance;
	 * the regression modes compare against the original picture, so switch them off there
	 * (the menu test checks the real defaults) */
	if (!getenv("HMENU"))
		cmd("d_mipcap 0\nr_interlace 0\nr_maxdist 0\n");
	if (getenv("HINTERLACE"))	/* HINTERLACE=1: every mode below with interlaced rendering */
		cmd("r_interlace 1\n");
	if (getenv("HCMD")) {		/* HCMD="...": console commands before any mode below */
		cmd(getenv("HCMD"));
		cmd("\n");
	}
	if (getenv("HGOLD"))
		return golden(getenv("HGOLD"));
	if (getenv("HFRAMES"))
		return frames_mode(getenv("HFRAMES"));
	if (getenv("HSHOTS"))
		return shots_mode(getenv("HSHOTS"));
	if (getenv("HMAPS"))
		return maps_mode();
	if (getenv("HMENU"))
		return menu_test();

	/* scenario 1: a level, walking, then overlays and view sizes */
	cur_scn = 1;
	cmd("map e1m1\n");
	step("load", NULL, 60);
	step("walk", "+forward\n+left\n", frames);
	step("fps counter", "scr_showfps 1\n", 40);
	step("notify lines", "echo hello notify line one\necho two\n", 30);
	step("notify decay", NULL, 200);
	step("console open", "toggleconsole\n", 40);
	step("console closed", "toggleconsole\n", 40);
	step("menu main", "menu_main\n", 30);
	step("menu options", "menu_options\n", 30);
	step("menu back to main", "togglemenu\n", 20);
	step("menu closed", "togglemenu\n", 30);
	step("viewsize 60", "viewsize 60\n", 40);
	step("viewsize 80", "viewsize 80\n", 30);
	step("viewsize 110", "viewsize 110\n", 40);
	step("viewsize 100", "viewsize 100\n", 30);
	step("pause", "pause\n", 10);
	step("unpause", "pause\n", 10);
	step("fov 120", "fov 120\n", 20);
	step("impulse 9 (weapons)", "impulse 9\n", 40);
	step("menu help", "menu_help\n", 20);
	step("menu help close", "togglemenu\n", 10);
	step("menu setup (translated pic)", "menu_setup\n", 20);
	step("menu setup close", "togglemenu\ntogglemenu\n", 10);
	step("menu keys", "menu_keys\n", 20);
	step("menu keys close", "togglemenu\ntogglemenu\n", 10);
	step("menu save", "menu_save\n", 20);
	step("menu load", "menu_load\n", 20);
	step("menu load close", "togglemenu\ntogglemenu\n", 10);
	step("menu quit", "menu_quit\n", 20);
	step("menu quit close", "togglemenu\ntogglemenu\n", 10);
	step("scoreboard", "+showscores\n", 30);
	step("scoreboard off", "-showscores\n", 10);
	step("crosshair", "crosshair 1\n", 20);
	step("stop", "-forward\n-left\n", 10);

	/* scenario 2: the attract-mode demos (HUD, weapons, monsters, effects) */
	cur_scn = 2;
	step("demo1", "playdemo demo1\n", frames * 3);
	step("demo2", "playdemo demo2\n", frames * 3);
	step("demo3", "playdemo demo3\n", frames * 3);
	step("demo1 fps counter", "playdemo demo1\nscr_showfps 1\n", frames * 2);

	printf("frames %ld | screen updates %ld | pending row pairs verified %ld | pairs expanded for overlays %ld\n",
		   frame_no, checks, pending_pairs_checked, flushed_pairs);
	printf("hook violations: %ld | LCD mismatches: %ld\n", hook_violations, mismatches);
	return (hook_violations || mismatches) ? 1 : 0;
}
