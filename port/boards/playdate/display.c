/*
 * Playdate video: Quake's 8-bit paletted frame -> 400x240 1-bit LCD.
 *
 * Every pixel is turned into a luminance, contrast-stretched
 * (Quake is dark and a 1-bit panel loses shadow detail), and then compared
 * against a threshold from a tile (ordered dithering). The "Dithering" option
 * (qembd_dither_mode) picks the tile and, for the 3D view, how it is used:
 *   patterns:   4x4 Bayer for everything except the low-res 3D view (see PD_LOWRES_3D below)
 *   bayer:      4x4 Bayer for every pixel, the 3D view included
 *   blue noise: a 32x32 blue noise tile for every pixel (bluenoise.h)
 *   diffusion:  error diffusion with a random threshold around the scene's median luminance
 * Quake renders at PD_RENDER_WIDTH x PD_RENDER_HEIGHT (320x240 by default) and is drawn 1:1, centred on the
 * panel; the border stays black.
 */

#include <math.h>
#include <string.h>
#include <quakembd.h>
#include "pd_port.h"
#include "bluenoise.h"

#ifndef PD_RENDER_WIDTH
#define PD_RENDER_WIDTH 320
#endif
#ifndef PD_RENDER_HEIGHT
#define PD_RENDER_HEIGHT 200
#endif

_Static_assert(PD_RENDER_WIDTH >= 320 && PD_RENDER_HEIGHT >= 200,
			   "Quake's menus need at least 320x200");
_Static_assert(PD_RENDER_WIDTH <= LCD_COLUMNS && PD_RENDER_HEIGHT <= LCD_ROWS,
			   "Rendering above the panel resolution is pointless");

/* Top-left of the image on the LCD. X must stay byte-aligned. */
#define X_OFF ((LCD_COLUMNS - PD_RENDER_WIDTH) / 2)
#define Y_OFF ((LCD_ROWS - PD_RENDER_HEIGHT) / 2)

_Static_assert(X_OFF % 8 == 0 && PD_RENDER_WIDTH % 8 == 0,
			   "Width and centring offset must be byte aligned");

/* Quake is dark and low-contrast; stretch [BLACK_POINT, WHITE_POINT] to the
 * full range before a mild gamma so surfaces separate after dithering. */
#define BLACK_POINT 4.0f
#define WHITE_POINT 120.0f
#define GAMMA 0.8f

#ifdef PD_LOWRES_3D
/*
 * The 3D view is rendered at half resolution and pixel-doubled by the engine
 * (D_UpscaleScreen). Inside that rectangle every uniform 2x2 block is drawn
 * as one of 5 fixed patterns instead of being dithered per pixel:
 *   0: 00/00   1: 10/00   2: 10/01   3: 10/11   4: 11/11   (top/bottom, 1 = white)
 * Levels 1-3 have equally bright variants:
 *   1: the white pixel in any of the 4 corners
 *   2: diagonal 10/01, anti-diagonal 01/10, vertical 10/10, horizontal 00/11
 *   3: the black pixel in any of the 4 corners
 * One is picked per palette colour ramp (index >> 4) so different surfaces of
 * the same brightness stay distinguishable.
 * Blocks that are not uniform (overlays drawn over the view) fall back to the
 * 4x4 Bayer dither.
 */
_Static_assert(Y_OFF % 2 == 0 && X_OFF % 2 == 0, "2x2 patterns need an even image origin");

extern int qembd_lowres_rect[4]; /* x, y, w, h in Quake pixels */

/* The half-resolution view, not yet expanded into the Quake buffer (see D_UpscaleScreen).
 * Row pair p (Quake rows 2p, 2p+1) is dithered straight from row p of qembd_lowres_src
 * while qembd_lowres_pending[p] is set; otherwise the expanded rows in the Quake buffer are used. */
extern int qembd_lowres_active;
extern const uint8_t *qembd_lowres_src;
extern int qembd_lowres_stride;
extern uint8_t qembd_lowres_pending[];
/* Row pair p on the LCD holds row p of qembd_lowres_src as it is now. With interlaced
 * rendering (pending 2) such pairs are not dithered again. */
extern uint8_t qembd_lowres_shown[];

static const uint8_t var_n[5] = {1, 4, 4, 4, 1};	/* variants per level */
static const uint8_t var_top[5][4] = {
	{0x0},
	{0x2, 0x1, 0x0, 0x0},	/* white at TL, TR, BL, BR */
	{0x2, 0x1, 0x2, 0x0},	/* diagonal, anti-diagonal, vertical, horizontal */
	{0x1, 0x2, 0x3, 0x3},	/* black at TL, TR, BL, BR */
	{0x3},
};
static const uint8_t var_bot[5][4] = {
	{0x0},
	{0x0, 0x0, 0x2, 0x1},
	{0x1, 0x2, 0x2, 0x3},
	{0x3, 0x3, 0x1, 0x2},
	{0x3},
};
static uint8_t ptop[256], pbot[256]; /* palette index -> 2-bit pattern for the top/bottom row */
static uint16_t pat2[256];			 /* ptop | pbot << 8, one lookup for both rows */
#endif

/*
 * Dithering modes (pd_dither in the Options menu, copied to qembd_dither_mode by VID_Update).
 * The threshold of LCD pixel (x, y) is tile[(y & tile_rmask) * tile_stride + (x & (tile_stride - 1))];
 * a tile is a whole number of LCD bytes wide and X_OFF is a multiple of 8, so LCD byte bx
 * uses the 8 thresholds at (bx & tile_bmask) * 8 of its row.
 */
#define DITHER_PATTERNS 0
#define DITHER_BAYER 1
#define DITHER_NOISE 2
#define DITHER_DIFFUSE 3
#define DITHER_MODES 4

_Static_assert(BLUENOISE_SIZE % 8 == 0 && (BLUENOISE_SIZE & (BLUENOISE_SIZE - 1)) == 0,
			   "The blue noise tile must be a power of two and a whole number of LCD bytes wide");

extern int qembd_dither_mode;
extern int qembd_frame_no; /* counts VID_Update calls, i.e. frames */

static uint8_t bayer_tile[4 * 8]; /* 4x4 Bayer matrix, each row repeated to fill one LCD byte */
static const uint8_t *tile;		  /* the active tile */
static unsigned tile_rmask;		  /* tile rows - 1 */
static unsigned tile_stride;	  /* tile width in pixels */
static unsigned tile_bmask;		  /* tile width in LCD bytes - 1; 0 = every byte uses the same 8 thresholds */
static int tile_mode = -1;		  /* DITHER_* the tile was selected for; -1 = none yet */

static uint8_t gamma_lut[256];
static uint32_t lum_clut[256]; /* palette lum[] was built from */
static uint8_t lum[256];	   /* palette index -> dithering luminance */
static int lum_valid;

/* The first of the 8 thresholds of LCD row y (a row of the active tile) */
static inline const uint8_t *tile_row(unsigned y)
{
	return tile + (y & tile_rmask) * tile_stride;
}

#ifdef PD_LOWRES_3D
/*
 * Error diffusion (DITHER_DIFFUSE). A pixel is white when its luminance plus the error carried
 * to it exceeds a random threshold, and the difference from the 0 or 255 that was output is
 * carried to its neighbours (Floyd-Steinberg: 7/16 right, 3/16 down-left, 5/16 down, 1/16 down-right).
 *
 * Diffusion keeps the average brightness whatever the threshold is, so a threshold that follows
 * the scene alone would change little. The median luminance of the 3D view therefore also drives
 * a tone curve (dlum) that stretches it to the middle grey the thresholds are centred on: half of
 * the pixels of a dark or a bright scene end up on either side, and the dithering spends its
 * levels where the picture has its detail.
 *
 * Everything an LCD row pair gets is a function of that pair's source rows and position only, so
 * interlaced frames can keep rows, rectangles can be drawn on their own, and the picture does not
 * flicker: the pair is diffused top row into bottom row and left to right, starting from no error,
 * and the random thresholds are hashed from the pixel position instead of drawn from a running
 * generator.
 */
#define DIFF_MID 128		/* the median luminance is stretched to this */
#define DIFF_JBITS 7		/* thresholds are 128 +/- 2^(DIFF_JBITS - 1) */
#define DIFF_T0 (128 - (1 << (DIFF_JBITS - 1)))
#define DIFF_MED_MIN 12		/* the median the curve follows stays within these */
#define DIFF_MED_MAX 200
#define DIFF_MED_STEP 2		/* ... and moves when the smoothed median is this far from it */

static uint8_t dlum[256];	/* palette index -> luminance after the tone curve */
static int dlum_valid;
static int diff_med = -1;	/* the median dlum was built for; -1 = none yet */
static int diff_med_f;		/* smoothed median, x16 */
static int diff_frame = -1; /* the frame the median was last sampled in */

/* The median luminance of the 3D view, from a sparse sample of its half-resolution rows. */
static int diff_sample_median(void)
{
	const int x0 = qembd_lowres_rect[0] >> 1, x1 = (qembd_lowres_rect[0] + qembd_lowres_rect[2]) >> 1;
	const int y0 = qembd_lowres_rect[1] >> 1, y1 = (qembd_lowres_rect[1] + qembd_lowres_rect[3]) >> 1;
	uint16_t hist[256];
	int n = 0, acc = 0, i;

	memset(hist, 0, sizeof hist);
	for (int py = y0; py < y1; py += 2)
		for (int px = x0; px < x1; px += 4, n++)
			hist[lum[qembd_lowres_src[py * qembd_lowres_stride + px]]]++;

	for (i = 0; i < 255; i++)
	{
		acc += hist[i];
		if (acc * 2 > n)
			break;
	}
	return i;
}

/* Once per frame: follow the scene's median, smoothed and with hysteresis so the tone curve (and
 * with it every pixel of the picture) only changes when the scene really does. */
static void diff_follow_median(void)
{
	const int raw = diff_sample_median();

	if (diff_med < 0)
	{
		diff_med_f = raw * 16;
		diff_med = raw;
		dlum_valid = 0;
		return;
	}
	diff_med_f += (raw * 16 - diff_med_f) >> 2;

	const int m = (diff_med_f + 8) >> 4;

	if (m - diff_med >= DIFF_MED_STEP || diff_med - m >= DIFF_MED_STEP)
	{
		diff_med = m;
		dlum_valid = 0;
	}
}

/* The tone curve: 0 -> 0, median -> DIFF_MID, 255 -> 255, straight in between. As the picture
 * changes with it, the rows an interlaced frame keeps have to be drawn again. */
static void diff_build_curve(void)
{
	const int m = diff_med < DIFF_MED_MIN ? DIFF_MED_MIN : (diff_med > DIFF_MED_MAX ? DIFF_MED_MAX : diff_med);

	for (int i = 0; i < 256; i++)
	{
		const int l = lum[i];

		dlum[i] = (uint8_t)(l <= m ? l * DIFF_MID / m : DIFF_MID + (l - m) * (255 - DIFF_MID) / (255 - m));
	}
	dlum_valid = 1;
#ifdef PD_LOWRES_3D
	memset(qembd_lowres_shown, 0, PD_RENDER_HEIGHT / 2);
#endif
}

/* The random numbers of LCD byte bx of LCD row y: two 32-bit words, from a hash of the position, each
 * holding the 7-bit threshold offsets of 4 pixels (bits 31-25, 24-18, 17-11 and 10-4) */
static inline void diff_noise(unsigned y, unsigned bx, unsigned w[2])
{
	unsigned h = bx * 0x9E3779B1u + y * 0x85EBCA6Bu;

	h ^= h >> 15;
	h *= 0x2C1B3C6Du;
	h ^= h >> 12;
	w[0] = h = h * 1664525u + 1013904223u;
	w[1] = h * 1664525u + 1013904223u;
}

/* One pixel: v is its luminance plus the carried error, r its random threshold offset (0 to 127).
 * Returns -1 for white (v is above the threshold) or 0 for black; *e is the error to spread over the
 * neighbours. */
static inline __attribute__((always_inline)) int diff_pixel(int v, int r, int *e)
{
	const int m = (DIFF_T0 + r - v) >> 31;

	*e = v - (m & 255);
	return m;
}

typedef struct
{
	int carry;	 /* the error carried to the next pixel of the row */
	int e1, e2;	 /* the errors of the two pixels before this one, in scan order */
	unsigned bits; /* the output byte being assembled */
} diff_scan;

/*
 * One pixel at LCD column x of a row, l its luminance (after the tone curve), r its random threshold
 * offset. Scanning left to right (rtl = 0) or right to left (rtl = 1, with the neighbours mirrored).
 * The top row (down != NULL) stores in down[] what it gives each pixel of the bottom row; the bottom
 * row (down == NULL) adds what it was given, up[x].
 */
static inline __attribute__((always_inline)) void diff_step(diff_scan *c, int l, int r, int x, int rtl,
															const int16_t *restrict up, int16_t *restrict down)
{
	int e, v = l + c->carry;

	if (up)
		v += up[x];

	const int m = diff_pixel(v, r, &e);

	c->bits = rtl ? (c->bits >> 1) - ((unsigned)m << 7) : (c->bits << 1) - (unsigned)m;
	c->carry = (e * 7) >> 4;
	if (down)
	{
		down[rtl ? x + 1 : x - 1] = (int16_t)((c->e2 + c->e1 * 5 + e * 3) >> 4); /* the pixel behind is complete */
		c->e2 = c->e1;
		c->e1 = e;
	}
}

/*
 * One row of a pair, bytes [b0, b1) in scan order. s is the source row (see diffuse_pair for half);
 * a half-resolution pixel is loaded once and feeds its two LCD pixels.
 */
static inline __attribute__((always_inline)) void diffuse_row(uint8_t *d, const uint8_t *restrict s, int half,
															  int rtl, int b0, int b1, unsigned y,
															  const int16_t *restrict up, int16_t *restrict down)
{
	diff_scan c = {0, 0, 0, 0};

	for (int n = b0; n < b1; n++)
	{
		const int bx = rtl ? b1 - 1 - (n - b0) : n;
		unsigned w[2];

		diff_noise(y, bx, w);
		c.bits = 0;
		if (half)
		{
			for (int j = 0; j < 4; j++)
			{
				const int sx = rtl ? bx * 4 + 3 - j : bx * 4 + j;
				const int l = dlum[s[sx]];
				const unsigned wj = w[j >> 1] >> (18 - 14 * (j & 1)); /* the offsets of scan pixels 2j and 2j + 1: bits 13-7 and 6-0 */

				diff_step(&c, l, (wj >> 7) & 127, rtl ? sx * 2 + 1 : sx * 2, rtl, up, down);
				diff_step(&c, l, wj & 127, rtl ? sx * 2 : sx * 2 + 1, rtl, up, down);
			}
		}
		else
		{
			for (int k = 0; k < 8; k++)
			{
				const int x = rtl ? bx * 8 + 7 - k : bx * 8 + k;

				diff_step(&c, dlum[s[x]], (w[k >> 2] >> (25 - 7 * (k & 3))) & 127, x, rtl, up, down);
			}
		}
		d[bx] = (uint8_t)c.bits;
	}
	if (down) /* the last pixel scanned has no neighbour after it */
		down[rtl ? b0 * 8 : b1 * 8 - 1] = (int16_t)((c.e2 + c.e1 * 5) >> 4);
}

/*
 * LCD bytes [b0, b1) of the row pair starting at LCD row y. st and sb are the two source rows
 * (palette indices, offset like dither_span's srow); with half they are one half-resolution row
 * instead, offset so that st + bx * 4 is the first source pixel of byte bx. The scan direction
 * alternates from pair to pair, and from the top row to the bottom row, so the pairs do not all
 * start in step with each other.
 */
static inline __attribute__((always_inline)) void diffuse_pair(uint8_t *dt, uint8_t *db, const uint8_t *st,
															   const uint8_t *sb, int half, int b0, int b1,
															   unsigned y)
{
	int16_t below_buf[LCD_COLUMNS + 2];
	int16_t *const below = below_buf + 1; /* what the top row gives each pixel of the bottom row */

	if (y & 2)
	{
		diffuse_row(dt, st, half, 1, b0, b1, y, NULL, below);
		diffuse_row(db, half ? st : sb, half, 0, b0, b1, y + 1, below, NULL);
	}
	else
	{
		diffuse_row(dt, st, half, 0, b0, b1, y, NULL, below);
		diffuse_row(db, half ? st : sb, half, 1, b0, b1, y + 1, below, NULL);
	}
}

static void diffuse_rows(uint8_t *dt, uint8_t *db, const uint8_t *st, const uint8_t *sb, int b0, int b1, unsigned y)
{
	diffuse_pair(dt, db, st, sb, 0, b0, b1, y);
}

static void diffuse_half(uint8_t *dt, uint8_t *db, const uint8_t *lrow, int b0, int b1, unsigned y)
{
	diffuse_pair(dt, db, lrow, lrow, 1, b0, b1, y);
}
#endif

/* Switch to a dithering mode. Whatever the LCD shows now was dithered the old way, so the
 * half-resolution rows an interlaced frame keeps (qembd_lowres_shown) are drawn again. */
static void select_dither(int mode)
{
	if (mode == DITHER_NOISE)
	{
		tile = bluenoise;
		tile_rmask = BLUENOISE_SIZE - 1;
		tile_stride = BLUENOISE_SIZE;
		tile_bmask = BLUENOISE_SIZE / 8 - 1;
	}
	else
	{
		tile = bayer_tile;
		tile_rmask = 3;
		tile_stride = 8;
		tile_bmask = 0;
	}
	tile_mode = mode;
#ifdef PD_LOWRES_3D
	diff_med = -1; /* follow the scene's median from the first frame in diffusion mode */
	dlum_valid = 0;
	diff_frame = -1;
	memset(qembd_lowres_shown, 0, PD_RENDER_HEIGHT / 2);
#endif
}

int qembd_get_width()
{
	return PD_RENDER_WIDTH;
}

int qembd_get_height()
{
	return PD_RENDER_HEIGHT;
}

void qembd_vidinit()
{
	static const uint8_t bayer4[4][4] = {
		{0, 8, 2, 10},
		{12, 4, 14, 6},
		{3, 11, 1, 9},
		{15, 7, 13, 5},
	};

	for (int i = 0; i < 256; i++)
	{
		float v = (i - BLACK_POINT) / (WHITE_POINT - BLACK_POINT);

		v = v < 0 ? 0 : (v > 1 ? 1 : v);
		gamma_lut[i] = (uint8_t)(255.0f * powf(v, GAMMA) + 0.5f);
	}

	for (int y = 0; y < 4; y++)
		for (int x = 0; x < 8; x++)
			bayer_tile[y * 8 + x] = bayer4[y][x & 3] * 16 + 8;

	select_dither(DITHER_PATTERNS);
	memset(qembd_pd->graphics->getFrame(), 0, LCD_ROWSIZE * LCD_ROWS);
}

/* Bayer-dither 8 Quake pixels into one LCD byte (MSB = leftmost, 1 = white).
 * The 4-wide matrix repeats twice per byte (X_OFF is a multiple of 8, so the
 * phase matches the Quake column). */
static inline uint8_t dither8(const uint8_t *sp, unsigned t0, unsigned t1, unsigned t2, unsigned t3)
{
	return (uint8_t)((lum[sp[0]] > t0) << 7 |
					 (lum[sp[1]] > t1) << 6 |
					 (lum[sp[2]] > t2) << 5 |
					 (lum[sp[3]] > t3) << 4 |
					 (lum[sp[4]] > t0) << 3 |
					 (lum[sp[5]] > t1) << 2 |
					 (lum[sp[6]] > t2) << 1 |
					 (lum[sp[7]] > t3));
}

/* The same with 8 different thresholds (a tile wider than 4 pixels) */
static inline uint8_t dither8t(const uint8_t *sp, const uint8_t *t)
{
	return (uint8_t)((lum[sp[0]] > t[0]) << 7 |
					 (lum[sp[1]] > t[1]) << 6 |
					 (lum[sp[2]] > t[2]) << 5 |
					 (lum[sp[3]] > t[3]) << 4 |
					 (lum[sp[4]] > t[4]) << 3 |
					 (lum[sp[5]] > t[5]) << 2 |
					 (lum[sp[6]] > t[6]) << 1 |
					 (lum[sp[7]] > t[7]));
}

/* Dither LCD bytes [b0, b1) of one Quake row. srow is offset so that
 * srow + bx * 8 is the first Quake pixel of byte bx. thr is the tile row of this LCD row. */
static inline void dither_span(uint8_t *dst, const uint8_t *srow, int b0, int b1, const uint8_t *thr)
{
	if (tile_bmask)
	{
		const unsigned m = tile_bmask;

		for (int bx = b0; bx < b1; bx++)
			dst[bx] = dither8t(srow + bx * 8, thr + (bx & m) * 8);
		return;
	}

	const unsigned t0 = thr[0], t1 = thr[1], t2 = thr[2], t3 = thr[3];

	for (int bx = b0; bx < b1; bx++)
		dst[bx] = dither8(srow + bx * 8, t0, t1, t2, t3);
}

#ifdef PD_LOWRES_3D
/* Unaligned-safe 32-bit load (a single LDR on Cortex-M7). Pixel order is
 * little endian: byte 0 is the leftmost pixel. */
static inline uint32_t ld32(const uint8_t *p)
{
	uint32_t v;

	memcpy(&v, p, sizeof v);
	return v;
}

/* One LCD byte (4 blocks) of a row inside the low-res view: uniform 2x2 blocks
 * become a pattern, anything else (overlays) is dithered per pixel. sp is this
 * row, op the other row of the 2x2 blocks. */
static inline uint8_t lowres_byte(const uint8_t *sp, const uint8_t *op, const uint8_t *pat, const uint8_t *thr)
{
	unsigned bits = 0;

	for (int i = 0; i < 8; i += 2)
	{
		uint8_t a = sp[i];
		unsigned p;

		if (a == sp[i + 1] && a == op[i] && a == op[i + 1])
			p = pat[a];
		else
			p = (lum[a] > thr[i & 3]) << 1 | (lum[sp[i + 1]] > thr[(i + 1) & 3]);
		bits = bits << 2 | p;
	}
	return (uint8_t)bits;
}

/* Is every 2x2 block of LCD bytes [b0, b1) of this expanded row pair uniform, i.e. is it still the
 * low-res view that diffuse_half would have drawn, rather than something drawn over it? */
static int diffusable(const uint8_t *st, const uint8_t *sb, int b0, int b1)
{
	uint32_t d = 0;

	for (int bx = b0; bx < b1; bx++)
	{
		const uint32_t t0 = ld32(st + bx * 8), t1 = ld32(st + bx * 8 + 4);

		d |= (t0 ^ ld32(sb + bx * 8)) | (t1 ^ ld32(sb + bx * 8 + 4)) |
			 (((t0 ^ (t0 >> 8)) | (t1 ^ (t1 >> 8))) & 0x00ff00ffu);
	}
	return d == 0;
}

/*
 * LCD bytes [b0, b1) of the row pair (top, bottom = top + 1) inside the
 * low-res view. Almost every byte is four uniform blocks, so test that with
 * a few word operations and produce both rows from one lookup per block.
 */
static void lowres_pair(uint8_t *dt, uint8_t *db, const uint8_t *st, const uint8_t *sb,
						int b0, int b1, const uint8_t *thr_t, const uint8_t *thr_b, unsigned y)
{
	if (tile_mode == DITHER_DIFFUSE && diffusable(st, sb, b0, b1))
	{
		diffuse_rows(dt, db, st, sb, b0, b1, y);
		return;
	}
	if (tile_mode != DITHER_PATTERNS)
	{
		/* no 2x2 patterns (or something was drawn over this pair in diffusion mode): every pixel
		 * against its own threshold */
		dither_span(dt, st, b0, b1, thr_t);
		dither_span(db, sb, b0, b1, thr_b);
		return;
	}

	for (int bx = b0; bx < b1; bx++)
	{
		const uint8_t *t = st + bx * 8;
		const uint8_t *b = sb + bx * 8;
		const uint32_t t0 = ld32(t), t1 = ld32(t + 4);

		/* both rows identical, and every pixel equal to its right neighbour
		 * inside its block (bytes 0-1 and 2-3 of each word) */
		if (t0 == ld32(b) && t1 == ld32(b + 4) &&
			!(((t0 ^ (t0 >> 8)) | (t1 ^ (t1 >> 8))) & 0x00ff00ffu))
		{
			/* each pat2 entry: top pattern in bits 0-1, bottom in bits 8-9 */
			const uint32_t v = (uint32_t)pat2[t0 & 0xff] << 6 |
							   (uint32_t)pat2[(t0 >> 16) & 0xff] << 4 |
							   (uint32_t)pat2[t1 & 0xff] << 2 |
							   (uint32_t)pat2[(t1 >> 16) & 0xff];

			dt[bx] = (uint8_t)v;
			db[bx] = (uint8_t)(v >> 8);
		}
		else
		{
			dt[bx] = lowres_byte(t, b, ptop, thr_t);
			db[bx] = lowres_byte(b, t, pbot, thr_b);
		}
	}
}

/*
 * LCD bytes [b0, b1) of a row pair straight from one half-resolution row.
 * lrow is offset so that lrow + bx * 4 is the first source pixel of byte bx.
 * Every 2x2 block is uniform here, so this produces exactly what lowres_pair
 * produces from the expanded rows.
 */
static void lowres_direct(uint8_t *dt, uint8_t *db, const uint8_t *lrow, int b0, int b1,
						  const uint8_t *thr_t, const uint8_t *thr_b, unsigned y)
{
	if (tile_mode == DITHER_DIFFUSE)
	{
		diffuse_half(dt, db, lrow, b0, b1, y);
		return;
	}
	if (tile_mode != DITHER_PATTERNS)
	{
		/* each source pixel is two LCD pixels wide: it meets two thresholds per row */
		const unsigned m = tile_bmask;

		for (int bx = b0; bx < b1; bx++)
		{
			const uint8_t *s = lrow + bx * 4;
			const unsigned l0 = lum[s[0]], l1 = lum[s[1]], l2 = lum[s[2]], l3 = lum[s[3]];
			const uint8_t *tt = thr_t + (bx & m) * 8;
			const uint8_t *tb = thr_b + (bx & m) * 8;

			dt[bx] = (uint8_t)((l0 > tt[0]) << 7 | (l0 > tt[1]) << 6 |
							   (l1 > tt[2]) << 5 | (l1 > tt[3]) << 4 |
							   (l2 > tt[4]) << 3 | (l2 > tt[5]) << 2 |
							   (l3 > tt[6]) << 1 | (l3 > tt[7]));
			db[bx] = (uint8_t)((l0 > tb[0]) << 7 | (l0 > tb[1]) << 6 |
							   (l1 > tb[2]) << 5 | (l1 > tb[3]) << 4 |
							   (l2 > tb[4]) << 3 | (l2 > tb[5]) << 2 |
							   (l3 > tb[6]) << 1 | (l3 > tb[7]));
		}
		return;
	}

	for (int bx = b0; bx < b1; bx++)
	{
		const uint32_t w = ld32(lrow + bx * 4);
		const uint32_t v = (uint32_t)pat2[w & 0xff] << 6 |
						   (uint32_t)pat2[(w >> 8) & 0xff] << 4 |
						   (uint32_t)pat2[(w >> 16) & 0xff] << 2 |
						   (uint32_t)pat2[w >> 24];

		dt[bx] = (uint8_t)v;
		db[bx] = (uint8_t)(v >> 8);
	}
}
#endif

void qembd_fillrect(uint8_t *src, uint32_t *clut,
					uint16_t x, uint16_t y, uint16_t xsize, uint16_t ysize)
{
	uint8_t *frame = qembd_pd->graphics->getFrame();
	int mode = qembd_dither_mode;

	if (mode < 0 || mode >= DITHER_MODES)
		mode = DITHER_PATTERNS;
	if (mode != tile_mode)
		select_dither(mode);

	/* Palette (0x00RRGGBB) -> gamma-corrected luminance. The palette rarely
	 * changes (only on flashes/gamma), so rebuild only when it differs. */
	if (!lum_valid || memcmp(lum_clut, clut, sizeof(lum_clut)) != 0)
	{
		for (int i = 0; i < 256; i++)
		{
			uint32_t c = clut[i];
			uint32_t l = (77 * ((c >> 16) & 0xff) + 151 * ((c >> 8) & 0xff) +
						  28 * (c & 0xff)) >>
						 8;
			lum[i] = gamma_lut[l];
		}
#ifdef PD_LOWRES_3D
		for (int i = 0; i < 256; i++)
		{
			int lv = (lum[i] * 4 + 128) >> 8;

			int v = (i >> 4) % var_n[lv];

			ptop[i] = var_top[lv][v];
			pbot[i] = var_bot[lv][v];
			pat2[i] = (uint16_t)(ptop[i] | pbot[i] << 8);
		}
		memset(qembd_lowres_shown, 0, PD_RENDER_HEIGHT / 2);
		dlum_valid = 0;
#endif
		memcpy(lum_clut, clut, sizeof(lum_clut));
		lum_valid = 1;
	}

#ifdef PD_LOWRES_3D
	if (mode == DITHER_DIFFUSE)
	{
		if (qembd_frame_no != diff_frame && qembd_lowres_active && qembd_lowres_rect[2] > 0)
		{
			diff_frame = qembd_frame_no;
			diff_follow_median();
		}
		if (!dlum_valid)
			diff_build_curve();
	}
#endif

	/* Quake rect -> LCD bytes. X_OFF and the width are byte aligned, so
	 * widening to whole bytes only redraws pixels inside the image. */
	int b0 = (X_OFF + x) >> 3;
	int b1 = (X_OFF + x + xsize + 7) >> 3;
	int bmax = (X_OFF + PD_RENDER_WIDTH) >> 3;

	if (b1 > bmax)
		b1 = bmax;
	if (y + ysize > PD_RENDER_HEIGHT)
		ysize = PD_RENDER_HEIGHT - y;

#ifdef PD_LOWRES_3D
	/* Patterns and diffusion span two rows: refresh whole row pairs. */
	if (ysize)
	{
		int y1 = (y + ysize + 1) & ~1;

		y &= ~1;
		ysize = y1 - y;
		if (y + ysize > PD_RENDER_HEIGHT)
			ysize = PD_RENDER_HEIGHT - y;
	}

	int rb0 = 0, rb1 = 0, ry0 = 0, ry1 = 0;

	if (qembd_lowres_rect[2] > 0)
	{
		rb0 = (X_OFF + qembd_lowres_rect[0] + 7) >> 3;
		rb1 = (X_OFF + qembd_lowres_rect[0] + qembd_lowres_rect[2]) >> 3;
		ry0 = qembd_lowres_rect[1];
		ry1 = ry0 + qembd_lowres_rect[3];
	}
#endif

	for (int qy = y; qy < y + ysize;)
	{
		const uint8_t *srow = src + qy * PD_RENDER_WIDTH - X_OFF;
		uint8_t *dst = frame + (Y_OFF + qy) * LCD_ROWSIZE;

#ifdef PD_LOWRES_3D
		if (qy >= ry0 && qy < ry1)
		{
			/* qy is even here: y, ry0 and ry1 are, and rows before ry0 step by one */
			const uint8_t *sbot = srow + PD_RENDER_WIDTH;
			uint8_t *dbot = dst + LCD_ROWSIZE;
			const uint8_t *thr_t = tile_row(Y_OFF + qy);
			const uint8_t *thr_b = tile_row(Y_OFF + qy + 1);
			int f0 = rb0 < b0 ? b0 : (rb0 > b1 ? b1 : rb0);	/* view columns: [f0, f1) */
			int f1 = rb1 < f0 ? f0 : (rb1 > b1 ? b1 : rb1);

			dither_span(dst, srow, b0, f0, thr_t);
			dither_span(dbot, sbot, b0, f0, thr_b);
			if (qembd_lowres_active && qembd_lowres_pending[qy >> 1] == 2 && qembd_lowres_shown[qy >> 1])
				; /* interlaced: not redrawn this frame, and the LCD still shows it */
			else if (qembd_lowres_active && qembd_lowres_pending[qy >> 1])
			{
				lowres_direct(dst, dbot, qembd_lowres_src + (qy >> 1) * qembd_lowres_stride - (X_OFF >> 1),
							  f0, f1, thr_t, thr_b, Y_OFF + qy);
				qembd_lowres_shown[qy >> 1] = f0 == rb0 && f1 == rb1;
			}
			else
				lowres_pair(dst, dbot, srow, sbot, f0, f1, thr_t, thr_b, Y_OFF + qy);
			if (!qembd_lowres_active || !qembd_lowres_pending[qy >> 1])
				qembd_lowres_shown[qy >> 1] = 0;
			dither_span(dst, srow, f1, b1, thr_t);
			dither_span(dbot, sbot, f1, b1, thr_b);
			qy += 2;
			continue;
		}
#endif

		dither_span(dst, srow, b0, b1, tile_row(Y_OFF + qy));
#ifdef PD_LOWRES_3D
		qembd_lowres_shown[qy >> 1] = 0;
#endif
		qy++;
	}
}

/* The LCD frame buffer may have been drawn over by the system (menu, lock screen):
 * dither every row again. */
void qembd_display_invalidate(void)
{
#ifdef PD_LOWRES_3D
	memset(qembd_lowres_shown, 0, PD_RENDER_HEIGHT / 2);
#endif
}

void qembd_refresh()
{
	qembd_pd->graphics->markUpdatedRows(0, LCD_ROWS - 1);
}
