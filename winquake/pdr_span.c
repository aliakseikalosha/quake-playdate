/*
 * pdr_span.c -- span drawing for the Playdate renderer (see pdr.h)
 *
 * Spans are textured straight from the mip texture (no surface cache): every 8 pixels the
 * perspective-correct s and t are computed (as D_DrawSpans8), and the light, bilinearly
 * sampled from the face's light block at those points, is stepped linearly in between and
 * applied through the colormap. Water and sky are drawn as Quake draws them.
 */
#include "pdr.h"

int		pdr_sintable[PDR_SIN_SIZE];
int		pdr_intsintable[PDR_SIN_SIZE];

void PDR_InitTurb (void)
{
	int		i;

	for (i=0 ; i<PDR_SIN_SIZE ; i++)
	{
		pdr_sintable[i] = PDR_TURB_AMP + sinf(i*3.14159f*2.0f/PDR_TURB_CYCLE)*PDR_TURB_AMP;
		pdr_intsintable[i] = PDR_TURB_AMP2 + sinf(i*3.14159f*2.0f/PDR_TURB_CYCLE)*PDR_TURB_AMP2;
	}
}

/*
==============================================================================

LIT TEXTURED SPANS

==============================================================================
*/

/* light (8.16, sample units) at cache-relative surface coordinates s, t (16.16) */
static inline int SampleLight (const pdr_spanctx_t *c, int s, int t)
{
	int			ls, lt, fs, ft, top, bot;
	const byte	*p;

	if (!c->light)
		return c->lconst;
	ls = s >> c->lshift;
	lt = t >> c->lshift;
	if (ls > c->lmaxs)
		ls = c->lmaxs;
	if (lt > c->lmaxt)
		lt = c->lmaxt;
	p = c->light + (lt >> 16) * c->lw + (ls >> 16);
	fs = (ls >> 8) & 0xff;
	ft = (lt >> 8) & 0xff;
	// the block is padded, so p[1] and p[lw] can be read at the edges (their weight is 0 there)
	top = (p[0] << 8) + (p[1] - p[0]) * fs;
	bot = (p[c->lw] << 8) + (p[c->lw + 1] - p[c->lw]) * fs;
	return (top << 8) + (bot - top) * ft;
}

static inline int ClampS (int s, int max)
{
	if (s > max)
		return max;
	if (s < 0)
		return 0;
	return s;
}

static void SpanLitPow2 (byte *dst, const byte *tex, const byte *cmap, int s, int t, int l,
						 int sstep, int tstep, int lstep, int n, int tshift, int smask, int tmask)
{
	do
	{
		*dst++ = cmap[((l >> 10) & 0x3f00) + tex[((t >> tshift) & tmask) + ((s >> 16) & smask)]];
		s += sstep;
		t += tstep;
		l += lstep;
	} while (--n);
}

static void SpanLitWrap (byte *dst, const byte *tex, const byte *cmap, int s, int t, int l,
						 int sstep, int tstep, int lstep, int n, int tw, int th)
{
	int		sw = tw << 16, tht = th << 16;

	s %= sw;
	if (s < 0)
		s += sw;
	t %= tht;
	if (t < 0)
		t += tht;
	do
	{
		*dst++ = cmap[((l >> 10) & 0x3f00) + tex[(t >> 16) * tw + (s >> 16)]];
		s += sstep;
		while (s >= sw)
			s -= sw;
		while (s < 0)
			s += sw;
		t += tstep;
		while (t >= tht)
			t -= tht;
		while (t < 0)
			t += tht;
		l += lstep;
	} while (--n);
}

static void SpanLit (const pdr_spanctx_t *c, byte *dst, int y, int x0, int count)
{
	float	sdivz, tdivz, zi, z, du = (float)x0, dv = (float)y;
	float	sdivz8 = c->sdivzstepu * PDR_SUBDIV, tdivz8 = c->tdivzstepu * PDR_SUBDIV, zi8 = c->zistepu * PDR_SUBDIV;
	int		s, t, l, snext, tnext, lnext, sstep, tstep, lstep, n;

	sdivz = c->sdivzorigin + dv*c->sdivzstepv + du*c->sdivzstepu;
	tdivz = c->tdivzorigin + dv*c->tdivzstepv + du*c->tdivzstepu;
	zi = c->ziorigin + dv*c->zistepv + du*c->zistepu;
	z = (float)0x10000 / zi;
	s = ClampS ((int)(sdivz * z) + c->sadjust, c->bbextents);
	t = ClampS ((int)(tdivz * z) + c->tadjust, c->bbextentt);
	l = SampleLight (c, s, t);

	do
	{
		n = count >= PDR_SUBDIV ? PDR_SUBDIV : count;
		count -= n;
		if (count)
		{
			sdivz += sdivz8;
			tdivz += tdivz8;
			zi += zi8;
			z = (float)0x10000 / zi;
			snext = (int)(sdivz * z) + c->sadjust;
			if (snext > c->bbextents)
				snext = c->bbextents;
			else if (snext < PDR_SUBDIV)
				snext = PDR_SUBDIV;	// prevent round-off error on <0 steps from running off the edge
			tnext = (int)(tdivz * z) + c->tadjust;
			if (tnext > c->bbextentt)
				tnext = c->bbextentt;
			else if (tnext < PDR_SUBDIV)
				tnext = PDR_SUBDIV;
			sstep = (snext - s) >> PDR_SUBDIV_SHIFT;
			tstep = (tnext - t) >> PDR_SUBDIV_SHIFT;
			lnext = SampleLight (c, snext, tnext);
			lstep = (lnext - l) >> PDR_SUBDIV_SHIFT;
		}
		else
		{
			// last pixel of the span, so it can't step off the polygon
			snext = s;
			tnext = t;
			lnext = l;
			sstep = tstep = lstep = 0;
			if (n > 1)
			{
				float	last = (float)(n - 1);
				float	ls = sdivz + c->sdivzstepu * last;
				float	lt = tdivz + c->tdivzstepu * last;
				float	lz = (float)0x10000 / (zi + c->zistepu * last);

				snext = ClampS ((int)(ls * lz) + c->sadjust, c->bbextents);
				if (snext < PDR_SUBDIV)
					snext = PDR_SUBDIV;
				tnext = ClampS ((int)(lt * lz) + c->tadjust, c->bbextentt);
				if (tnext < PDR_SUBDIV)
					tnext = PDR_SUBDIV;
				sstep = (snext - s) / (n - 1);
				tstep = (tnext - t) / (n - 1);
				lnext = SampleLight (c, snext, tnext);
				lstep = (lnext - l) / (n - 1);
			}
		}

		if (c->tshift >= 0)
			SpanLitPow2 (dst, c->tex, c->colormap, s + c->soff, t + c->toff, l, sstep, tstep, lstep, n,
						 c->tshift, c->smask, c->tmask);
		else
			SpanLitWrap (dst, c->tex, c->colormap, s + c->soff, t + c->toff, l, sstep, tstep, lstep, n,
						 c->tw, c->th);
		dst += n;
		s = snext;
		t = tnext;
		l = lnext;
	} while (count > 0);
}

/*
==============================================================================

WATER (Turbulent8)

==============================================================================
*/

static void SpanTurb (const pdr_spanctx_t *c, byte *dst, int y, int x0, int count)
{
	float	sdivz, tdivz, zi, z, du = (float)x0, dv = (float)y;
	float	sdivz16 = c->sdivzstepu * 16, tdivz16 = c->tdivzstepu * 16, zi16 = c->zistepu * 16;
	int		s, t, snext, tnext, sstep, tstep, n;
	const int	*turb = pdr_sintable + ((int)(cl.time*PDR_TURB_SPEED) & (PDR_TURB_CYCLE-1));
	const byte	*tex = c->tex;

	sdivz = c->sdivzorigin + dv*c->sdivzstepv + du*c->sdivzstepu;
	tdivz = c->tdivzorigin + dv*c->tdivzstepv + du*c->tdivzstepu;
	zi = c->ziorigin + dv*c->zistepv + du*c->zistepu;
	z = (float)0x10000 / zi;
	s = ClampS ((int)(sdivz * z) + c->sadjust, c->bbextents);
	t = ClampS ((int)(tdivz * z) + c->tadjust, c->bbextentt);

	do
	{
		n = count >= 16 ? 16 : count;
		count -= n;
		sstep = tstep = 0;
		if (count)
		{
			sdivz += sdivz16;
			tdivz += tdivz16;
			zi += zi16;
			z = (float)0x10000 / zi;
			snext = (int)(sdivz * z) + c->sadjust;
			if (snext > c->bbextents)
				snext = c->bbextents;
			else if (snext < 16)
				snext = 16;
			tnext = (int)(tdivz * z) + c->tadjust;
			if (tnext > c->bbextentt)
				tnext = c->bbextentt;
			else if (tnext < 16)
				tnext = 16;
			sstep = (snext - s) >> 4;
			tstep = (tnext - t) >> 4;
		}
		else
		{
			snext = s;
			tnext = t;
			if (n > 1)
			{
				float	last = (float)(n - 1);
				float	lz = (float)0x10000 / (zi + c->zistepu * last);

				snext = (int)((sdivz + c->sdivzstepu * last) * lz) + c->sadjust;
				if (snext > c->bbextents)
					snext = c->bbextents;
				else if (snext < 16)
					snext = 16;
				tnext = (int)((tdivz + c->tdivzstepu * last) * lz) + c->tadjust;
				if (tnext > c->bbextentt)
					tnext = c->bbextentt;
				else if (tnext < 16)
					tnext = 16;
				sstep = (snext - s) / (n - 1);
				tstep = (tnext - t) / (n - 1);
			}
		}
		do
		{
			int	sturb = ((s + turb[(t>>16)&(PDR_TURB_CYCLE-1)])>>16)&63;
			int	tturb = ((t + turb[(s>>16)&(PDR_TURB_CYCLE-1)])>>16)&63;

			*dst++ = tex[(tturb<<6) + sturb];
			s += sstep;
			t += tstep;
		} while (--n);
		s = snext;
		t = tnext;
	} while (count > 0);
}

/*
==============================================================================

SKY (D_DrawSkyScans8, with the two layers combined per pixel instead of in a 128x128
buffer rebuilt as the sky scrolls)

==============================================================================
*/

#define PDR_SKYSIZE			128
#define PDR_SKYMASK			127
#define SKY_SPAN_SHIFT	5
#define SKY_SPAN_MAX	(1 << SKY_SPAN_SHIFT)

static byte		skyback[PDR_SKYSIZE*PDR_SKYSIZE];	/* right half of the sky texture */
static byte		skyfront[PDR_SKYSIZE*PDR_SKYSIZE];	/* left half: 0 = see the back layer */
static float	pdr_skytime, pdr_skyspeed = 8;
static int		skyshift;

void R_InitSky (texture_t *mt)
{
	int		i, j;
	byte	*src = (byte *)mt + mt->offsets[0];

	for (i=0 ; i<PDR_SKYSIZE ; i++)
		for (j=0 ; j<PDR_SKYSIZE ; j++)
		{
			skyback[i*PDR_SKYSIZE + j] = src[i*256 + j + 128];
			skyfront[i*PDR_SKYSIZE + j] = src[i*256 + j];
		}
}

void PDR_SetupSky (void)
{
	float	temp = PDR_SKYSIZE * 4 * 1;	/* PDR_SKYSIZE * (8/gcd) * (2/gcd), gcd(8, 2) = 2 */

	pdr_skytime = cl.time - ((int)(cl.time / temp) * temp);
	skyshift = (int)(pdr_skytime * pdr_skyspeed);
}

static void Sky_uv_To_st (int u, int v, int *s, int *t)
{
	float	wu, wv, temp;
	vec3_t	end;

	temp = (float)(r_refdef.vrect.width >= r_refdef.vrect.height ? r_refdef.vrect.width : r_refdef.vrect.height);
	wu = 8192.0f * (float)(u - (WARP_WIDTH>>1)) / temp;
	wv = 8192.0f * (float)((WARP_HEIGHT>>1) - v) / temp;
	end[0] = 4096*vpn[0] + wu*vright[0] + wv*vup[0];
	end[1] = 4096*vpn[1] + wu*vright[1] + wv*vup[1];
	end[2] = 4096*vpn[2] + wu*vright[2] + wv*vup[2];
	end[2] *= 3;
	VectorNormalize (end);
	temp = pdr_skytime * pdr_skyspeed;
	*s = (int)((temp + 6*(PDR_SKYSIZE/2-1)*end[0]) * 0x10000);
	*t = (int)((temp + 6*(PDR_SKYSIZE/2-1)*end[1]) * 0x10000);
}

static void SpanSky (byte *dst, int y, int x0, int count)
{
	int		s, t, snext = 0, tnext = 0, sstep = 0, tstep = 0, n, u = x0;
	int		shift = skyshift;

	Sky_uv_To_st (u, y, &s, &t);
	do
	{
		n = count >= SKY_SPAN_MAX ? SKY_SPAN_MAX : count;
		count -= n;
		if (count)
		{
			u += n;
			Sky_uv_To_st (u, y, &snext, &tnext);
			sstep = (snext - s) >> SKY_SPAN_SHIFT;
			tstep = (tnext - t) >> SKY_SPAN_SHIFT;
		}
		else if (n > 1)
		{
			u += n - 1;
			Sky_uv_To_st (u, y, &snext, &tnext);
			sstep = (snext - s) / (n - 1);
			tstep = (tnext - t) / (n - 1);
		}
		do
		{
			int		ss = s >> 16, tt = t >> 16;
			byte	f = skyfront[(((tt + shift) & PDR_SKYMASK) << 7) + ((ss + shift) & PDR_SKYMASK)];

			*dst++ = f ? f : skyback[((tt & PDR_SKYMASK) << 7) + (ss & PDR_SKYMASK)];
			s += sstep;
			t += tstep;
		} while (--n > 0);
		s = snext;
		t = tnext;
	} while (count > 0);
}

/*
==============================================================================

ENTRY POINTS

==============================================================================
*/

void PDR_DrawSpan (const pdr_spanctx_t *c, int y, int x0, int x1)
{
	byte	*dst = pdr_vbuf + y * pdr_stride + x0;

	switch (c->kind)
	{
	case PDR_SPAN_LIT:
		SpanLit (c, dst, y, x0, x1 - x0);
		break;
	case PDR_SPAN_TURB:
		SpanTurb (c, dst, y, x0, x1 - x0);
		break;
	default:
		SpanSky (dst, y, x0, x1 - x0);
		break;
	}
}

/* 1/z of the span into the z buffer (D_DrawZSpans) */
void PDR_ZSpan (const pdr_spanctx_t *c, int y, int x0, int x1)
{
	short	*pz = pdr_zbuf + y * pdr_stride + x0;
	float	zi = c->ziorigin + (float)y * c->zistepv + (float)x0 * c->zistepu;
	int		izi = (int)(zi * 0x8000 * 0x10000);
	int		izistep = (int)(c->zistepu * 0x8000 * 0x10000);
	int		n = x1 - x0;

	do
	{
		*pz++ = (short)(izi >> 16);
		izi += izistep;
	} while (--n);
}

void PDR_FillSpan (int y, int x0, int x1, int color)
{
	memset (pdr_vbuf + y * pdr_stride + x0, color, x1 - x0);
	if (pdr_zany)
	{
		int	r = y - pdr_vy, a = x0, b = x1;

		if (a < pdr_zx0[r])
			a = pdr_zx0[r];
		if (b > pdr_zx1[r])
			b = pdr_zx1[r];
		if (a < b)
			memset (pdr_zbuf + y * pdr_stride + a, 0, (b - a) * sizeof(short));
	}
}
