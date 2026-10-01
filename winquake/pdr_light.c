/*
 * pdr_light.c -- lighting for the Playdate renderer (see pdr.h)
 *
 * Instead of Quake's surface cache (the texture pre-lit at full texel resolution, rebuilt
 * whenever a light changes) each face keeps a light block: its lightmap with the light styles
 * and dynamic lights applied, one byte per sample (16x16 texels). The span drawer samples it
 * bilinearly, which is what the surface cache build did per texel. A block is rebuilt when a
 * light style it uses changes value or a dynamic light reaches it (as the surface cache was);
 * that is a few hundred bytes instead of up to tens of kilobytes.
 */
#include "pdr.h"
#include <stddef.h>

int		pdr_lightstyle[MAX_LIGHTSTYLES];	/* d_lightstylevalue: 8.8 scale per style */
int		pdr_dlightframe;
int		pdr_c_lbuild;
byte	**pdr_lightptr;					/* per face slot: its block in the pool, NULL = none */

/*
==============================================================================

LIGHT STYLES

==============================================================================
*/

void PDR_AnimateLights (void)
{
	int		i, j, k;

// 'm' is normal light, 'a' is no light, 'z' is double bright
	i = (int)(cl.time*10);
	for (j=0 ; j<MAX_LIGHTSTYLES ; j++)
	{
		if (!cl_lightstyle[j].length)
		{
			pdr_lightstyle[j] = 256;
			continue;
		}
		k = i % cl_lightstyle[j].length;
		k = cl_lightstyle[j].map[k] - 'a';
		pdr_lightstyle[j] = k*22;
	}
}

/*
==============================================================================

LIGHT BLOCK POOL

A ring of entries tiling the pool (as Quake's surface cache).
==============================================================================
*/

typedef struct
{
	int				owner;			/* face slot, -1 = free */
	unsigned short	size;			/* whole entry, header included */
	byte			dlight;			/* built with dynamic lights: rebuild next time */
	byte			pad;
	int				styleval[MAXLIGHTMAPS];
	byte			data[];
} lentry_t;

static byte		*lpool;
static int		lpoolsize;
static int		lrover;

#define LENTRY(o)	((lentry_t *)(lpool + (o)))
#define pdr_pool_entry(p)	((p) ? (lentry_t *)((byte *)(p) - offsetof(lentry_t, data)) : NULL)
#define LMIN		((int)sizeof(lentry_t) + 4)

#define LCHUNK	32768		/* the pool is tiled with free entries of this size to start with */

void PDR_InitLightCache (void *buf, int size)
{
	lpool = buf;
	lpoolsize = size >= LCHUNK ? size & ~(LCHUNK - 1) : size & ~3;
	PDR_FlushLightCache ();
}

void PDR_FlushLightCache (void)
{
	int		o;

	if (!lpool)
		return;
	lrover = 0;
	for (o=0 ; o<lpoolsize ; o+=LCHUNK)
	{
		LENTRY(o)->owner = -1;
		LENTRY(o)->size = lpoolsize - o < LCHUNK ? lpoolsize - o : LCHUNK;
	}
	if (pdr_lightptr)
		memset (pdr_lightptr, 0, sizeof(byte *) * pdr_totalfaces);
}

static void Evict (lentry_t *e)
{
	if (e->owner >= 0)
	{
		pdr_lightptr[e->owner] = NULL;
		e->owner = -1;
	}
}

static lentry_t *AllocBlock (int datasize, int owner)
{
	int		size = (sizeof(lentry_t) + datasize + 3) & ~3;
	int		total;
	lentry_t	*e;

	if (size > lpoolsize || size > LCHUNK)
		return NULL;
	if (lrover + size > lpoolsize)
		lrover = 0;
	total = 0;
	while (total < size)
	{
		e = LENTRY(lrover + total);
		Evict (e);
		total += e->size;
	}
	e = LENTRY(lrover);
	if (total - size >= LMIN)
	{
		lentry_t	*rest = LENTRY(lrover + size);

		rest->owner = -1;
		rest->size = total - size;
		e->size = size;
	}
	else
		e->size = total;
	e->owner = owner;
	lrover += e->size;
	if (lrover >= lpoolsize)
		lrover = 0;
	return e;
}

/*
==============================================================================

BUILD

==============================================================================
*/

typedef struct
{
	float	local[2];
	float	rad, minlight;
} dlrun_t;

static void BuildBlock (lentry_t *e, pdr_brush_t *b, const pdr_face_t *f, qboolean dynamic, int slot)
{
	int			lw = (f->extents[0] >> 4) + 1, lh = (f->extents[1] >> 4) + 1, size = lw * lh;
	const byte	*lightmap = b->model->lightdata + f->lightofs;
	int			nmaps, m, s, t, i, k;
	int			adj[MAXLIGHTMAPS];
	static dlrun_t	dl[MAX_DLIGHTS];	/* (rarely used: kept off the small stack) */
	int			ndl = 0;
	int			ambient = r_refdef.ambientlight << 8;
	byte		*out = e->data;

	for (nmaps=0 ; nmaps<MAXLIGHTMAPS && f->styles[nmaps] != 255 ; nmaps++)
		adj[nmaps] = pdr_lightstyle[f->styles[nmaps]];
	for (m=0 ; m<MAXLIGHTMAPS ; m++)
		e->styleval[m] = m < nmaps ? adj[m] : -1;

	if (dynamic)
	{
		mtexinfo_t	*tex = &b->model->texinfo[f->texinfo];
		unsigned	bits = pdr_dlbits[slot];

		for (k=0 ; k<MAX_DLIGHTS ; k++)
		{
			dlight_t	*l = &cl_dlights[k];
			float		dist, rad, minlight;
			vec3_t		impact;

			if (!(bits & (1u << k)))
				continue;
			rad = l->radius;
			dist = DotProduct (l->origin, f->plane) - f->plane[3];
			rad -= fabsf (dist);
			minlight = l->minlight;
			if (rad < minlight)
				continue;
			for (i=0 ; i<3 ; i++)
				impact[i] = l->origin[i] - f->plane[i]*dist;
			dl[ndl].local[0] = DotProduct (impact, tex->vecs[0]) + tex->vecs[0][3] - f->texturemins[0];
			dl[ndl].local[1] = DotProduct (impact, tex->vecs[1]) + tex->vecs[1][3] - f->texturemins[1];
			dl[ndl].rad = rad;
			dl[ndl].minlight = rad - minlight;
			ndl++;
		}
	}
	e->dlight = ndl > 0;

	i = 0;
	for (t=0 ; t<lh ; t++)
	{
		for (s=0 ; s<lw ; s++, i++)
		{
			int	acc = ambient, v;

			for (m=0 ; m<nmaps ; m++)
				acc += lightmap[m*size + i] * adj[m];
			for (k=0 ; k<ndl ; k++)
			{
				int		sd, td;
				float	dist;

				td = dl[k].local[1] - t*16;
				if (td < 0)
					td = -td;
				sd = dl[k].local[0] - s*16;
				if (sd < 0)
					sd = -sd;
				if (sd > td)
					dist = sd + (td>>1);
				else
					dist = td + (sd>>1);
				if (dist < dl[k].minlight)
					acc += (int)((dl[k].rad - dist)*256);
			}
		// bound, invert, and shift (R_BuildLightMap), kept to 8 bits: 4 steps per colormap row
			v = (255*256 - acc) >> 2;
			if (v < (1 << 6))
				v = 1 << 6;
			out[i] = v >> 6;
		}
	}
	// padding read with weight 0 by the bilinear sampler
	for (k=0 ; k<lw + 2 ; k++)
		out[size + k] = out[size - 1];
}

/*
================
PDR_FaceLight

The face's light block, or NULL with *lconst set when the light is the same everywhere.
================
*/
const byte *PDR_FaceLight (pdr_brush_t *b, int fi, const pdr_face_t *f, int *lconst)
{
	int			slot = b->facebase + fi;
	lentry_t	*e;
	qboolean	dynamic;
	int			m;

	if (pdr_fullbright || !b->model->lightdata)
	{
		*lconst = 0;
		return NULL;
	}
	if (f->flags & PF_NOLIGHT)
	{
		int	v = (255*256 - (r_refdef.ambientlight << 8)) >> 2;

		if (v < (1 << 6))
			v = 1 << 6;
		*lconst = (v >> 6) << 16;
		return NULL;
	}

	dynamic = pdr_dlframe[slot] == r_framecount;
	e = pdr_pool_entry (pdr_lightptr[slot]);
	if (e && !dynamic && !e->dlight)
	{
		for (m=0 ; m<MAXLIGHTMAPS ; m++)
		{
			int	want = (m < MAXLIGHTMAPS && f->styles[m] != 255) ? pdr_lightstyle[f->styles[m]] : -1;

			if (e->styleval[m] != want)
				break;
			if (want < 0)
			{
				m = MAXLIGHTMAPS;
				break;
			}
		}
		if (m == MAXLIGHTMAPS)
			return e->data;
	}

	if (!e)
	{
		int	lw = (f->extents[0] >> 4) + 1, lh = (f->extents[1] >> 4) + 1;

		e = AllocBlock (lw * lh + lw + 2, slot);
		if (!e)
		{
			*lconst = 32 << 18;		/* (pool too small) normal light */
			return NULL;
		}
		pdr_lightptr[slot] = e->data;
	}
	BuildBlock (e, b, f, dynamic, slot);
	pdr_c_lbuild++;
	return e->data;
}

/*
==============================================================================

DYNAMIC LIGHTS

==============================================================================
*/

static void MarkLights (dlight_t *light, unsigned bit, pdr_brush_t *b, int node)
{
	int		stack[64], sp = 0;

	while (1)
	{
		while (node >= 0)
		{
			const pdr_node_t		*pn = &b->nodes[node];
			const pdr_nodefaces_t	*nf;
			float					dist = DotProduct (light->origin, pn->normal) - pn->dist;
			int						i, n;

			if (dist > light->radius)
			{
				node = pn->children[0];
				continue;
			}
			if (dist < -light->radius)
			{
				node = pn->children[1];
				continue;
			}
			nf = &b->nodefaces[node];
			n = nf->nfront + nf->nback;
			for (i=0 ; i<n ; i++)
			{
				int	slot = b->facebase + nf->first + i;

				if (pdr_dlframe[slot] != pdr_dlightframe)
				{
					pdr_dlbits[slot] = 0;
					pdr_dlframe[slot] = pdr_dlightframe;
				}
				pdr_dlbits[slot] |= bit;
			}
			if (sp < 64)
				stack[sp++] = pn->children[1];
			node = pn->children[0];
		}
		if (!sp)
			break;
		node = stack[--sp];
	}
}

/* called by V_RenderView before R_RenderView, so the frame count has not advanced yet */
void R_PushDlights (void)
{
	int			i;
	dlight_t	*l;

	pdr_dlightframe = r_framecount + 1;
	if (!pdr_world)
		return;
	l = cl_dlights;
	for (i=0 ; i<MAX_DLIGHTS ; i++, l++)
	{
		if (l->die < cl.time || !l->radius)
			continue;
		MarkLights (l, 1u << i, pdr_world, 0);
	}
}

void PDR_MarkBmodelLights (pdr_brush_t *b, int headnode)
{
	int		k;

	pdr_dlightframe = r_framecount;
	for (k=0 ; k<MAX_DLIGHTS ; k++)
	{
		if (cl_dlights[k].die < cl.time || !cl_dlights[k].radius)
			continue;
		MarkLights (&cl_dlights[k], 1u << k, b, headnode);
	}
}

/*
==============================================================================

LIGHT SAMPLING (R_LightPoint)

The walk down the world from a point and the face/texel it lands on depend only on the point,
so the last few are remembered; the light itself is recomputed every time.
==============================================================================
*/

typedef struct
{
	const pdr_face_t	*f;			/* NULL = nothing hit */
	int					ds, dt;
} lighthit_t;

static qboolean LightHit (int node, const vec3_t start, const vec3_t end, lighthit_t *hit)
{
	const pdr_node_t	*pn;
	float		front, back, frac;
	vec3_t		mid;
	int			side, i, n;

	while (1)
	{
		if (node < 0)
			return false;
		pn = &pdr_world->nodes[node];
		front = DotProduct (start, pn->normal) - pn->dist;
		back = DotProduct (end, pn->normal) - pn->dist;
		side = front < 0;
		if ((back < 0) == side)
		{
			node = pn->children[side];
			continue;
		}
		break;
	}

	frac = front / (front-back);
	mid[0] = start[0] + (end[0] - start[0])*frac;
	mid[1] = start[1] + (end[1] - start[1])*frac;
	mid[2] = start[2] + (end[2] - start[2])*frac;

	if (LightHit (pn->children[side], start, mid, hit))
		return true;

	{
		const pdr_nodefaces_t	*nf = &pdr_world->nodefaces[node];

		n = nf->nfront + nf->nback;
		for (i=0 ; i<n ; i++)
		{
			const pdr_face_t	*f = PDR_FACE (pdr_world, nf->first + i);
			const mtexinfo_t	*tex;
			int					s, t, ds, dt;

			if (f->flags & (PF_SKY | PF_TURB))
				continue;	// no lightmaps
			tex = &pdr_world->model->texinfo[f->texinfo];
			s = DotProduct (mid, tex->vecs[0]) + tex->vecs[0][3];
			t = DotProduct (mid, tex->vecs[1]) + tex->vecs[1][3];
			if (s < f->texturemins[0] || t < f->texturemins[1])
				continue;
			ds = s - f->texturemins[0];
			dt = t - f->texturemins[1];
			if (ds > f->extents[0] || dt > f->extents[1])
				continue;
			hit->f = f;
			hit->ds = ds;
			hit->dt = dt;
			return true;
		}
	}

	return LightHit (pn->children[!side], mid, end, hit);
}

#define LPCACHE	16
static struct
{
	float		org[3];
	lighthit_t	hit;
	model_t		*world;
} lpcache[LPCACHE];
static int		lpnext;

int PDR_LightPoint (const vec3_t p)
{
	lighthit_t	hit;
	int			i, light;

	if (!cl.worldmodel->lightdata)
		return 255;

	for (i=0 ; i<LPCACHE ; i++)
		if (lpcache[i].world == cl.worldmodel && lpcache[i].org[0] == p[0] &&
			lpcache[i].org[1] == p[1] && lpcache[i].org[2] == p[2])
			break;
	if (i < LPCACHE)
		hit = lpcache[i].hit;
	else
	{
		vec3_t	end;

		end[0] = p[0];
		end[1] = p[1];
		end[2] = p[2] - 2048;
		hit.f = NULL;
		LightHit (0, p, end, &hit);
		i = lpnext;
		lpnext = (lpnext + 1) % LPCACHE;
		VectorCopy (p, lpcache[i].org);
		lpcache[i].hit = hit;
		lpcache[i].world = cl.worldmodel;
	}

	light = 0;
	if (hit.f && hit.f->lightofs >= 0)
	{
		const pdr_face_t	*f = hit.f;
		int					lw = (f->extents[0] >> 4) + 1;
		int					size = lw * ((f->extents[1] >> 4) + 1);
		const byte			*lightmap = cl.worldmodel->lightdata + f->lightofs + (hit.dt >> 4) * lw + (hit.ds >> 4);
		int					m;

		for (m=0 ; m<MAXLIGHTMAPS && f->styles[m] != 255 ; m++)
		{
			light += *lightmap * pdr_lightstyle[f->styles[m]];
			lightmap += size;
		}
		light >>= 8;
	}
	if (light < r_refdef.ambientlight)
		light = r_refdef.ambientlight;
	return light;
}

void PDR_NewMapLight (void)
{
	for (lpnext=0 ; lpnext<LPCACHE ; lpnext++)
		lpcache[lpnext].world = NULL;
	lpnext = 0;
	PDR_FlushLightCache ();
}
