/*
 * pdr_main.c -- the Playdate renderer's entry points (see pdr.h for the design)
 *
 * R_RenderView:
 *   1. view setup: axes, projection, frustum planes, interlaced row parity, PVS
 *   2. entities: static entities in the PVS join the visible list; alias models and sprites
 *      are box-tested and their screen rectangles recorded as rows that need z; brush
 *      entities are cut into the world BSP's leaves (pdr_world.c)
 *   3. the world, front to back with the coverage mask (pdr_world.c)
 *   4. alias models and sprites (z-tested against the world), the weapon, particles
 *   5. the half-resolution view goes to the display (lazy upscale) or the underwater warp
 */
#include "pdr.h"
#include "pd_stack.h"

/* ------------------------------------------------------------------ globals of the old refresh still used elsewhere */

refdef_t	r_refdef;
vec3_t		r_origin, vpn, vright, vup;
int			r_framecount = 1;
int			r_visframecount;
mleaf_t		*r_viewleaf, *r_oldviewleaf;
texture_t	*r_notexture_mip;
int			r_pixbytes = 1;
qboolean	r_cache_thrash;
int			reinit_surfcache = 1;
short		*d_pzbuffer;
float		xscaleshrink, yscaleshrink;
float		r_maxdist2;
entity_t	*currententity;
int			r_amodels_drawn;

cvar_t	r_clearcolor = {"r_clearcolor","2"};
cvar_t	r_waterwarp = {"r_waterwarp","1"};
cvar_t	r_interlace = {"r_interlace","1", true};	// port: "Interlaced" in the options menu
cvar_t	r_maxdist = {"r_maxdist","512", true};	// port: "Draw distance" in the options menu, 0 = unlimited
cvar_t	r_fullbright = {"r_fullbright","0"};
cvar_t	r_drawentities = {"r_drawentities","1"};
cvar_t	r_drawviewmodel = {"r_drawviewmodel","1"};
cvar_t	r_ambient = {"r_ambient", "0"};
cvar_t	d_mipcap = {"d_mipcap", "1", true};		// port: "Texture detail" in the options menu
cvar_t	d_mipscale = {"d_mipscale", "1"};

extern cvar_t	scr_fov;
extern cvar_t	lcd_x;

/* ------------------------------------------------------------------ renderer state */

pdr_basis_t	pdr_wbasis;
float	pdr_xcenter, pdr_ycenter, pdr_xscale, pdr_yscale, pdr_xscaleinv, pdr_yscaleinv;
float	pdr_umin, pdr_umax, pdr_vmin, pdr_vmax;
float	pdr_hw, pdr_hh;
int		pdr_vx, pdr_vy, pdr_vw, pdr_vh;
byte	*pdr_vbuf;
short	*pdr_zbuf;
int		pdr_stride;
int		pdr_skip = 2;
float	pdr_mipscale[3];
int		pdr_minmip;
float	pdr_scale_for_mip;
float	pdr_maxdist2;
qboolean	pdr_fullbright;
float	pdr_frustum[4][4];
int		pdr_frustum_idx[4][6];
short	pdr_zx0[PDR_MAXH], pdr_zx1[PDR_MAXH];
qboolean	pdr_zany;

static byte		warpbuffer[WARP_WIDTH * WARP_HEIGHT];
static qboolean	r_viewchanged;
static qboolean	pdr_dosinewarp;
static float	pixelAspect;
static qboolean	r_fov_greater_than_90;
static model_t	*interlace_world;

extern particle_t	*active_particles;
qboolean PDR_LeafVisible (int leafnum);
void PDR_MarkLeavesNow (void);

void PDR_TransformToView (const pdr_basis_t *b, const float *in, float *out)
{
	out[0] = DotProduct (in, b->right);
	out[1] = DotProduct (in, b->up);
	out[2] = DotProduct (in, b->fwd);
}

/*
==============================================================================

INIT

==============================================================================
*/

void R_InitTextures (void)
{
	int		x, y, m;
	byte	*dest;

// create a simple checkerboard texture for the default
	r_notexture_mip = Hunk_AllocName (sizeof(texture_t) + 16*16+8*8+4*4+2*2, "notexture");
	r_notexture_mip->width = r_notexture_mip->height = 16;
	r_notexture_mip->offsets[0] = sizeof(texture_t);
	r_notexture_mip->offsets[1] = r_notexture_mip->offsets[0] + 16*16;
	r_notexture_mip->offsets[2] = r_notexture_mip->offsets[1] + 8*8;
	r_notexture_mip->offsets[3] = r_notexture_mip->offsets[2] + 4*4;
	for (m=0 ; m<4 ; m++)
	{
		dest = (byte *)r_notexture_mip + r_notexture_mip->offsets[m];
		for (y=0 ; y< (16>>m) ; y++)
			for (x=0 ; x< (16>>m) ; x++)
			{
				if ((y < (8>>m)) ^ (x < (8>>m)))
					*dest++ = 0;
				else
					*dest++ = 0xff;
			}
	}
}

void R_InitParticles (void);
void R_ClearParticles (void);
void R_ReadPointFile_f (void);

void R_Init (void)
{
	Cmd_AddCommand ("pointfile", R_ReadPointFile_f);
	Cvar_RegisterVariable (&r_clearcolor);
	Cvar_RegisterVariable (&r_waterwarp);
	Cvar_RegisterVariable (&r_interlace);
	Cvar_RegisterVariable (&r_maxdist);
	Cvar_RegisterVariable (&r_fullbright);
	Cvar_RegisterVariable (&r_drawentities);
	Cvar_RegisterVariable (&r_drawviewmodel);
	Cvar_RegisterVariable (&r_ambient);
	Cvar_RegisterVariable (&d_mipcap);
	Cvar_RegisterVariable (&d_mipscale);

	r_refdef.xOrigin = 0.5f;
	r_refdef.yOrigin = 0.5f;

	PDR_InitTurb ();
	R_InitParticles ();
}

void R_NewMap (void)
{
	int		i;

// clear out efrags in case the level hasn't been reloaded
	for (i=0 ; i<cl.worldmodel->numleafs ; i++)
		cl.worldmodel->leafs[i].efrags = NULL;

	r_viewleaf = NULL;
	R_ClearParticles ();
	PDR_NewMapWorld ();
	PDR_NewMapLight ();
	PDR_NewMapAlias ();
	r_viewchanged = true;
}

/*
==============================================================================

SURFACE CACHE INTERFACE (the light block pool, see pdr_light.c)

==============================================================================
*/

#define PDR_LIGHTPOOL	(128 * 1024)

int D_SurfaceCacheForRes (int width, int height)
{
	(void)width;
	(void)height;
	return PDR_LIGHTPOOL;
}

int		sc_size;		/* (reported by the profiler) */

void D_InitCaches (void *buffer, int size)
{
	sc_size = size;
	PDR_InitLightCache (buffer, size);
}

/* Host_ClearMemory: the level is going away */
void D_FlushCaches (void)
{
	pdr_lightptr = NULL;
	pdr_world = NULL;
	PDR_FlushLightCache ();
}

void D_EnableBackBufferAccess (void)
{
	VID_LockBuffer ();
}

void D_DisableBackBufferAccess (void)
{
	VID_UnlockBuffer ();
}

/*
==============================================================================

VIEW

==============================================================================
*/

void R_SetVrect (vrect_t *pvrectin, vrect_t *pvrect, int lineadj)
{
	int		h;
	float	size;

	size = scr_viewsize.value > 100 ? 100 : scr_viewsize.value;
	if (cl.intermission)
	{
		size = 100;
		lineadj = 0;
	}
	size /= 100;

	h = pvrectin->height - lineadj;
	pvrect->width = pvrectin->width * size;
	if (pvrect->width < 96)
	{
		size = 96.0f / pvrectin->width;
		pvrect->width = 96;	// min for icons
	}
	pvrect->width &= ~7;
	pvrect->height = pvrectin->height * size;
	if (pvrect->height > pvrectin->height - lineadj)
		pvrect->height = pvrectin->height - lineadj;
	pvrect->height &= ~1;
	pvrect->x = (pvrectin->width - pvrect->width)/2;
	pvrect->y = (h - pvrect->height)/2;

	// keep the view on the 2x2 / byte grid the low-res upscale and the LCD pattern expansion rely on
	pvrect->x &= ~7;
	pvrect->y &= ~1;

	if (lcd_x.value)
	{
		pvrect->y >>= 1;
		pvrect->height >>= 1;
	}
}

void R_ViewChanged (vrect_t *pvrect, int lineadj, float aspect)
{
	float	hfov, screenAspect;

	r_viewchanged = true;
	R_SetVrect (pvrect, &r_refdef.vrect, lineadj);

	// pvrect is the full-size screen; the 3D view is rendered at half size
	r_refdef.vrect.x >>= 1;
	r_refdef.vrect.y >>= 1;
	r_refdef.vrect.width >>= 1;
	r_refdef.vrect.height >>= 1;

	r_refdef.horizontalFieldOfView = hfov = 2.0f * tanf (r_refdef.fov_x * (M_PI/360.0f));
	r_refdef.vrectright = r_refdef.vrect.x + r_refdef.vrect.width;
	r_refdef.vrectbottom = r_refdef.vrect.y + r_refdef.vrect.height;
	r_refdef.fvrectx = (float)r_refdef.vrect.x;
	r_refdef.fvrecty = (float)r_refdef.vrect.y;
	r_refdef.fvrectright = (float)r_refdef.vrectright;
	r_refdef.fvrectbottom = (float)r_refdef.vrectbottom;
	r_refdef.aliasvrect = r_refdef.vrect;
	r_refdef.aliasvrectright = r_refdef.vrectright;
	r_refdef.aliasvrectbottom = r_refdef.vrectbottom;

	pixelAspect = aspect;
	screenAspect = r_refdef.vrect.width * pixelAspect / r_refdef.vrect.height;
	(void)screenAspect;

	pdr_vx = r_refdef.vrect.x;
	pdr_vy = r_refdef.vrect.y;
	pdr_vw = r_refdef.vrect.width;
	pdr_vh = r_refdef.vrect.height;
	pdr_xcenter = ((float)pdr_vw * 0.5f) + pdr_vx - 0.5f;
	pdr_ycenter = ((float)pdr_vh * 0.5f) + pdr_vy - 0.5f;
	pdr_xscale = pdr_vw / hfov;
	pdr_xscaleinv = 1.0f / pdr_xscale;
	pdr_yscale = pdr_xscale * pixelAspect;
	pdr_yscaleinv = 1.0f / pdr_yscale;
	pdr_umin = (float)pdr_vx - 0.5f;
	pdr_umax = (float)(pdr_vx + pdr_vw) - 0.5f;
	pdr_vmin = (float)pdr_vy - 0.5f;
	pdr_vmax = (float)(pdr_vy + pdr_vh) - 0.5f;
	pdr_hw = pdr_vw * 0.5f;
	pdr_hh = pdr_vh * 0.5f;
	xscaleshrink = (pdr_vw - 6) / hfov;
	yscaleshrink = xscaleshrink * pixelAspect;
	pdr_scale_for_mip = pdr_xscale > pdr_yscale ? pdr_xscale : pdr_yscale;
	r_fov_greater_than_90 = scr_fov.value > 90.0f;
}

static void PDR_SetupFrustum (void)
{
	int		i, j;
	float	n[4][3];

	for (j=0 ; j<3 ; j++)
	{
		n[0][j] = pdr_xscale * vright[j] + pdr_hw * vpn[j];
		n[1][j] = -pdr_xscale * vright[j] + pdr_hw * vpn[j];
		n[2][j] = -pdr_yscale * vup[j] + pdr_hh * vpn[j];
		n[3][j] = pdr_yscale * vup[j] + pdr_hh * vpn[j];
	}
	for (i=0 ; i<4 ; i++)
	{
		for (j=0 ; j<3 ; j++)
		{
			pdr_frustum[i][j] = n[i][j];
			// [0..2]: the box corner farthest along the normal, [3..5]: the nearest
			pdr_frustum_idx[i][j] = n[i][j] >= 0 ? 3 + j : j;
			pdr_frustum_idx[i][3+j] = n[i][j] >= 0 ? j : 3 + j;
		}
		pdr_frustum[i][3] = -DotProduct (n[i], r_origin);
	}
}

static void PDR_SetupFrame (void)
{
	int		i;
	static const float	basemip[3] = {1.0f, 0.5f*0.8f, 0.25f*0.8f};

	r_refdef.ambientlight = r_ambient.value;
	if (r_refdef.ambientlight < 0)
		r_refdef.ambientlight = 0;
	if (cl.maxclients > 1)
	{
		Cvar_Set ("r_fullbright", "0");
		Cvar_Set ("r_ambient", "0");
		r_refdef.ambientlight = 0;
	}
	pdr_fullbright = r_fullbright.value != 0;

	PDR_AnimateLights ();
	r_framecount++;

	pdr_maxdist2 = r_maxdist2 = r_maxdist.value > 0 ? r_maxdist.value * r_maxdist.value : 0;

	VectorCopy (r_refdef.vieworg, r_origin);
	AngleVectors (r_refdef.viewangles, vpn, vright, vup);
	VectorCopy (r_origin, pdr_wbasis.org);
	VectorCopy (vright, pdr_wbasis.right);
	VectorCopy (vup, pdr_wbasis.up);
	VectorCopy (vpn, pdr_wbasis.fwd);

	r_oldviewleaf = r_viewleaf;
	r_viewleaf = Mod_PointInLeaf (r_origin, cl.worldmodel);
	pdr_dosinewarp = r_waterwarp.value && (r_viewleaf->contents <= CONTENTS_WATER);

// Interlaced: draw only every other row of the view, alternating each frame. The other
// rows keep the previous frame. Draw every row when the view was resized or the map
// changed, since the kept rows would not match.
	{
		qboolean	full = !r_interlace.value || r_viewchanged || lcd_x.value ||
				cl.worldmodel != interlace_world;

		interlace_world = cl.worldmodel;
		pdr_skip = full ? 2 : (r_framecount & 1);
	}

	if (r_viewchanged || lcd_x.value)
	{
		vrect_t	vrect;

		vrect.x = 0;
		vrect.y = 0;
		vrect.width = vid.width;
		vrect.height = vid.height;
		R_ViewChanged (&vrect, sb_lines, vid.aspect);
		r_viewchanged = false;
	}

	PDR_SetupFrustum ();

	pdr_minmip = d_mipcap.value;
	if (pdr_minmip > 3)
		pdr_minmip = 3;
	else if (pdr_minmip < 0)
		pdr_minmip = 0;
	for (i=0 ; i<3 ; i++)
		pdr_mipscale[i] = basemip[i] * d_mipscale.value;

	pdr_vbuf = warpbuffer;
	pdr_zbuf = d_pzbuffer;
	pdr_stride = WARP_WIDTH;
	PDR_SetupSky ();
}

/*
==============================================================================

ENTITIES

==============================================================================
*/

#define MAX_DRAWENTS	256

static entity_t	*pdr_aliasents[MAX_DRAWENTS];
static int		pdr_numaliasents;
static entity_t	*pdr_spriteents[64];
static entity_t	*pdr_bents_pending[64];
static int		pdr_numbents_pending;
static int		pdr_numspriteents;

static void PDR_AddZRect (const int rect[4])
{
	int		x0 = rect[0], y0 = rect[1], x1 = rect[2], y1 = rect[3], y;

	if (x0 < pdr_vx)
		x0 = pdr_vx;
	if (y0 < pdr_vy)
		y0 = pdr_vy;
	if (x1 > pdr_vx + pdr_vw)
		x1 = pdr_vx + pdr_vw;
	if (y1 > pdr_vy + pdr_vh)
		y1 = pdr_vy + pdr_vh;
	if (x0 >= x1 || y0 >= y1)
		return;
	pdr_zany = true;
	for (y=y0 ; y<y1 ; y++)
	{
		int	r = y - pdr_vy;

		if (x0 < pdr_zx0[r])
			pdr_zx0[r] = x0;
		if (x1 > pdr_zx1[r])
			pdr_zx1[r] = x1;
	}
}

/* an alias model's or sprite's bounds at its origin, grown to cover any rotation */
static qboolean PDR_EntityBeyond (entity_t *ent)
{
	vec3_t	mins, maxs;
	float	r = 0;
	int		j;

	if (pdr_maxdist2 <= 0)
		return false;
	for (j=0 ; j<3 ; j++)
	{
		if (-ent->model->mins[j] > r)
			r = -ent->model->mins[j];
		if (ent->model->maxs[j] > r)
			r = ent->model->maxs[j];
	}
	r = r * 1.8f + 32;
	for (j=0 ; j<3 ; j++)
	{
		mins[j] = ent->origin[j] - r;
		maxs[j] = ent->origin[j] + r;
	}
	return R_BoxBeyond (mins, maxs);
}

qboolean R_BoxBeyond (const vec3_t mins, const vec3_t maxs)
{
	float	d, dist2 = 0;
	int		j;

	if (pdr_maxdist2 <= 0)
		return false;
	for (j=0 ; j<3 ; j++)
	{
		if (r_origin[j] < mins[j])
			d = mins[j] - r_origin[j];
		else if (r_origin[j] > maxs[j])
			d = r_origin[j] - maxs[j];
		else
			continue;
		dist2 += d * d;
	}
	return dist2 > pdr_maxdist2;
}

/* frustum test of a float box */
static qboolean PDR_BoxOutside (const float *mins, const float *maxs)
{
	int		i, j;

	for (i=0 ; i<4 ; i++)
	{
		float	d = pdr_frustum[i][3];

		for (j=0 ; j<3 ; j++)
			d += pdr_frustum[i][j] * (pdr_frustum[i][j] >= 0 ? maxs[j] : mins[j]);
		if (d <= 0)
			return true;
	}
	return false;
}

static qboolean PDR_ViewModelDrawn (void)
{
	if (!r_drawviewmodel.value || r_fov_greater_than_90)
		return false;
	if (cl.items & IT_INVISIBILITY)
		return false;
	if (cl.stats[STAT_HEALTH] <= 0)
		return false;
	if (!cl.viewent.model)
		return false;
	return true;
}

/* static entities whose leaves are in the PVS join the visible list (R_StoreEfrags) */
static void PDR_StoreStatics (void)
{
	int		i;

	for (i=0 ; i<cl.num_statics ; i++)
	{
		entity_t	*e = &cl_static_entities[i];
		efrag_t		*ef;

		if (!e->model || e->visframe == r_framecount)
			continue;
		for (ef = e->efrag ; ef ; ef = ef->entnext)
		{
			if (PDR_LeafVisible (ef->leaf - cl.worldmodel->leafs))
			{
				if (cl_numvisedicts < MAX_VISEDICTS)
				{
					cl_visedicts[cl_numvisedicts++] = e;
					e->visframe = r_framecount;
				}
				break;
			}
		}
	}
}

static void PDR_SetupEntities (void)
{
	entity_t	*bents[64];
	float		bdist[64];
	int			nbents = 0, i, j, rows = pdr_vh;
	int			rect[4];

	for (i=0 ; i<rows ; i++)
	{
		pdr_zx0[i] = pdr_vx + pdr_vw;
		pdr_zx1[i] = pdr_vx;
	}
	pdr_zany = false;
	pdr_numaliasents = pdr_numspriteents = 0;
	PDR_ClearBmodels ();

	if (r_drawentities.value)
	{
		PDR_StoreStatics ();
		for (i=0 ; i<cl_numvisedicts ; i++)
		{
			entity_t	*e = cl_visedicts[i];

			switch (e->model->type)
			{
			case mod_brush:
				{
					float	mins[3], maxs[3];
					vec3_t	d;

					for (j=0 ; j<3 ; j++)
					{
						if (e->angles[0] || e->angles[1] || e->angles[2])
						{
							mins[j] = e->origin[j] - e->model->radius;
							maxs[j] = e->origin[j] + e->model->radius;
						}
						else
						{
							mins[j] = e->origin[j] + e->model->mins[j];
							maxs[j] = e->origin[j] + e->model->maxs[j];
						}
					}
					if (R_BoxBeyond (mins, maxs) || PDR_BoxOutside (mins, maxs))
						break;
					if (nbents == 64)
						break;
					for (j=0 ; j<3 ; j++)
						d[j] = (mins[j] + maxs[j]) * 0.5f - r_origin[j];
					bdist[nbents] = DotProduct (d, d);
					bents[nbents++] = e;
				}
				break;

			case mod_alias:
				if (e == &cl_entities[cl.viewentity])
					break;	// don't draw the player
				if (PDR_EntityBeyond (e))
					break;
				if (pdr_numaliasents < MAX_DRAWENTS && PDR_AliasRect (e, rect))
				{
					pdr_aliasents[pdr_numaliasents++] = e;
					PDR_AddZRect (rect);
				}
				break;

			case mod_sprite:
				if (e == &cl_entities[cl.viewentity])
					break;
				if (PDR_EntityBeyond (e))
					break;
				if (pdr_numspriteents < 64 && PDR_SpriteRect (e, rect))
				{
					pdr_spriteents[pdr_numspriteents++] = e;
					PDR_AddZRect (rect);
				}
				break;

			default:
				break;
			}
		}
	}

	// brush entities, nearest first
	for (i=1 ; i<nbents ; i++)
	{
		for (j=i ; j>0 && bdist[j-1] > bdist[j] ; j--)
		{
			float		td = bdist[j];
			entity_t	*te = bents[j];

			bdist[j] = bdist[j-1];
			bents[j] = bents[j-1];
			bdist[j-1] = td;
			bents[j-1] = te;
		}
	}
	pdr_numbents_pending = nbents;
	memcpy (pdr_bents_pending, bents, nbents * sizeof(entity_t *));

	if (PDR_ViewModelDrawn () && PDR_AliasRect (&cl.viewent, rect))
		PDR_AddZRect (rect);

	if (active_particles)
	{
		rect[0] = pdr_vx;
		rect[1] = pdr_vy;
		rect[2] = pdr_vx + pdr_vw;
		rect[3] = pdr_vy + pdr_vh;
		PDR_AddZRect (rect);
	}
}

static void PDR_SetupBmodels (void)
{
	int		i;

	for (i=0 ; i<pdr_numbents_pending ; i++)
	{
		entity_t	*e = pdr_bents_pending[i];

		PDR_AddBmodel (e, PDR_BrushForModel (e->model), e->model);
	}
}

static void PDR_DrawEntities (void)
{
	int		i;

	for (i=0 ; i<pdr_numaliasents ; i++)
	{
		currententity = pdr_aliasents[i];
		PDR_DrawAliasModel (pdr_aliasents[i], false);
	}
	for (i=0 ; i<pdr_numspriteents ; i++)
	{
		currententity = pdr_spriteents[i];
		PDR_DrawSprite (pdr_spriteents[i]);
	}
}

/* the underwater sine warp, with a slight compression to keep the edges from wrapping */
static void PDR_WarpScreen (void)
{
	int		w, h, src_w, src_h, u, v;
	byte	*dest;
	int		*turb, *col;
	byte	**row;
	static byte	*rowptr[PD_RENDER_HEIGHT + (PDR_TURB_AMP2*2)];
	static int	column[PD_RENDER_WIDTH + (PDR_TURB_AMP2*2)];
	float	wratio, hratio;

	w = r_refdef.vrect.width;
	h = r_refdef.vrect.height;
	src_w = scr_vrect.width;
	src_h = scr_vrect.height;
	wratio = w / (float)src_w;
	hratio = h / (float)src_h;

	for (v=0 ; v<src_h+PDR_TURB_AMP2*2 ; v++)
		rowptr[v] = pdr_vbuf + (r_refdef.vrect.y * pdr_stride) +
				(pdr_stride * (int)((float)v * hratio * h / (h + PDR_TURB_AMP2 * 2)));
	for (u=0 ; u<src_w+PDR_TURB_AMP2*2 ; u++)
		column[u] = r_refdef.vrect.x + (int)((float)u * wratio * w / (w + PDR_TURB_AMP2 * 2));

	turb = pdr_intsintable + ((int)(cl.time*PDR_TURB_SPEED)&(PDR_TURB_CYCLE-1));
	dest = vid.buffer + scr_vrect.y * vid.rowbytes + scr_vrect.x;
	for (v=0 ; v<src_h ; v++, dest += vid.rowbytes)
	{
		col = &column[turb[v]];
		row = &rowptr[v];
		for (u=0 ; u<src_w ; u+=4)
		{
			dest[u+0] = row[turb[u+0]][col[u+0]];
			dest[u+1] = row[turb[u+1]][col[u+1]];
			dest[u+2] = row[turb[u+2]][col[u+2]];
			dest[u+3] = row[turb[u+3]][col[u+3]];
		}
	}
}

/*
==============================================================================

R_RenderView

==============================================================================
*/

void R_DrawParticles (void);

void R_RenderView (void)
{
	if (!cl_entities[0].model || !cl.worldmodel)
		Sys_Error ("R_RenderView: NULL worldmodel");
	if (!pdr_world || pdr_world->model != cl.worldmodel)
		return;		/* (not built yet) */

	pdr_c_lbuild = pdr_c_aliasmodels = pdr_c_atris = pdr_c_averts = 0;

	PROF_BEGIN(P_SETUP);
	PDR_SetupFrame ();
	PDR_MarkLeavesNow ();
	PDR_SetupEntities ();
	PROF_END(P_SETUP);

	PROF_BEGIN(P_BENT);
	PDR_SetupBmodels ();
	PROF_END(P_BENT);

	S_ExtraUpdate ();	// don't let sound get messed up if going slow

	PROF_BEGIN(P_WORLD);
	PDR_DrawWorld ();
	PROF_END(P_WORLD);

	S_ExtraUpdate ();

	PROF_BEGIN(P_ENT);
	PDR_DrawEntities ();
	PROF_END(P_ENT);

	PROF_BEGIN(P_VIEW);
	if (PDR_ViewModelDrawn ())
	{
		currententity = &cl.viewent;
		PDR_DrawAliasModel (&cl.viewent, true);
	}
	PROF_END(P_VIEW);

	PROF_BEGIN(P_PART);
	R_DrawParticles ();
	PROF_END(P_PART);

	PROF_BEGIN(P_UPSCALE);
	if (pdr_dosinewarp)
		PDR_WarpScreen ();
	else
		D_UpscaleScreen ();
	PROF_END(P_UPSCALE);

	V_SetContentsColor (r_viewleaf->contents);
	r_amodels_drawn = pdr_c_aliasmodels;

	PROF_CNT(C_NODES, pdr_c_nodes);
	PROF_CNT(C_LEAVES, pdr_c_leafs);
	PROF_CNT(C_FACES, pdr_c_faces);
	PROF_CNT(C_DRAWN, pdr_c_drawn);
	PROF_CNT(C_SPANS, pdr_c_spans);
	PROF_CNT(C_PIXELS, pdr_c_pixels);
	PROF_CNT(C_CBUILD, pdr_c_lbuild);
	PROF_CNT(C_MARKS, pdr_c_occl);
	PROF_CNT(C_AMODELS, pdr_c_aliasmodels);
	PROF_CNT(C_ATRIS, pdr_c_atris);
	PROF_CNT(C_AVERTS, pdr_c_averts);
}
