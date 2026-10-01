/*
 * pdr_sprite.c -- sprites and particles for the Playdate renderer (see pdr.h)
 *
 * Sprites are rare (explosions, bubbles), so they are drawn simply: the quad is clipped to the
 * view, and each pixel gets its perspective-correct texel and a z test. Particles are Quake's
 * square dots with a z test; their motion stays in r_part.c.
 */
#include "pdr.h"

extern vec3_t	r_pright, r_pup, r_ppn;		/* r_part.c */

/*
==============================================================================

SPRITES

==============================================================================
*/

static mspriteframe_t *GetSpriteFrame (entity_t *e, msprite_t *psprite)
{
	mspritegroup_t	*group;
	int				i, n, frame = e->frame;
	float			*intervals, full, target, time;

	if (frame >= psprite->numframes || frame < 0)
	{
		Con_Printf ("R_DrawSprite: no such frame %d\n", frame);
		frame = 0;
	}
	if (psprite->frames[frame].type == SPR_SINGLE)
		return psprite->frames[frame].frameptr;
	group = (mspritegroup_t *)psprite->frames[frame].frameptr;
	intervals = group->intervals;
	n = group->numframes;
	full = intervals[n-1];
	time = pdr_time + e->syncbase;
	target = time - ((int)(time / full)) * full;
	for (i=0 ; i<n-1 ; i++)
		if (intervals[i] > target)
			break;
	return group->frames[i];
}

typedef struct
{
	mspriteframe_t	*frame;
	vec3_t			origin;
	vec3_t			vpn, vright, vup;
	vec3_t			corners[4];		/* (up, left), (up, right), (down, right), (down, left) */
} sprite_t;

/* the sprite's axes and corners (R_DrawSprite); false if it is not drawn this frame */
static qboolean SetupSprite (entity_t *e, sprite_t *sp)
{
	msprite_t	*psprite = e->model->cache.data;
	vec3_t		tvec, modelorg;
	float		dot;
	int			i;

	sp->frame = GetSpriteFrame (e, psprite);
	VectorCopy (e->origin, sp->origin);
	VectorSubtract (r_origin, e->origin, modelorg);

	if (psprite->type == SPR_FACING_UPRIGHT)
	{
		tvec[0] = -modelorg[0];
		tvec[1] = -modelorg[1];
		tvec[2] = -modelorg[2];
		VectorNormalize (tvec);
		dot = tvec[2];
		if (dot > 0.999848f || dot < -0.999848f)
			return false;
		sp->vup[0] = 0; sp->vup[1] = 0; sp->vup[2] = 1;
		sp->vright[0] = tvec[1];
		sp->vright[1] = -tvec[0];
		sp->vright[2] = 0;
		VectorNormalize (sp->vright);
		sp->vpn[0] = -sp->vright[1];
		sp->vpn[1] = sp->vright[0];
		sp->vpn[2] = 0;
	}
	else if (psprite->type == SPR_VP_PARALLEL)
	{
		VectorCopy (vup, sp->vup);
		VectorCopy (vright, sp->vright);
		VectorCopy (vpn, sp->vpn);
	}
	else if (psprite->type == SPR_VP_PARALLEL_UPRIGHT)
	{
		dot = vpn[2];
		if (dot > 0.999848f || dot < -0.999848f)
			return false;
		sp->vup[0] = 0; sp->vup[1] = 0; sp->vup[2] = 1;
		sp->vright[0] = vpn[1];
		sp->vright[1] = -vpn[0];
		sp->vright[2] = 0;
		VectorNormalize (sp->vright);
		sp->vpn[0] = -sp->vright[1];
		sp->vpn[1] = sp->vright[0];
		sp->vpn[2] = 0;
	}
	else if (psprite->type == SPR_ORIENTED)
	{
		PDR_AngleVectors (e->angles, sp->vpn, sp->vright, sp->vup);
	}
	else if (psprite->type == SPR_VP_PARALLEL_ORIENTED)
	{
		float	sr, cr;

		PDR_SinCos (e->angles[ROLL], &sr, &cr);

		for (i=0 ; i<3 ; i++)
		{
			sp->vpn[i] = vpn[i];
			sp->vright[i] = vright[i] * cr + vup[i] * sr;
			sp->vup[i] = vright[i] * -sr + vup[i] * cr;
		}
	}
	else
		Sys_Error ("R_DrawSprite: Bad sprite type %d", psprite->type);

	// R_RotateSprite
	if (psprite->beamlength != 0)
	{
		vec3_t	vec;

		VectorScale (sp->vpn, -psprite->beamlength, vec);
		VectorAdd (sp->origin, vec, sp->origin);
		VectorSubtract (r_origin, sp->origin, modelorg);
	}

	// R_SetupAndDrawSprite: only the side facing the viewer
	if (DotProduct (sp->vpn, modelorg) >= 0)
		return false;

	for (i=0 ; i<3 ; i++)
	{
		sp->corners[0][i] = sp->origin[i] + sp->frame->up * sp->vup[i] + sp->frame->left * sp->vright[i];
		sp->corners[1][i] = sp->origin[i] + sp->frame->up * sp->vup[i] + sp->frame->right * sp->vright[i];
		sp->corners[2][i] = sp->origin[i] + sp->frame->down * sp->vup[i] + sp->frame->right * sp->vright[i];
		sp->corners[3][i] = sp->origin[i] + sp->frame->down * sp->vup[i] + sp->frame->left * sp->vright[i];
	}
	return true;
}

qboolean PDR_SpriteRect (entity_t *e, int rect[4])
{
	sprite_t	sp;
	float		umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
	int			i;

	if (!SetupSprite (e, &sp))
		return false;
	for (i=0 ; i<4 ; i++)
	{
		vec3_t	d, t;
		float	zi, u, v;

		VectorSubtract (sp.corners[i], r_origin, d);
		PDR_TransformToView (&pdr_wbasis, d, t);
		if (t[2] < 1)
		{
			rect[0] = pdr_vx;
			rect[1] = pdr_vy;
			rect[2] = pdr_vx + pdr_vw;
			rect[3] = pdr_vy + pdr_vh;
			return true;
		}
		zi = 1.0f / t[2];
		u = pdr_xcenter + pdr_xscale * t[0] * zi;
		v = pdr_ycenter - pdr_yscale * t[1] * zi;
		if (u < umin) umin = u;
		if (u > umax) umax = u;
		if (v < vmin) vmin = v;
		if (v > vmax) vmax = v;
	}
	if (umax < pdr_vx || umin > pdr_vx + pdr_vw || vmax < pdr_vy || vmin > pdr_vy + pdr_vh)
		return false;
	rect[0] = (int)umin - 1;
	rect[1] = (int)vmin - 1;
	rect[2] = (int)umax + 2;
	rect[3] = (int)vmax + 2;
	return true;
}

typedef struct
{
	float	x, y, z;
} svert_t;

static int ClipSprite (const svert_t *in, int n, svert_t *out, int plane)
{
	int		i, o = 0;

	for (i=0 ; i<n ; i++)
	{
		const svert_t	*a = &in[i], *b = &in[(i + 1) % n];
		float			da, db;

		switch (plane)
		{
		case 0: da = pdr_xscale * a->x + pdr_hw * a->z; db = pdr_xscale * b->x + pdr_hw * b->z; break;
		case 1: da = pdr_hw * a->z - pdr_xscale * a->x; db = pdr_hw * b->z - pdr_xscale * b->x; break;
		case 2: da = pdr_hh * a->z - pdr_yscale * a->y; db = pdr_hh * b->z - pdr_yscale * b->y; break;
		case 3: da = pdr_hh * a->z + pdr_yscale * a->y; db = pdr_hh * b->z + pdr_yscale * b->y; break;
		default: da = a->z - PDR_NEAR_CLIP; db = b->z - PDR_NEAR_CLIP; break;
		}
		if (da >= 0)
			out[o++] = *a;
		if ((da >= 0) != (db >= 0))
		{
			float	t = da / (da - db);

			out[o].x = a->x + t * (b->x - a->x);
			out[o].y = a->y + t * (b->y - a->y);
			out[o].z = a->z + t * (b->z - a->z);
			o++;
		}
	}
	return o;
}

void PDR_DrawSprite (entity_t *e)
{
	sprite_t	sp;
	svert_t		a[12], b[12];
	float		pu[12], pv[12];
	vec3_t		pn, A, B, d;
	float		dist, cs, ct;
	float		zistepu, zistepv, ziorigin, sstepu, sstepv, sorigin, tstepu, tstepv, torigin;
	int			i, n, p, y, ytop, ybot, w, h;
	float		vmin = 1e9f, vmax = -1e9f;
	byte		*pixels;

	if (!SetupSprite (e, &sp))
		return;
	w = sp.frame->width;
	h = sp.frame->height;
	pixels = sp.frame->pixels;

	for (i=0 ; i<4 ; i++)
	{
		vec3_t	t;

		VectorSubtract (sp.corners[i], r_origin, d);
		PDR_TransformToView (&pdr_wbasis, d, t);
		a[i].x = t[0]; a[i].y = t[1]; a[i].z = t[2];
	}
	n = 4;
	for (p=4 ; p>=0 ; p--)
	{
		n = ClipSprite (a, n, b, p);
		if (n < 3)
			return;
		memcpy (a, b, n * sizeof(svert_t));
	}

// gradients: 1/z from the sprite plane, s/z and t/z from its axes
	PDR_TransformToView (&pdr_wbasis, sp.vpn, pn);
	VectorSubtract (sp.origin, r_origin, d);
	dist = DotProduct (sp.vpn, d);
	if (dist == 0)
		return;
	zistepu = pn[0] / (pdr_xscale * dist);
	zistepv = -pn[1] / (pdr_yscale * dist);
	ziorigin = pn[2] / dist - pdr_xcenter * zistepu - pdr_ycenter * zistepv;
	PDR_TransformToView (&pdr_wbasis, sp.vright, A);
	cs = -DotProduct (d, sp.vright) - sp.frame->left;
	sstepu = A[0] / pdr_xscale + cs * zistepu;
	sstepv = -A[1] / pdr_yscale + cs * zistepv;
	sorigin = A[2] - pdr_xcenter * A[0] / pdr_xscale + pdr_ycenter * A[1] / pdr_yscale + cs * ziorigin;
	PDR_TransformToView (&pdr_wbasis, sp.vup, B);
	VectorInverse (B);
	ct = DotProduct (d, sp.vup) + sp.frame->up;
	tstepu = B[0] / pdr_xscale + ct * zistepu;
	tstepv = -B[1] / pdr_yscale + ct * zistepv;
	torigin = B[2] - pdr_xcenter * B[0] / pdr_xscale + pdr_ycenter * B[1] / pdr_yscale + ct * ziorigin;

	for (i=0 ; i<n ; i++)
	{
		float	zi = 1.0f / a[i].z;

		pu[i] = pdr_xcenter + pdr_xscale * a[i].x * zi;
		pv[i] = pdr_ycenter - pdr_yscale * a[i].y * zi;
		if (pv[i] < vmin) vmin = pv[i];
		if (pv[i] > vmax) vmax = pv[i];
	}
	ytop = (int)ceilf (vmin);
	ybot = (int)ceilf (vmax);
	if (ytop < pdr_vy)
		ytop = pdr_vy;
	if (ybot > pdr_vy + pdr_vh)
		ybot = pdr_vy + pdr_vh;

	for (y=ytop ; y<ybot ; y++)
	{
		float	xl = 1e9f, xr = -1e9f, fy = (float)y;
		int		x0, x1, x;

		if (pdr_skip != 2 && PDR_ROW_SKIPPED(y))
			continue;
		for (i=0 ; i<n ; i++)
		{
			int		j = (i + 1) % n;
			float	v0 = pv[i], v1 = pv[j], x;

			if ((fy < v0) == (fy < v1))
				continue;
			x = pu[i] + (fy - v0) * (pu[j] - pu[i]) / (v1 - v0);
			if (x < xl) xl = x;
			if (x > xr) xr = x;
		}
		if (xl > xr)
			continue;
		x0 = (int)ceilf (xl);
		x1 = (int)ceilf (xr);
		if (x0 < pdr_vx)
			x0 = pdr_vx;
		if (x1 > pdr_vx + pdr_vw)
			x1 = pdr_vx + pdr_vw;
		for (x=x0 ; x<x1 ; x++)
		{
			float	zi = ziorigin + zistepu * x + zistepv * fy;
			float	z, s, t;
			int		ss, tt, izi;
			short	*pz;

			if (zi <= 0)
				continue;
			z = 1.0f / zi;
			s = (sorigin + sstepu * x + sstepv * fy) * z;
			t = (torigin + tstepu * x + tstepv * fy) * z;
			ss = (int)s;
			tt = (int)t;
			if (ss < 0 || tt < 0 || ss >= w || tt >= h)
				continue;
			if (pixels[tt * w + ss] == 255)
				continue;
			izi = (int)(zi * 0x8000);
			pz = pdr_zbuf + y * pdr_stride + x;
			if (*pz <= izi)
			{
				*pz = izi;
				pdr_vbuf[y * pdr_stride + x] = pixels[tt * w + ss];
			}
		}
	}
}

/*
==============================================================================

PARTICLES (D_DrawParticle)

==============================================================================
*/

static int	pix_min, pix_max, pix_shift, y_aspect_shift, vrectright_particle, vrectbottom_particle;

void D_StartParticles (void)
{
	pix_min = pdr_vw / 320;
	if (pix_min < 1)
		pix_min = 1;
	pix_max = (int)((float)pdr_vw * (4.0f / 320.0f) + 0.5f);
	pix_shift = 8 - (int)((float)pdr_vw * (1.0f / 320.0f) + 0.5f);
	if (pix_max < 1)
		pix_max = 1;
	y_aspect_shift = 0;
	vrectright_particle = pdr_vx + pdr_vw - pix_max;
	vrectbottom_particle = pdr_vy + pdr_vh - (pix_max << y_aspect_shift);
}

void D_EndParticles (void)
{
}

void D_DrawParticle (particle_t *p)
{
	vec3_t	local, tr;
	float	zi;
	int		u, v, izi, pix, count, i, color = (int)p->color;
	short	*pz;
	byte	*pd;

	VectorSubtract (p->org, r_origin, local);
	tr[0] = DotProduct (local, r_pright);
	tr[1] = DotProduct (local, r_pup);
	tr[2] = DotProduct (local, r_ppn);
	if (tr[2] < PARTICLE_Z_CLIP)
		return;
	zi = 1.0f / tr[2];
	u = (int)(pdr_xcenter + zi * tr[0] + 0.5f);
	v = (int)(pdr_ycenter - zi * tr[1] + 0.5f);
	if (v > vrectbottom_particle || u > vrectright_particle || v < pdr_vy || u < pdr_vx)
		return;
	izi = (int)(zi * 0x8000);
	pix = izi >> pix_shift;
	if (pix < pix_min)
		pix = pix_min;
	else if (pix > pix_max)
		pix = pix_max;
	pz = pdr_zbuf + pdr_stride * v + u;
	pd = pdr_vbuf + pdr_stride * v + u;
	for (count = pix << y_aspect_shift ; count ; count--, v++, pz += pdr_stride, pd += pdr_stride)
	{
		if (pdr_skip != 2 && PDR_ROW_SKIPPED(v))
			continue;
		for (i=0 ; i<pix ; i++)
		{
			if (pz[i] <= izi)
			{
				pz[i] = izi;
				pd[i] = color;
			}
		}
	}
}
