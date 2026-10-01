/*
 * pdr_world.c -- world and brush model geometry for the Playdate renderer (see pdr.h)
 *
 * Build (at map load): every brush model's nodes, leaves and faces are copied into compact
 * arrays. A face record holds its polygon inline, so drawing a face reads a few consecutive
 * cache lines instead of chasing surfedges -> edges -> vertexes. Faces are renumbered so the
 * faces of a node are consecutive, front-facing ones first.
 *
 * Draw: the BSP is walked front to back. A coverage bitmask (one bit per pixel of the rows
 * drawn this frame) says which pixels are final; a face only draws its spans where the bits
 * are clear and then sets them, so nothing is drawn twice and nothing needs sorting. Nodes
 * and leaves whose projected box is already fully covered are skipped. Brush entities are
 * clipped into the world BSP's leaves beforehand and drawn when the walk reaches each leaf.
 */
#include "pdr.h"
#include "pd_stack.h"

extern mleaf_t	*r_viewleaf;
extern cvar_t	r_clearcolor;

pdr_brush_t		*pdr_world;

#define MAX_PDR_BRUSHES	64
static pdr_brush_t	pdr_brushes[MAX_PDR_BRUSHES];
static int			pdr_numbrushes;
int					pdr_totalfaces;

int		*pdr_dlframe;
unsigned	*pdr_dlbits;

int		pdr_c_nodes, pdr_c_leafs, pdr_c_faces, pdr_c_drawn, pdr_c_occl, pdr_c_spans, pdr_c_pixels;

/*
==============================================================================

BUILD

==============================================================================
*/

static int IsPow2 (unsigned v)
{
	return v && !(v & (v - 1));
}

static void PDR_BuildBrush (pdr_brush_t *b, model_t *m)
{
	int			i, j, k, n, numsurfs = m->numsurfaces;
	int			*order;
	unsigned	size;
	byte		*p;
	msurface_t	*s;

	b->surfaces = m->surfaces;
	b->model = m;
	b->numfaces = numsurfs;
	b->facebase = pdr_totalfaces;
	pdr_totalfaces += numsurfs;

// pdr order: the faces of node 0, front-facing first, then node 1 ...; then any face no node has
	order = Hunk_TempAlloc (numsurfs * sizeof(int));
	b->origtopdr = Hunk_AllocName (numsurfs * sizeof(unsigned short), "pdr");
	b->nodefaces = Hunk_AllocName (m->numnodes * sizeof(pdr_nodefaces_t), "pdr");
	for (i=0 ; i<numsurfs ; i++)
		b->origtopdr[i] = 0xffff;
	n = 0;
	for (i=0 ; i<m->numnodes ; i++)
	{
		mnode_t	*node = &m->nodes[i];
		int		pass;

		b->nodefaces[i].first = n;
		b->nodefaces[i].nfront = b->nodefaces[i].nback = 0;
		for (pass=0 ; pass<2 ; pass++)
		{
			for (j=0 ; j<node->numsurfaces ; j++)
			{
				k = node->firstsurface + j;
				if (k >= numsurfs || b->origtopdr[k] != 0xffff)
					continue;
				if (!(m->surfaces[k].flags & SURF_PLANEBACK) != !pass)
					continue;
				b->origtopdr[k] = n;
				order[n++] = k;
				if (pass)
					b->nodefaces[i].nback++;
				else
					b->nodefaces[i].nfront++;
			}
		}
	}
	for (i=0 ; i<numsurfs ; i++)
	{
		if (b->origtopdr[i] == 0xffff)
		{
			b->origtopdr[i] = n;
			order[n++] = i;
		}
	}

// face records
	size = 0;
	for (i=0 ; i<numsurfs ; i++)
		size += sizeof(pdr_face_t) + m->surfaces[i].numedges * 12;
	b->facedata = Hunk_AllocName (size, "pdrfaces");
	b->faceofs = Hunk_AllocName (numsurfs * sizeof(unsigned), "pdr");
	p = b->facedata;
	for (i=0 ; i<numsurfs ; i++)
	{
		pdr_face_t	*f = (pdr_face_t *)p;
		texture_t	*tex;

		s = &m->surfaces[order[i]];
		b->faceofs[i] = p - b->facedata;
		f->numverts = s->numedges;
		f->flags = 0;
		if (s->flags & SURF_DRAWSKY)
			f->flags |= PF_SKY;
		if (s->flags & SURF_DRAWTURB)
			f->flags |= PF_TURB;
		if (!s->samples)
			f->flags |= PF_NOLIGHT;
		tex = s->texinfo->texture;
		if (IsPow2 (tex->width) && IsPow2 (tex->height))
			f->flags |= PF_POW2;
		f->texinfo = s->texinfo - m->texinfo;
		f->texturemins[0] = s->texturemins[0];
		f->texturemins[1] = s->texturemins[1];
		f->extents[0] = s->extents[0];
		f->extents[1] = s->extents[1];
		for (j=0 ; j<MAXLIGHTMAPS ; j++)
			f->styles[j] = s->styles[j];
		f->lightofs = s->samples ? (int)(s->samples - m->lightdata) : -1;
		if (s->flags & SURF_PLANEBACK)
		{
			f->plane[0] = -s->plane->normal[0];
			f->plane[1] = -s->plane->normal[1];
			f->plane[2] = -s->plane->normal[2];
			f->plane[3] = -s->plane->dist;
		}
		else
		{
			f->plane[0] = s->plane->normal[0];
			f->plane[1] = s->plane->normal[1];
			f->plane[2] = s->plane->normal[2];
			f->plane[3] = s->plane->dist;
		}
		for (j=0 ; j<s->numedges ; j++)
		{
			int			lindex = m->surfedges[s->firstedge + j];
			mvertex_t	*v;

			if (lindex > 0)
				v = &m->vertexes[m->edges[lindex].v[0]];
			else
				v = &m->vertexes[m->edges[-lindex].v[1]];
			VectorCopy (v->position, f->verts[j]);
		}
		p += sizeof(pdr_face_t) + s->numedges * 12;
	}

// nodes
	b->numnodes = m->numnodes;
	b->nodes = Hunk_AllocName (m->numnodes * sizeof(pdr_node_t), "pdrnodes");
	for (i=0 ; i<m->numnodes ; i++)
	{
		mnode_t		*node = &m->nodes[i];
		pdr_node_t	*out = &b->nodes[i];

		VectorCopy (node->plane->normal, out->normal);
		out->dist = node->plane->dist;
		for (j=0 ; j<6 ; j++)
			out->minmaxs[j] = node->minmaxs[j];
		for (j=0 ; j<2 ; j++)
		{
			if (node->children[j]->contents >= 0)
				out->children[j] = node->children[j] - m->nodes;
			else
				out->children[j] = ~(int)((mleaf_t *)node->children[j] - m->leafs);
		}
	}

// leaves
	b->numleafs = m->numleafs + 1;
	b->leafs = Hunk_AllocName (b->numleafs * sizeof(pdr_leaf_t), "pdrleafs");
	n = 0;
	for (i=0 ; i<b->numleafs ; i++)
		n += m->leafs[i].nummarksurfaces;
	b->marks = Hunk_AllocName ((n + 1) * sizeof(unsigned short), "pdr");
	n = 0;
	for (i=0 ; i<b->numleafs ; i++)
	{
		mleaf_t		*leaf = &m->leafs[i];
		pdr_leaf_t	*out = &b->leafs[i];

		for (j=0 ; j<6 ; j++)
			out->minmaxs[j] = leaf->minmaxs[j];
		out->firstmark = n;
		out->nummarks = leaf->contents == CONTENTS_SOLID ? 0 : leaf->nummarksurfaces;
		for (j=0 ; j<out->nummarks ; j++)
			b->marks[n++] = b->origtopdr[leaf->firstmarksurface[j] - m->surfaces];
	}
}

pdr_brush_t *PDR_BrushForModel (model_t *m)
{
	int		i;

	for (i=0 ; i<pdr_numbrushes ; i++)
		if (pdr_brushes[i].surfaces == m->surfaces)
			return &pdr_brushes[i];
	if (pdr_numbrushes == MAX_PDR_BRUSHES)
		Sys_Error ("PDR_BrushForModel: too many brush models");
	PDR_BuildBrush (&pdr_brushes[pdr_numbrushes], m);
	return &pdr_brushes[pdr_numbrushes++];
}

static byte		*pdr_facevis;		/* world faces marked by visible leaves this frame */
static int		pdr_facevis_bytes;
static byte		*pdr_leafvis;		/* PVS of the view leaf, one bit per leaf (leaf 0 = bit 0) */
static byte		*pdr_nodevis;		/* nodes above a PVS leaf */
static short	*pdr_leaffrag;		/* first bmodel fragment in each world leaf, -1 = none */
static mleaf_t	*pdr_visleaf;		/* leaf the PVS bits are for */
static byte		*pdr_leafsolid;

/*
================
PDR_BuildBrushes

Called from R_NewMap, once every model of the level has been loaded.
================
*/
void PDR_BuildBrushes (void)
{
	int		i;
	model_t	*m;

	pdr_numbrushes = 0;
	pdr_totalfaces = 0;
	pdr_world = PDR_BrushForModel (cl.worldmodel);
	for (i=1 ; i<MAX_MODELS ; i++)
	{
		m = cl.model_precache[i];
		if (!m)
			continue;
		if (m->type == mod_brush)
			PDR_BrushForModel (m);
	}

	pdr_dlframe = Hunk_AllocName (pdr_totalfaces * sizeof(int), "pdr");
	pdr_dlbits = Hunk_AllocName (pdr_totalfaces * sizeof(unsigned), "pdr");
	pdr_lightptr = Hunk_AllocName (pdr_totalfaces * sizeof(byte *), "pdr");
	pdr_facevis_bytes = (pdr_world->numfaces + 7) >> 3;
	pdr_facevis = Hunk_AllocName (pdr_facevis_bytes, "pdr");
	pdr_leafvis = Hunk_AllocName ((pdr_world->numleafs + 7) >> 3, "pdr");
	pdr_nodevis = Hunk_AllocName ((pdr_world->numnodes + 7) >> 3, "pdr");
	pdr_leafsolid = Hunk_AllocName (pdr_world->numleafs, "pdr");
	pdr_leaffrag = Hunk_AllocName (pdr_world->numleafs * sizeof(short), "pdr");
	for (i=0 ; i<pdr_world->numleafs ; i++)
	{
		pdr_leaffrag[i] = -1;
		pdr_leafsolid[i] = cl.worldmodel->leafs[i].contents == CONTENTS_SOLID;
	}
	pdr_visleaf = NULL;
}

void PDR_NewMapWorld (void)
{
	PDR_BuildBrushes ();
}

/*
==============================================================================

VISIBILITY

==============================================================================
*/

#define BIT_SET(a, i)	((a)[(i) >> 3] |= 1 << ((i) & 7))
#define BIT_TEST(a, i)	((a)[(i) >> 3] & (1 << ((i) & 7)))

static void PDR_MarkLeaves (void)
{
	model_t	*m = cl.worldmodel;
	byte	*vis;
	int		i;

	if (pdr_visleaf == r_viewleaf)
		return;
	pdr_visleaf = r_viewleaf;

	vis = Mod_LeafPVS (pdr_visleaf, m);
	memset (pdr_leafvis, 0, (pdr_world->numleafs + 7) >> 3);
	memset (pdr_nodevis, 0, (pdr_world->numnodes + 7) >> 3);
	for (i=0 ; i<m->numleafs ; i++)
	{
		if (vis[i>>3] & (1<<(i&7)))
		{
			mnode_t	*node;

			BIT_SET (pdr_leafvis, i + 1);
			node = m->leafs[i+1].parent;
			while (node)
			{
				int	n = node - m->nodes;

				if (BIT_TEST (pdr_nodevis, n))
					break;
				BIT_SET (pdr_nodevis, n);
				node = node->parent;
			}
		}
	}
}

void PDR_MarkLeavesNow (void)
{
	PDR_MarkLeaves ();
}

qboolean PDR_LeafVisible (int leafnum)
{
	return BIT_TEST (pdr_leafvis, leafnum) != 0;
}

/*
==============================================================================

COVERAGE

==============================================================================
*/

static uint32_t	*pdr_cov;			/* PDR_CWORDS words per drawn row */
static int		pdr_rowshift;		/* 1 when interlaced: coverage row = (y - vy) >> 1 */
static int		pdr_covered, pdr_covtotal;
static int		pdr_ystart, pdr_ystep;	/* first drawn row, row step */

static inline uint32_t *CovRow (int y)
{
	return pdr_cov + ((y - pdr_vy) >> pdr_rowshift) * PDR_CWORDS;
}

static inline uint32_t RangeMask (int b0, int b1)	/* bits [b0, b1) of one word, 0 <= b0 < b1 <= 32 */
{
	uint32_t	hi = b1 >= 32 ? 0xffffffffu : ((1u << b1) - 1);

	return hi & ~((1u << b0) - 1);
}

/* is every pixel of [x0, x1) x [y0, y1) (clamped to the view) covered? */
static qboolean PDR_RectCovered (int x0, int x1, int y0, int y1)
{
	int		y, w, w0, w1;

	if (x0 < pdr_vx)
		x0 = pdr_vx;
	if (x1 > pdr_vx + pdr_vw)
		x1 = pdr_vx + pdr_vw;
	if (y0 < pdr_vy)
		y0 = pdr_vy;
	if (y1 > pdr_vy + pdr_vh)
		y1 = pdr_vy + pdr_vh;
	if (x0 >= x1 || y0 >= y1)
		return true;
	x0 -= pdr_vx;
	x1 -= pdr_vx;
	w0 = x0 >> 5;
	w1 = (x1 - 1) >> 5;
	if (pdr_skip != 2 && PDR_ROW_SKIPPED(y0))
		y0++;
	for (y=y0 ; y<y1 ; y+=pdr_ystep)
	{
		uint32_t	*c = CovRow (y);

		if (w0 == w1)
		{
			uint32_t	m = RangeMask (x0 & 31, ((x1 - 1) & 31) + 1);

			if ((c[w0] & m) != m)
				return false;
			continue;
		}
		if ((c[w0] | ((1u << (x0 & 31)) - 1)) != 0xffffffffu)
			return false;
		for (w=w0+1 ; w<w1 ; w++)
			if (c[w] != 0xffffffffu)
				return false;
		if ((c[w1] & RangeMask (0, ((x1 - 1) & 31) + 1)) != RangeMask (0, ((x1 - 1) & 31) + 1))
			return false;
	}
	return true;
}

/* is the box (world space) entirely behind pixels that are already covered? */
static qboolean PDR_BoxCovered (const short *mm)
{
	float	base[3], ex[3], ey[3], ez[3], c[3];
	float	umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
	int		i;

	if (r_origin[0] >= mm[0] - 1 && r_origin[0] <= mm[3] + 1 &&
		r_origin[1] >= mm[1] - 1 && r_origin[1] <= mm[4] + 1 &&
		r_origin[2] >= mm[2] - 1 && r_origin[2] <= mm[5] + 1)
		return false;

	for (i=0 ; i<3 ; i++)
		c[i] = mm[i] - r_origin[i];
	base[0] = DotProduct (c, vright);
	base[1] = DotProduct (c, vup);
	base[2] = DotProduct (c, vpn);
	i = mm[3] - mm[0];
	ex[0] = vright[0] * i; ex[1] = vup[0] * i; ex[2] = vpn[0] * i;
	i = mm[4] - mm[1];
	ey[0] = vright[1] * i; ey[1] = vup[1] * i; ey[2] = vpn[1] * i;
	i = mm[5] - mm[2];
	ez[0] = vright[2] * i; ez[1] = vup[2] * i; ez[2] = vpn[2] * i;

	for (i=0 ; i<8 ; i++)
	{
		float	x = base[0], y = base[1], z = base[2], zi, u, v;

		if (i & 1)
			x += ex[0], y += ex[1], z += ex[2];
		if (i & 2)
			x += ey[0], y += ey[1], z += ey[2];
		if (i & 4)
			x += ez[0], y += ez[1], z += ez[2];
		if (z < 1.0f)
			return false;
		zi = 1.0f / z;
		u = pdr_xcenter + pdr_xscale * x * zi;
		v = pdr_ycenter - pdr_yscale * y * zi;
		if (u < umin) umin = u;
		if (u > umax) umax = u;
		if (v < vmin) vmin = v;
		if (v > vmax) vmax = v;
	}
	if (umax < pdr_umin || umin > pdr_umax || vmax < pdr_vmin || vmin > pdr_vmax)
		return true;	/* off screen */
	return PDR_RectCovered ((int)(umin - 1), (int)(umax + 2), (int)(vmin - 1), (int)(vmax + 2));
}

/* worth testing boxes against the coverage? (not while little of the view is covered) */
static inline qboolean PDR_OcclusionUseful (void)
{
	return pdr_covered * 4 > pdr_covtotal;
}

/*
==============================================================================

FACES

==============================================================================
*/

typedef struct
{
	float	x, y, z;
} vvert_t;

typedef struct
{
	pdr_brush_t			*brush;
	int					fi;			/* face index in the brush */
	const pdr_face_t	*f;
	const pdr_basis_t	*tb;		/* the face's model space (texture axes, plane) */
	entity_t			*ent;
	float				nearzi;
	qboolean			ready;
	pdr_spanctx_t		ctx;
} faceinfo_t;

/* clip a view-space polygon to one frustum plane; the new vertex of an edge is always computed
   from the edge's inside end, so the two faces sharing an edge get the same point */
static int ClipToPlane (const vvert_t *in, int n, vvert_t *out, int cap, int plane)
{
	int		i, o = 0;
	float	d[n];

	for (i=0 ; i<n ; i++)
	{
		const vvert_t	*v = &in[i];

		switch (plane)
		{
		case 0:	d[i] = pdr_xscale * v->x + pdr_hw * v->z; break;
		case 1:	d[i] = pdr_hw * v->z - pdr_xscale * v->x; break;
		case 2:	d[i] = pdr_hh * v->z - pdr_yscale * v->y; break;
		default: d[i] = pdr_hh * v->z + pdr_yscale * v->y; break;
		}
	}
	for (i=0 ; i<n ; i++)
	{
		int		j = i + 1 == n ? 0 : i + 1;
		float	di = d[i], dj = d[j];

		if (di >= 0)
		{
			if (o >= cap)
				return 0;
			out[o++] = in[i];
		}
		if ((di >= 0) != (dj >= 0))
		{
			const vvert_t	*a, *b;
			float			t;

			if (o >= cap)
				return 0;
			if (di >= 0)
			{
				a = &in[i]; b = &in[j];
				t = di / (di - dj);
			}
			else
			{
				a = &in[j]; b = &in[i];
				t = dj / (dj - di);
			}
			out[o].x = a->x + t * (b->x - a->x);
			out[o].y = a->y + t * (b->y - a->y);
			out[o].z = a->z + t * (b->z - a->z);
			o++;
		}
	}
	return o;
}

texture_t *PDR_TextureAnimation (texture_t *base, int frame)
{
	int		relative, count;

	if (frame && base->alternate_anims)
		base = base->alternate_anims;
	if (!base->anim_total)
		return base;
	relative = (int)(cl.time*10) % base->anim_total;
	count = 0;
	while (base->anim_min > relative || base->anim_max <= relative)
	{
		base = base->anim_next;
		if (!base)
			Sys_Error ("R_TextureAnimation: broken cycle");
		if (++count > 100)
			Sys_Error ("R_TextureAnimation: infinite cycle");
	}
	return base;
}

static int PDR_MipLevel (float scale)
{
	int		mip;

	if (scale >= pdr_mipscale[0])
		mip = 0;
	else if (scale >= pdr_mipscale[1])
		mip = 1;
	else if (scale >= pdr_mipscale[2])
		mip = 2;
	else
		mip = 3;
	return mip < pdr_minmip ? pdr_minmip : mip;
}

static inline int Log2 (unsigned v)
{
	return 31 - __builtin_clz (v);
}

/* span parameters of a face, set up the first time one of its spans is drawn */
static void PDR_SetupFace (faceinfo_t *fi)
{
	pdr_spanctx_t		*c = &fi->ctx;
	const pdr_face_t	*f = fi->f;
	const pdr_basis_t	*b = fi->tb;
	model_t				*m = fi->brush->model;
	mtexinfo_t			*ti = &m->texinfo[f->texinfo];
	texture_t			*tex;
	vec3_t				pn, sa, ta, torg;
	float				distinv, mipscale, t;
	int					mip;

	fi->ready = true;

// 1/z
	PDR_TransformToView (b, f->plane, pn);
	distinv = f->plane[3] - DotProduct (b->org, f->plane);
	distinv = distinv != 0 ? 1.0f / distinv : 0;
	c->zistepu = pn[0] * pdr_xscaleinv * distinv;
	c->zistepv = -pn[1] * pdr_yscaleinv * distinv;
	c->ziorigin = pn[2] * distinv - pdr_xcenter * c->zistepu - pdr_ycenter * c->zistepv;

	if (f->flags & PF_SKY)
	{
		c->kind = PDR_SPAN_SKY;
		return;
	}

	tex = PDR_TextureAnimation (ti->texture, fi->ent ? fi->ent->frame : 0);
	if (f->flags & PF_TURB)
	{
		c->kind = PDR_SPAN_TURB;
		mip = 0;
	}
	else
	{
		c->kind = PDR_SPAN_LIT;
		mip = PDR_MipLevel (fi->nearzi * pdr_scale_for_mip * ti->mipadjust);
	}

// s/z and t/z (as D_CalcGradients)
	mipscale = 1.0f / (float)(1 << mip);
	PDR_TransformToView (b, ti->vecs[0], sa);
	PDR_TransformToView (b, ti->vecs[1], ta);
	t = pdr_xscaleinv * mipscale;
	c->sdivzstepu = sa[0] * t;
	c->tdivzstepu = ta[0] * t;
	t = pdr_yscaleinv * mipscale;
	c->sdivzstepv = -sa[1] * t;
	c->tdivzstepv = -ta[1] * t;
	c->sdivzorigin = sa[2] * mipscale - pdr_xcenter * c->sdivzstepu - pdr_ycenter * c->sdivzstepv;
	c->tdivzorigin = ta[2] * mipscale - pdr_xcenter * c->tdivzstepu - pdr_ycenter * c->tdivzstepv;
	PDR_TransformToView (b, b->org, torg);
	VectorScale (torg, mipscale, torg);
	t = 0x10000 * mipscale;
	c->sadjust = ((int)(DotProduct (torg, sa) * 0x10000 + 0.5f)) -
			((f->texturemins[0] << 16) >> mip) + (int)(ti->vecs[0][3] * t);
	c->tadjust = ((int)(DotProduct (torg, ta) * 0x10000 + 0.5f)) -
			((f->texturemins[1] << 16) >> mip) + (int)(ti->vecs[1][3] * t);
	c->bbextents = ((f->extents[0] << 16) >> mip) - 1;
	c->bbextentt = ((f->extents[1] << 16) >> mip) - 1;

// texture
	c->tex = (byte *)tex + tex->offsets[mip];
	c->tw = tex->width >> mip;
	c->th = tex->height >> mip;
	c->soff = (f->texturemins[0] << 16) >> mip;
	c->toff = (f->texturemins[1] << 16) >> mip;
	if (IsPow2 (c->tw) && IsPow2 (c->th))
	{
		c->tshift = 16 - Log2 (c->tw);
		c->smask = c->tw - 1;
		c->tmask = (c->th - 1) << Log2 (c->tw);
	}
	else
		c->tshift = -1;

	if (c->kind == PDR_SPAN_TURB)
		return;

// light
	c->colormap = vid.colormap;
	c->lshift = 4 - mip;
	c->lw = (f->extents[0] >> 4) + 1;
	c->lh = (f->extents[1] >> 4) + 1;
	c->lmaxs = (c->lw - 1) << 16;
	c->lmaxt = (c->lh - 1) << 16;
	c->light = PDR_FaceLight (fi->brush, fi->fi, f, &c->lconst);
}

/* a run of uncovered pixels [x0, x1) on row y */
static inline void PDR_Run (faceinfo_t *fi, int y, int x0, int x1)
{
	if (!fi->ready)
		PDR_SetupFace (fi);
	PDR_DrawSpan (&fi->ctx, y, x0, x1);
	if (pdr_zany)
	{
		int	r = y - pdr_vy, a = x0, b = x1;

		if (a < pdr_zx0[r])
			a = pdr_zx0[r];
		if (b > pdr_zx1[r])
			b = pdr_zx1[r];
		if (a < b)
			PDR_ZSpan (&fi->ctx, y, a, b);
	}
	pdr_c_spans++;
	pdr_c_pixels += x1 - x0;
	pdr_covered += x1 - x0;
}

/* draw the uncovered part of [x0, x1) on row y and mark it covered */
static void PDR_CoverSpan (faceinfo_t *fi, int y, int x0, int x1)
{
	uint32_t	*cov = CovRow (y);
	int			b0 = x0 - pdr_vx, b1 = x1 - pdr_vx;
	int			w, w0 = b0 >> 5, w1 = (b1 - 1) >> 5;
	int			runstart = -1;

	for (w=w0 ; w<=w1 ; w++)
	{
		uint32_t	m = RangeMask (w == w0 ? b0 & 31 : 0, w == w1 ? ((b1 - 1) & 31) + 1 : 32);
		uint32_t	unc = m & ~cov[w];
		int			base = (w << 5) + pdr_vx;

		if (unc == m && unc == 0xffffffffu)
		{
			// the whole word is open: extend (or start) the run
			if (runstart < 0)
				runstart = base;
			cov[w] = 0xffffffffu;
			continue;
		}
		if (!unc)
		{
			if (runstart >= 0)
			{
				PDR_Run (fi, y, runstart, base);
				runstart = -1;
			}
			continue;
		}
		cov[w] |= m;
		while (unc)
		{
			int			s = __builtin_ctz (unc);
			uint32_t	rest = ~(unc >> s);
			int			len = rest ? __builtin_ctz (rest) : 32 - s;

			if (s != 0 && runstart >= 0)
			{
				PDR_Run (fi, y, runstart, base);
				runstart = -1;
			}
			if (runstart < 0)
				runstart = base + s;
			if (s + len < 32)
			{
				PDR_Run (fi, y, runstart, base + s + len);
				runstart = -1;
				unc &= ~(len >= 32 ? 0xffffffffu : (((1u << len) - 1) << s));
			}
			else
				unc = 0;	/* runs into the next word */
		}
	}
	if (runstart >= 0)
		PDR_Run (fi, y, runstart, x1);
}

static void PDR_RasterRows (faceinfo_t *fi, const float *pu, const float *pv, int n, int ytop, int ybot);

/*
================
PDR_RasterFace

verts: the polygon in world space; geometry is transformed with the world basis, texture and
1/z come from fi->tb (the face's model space).
================
*/
static void PDR_RasterFace (faceinfo_t *fi, const float (*verts)[3], int nverts, int clipflags)
{
	// sized for this polygon: each of the 4 planes can add a vertex
	vvert_t		bufa[nverts + 4], bufb[nverts + 4], *in, *out, *tmp;
	float		pu[nverts + 4], pv[nverts + 4];
	float		nearzi = 0, vmin = 1e9f, vmax = -1e9f;
	int			i, n, p, ytop, ybot, y;

// transform
	in = bufa;
	for (i=0 ; i<nverts ; i++)
	{
		float	d[3];

		d[0] = verts[i][0] - r_origin[0];
		d[1] = verts[i][1] - r_origin[1];
		d[2] = verts[i][2] - r_origin[2];
		in[i].x = DotProduct (d, vright);
		in[i].y = DotProduct (d, vup);
		in[i].z = DotProduct (d, vpn);
	}
	n = nverts;

// clip
	out = bufb;
	for (p=0 ; p<4 ; p++)
	{
		if (!(clipflags & (1 << p)))
			continue;
		n = ClipToPlane (in, n, out, nverts + 4, p);
		if (n < 3)
			return;
		tmp = in; in = out; out = tmp;
	}

// project
	for (i=0 ; i<n ; i++)
	{
		float	z = in[i].z, zi, u, v;

		if (z < PDR_NEAR_CLIP)
			z = PDR_NEAR_CLIP;
		zi = 1.0f / z;
		if (zi > nearzi)
			nearzi = zi;
		u = pdr_xcenter + pdr_xscale * in[i].x * zi;
		v = pdr_ycenter - pdr_yscale * in[i].y * zi;
		if (u < pdr_umin) u = pdr_umin;
		if (u > pdr_umax) u = pdr_umax;
		if (v < pdr_vmin) v = pdr_vmin;
		if (v > pdr_vmax) v = pdr_vmax;
		pu[i] = u;
		pv[i] = v;
		if (v < vmin) vmin = v;
		if (v > vmax) vmax = v;
	}
	fi->nearzi = nearzi;

	ytop = (int)ceilf (vmin);
	ybot = (int)ceilf (vmax);		/* rows [ytop, ybot) */
	if (pdr_skip != 2 && ytop < ybot && PDR_ROW_SKIPPED(ytop))
		ytop++;
	if (ytop >= ybot)
		return;
	PDR_RasterRows (fi, pu, pv, n, ytop, ybot);
}

/* the polygon's rows [ytop, ybot): x of both sides on each drawn row, then the spans */
static void PDR_RasterRows (faceinfo_t *fi, const float *pu, const float *pv, int n, int ytop, int ybot)
{
	short	xa[ybot - ytop], xb[ybot - ytop];
	int		i, y;

// edges: x at each drawn row, from the edge's upper end (so both faces of an edge agree)
	for (i=0 ; i<n ; i++)
	{
		int		j = i + 1 == n ? 0 : i + 1;
		float	u0, v0, u1, v1, dudv, x;
		int		y0, y1, xf, step;
		short	*dst;

		if (pv[i] < pv[j])
		{
			u0 = pu[i]; v0 = pv[i]; u1 = pu[j]; v1 = pv[j];
			dst = xa;
		}
		else
		{
			u0 = pu[j]; v0 = pv[j]; u1 = pu[i]; v1 = pv[i];
			dst = xb;
		}
		y0 = (int)ceilf (v0);
		y1 = (int)ceilf (v1);
		if (y0 >= y1)
			continue;
		if (pdr_skip != 2 && PDR_ROW_SKIPPED(y0))
			y0++;
		if (y0 >= y1)
			continue;
		dudv = (u1 - u0) / (v1 - v0);
		x = u0 + ((float)y0 - v0) * dudv;
		xf = (int)(x * 65536.0f) + 0xffff;
		step = (int)(dudv * 65536.0f) * pdr_ystep;
		if (y0 < ytop)		/* (only rows the polygon has; float rounding) */
			continue;
		if (y1 > ybot)
			y1 = ybot;
		for (y=y0 ; y<y1 ; y+=pdr_ystep)
		{
			dst[y - ytop] = xf >> 16;
			xf += step;
		}
	}

// spans
	pdr_c_faces++;
	{
		int	before = pdr_c_spans;

		for (y=ytop ; y<ybot ; y+=pdr_ystep)
		{
			int	r = y - ytop, x0 = xa[r], x1 = xb[r];

			if (x0 > x1)
			{
				int	t = x0;
				x0 = x1;
				x1 = t;
			}
			if (x0 < pdr_vx)
				x0 = pdr_vx;
			if (x1 > pdr_vx + pdr_vw)
				x1 = pdr_vx + pdr_vw;
			if (x0 < x1)
				PDR_CoverSpan (fi, y, x0, x1);
		}
		if (pdr_c_spans != before)
			pdr_c_drawn++;
	}
}

/*
==============================================================================

BRUSH ENTITIES

Each visible brush entity's faces are taken front to back (its own BSP), moved into world
space and split by the world BSP into the leaves they cross; the walk draws a leaf's fragments
when it reaches the leaf. As Quake does, a brush entity in front of another in the same leaf is
not guaranteed to win (entities are taken nearest first, which handles the usual cases).
==============================================================================
*/

#define MAX_BENTS		32
#define FRAG_POOL		(24 * 1024)

typedef struct
{
	entity_t		*ent;
	pdr_brush_t		*brush;
	pdr_basis_t		basis;			/* view in the model's space */
	float			rot[3][3];		/* model -> world rotation (rows), identity if not rotated */
	qboolean		rotated;
} bent_t;

typedef struct
{
	short			next;			/* next fragment in the leaf, -1 = end */
	byte			bent;
	byte			numverts;
	unsigned short	face;
	unsigned short	pad;
	float			verts[][3];
} frag_t;

static bent_t	pdr_bents[MAX_BENTS];
static int		pdr_numbents;
static int		pdr_fragbytes;
static int		pdr_fragleafs[1024];
static int		pdr_numfragleafs;
static byte		pdr_fragpool[FRAG_POOL] __attribute__((aligned(4)));

#define FRAG_AT(o)	((frag_t *)(pdr_fragpool + (o) * 4))

void PDR_ClearBmodels (void)
{
	int		i;

	for (i=0 ; i<pdr_numfragleafs ; i++)
		pdr_leaffrag[pdr_fragleafs[i]] = -1;
	pdr_numfragleafs = 0;
	pdr_numbents = 0;
	pdr_fragbytes = 0;
}

static void PDR_StoreFragment (int leaf, int bent, int face, const float (*v)[3], int n)
{
	int		size = sizeof(frag_t) + n * 12;
	frag_t	*fr;
	short	*link;

	if (pdr_fragbytes + size > FRAG_POOL || (pdr_fragbytes >> 2) > 0x7fff)
		return;
	fr = FRAG_AT (pdr_fragbytes >> 2);
	fr->next = -1;
	fr->bent = bent;
	fr->numverts = n;
	fr->face = face;
	memcpy (fr->verts, v, n * 12);

// append at the end of the leaf's list (keeps the front-to-back order of one entity)
	link = &pdr_leaffrag[leaf];
	if (*link < 0)
	{
		if (pdr_numfragleafs < (int)(sizeof(pdr_fragleafs) / sizeof(pdr_fragleafs[0])))
			pdr_fragleafs[pdr_numfragleafs++] = leaf;
		else
			return;
	}
	while (*link >= 0)
		link = &FRAG_AT (*link)->next;
	*link = pdr_fragbytes >> 2;
	pdr_fragbytes += size;
}

/* split a world-space polygon by the world BSP down to the leaves. Brush entities are few, so the
   polygons in flight live in static memory (the stack is too small for a recursion of them) */
#define CLIPWORK_VERTS	4096
#define CLIPWORK_ITEMS	256

typedef struct
{
	int		node;
	int		first, n;		/* vertices in clipwork_v */
} clipitem_t;

static float		clipwork_v[CLIPWORK_VERTS][3];
static clipitem_t	clipwork_items[CLIPWORK_ITEMS];

static void PDR_ClipFragment (int bent, int face, const float (*v0)[3], int n0)
{
	int		nitems = 0, nv = 0;

	if (n0 > PDR_MAXCLIPVERTS)
		return;
	memcpy (clipwork_v[0], v0, n0 * 12);
	clipwork_items[0].node = 0;
	clipwork_items[0].first = 0;
	clipwork_items[0].n = n0;
	nitems = 1;
	nv = n0;

	while (nitems)
	{
		clipitem_t			it = clipwork_items[--nitems];
		const float			(*v)[3] = (const float (*)[3])clipwork_v[it.first];
		int					n = it.n, node = it.node, i, nf, nb, sides;
		const pdr_node_t	*pn = NULL;
		float				d[PDR_MAXCLIPVERTS];
		float				(*front)[3], (*back)[3];

		while (1)
		{
			if (node < 0)
				break;
			if (!BIT_TEST (pdr_nodevis, node))
			{
				pn = NULL;
				break;
			}
			pn = &pdr_world->nodes[node];
			sides = 0;
			for (i=0 ; i<n ; i++)
			{
				d[i] = DotProduct (v[i], pn->normal) - pn->dist;
				sides |= d[i] > 0 ? 1 : 2;
			}
			if (sides == 3)
				break;
			node = pn->children[sides == 1 ? 0 : 1];
		}
		if (node < 0)
		{
			int	leaf = ~node;

			if (!pdr_leafsolid[leaf] && BIT_TEST (pdr_leafvis, leaf))
				PDR_StoreFragment (leaf, bent, face, v, n);
			continue;
		}
		if (!pn)
			continue;

		if (nv + 2 * (n + 2) > CLIPWORK_VERTS || nitems + 2 > CLIPWORK_ITEMS)
			continue;
		front = &clipwork_v[nv];
		back = &clipwork_v[nv + n + 2];
		nf = nb = 0;
		for (i=0 ; i<n ; i++)
		{
			int	j = i + 1 == n ? 0 : i + 1;

			if (d[i] > 0)
			{
				VectorCopy (v[i], front[nf]); nf++;
			}
			else
			{
				VectorCopy (v[i], back[nb]); nb++;
			}
			if ((d[i] > 0) != (d[j] > 0))
			{
				float	t = d[i] / (d[i] - d[j]);
				float	mid[3];

				mid[0] = v[i][0] + t * (v[j][0] - v[i][0]);
				mid[1] = v[i][1] + t * (v[j][1] - v[i][1]);
				mid[2] = v[i][2] + t * (v[j][2] - v[i][2]);
				VectorCopy (mid, front[nf]); nf++;
				VectorCopy (mid, back[nb]); nb++;
			}
		}
		if (nf > PDR_MAXCLIPVERTS || nb > PDR_MAXCLIPVERTS)
			continue;
		// back first on the stack so the front side is finished first
		if (nb >= 3)
		{
			clipwork_items[nitems].node = pn->children[1];
			clipwork_items[nitems].first = nv + n + 2;
			clipwork_items[nitems].n = nb;
			nitems++;
		}
		if (nf >= 3)
		{
			clipwork_items[nitems].node = pn->children[0];
			clipwork_items[nitems].first = nv;
			clipwork_items[nitems].n = nf;
			nitems++;
		}
		nv += 2 * (n + 2);
	}
}

static void PDR_BmodelFace (bent_t *be, int bi, int fi)
{
	const pdr_face_t	*f = PDR_FACE (be->brush, fi);
	static float		w[PDR_MAXCLIPVERTS][3];
	const entity_t		*e = be->ent;
	int					i;

	if (DotProduct (be->basis.org, f->plane) - f->plane[3] <= PDR_BACKFACE_EPSILON)
		return;		/* facing away */
	if (f->numverts > PDR_MAXCLIPVERTS - 2)
		return;
	for (i=0 ; i<f->numverts ; i++)
	{
		const float	*p = f->verts[i];

		if (be->rotated)
		{
			w[i][0] = DotProduct (be->rot[0], p) + e->origin[0];
			w[i][1] = DotProduct (be->rot[1], p) + e->origin[1];
			w[i][2] = DotProduct (be->rot[2], p) + e->origin[2];
		}
		else
		{
			w[i][0] = p[0] + e->origin[0];
			w[i][1] = p[1] + e->origin[1];
			w[i][2] = p[2] + e->origin[2];
		}
	}
	PDR_ClipFragment (bi, fi, (const float (*)[3])w, f->numverts);
}

/* the model's faces, front to back from the eye (in model space) */
static void PDR_BmodelNode (bent_t *be, int bi, int node, int depth)
{
	const pdr_node_t		*pn;
	const pdr_nodefaces_t	*nf;
	float					dot;
	int						side, i;

	if (node < 0 || depth > 64)
		return;
	pn = &be->brush->nodes[node];
	dot = DotProduct (be->basis.org, pn->normal) - pn->dist;
	side = dot < 0;
	PDR_BmodelNode (be, bi, pn->children[side], depth + 1);
	nf = &be->brush->nodefaces[node];
	if (side == 0)
	{
		for (i=0 ; i<nf->nfront ; i++)
			PDR_BmodelFace (be, bi, nf->first + i);
	}
	else
	{
		for (i=0 ; i<nf->nback ; i++)
			PDR_BmodelFace (be, bi, nf->first + nf->nfront + i);
	}
	PDR_BmodelNode (be, bi, pn->children[!side], depth + 1);
}

/*
================
PDR_AddBmodel

e is visible (its box passed the frustum test). Called nearest entity first.
================
*/
void PDR_AddBmodel (entity_t *e, pdr_brush_t *b, model_t *m)
{
	bent_t	*be;
	int		bi, head = m->hulls[0].firstclipnode;

	if (pdr_numbents == MAX_BENTS)
		return;
	bi = pdr_numbents++;
	be = &pdr_bents[bi];
	be->ent = e;
	be->brush = b;
	be->rotated = e->angles[0] || e->angles[1] || e->angles[2];
	if (be->rotated)
	{
	// Quake's R_RotateBmodel: yaw, then pitch, then roll
		float	a, s, c, t1[3][3], t2[3][3], t3[3][3];
		int		j, k;

		a = e->angles[YAW] * (M_PI/180.0f);
		s = sinf (a); c = cosf (a);
		t1[0][0] = c; t1[0][1] = s; t1[0][2] = 0;
		t1[1][0] = -s; t1[1][1] = c; t1[1][2] = 0;
		t1[2][0] = 0; t1[2][1] = 0; t1[2][2] = 1;
		a = e->angles[PITCH] * (M_PI/180.0f);
		s = sinf (a); c = cosf (a);
		t2[0][0] = c; t2[0][1] = 0; t2[0][2] = -s;
		t2[1][0] = 0; t2[1][1] = 1; t2[1][2] = 0;
		t2[2][0] = s; t2[2][1] = 0; t2[2][2] = c;
		for (j=0 ; j<3 ; j++)
			for (k=0 ; k<3 ; k++)
				t3[j][k] = t2[j][0]*t1[0][k] + t2[j][1]*t1[1][k] + t2[j][2]*t1[2][k];
		a = e->angles[ROLL] * (M_PI/180.0f);
		s = sinf (a); c = cosf (a);
		t1[0][0] = 1; t1[0][1] = 0; t1[0][2] = 0;
		t1[1][0] = 0; t1[1][1] = c; t1[1][2] = s;
		t1[2][0] = 0; t1[2][1] = -s; t1[2][2] = c;
		for (j=0 ; j<3 ; j++)
			for (k=0 ; k<3 ; k++)
				t2[j][k] = t1[j][0]*t3[0][k] + t1[j][1]*t3[1][k] + t1[j][2]*t3[2][k];
		// t2 is Quake's entity_rotation: world -> model (R_EntityRotate). Its transpose takes
		// model -> world.
		for (j=0 ; j<3 ; j++)
			for (k=0 ; k<3 ; k++)
				be->rot[j][k] = t2[k][j];
		{
			vec3_t	d;

			VectorSubtract (r_origin, e->origin, d);
			for (j=0 ; j<3 ; j++)
			{
				be->basis.org[j] = DotProduct (t2[j], d);
				be->basis.right[j] = DotProduct (t2[j], vright);
				be->basis.up[j] = DotProduct (t2[j], vup);
				be->basis.fwd[j] = DotProduct (t2[j], vpn);
			}
		}
	}
	else
	{
		VectorSubtract (r_origin, e->origin, be->basis.org);
		VectorCopy (vright, be->basis.right);
		VectorCopy (vup, be->basis.up);
		VectorCopy (vpn, be->basis.fwd);
	}

// dynamic lights (as Quake, in world coordinates against the model's planes)
	if (m->firstmodelsurface != 0)
		PDR_MarkBmodelLights (b, head);

	if (head >= 0 && head < b->numnodes)
		PDR_BmodelNode (be, bi, head, 0);
}

static void PDR_DrawLeafFragments (int leaf)
{
	int		o;

	for (o = pdr_leaffrag[leaf] ; o >= 0 ; )
	{
		frag_t		*fr = FRAG_AT (o);
		bent_t		*be = &pdr_bents[fr->bent];
		faceinfo_t	fi;

		fi.brush = be->brush;
		fi.fi = fr->face;
		fi.f = PDR_FACE (be->brush, fr->face);
		fi.tb = &be->basis;
		fi.ent = be->ent;
		fi.ready = false;
		PDR_RasterFace (&fi, (const float (*)[3])fr->verts, fr->numverts, 15);
		o = fr->next;
	}
}

/*
==============================================================================

THE WALK

==============================================================================
*/

#define CLIP_FAR	16		/* subtree beyond r_maxdist: only sky faces */

/* frustum test of a box: -1 = outside, else the planes the children still need */
static int PDR_CullBox (const short *mm, int clip)
{
	int		i;

	for (i=0 ; i<4 ; i++)
	{
		const int	*ix;
		const float	*pl;
		float		d;

		if (!(clip & (1 << i)))
			continue;
		ix = pdr_frustum_idx[i];
		pl = pdr_frustum[i];
		d = pl[0] * mm[ix[0]] + pl[1] * mm[ix[1]] + pl[2] * mm[ix[2]] + pl[3];
		if (d <= 0)
			return -1;
		d = pl[0] * mm[ix[3]] + pl[1] * mm[ix[4]] + pl[2] * mm[ix[5]] + pl[3];
		if (d >= 0)
			clip &= ~(1 << i);
	}
	return clip;
}

static qboolean PDR_BoxBeyond (const short *mm)
{
	float	d, dist2 = 0;
	int		j;

	for (j=0 ; j<3 ; j++)
	{
		if (r_origin[j] < mm[j])
			d = mm[j] - r_origin[j];
		else if (r_origin[j] > mm[3+j])
			d = r_origin[j] - mm[3+j];
		else
			continue;
		dist2 += d * d;
	}
	return dist2 > pdr_maxdist2;
}

static void PDR_WorldFace (int fi, int clip)
{
	faceinfo_t	info;

	info.brush = pdr_world;
	info.fi = fi;
	info.f = PDR_FACE (pdr_world, fi);
	info.tb = &pdr_wbasis;
	info.ent = NULL;
	info.ready = false;
	PDR_RasterFace (&info, (const float (*)[3])info.f->verts, info.f->numverts, clip & 15);
}

static void PDR_VisitLeaf (int leaf, int clip)
{
	const pdr_leaf_t	*pl;
	int					i;

	if (pdr_leafsolid[leaf] || !BIT_TEST (pdr_leafvis, leaf))
		return;
	pl = &pdr_world->leafs[leaf];
	if (clip & 15)
	{
		clip = PDR_CullBox (pl->minmaxs, clip);
		if (clip < 0)
			return;
	}
	if (PDR_OcclusionUseful () && PDR_BoxCovered (pl->minmaxs))
	{
		pdr_c_occl++;
		return;
	}
	pdr_c_leafs++;

	{
		const unsigned short	*mark = pdr_world->marks + pl->firstmark;
		int						c = pl->nummarks;

		if (!(clip & CLIP_FAR))
		{
			for (i=0 ; i<c ; i++)
				BIT_SET (pdr_facevis, mark[i]);
		}
		else
		{
			for (i=0 ; i<c ; i++)
				if (PDR_FACE (pdr_world, mark[i])->flags & PF_SKY)
					BIT_SET (pdr_facevis, mark[i]);
		}
	}

	if (pdr_leaffrag[leaf] >= 0 && !(clip & CLIP_FAR))
		PDR_DrawLeafFragments (leaf);
}

typedef struct
{
	short	node;
	byte	clip;
	byte	state;		/* 0 = enter; 1/2 = faces of side 0/1, then the far child; 3/4 = no faces */
} wstack_t;

#define WSTACK	96

static void PDR_Walk (void)
{
	wstack_t	stack[WSTACK];
	int			sp = 0;
	int			clip0 = 15;

	if (pdr_maxdist2 > 0 && PDR_BoxBeyond (pdr_world->nodes[0].minmaxs))
		clip0 |= CLIP_FAR;
	stack[sp].node = 0;
	stack[sp].clip = clip0;
	stack[sp].state = 0;
	sp++;

	while (sp)
	{
		wstack_t			e = stack[--sp];
		const pdr_node_t	*pn;
		int					clip = e.clip;

		if (pdr_covered >= pdr_covtotal)
			break;		/* the view is full */

		if (e.state == 0)
		{
			float	dot;
			int		side;

			if (e.node < 0)
			{
				PDR_VisitLeaf (~e.node, clip);
				continue;
			}
			if (!BIT_TEST (pdr_nodevis, e.node))
				continue;
			pn = &pdr_world->nodes[e.node];
			if (clip & 15)
			{
				clip = PDR_CullBox (pn->minmaxs, clip);
				if (clip < 0)
					continue;
			}
			if (pdr_maxdist2 > 0 && !(clip & CLIP_FAR) && PDR_BoxBeyond (pn->minmaxs))
				clip |= CLIP_FAR;
			if (PDR_OcclusionUseful () && PDR_BoxCovered (pn->minmaxs))
			{
				pdr_c_occl++;
				continue;
			}
			pdr_c_nodes++;
			dot = DotProduct (r_origin, pn->normal) - pn->dist;
			side = dot < 0;
			if (sp + 2 > WSTACK)
				continue;
			stack[sp].node = e.node;
			stack[sp].clip = clip;
			if (dot > PDR_BACKFACE_EPSILON)
				stack[sp].state = 1;
			else if (dot < -PDR_BACKFACE_EPSILON)
				stack[sp].state = 2;
			else
				stack[sp].state = 3 + side;
			sp++;
			stack[sp].node = pn->children[side];
			stack[sp].clip = clip;
			stack[sp].state = 0;
			sp++;
			continue;
		}

		// back from the near side: this node's faces, then the far side
		{
			const pdr_nodefaces_t	*nf = &pdr_world->nodefaces[e.node];
			int						side = (e.state - 1) & 1;

			if (e.state <= 2)
			{
				int	first = side ? nf->first + nf->nfront : nf->first;
				int	count = side ? nf->nback : nf->nfront;
				int	i;

				for (i=0 ; i<count ; i++)
				{
					int	fi = first + i;

					if (BIT_TEST (pdr_facevis, fi))
						PDR_WorldFace (fi, clip);
				}
			}
			pn = &pdr_world->nodes[e.node];
			stack[sp].node = pn->children[!side];
			stack[sp].clip = clip;
			stack[sp].state = 0;
			sp++;
		}
	}
}

/*
================
PDR_DrawWorld
================
*/
static void PDR_FillUncovered (void)
{
	int		y, w, color = pdr_maxdist2 > 0 ? 0 : ((int)r_clearcolor.value & 0xff);

	for (y=pdr_ystart ; y<pdr_vy + pdr_vh ; y+=pdr_ystep)
	{
		uint32_t	*cov = CovRow (y);

		for (w=0 ; w<PDR_CWORDS ; w++)
		{
			uint32_t	unc = ~cov[w];

			while (unc)
			{
				int			s = __builtin_ctz (unc);
				uint32_t	rest = ~(unc >> s);
				int			len = rest ? __builtin_ctz (rest) : 32 - s;
				int			x0 = pdr_vx + (w << 5) + s;

				PDR_FillSpan (y, x0, x0 + len, color);
				unc &= len >= 32 ? 0 : ~(((1u << len) - 1) << s);
			}
		}
	}
}

static void PDR_DrawWorldWith (uint32_t *cov)
{
	int		rows, y, w;

	pdr_cov = cov;
	pdr_rowshift = pdr_skip != 2;
	pdr_ystep = pdr_skip != 2 ? 2 : 1;
	pdr_ystart = pdr_vy;
	if (pdr_skip != 2 && PDR_ROW_SKIPPED(pdr_ystart))
		pdr_ystart++;
	rows = 0;
	for (y=pdr_ystart ; y<pdr_vy + pdr_vh ; y+=pdr_ystep)
	{
		uint32_t	*c = CovRow (y);

		for (w=0 ; w<PDR_CWORDS ; w++)
		{
			int	b0 = w << 5;

			if (b0 + 32 <= pdr_vw)
				c[w] = 0;
			else if (b0 >= pdr_vw)
				c[w] = 0xffffffffu;
			else
				c[w] = ~RangeMask (0, pdr_vw - b0);
		}
		rows++;
	}
	pdr_covered = 0;
	pdr_covtotal = rows * pdr_vw;

	memset (pdr_facevis, 0, pdr_facevis_bytes);
	PDR_Walk ();
	if (pdr_covered < pdr_covtotal)
		PDR_FillUncovered ();
}

void PDR_DrawWorld (void)
{
	static uint32_t	cov_static[PDR_MAXH * PDR_CWORDS];
	int				rows = pdr_skip != 2 ? (pdr_vh + 1) / 2 : pdr_vh;

	pdr_c_nodes = pdr_c_leafs = pdr_c_faces = pdr_c_drawn = pdr_c_occl = 0;
	pdr_c_spans = pdr_c_pixels = 0;

	// the coverage bits are read and written for every span: keep them on the fast stack
	if (PD_StackRoom (rows * PDR_CWORDS * 4 + 2048))
	{
		uint32_t	cov[rows * PDR_CWORDS];

		PDR_DrawWorldWith (cov);
	}
	else
		PDR_DrawWorldWith (cov_static);
}
