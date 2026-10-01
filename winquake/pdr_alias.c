/*
 * pdr_alias.c -- alias models (monsters, items, the weapon) for the Playdate renderer
 *
 * Same transform, lighting and affine texturing as Quake's r_alias.c / d_polyse.c, laid out for
 * the device: triangles and texture coordinates are copied at map load into compact arrays
 * (8 and 4 bytes instead of 16 and 12), projected vertices go on the fast stack, and each
 * triangle is set up once with plane gradients and drawn row by row, which suits the
 * one-to-three-pixel triangles most models are made of at half resolution.
 */
#include "pdr.h"
#include "pd_stack.h"

#define ALIAS_Z_CLIP_PLANE	5
#define LIGHT_MIN			5
#define MAX_PDR_AVERTS		2048

float	r_avertexnormals[162][3] = {
#include "anorms.h"
};

int		pdr_c_aliasmodels, pdr_c_atris, pdr_c_averts;

/*
==============================================================================

COMPACT MODEL DATA

==============================================================================
*/

typedef struct
{
	unsigned short	v[3];
	unsigned short	front;		/* facesfront */
} atri_t;

typedef struct
{
	unsigned short	s;			/* bit 15: on the seam */
	unsigned short	t;
} ast_t;

typedef struct
{
	model_t		*model;
	int			numverts, numtris;
	atri_t		*tris;
	ast_t		*st;
} amodel_t;

#define MAX_AMODELS	256
static amodel_t	amodels[MAX_AMODELS];
static int		numamodels;
static byte		apool[48 * 1024];	/* models that turn up after the level was loaded */
static int		apoolused;

static amodel_t *BuildAModel (model_t *m, qboolean hunk)
{
	aliashdr_t	*hdr = Mod_Extradata (m);
	mdl_t		*pmdl = (mdl_t *)((byte *)hdr + hdr->model);
	stvert_t	*pst = (stvert_t *)((byte *)hdr + hdr->stverts);
	mtriangle_t	*ptri = (mtriangle_t *)((byte *)hdr + hdr->triangles);
	amodel_t	*am;
	int			i, size;
	byte		*mem;

	if (numamodels == MAX_AMODELS)
		return NULL;
	size = pmdl->numtris * sizeof(atri_t) + pmdl->numverts * sizeof(ast_t);
	if (hunk)
		mem = Hunk_AllocName (size, "pdralias");
	else
	{
		if (apoolused + size > (int)sizeof(apool))
			return NULL;
		mem = apool + apoolused;
		apoolused += (size + 3) & ~3;
	}
	am = &amodels[numamodels++];
	am->model = m;
	am->numverts = pmdl->numverts;
	am->numtris = pmdl->numtris;
	am->tris = (atri_t *)mem;
	am->st = (ast_t *)(mem + pmdl->numtris * sizeof(atri_t));
	for (i=0 ; i<pmdl->numtris ; i++)
	{
		am->tris[i].v[0] = ptri[i].vertindex[0];
		am->tris[i].v[1] = ptri[i].vertindex[1];
		am->tris[i].v[2] = ptri[i].vertindex[2];
		am->tris[i].front = ptri[i].facesfront != 0;
	}
	for (i=0 ; i<pmdl->numverts ; i++)
	{
		am->st[i].s = (pst[i].s >> 16) | ((pst[i].onseam & ALIAS_ONSEAM) ? 0x8000 : 0);
		am->st[i].t = pst[i].t >> 16;
	}
	return am;
}

static amodel_t *AModelFor (model_t *m)
{
	int		i;

	for (i=0 ; i<numamodels ; i++)
		if (amodels[i].model == m)
			return &amodels[i];
	return BuildAModel (m, false);
}

void PDR_NewMapAlias (void)
{
	int		i;

	numamodels = 0;
	apoolused = 0;
	for (i=1 ; i<MAX_MODELS ; i++)
	{
		model_t	*m = cl.model_precache[i];

		if (m && m->type == mod_alias)
			BuildAModel (m, true);
	}
}

/*
==============================================================================

SETUP

==============================================================================
*/

typedef struct
{
	aliashdr_t	*hdr;
	mdl_t		*pmdl;
	float		xf[3][4];		/* model vertex -> view space (x right, y down, z forward) */
	vec3_t		forward, right, up;
	trivertx_t	*verts;			/* the frame */
	maliasframedesc_t	*frame;
} asetup_t;

static void SetupTransform (entity_t *e, asetup_t *a)
{
	vec3_t	angles, org;
	float	rot[3][4];
	int		i, j;

	angles[ROLL] = e->angles[ROLL];
	angles[PITCH] = -e->angles[PITCH];
	angles[YAW] = e->angles[YAW];
	AngleVectors (angles, a->forward, a->right, a->up);
	VectorSubtract (r_origin, e->origin, org);

// model axes, scale and offset (R_AliasSetUpTransform's rotationmatrix)
	for (i=0 ; i<3 ; i++)
	{
		float	f = a->forward[i], r = -a->right[i], u = a->up[i];

		rot[i][0] = f * a->pmdl->scale[0];
		rot[i][1] = r * a->pmdl->scale[1];
		rot[i][2] = u * a->pmdl->scale[2];
		rot[i][3] = f * a->pmdl->scale_origin[0] + r * a->pmdl->scale_origin[1] +
				u * a->pmdl->scale_origin[2] - org[i];
	}
// then into view space: rows vright, -vup, vpn
	for (j=0 ; j<4 ; j++)
	{
		a->xf[0][j] = vright[0]*rot[0][j] + vright[1]*rot[1][j] + vright[2]*rot[2][j];
		a->xf[1][j] = -(vup[0]*rot[0][j] + vup[1]*rot[1][j] + vup[2]*rot[2][j]);
		a->xf[2][j] = vpn[0]*rot[0][j] + vpn[1]*rot[1][j] + vpn[2]*rot[2][j];
	}
}

static qboolean SetupModel (entity_t *e, asetup_t *a)
{
	int		frame;

	a->hdr = Mod_Extradata (e->model);
	a->pmdl = (mdl_t *)((byte *)a->hdr + a->hdr->model);
	frame = e->frame;
	if (frame >= a->pmdl->numframes || frame < 0)
	{
		Con_DPrintf ("R_AliasSetupFrame: no such frame %d\n", frame);
		frame = 0;
	}
	a->frame = &a->hdr->frames[frame];
	SetupTransform (e, a);
	return true;
}

/* the frame's vertices (frame groups animate by time) */
static void SetupFrameVerts (entity_t *e, asetup_t *a)
{
	maliasgroup_t	*group;
	float			*intervals, full, target, time;
	int				i, n;

	if (a->frame->type == ALIAS_SINGLE)
	{
		a->verts = (trivertx_t *)((byte *)a->hdr + a->frame->frame);
		return;
	}
	group = (maliasgroup_t *)((byte *)a->hdr + a->frame->frame);
	intervals = (float *)((byte *)a->hdr + group->intervals);
	n = group->numframes;
	full = intervals[n-1];
	time = cl.time + e->syncbase;
	target = time - ((int)(time / full)) * full;
	for (i=0 ; i<n-1 ; i++)
		if (intervals[i] > target)
			break;
	a->verts = (trivertx_t *)((byte *)a->hdr + group->frames[i].frame);
}

static byte *SetupSkin (entity_t *e, asetup_t *a)
{
	maliasskindesc_t	*desc;
	int					skinnum = e->skinnum;

	if (skinnum >= a->pmdl->numskins || skinnum < 0)
	{
		Con_DPrintf ("R_AliasSetupSkin: no such skin # %d\n", skinnum);
		skinnum = 0;
	}
	desc = ((maliasskindesc_t *)((byte *)a->hdr + a->hdr->skindesc)) + skinnum;
	if (desc->type == ALIAS_SKIN_GROUP)
	{
		maliasskingroup_t	*group = (maliasskingroup_t *)((byte *)a->hdr + desc->skin);
		float				*intervals = (float *)((byte *)a->hdr + group->intervals);
		int					i, n = group->numskins;
		float				full = intervals[n-1], time = cl.time + e->syncbase;
		float				target = time - ((int)(time / full)) * full;

		for (i=0 ; i<n-1 ; i++)
			if (intervals[i] > target)
				break;
		desc = &group->skindescs[i];
	}
	return (byte *)a->hdr + desc->skin;
}

/*
================
PDR_AliasRect

The screen rectangle of the entity's frame box; false if it is off screen (R_AliasCheckBBox).
================
*/
qboolean PDR_AliasRect (entity_t *e, int rect[4])
{
	static const byte	edges[12][2] = {
		{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}
	};
	asetup_t	a;
	float		v[8][3];
	float		umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
	int			i, n = 0;

	SetupModel (e, &a);
	for (i=0 ; i<8 ; i++)
	{
		float	p[3];

		p[0] = (i & 1) ? a.frame->bboxmax.v[0] : a.frame->bboxmin.v[0];
		p[1] = (i & 2) ? a.frame->bboxmax.v[1] : a.frame->bboxmin.v[1];
		p[2] = (i & 4) ? a.frame->bboxmax.v[2] : a.frame->bboxmin.v[2];
		v[i][0] = DotProduct (p, a.xf[0]) + a.xf[0][3];
		v[i][1] = DotProduct (p, a.xf[1]) + a.xf[1][3];
		v[i][2] = DotProduct (p, a.xf[2]) + a.xf[2][3];
	}

// the corners in front of the near plane, and where the box's edges cross it
	for (i=0 ; i<20 ; i++)
	{
		float	x, y, z;

		if (i < 8)
		{
			if (v[i][2] < ALIAS_Z_CLIP_PLANE)
				continue;
			x = v[i][0]; y = v[i][1]; z = v[i][2];
		}
		else
		{
			const float	*p0 = v[edges[i-8][0]], *p1 = v[edges[i-8][1]];
			float		f;

			if ((p0[2] < ALIAS_Z_CLIP_PLANE) == (p1[2] < ALIAS_Z_CLIP_PLANE))
				continue;
			f = (ALIAS_Z_CLIP_PLANE - p0[2]) / (p1[2] - p0[2]);
			x = p0[0] + (p1[0] - p0[0]) * f;
			y = p0[1] + (p1[1] - p0[1]) * f;
			z = ALIAS_Z_CLIP_PLANE;
		}
		z = 1.0f / z;
		x = x * pdr_xscale * z + pdr_xcenter;
		y = y * pdr_yscale * z + pdr_ycenter;
		if (x < umin) umin = x;
		if (x > umax) umax = x;
		if (y < vmin) vmin = y;
		if (y > vmax) vmax = y;
		n++;
	}
	if (!n)
		return false;	/* entirely behind the near plane */
	if (umax < pdr_vx || umin > pdr_vx + pdr_vw || vmax < pdr_vy || vmin > pdr_vy + pdr_vh)
		return false;
	rect[0] = umin < -30000 ? -30000 : (int)umin - 1;
	rect[1] = vmin < -30000 ? -30000 : (int)vmin - 1;
	rect[2] = umax > 30000 ? 30000 : (int)umax + 2;
	rect[3] = vmax > 30000 ? 30000 : (int)vmax + 2;
	return true;
}

/*
==============================================================================

RASTERIZATION

==============================================================================
*/

typedef struct
{
	int		u, v;			/* screen */
	int		s, t;			/* skin, 16.16 */
	int		l;				/* light, 8.8 (colormap row in the high byte) */
	int		z;				/* 1/z * 0x8000 (* 3 for the weapon), 16.16 */
} rvert_t;

typedef struct
{
	const byte	*skin;
	int			sw, sh;
	const byte	*cmap;
} araster_t;

static void RasterTri (const araster_t *ar, const rvert_t *a, const rvert_t *b, const rvert_t *c)
{
	const rvert_t	*p0, *p1, *p2, *t;
	float	inv, d1u, d1v, d2u, d2v;
	float	dsdu, dsdv, dtdu, dtdv, dldu, dldv, dzdu, dzdv;
	int		y, ystart;
	int		denom = (a->v - b->v) * (a->u - c->u) - (a->u - b->u) * (a->v - c->v);

	if (denom >= 0)
		return;		/* facing away (Quake's winding test) */

	p0 = a; p1 = b; p2 = c;
	if (p1->v < p0->v) { t = p0; p0 = p1; p1 = t; }
	if (p2->v < p0->v) { t = p0; p0 = p2; p2 = t; }
	if (p2->v < p1->v) { t = p1; p1 = p2; p2 = t; }
	if (p0->v == p2->v)
		return;

	d1u = p1->u - p0->u; d1v = p1->v - p0->v;
	d2u = p2->u - p0->u; d2v = p2->v - p0->v;
	inv = d1u * d2v - d2u * d1v;
	if (inv == 0)
		return;
	inv = 1.0f / inv;
#define GRAD(f, du, dv) \
	du = ((float)(p1->f - p0->f) * d2v - (float)(p2->f - p0->f) * d1v) * inv; \
	dv = ((float)(p2->f - p0->f) * d1u - (float)(p1->f - p0->f) * d2u) * inv
	GRAD(s, dsdu, dsdv);
	GRAD(t, dtdu, dtdv);
	GRAD(l, dldu, dldv);
	GRAD(z, dzdu, dzdv);
#undef GRAD

	ystart = p0->v;
	if (pdr_skip != 2 && PDR_ROW_SKIPPED(ystart))
		ystart++;
	for (y=ystart ; y<p2->v ; y+=(pdr_skip != 2 ? 2 : 1))
	{
		float	xlong, xshort, fy = (float)(y - p0->v), fx;
		int		xl, xr, n, s, tt, l, z, ds, dt, dl, dz;
		short	*pz;
		byte	*pd;

		xlong = p0->u + fy * d2u / d2v;
		if (y < p1->v)
			xshort = p0->u + fy * d1u / d1v;
		else
			xshort = p1->u + (float)(y - p1->v) * (p2->u - p1->u) / (float)(p2->v - p1->v);
		if (xlong < xshort)
		{
			xl = (int)ceilf (xlong);
			xr = (int)ceilf (xshort);
		}
		else
		{
			xl = (int)ceilf (xshort);
			xr = (int)ceilf (xlong);
		}
		if (xl < pdr_vx)
			xl = pdr_vx;
		if (xr > pdr_vx + pdr_vw)
			xr = pdr_vx + pdr_vw;
		n = xr - xl;
		if (n <= 0)
			continue;

		fx = (float)(xl - p0->u);
		s = p0->s + (int)(fx * dsdu + fy * dsdv);
		tt = p0->t + (int)(fx * dtdu + fy * dtdv);
		l = p0->l + (int)(fx * dldu + fy * dldv);
		z = p0->z + (int)(fx * dzdu + fy * dzdv);
		ds = (int)dsdu; dt = (int)dtdu; dl = (int)dldu; dz = (int)dzdu;
		pz = pdr_zbuf + y * pdr_stride + xl;
		pd = pdr_vbuf + y * pdr_stride + xl;
		do
		{
			if ((z >> 16) >= *pz)
			{
				int	ss = s >> 16, ts = tt >> 16;

				if ((unsigned)ss >= (unsigned)ar->sw)
					ss = ss < 0 ? 0 : ar->sw - 1;
				if ((unsigned)ts >= (unsigned)ar->sh)
					ts = ts < 0 ? 0 : ar->sh - 1;
				*pz = z >> 16;
				*pd = ar->cmap[(l & 0x3f00) + ar->skin[ts * ar->sw + ss]];
			}
			pz++;
			pd++;
			s += ds;
			tt += dt;
			l += dl;
			z += dz;
		} while (--n);
	}
}

/*
==============================================================================

CLIPPED TRIANGLES (R_AliasClipTriangle): near plane in view space, then the screen edges

==============================================================================
*/

typedef struct
{
	float	x, y, z;		/* view space; after projection x, y = screen u, v and z = zi */
	float	s, t, l;
} cvert_t;

#define CLIPV	12

static int ClipNear (const cvert_t *in, int n, cvert_t *out)
{
	int		i, o = 0;

	for (i=0 ; i<n ; i++)
	{
		const cvert_t	*a = &in[i], *b = &in[(i + 1) % n];
		int				ina = a->z >= ALIAS_Z_CLIP_PLANE, inb = b->z >= ALIAS_Z_CLIP_PLANE;

		if (ina)
			out[o++] = *a;
		if (ina != inb)
		{
			float	f = (ALIAS_Z_CLIP_PLANE - a->z) / (b->z - a->z);

			out[o].x = a->x + (b->x - a->x) * f;
			out[o].y = a->y + (b->y - a->y) * f;
			out[o].z = ALIAS_Z_CLIP_PLANE;
			out[o].s = a->s + (b->s - a->s) * f;
			out[o].t = a->t + (b->t - a->t) * f;
			out[o].l = a->l + (b->l - a->l) * f;
			o++;
		}
	}
	return o;
}

/* plane: 0 left, 1 right, 2 top, 3 bottom (in screen space, x = u, y = v) */
static int ClipEdge (const cvert_t *in, int n, cvert_t *out, int plane, float lim)
{
	int		i, o = 0;

	for (i=0 ; i<n ; i++)
	{
		const cvert_t	*a = &in[i], *b = &in[(i + 1) % n];
		float			da, db;

		switch (plane)
		{
		case 0: da = a->x - lim; db = b->x - lim; break;
		case 1: da = lim - a->x; db = lim - b->x; break;
		case 2: da = a->y - lim; db = b->y - lim; break;
		default: da = lim - a->y; db = lim - b->y; break;
		}
		if (da >= 0)
			out[o++] = *a;
		if ((da >= 0) != (db >= 0) && o < CLIPV)
		{
			float	f = da / (da - db);

			out[o].x = a->x + (b->x - a->x) * f;
			out[o].y = a->y + (b->y - a->y) * f;
			out[o].z = a->z + (b->z - a->z) * f;
			out[o].s = a->s + (b->s - a->s) * f;
			out[o].t = a->t + (b->t - a->t) * f;
			out[o].l = a->l + (b->l - a->l) * f;
			o++;
		}
		if (o >= CLIPV)
			break;
	}
	return o;
}

/*
==============================================================================

DRAW

==============================================================================
*/

typedef struct
{
	short			u, v;
	int				z;
	unsigned short	l;
	byte			flags;
	byte			pad;
} avert_t;

#define AF_LEFT		1
#define AF_RIGHT	2
#define AF_TOP		4
#define AF_BOTTOM	8
#define AF_Z		16

static void DrawModel (entity_t *e, qboolean viewmodel, asetup_t *a, amodel_t *am, avert_t *av)
{
	araster_t	ar;
	vec3_t		plightvec;
	float		ziscale = (float)0x8000 * (float)0x10000 * (viewmodel ? 3.0f : 1.0f);
	int			ambient, i;
	float		shade;
	float		lightvec[3] = {-1, 0, 0};
	int			seamfix;

// lighting (R_DrawEntitiesOnList / R_DrawViewModel, then R_AliasSetupLighting)
	{
		int		j = PDR_LightPoint (e->origin), lnum;
		int		amb, shd;

		if (viewmodel && j < 24)
			j = 24;		// always give some light on gun
		amb = shd = j;
		for (lnum=0 ; lnum<MAX_DLIGHTS ; lnum++)
		{
			dlight_t	*dl = &cl_dlights[lnum];
			vec3_t		dist;
			float		add;

			if (dl->die < cl.time || (viewmodel && !dl->radius))
				continue;
			VectorSubtract (e->origin, dl->origin, dist);
			add = dl->radius - Length (dist);
			if (add > 0)
				amb += add;
		}
		if (amb > 128)
			amb = 128;
		if (amb + shd > 192)
			shd = 192 - amb;

		ambient = amb;
		if (ambient < LIGHT_MIN)
			ambient = LIGHT_MIN;
		ambient = (255 - ambient) << VID_CBITS;
		if (ambient < LIGHT_MIN)
			ambient = LIGHT_MIN;
		shade = shd < 0 ? 0 : (float)shd;
		shade *= VID_GRADES;
		plightvec[0] = DotProduct (lightvec, a->forward);
		plightvec[1] = -DotProduct (lightvec, a->right);
		plightvec[2] = DotProduct (lightvec, a->up);
	}

	ar.skin = SetupSkin (e, a);
	ar.sw = a->pmdl->skinwidth;
	ar.sh = a->pmdl->skinheight;
	ar.cmap = e->colormap;
	seamfix = (ar.sw >> 1) << 16;
	SetupFrameVerts (e, a);

// vertices
	for (i=0 ; i<am->numverts ; i++)
	{
		const trivertx_t	*pv = &a->verts[i];
		float	x, y, z, lightcos;
		int		temp;
		avert_t	*o = &av[i];

		x = a->xf[0][0]*pv->v[0] + a->xf[0][1]*pv->v[1] + a->xf[0][2]*pv->v[2] + a->xf[0][3];
		y = a->xf[1][0]*pv->v[0] + a->xf[1][1]*pv->v[1] + a->xf[1][2]*pv->v[2] + a->xf[1][3];
		z = a->xf[2][0]*pv->v[0] + a->xf[2][1]*pv->v[1] + a->xf[2][2]*pv->v[2] + a->xf[2][3];

		lightcos = DotProduct (r_avertexnormals[pv->lightnormalindex], plightvec);
		temp = ambient;
		if (lightcos < 0)
		{
			temp += (int)(shade * lightcos);
			if (temp < 0)
				temp = 0;
		}
		o->l = temp;

		if (z < ALIAS_Z_CLIP_PLANE)
		{
			o->flags = AF_Z;
			continue;
		}
		{
			float	zi = 1.0f / z;
			int		u = (int)(x * pdr_xscale * zi + pdr_xcenter);
			int		v = (int)(y * pdr_yscale * zi + pdr_ycenter);
			int		f = 0;

			if (u < pdr_vx)
				f |= AF_LEFT;
			if (u > pdr_vx + pdr_vw)
				f |= AF_RIGHT;
			if (v < pdr_vy)
				f |= AF_TOP;
			if (v > pdr_vy + pdr_vh)
				f |= AF_BOTTOM;
			if (f)
			{
				// (only the flags matter: clipped triangles are rebuilt from the frame)
				if (u < -30000) u = -30000;
				if (u > 30000) u = 30000;
				if (v < -30000) v = -30000;
				if (v > 30000) v = 30000;
			}
			o->u = u;
			o->v = v;
			o->z = (int)(zi * ziscale);
			o->flags = f;
		}
	}
	pdr_c_averts += am->numverts;

// triangles
	for (i=0 ; i<am->numtris ; i++)
	{
		const atri_t	*tri = &am->tris[i];
		const avert_t	*v0 = &av[tri->v[0]], *v1 = &av[tri->v[1]], *v2 = &av[tri->v[2]];
		rvert_t			r[3];
		int				k;

		if (v0->flags & v1->flags & v2->flags)
			continue;		/* all off the same side */

		if (!((v0->flags | v1->flags | v2->flags)))
		{
			const avert_t	*vv[3] = {v0, v1, v2};

			// cheap reject of back faces before the attribute setup
			if ((v0->v - v1->v) * (v0->u - v2->u) - (v0->u - v1->u) * (v0->v - v2->v) >= 0)
				continue;
			for (k=0 ; k<3 ; k++)
			{
				const ast_t	*st = &am->st[tri->v[k]];

				r[k].u = vv[k]->u;
				r[k].v = vv[k]->v;
				r[k].s = (st->s & 0x7fff) << 16;
				if (!tri->front && (st->s & 0x8000))
					r[k].s += seamfix;
				r[k].t = st->t << 16;
				r[k].l = vv[k]->l;
				r[k].z = vv[k]->z;
			}
			RasterTri (&ar, &r[0], &r[1], &r[2]);
			pdr_c_atris++;
			continue;
		}

		// partly clipped: rebuild the corners in view space and clip them
		{
			cvert_t	ca[CLIPV], cb[CLIPV];
			int		n = 3, j;

			for (k=0 ; k<3 ; k++)
			{
				const trivertx_t	*pv = &a->verts[tri->v[k]];
				const ast_t			*st = &am->st[tri->v[k]];

				ca[k].x = a->xf[0][0]*pv->v[0] + a->xf[0][1]*pv->v[1] + a->xf[0][2]*pv->v[2] + a->xf[0][3];
				ca[k].y = a->xf[1][0]*pv->v[0] + a->xf[1][1]*pv->v[1] + a->xf[1][2]*pv->v[2] + a->xf[1][3];
				ca[k].z = a->xf[2][0]*pv->v[0] + a->xf[2][1]*pv->v[1] + a->xf[2][2]*pv->v[2] + a->xf[2][3];
				ca[k].s = (float)((st->s & 0x7fff) << 16);
				if (!tri->front && (st->s & 0x8000))
					ca[k].s += seamfix;
				ca[k].t = (float)(st->t << 16);
				ca[k].l = av[tri->v[k]].l;
			}
			if ((v0->flags | v1->flags | v2->flags) & AF_Z)
			{
				n = ClipNear (ca, n, cb);
				if (n < 3)
					continue;
				memcpy (ca, cb, n * sizeof(cvert_t));
			}
			for (k=0 ; k<n ; k++)
			{
				float	zi = 1.0f / ca[k].z;

				ca[k].x = ca[k].x * pdr_xscale * zi + pdr_xcenter;
				ca[k].y = ca[k].y * pdr_yscale * zi + pdr_ycenter;
				ca[k].z = zi * ziscale;
			}
			n = ClipEdge (ca, n, cb, 0, (float)pdr_vx);
			if (n < 3) continue;
			n = ClipEdge (cb, n, ca, 1, (float)(pdr_vx + pdr_vw));
			if (n < 3) continue;
			n = ClipEdge (ca, n, cb, 2, (float)pdr_vy);
			if (n < 3) continue;
			n = ClipEdge (cb, n, ca, 3, (float)(pdr_vy + pdr_vh));
			if (n < 3) continue;
			for (j=1 ; j+1<n ; j++)
			{
				int		idx[3] = {0, j, j + 1};

				for (k=0 ; k<3 ; k++)
				{
					const cvert_t	*cv = &ca[idx[k]];

					r[k].u = (int)cv->x;
					r[k].v = (int)cv->y;
					r[k].s = (int)cv->s;
					r[k].t = (int)cv->t;
					r[k].l = (int)cv->l;
					r[k].z = (int)cv->z;
				}
				RasterTri (&ar, &r[0], &r[1], &r[2]);
			}
			pdr_c_atris++;
		}
	}
}

void PDR_DrawAliasModel (entity_t *e, qboolean viewmodel)
{
	static avert_t	av_static[MAX_PDR_AVERTS];
	asetup_t		a;
	amodel_t		*am;

	if (!e->colormap)
		Sys_Error ("R_AliasDrawModel: !currententity->colormap");
	am = AModelFor (e->model);
	if (!am || am->numverts > MAX_PDR_AVERTS)
		return;
	SetupModel (e, &a);
	pdr_c_aliasmodels++;

	if (PD_StackRoom (am->numverts * sizeof(avert_t) + 1536))
	{
		avert_t	av[am->numverts];

		DrawModel (e, viewmodel, &a, am, av);
	}
	else
		DrawModel (e, viewmodel, &a, am, av_static);
}
