/*
 * pdr.h -- the Playdate renderer (PD_NEW_RENDERER), internal interface.
 *
 * A replacement for Quake's software refresh (r_*.c / d_*.c) written for the Playdate's
 * memory system: code and data live in slow external memory behind a small write-through
 * cache, so a store costs ~26 ns per byte and a missed 32-byte line ~1 us, while the CPU
 * (Cortex-M7, 168 MHz, single-precision FPU) is comparatively fast. The renderer therefore
 * minimises cache lines touched and bytes stored rather than instructions:
 *
 *  - The world is walked front to back. A per-row coverage bitmask (on the fast stack) records
 *    which pixels are final, so every pixel is written once, there is no edge sorting, and
 *    BSP nodes and leaves whose screen box is already covered are skipped with everything in
 *    them (pdr_world.c).
 *  - Nodes, leaves and faces are copied at map load into compact arrays in traversal order:
 *    a face's vertices sit next to its header instead of behind surfedges, edges and
 *    vertexes scattered over the map data (pdr_world.c).
 *  - There is no surface cache. Spans read the mip texture directly and light it through the
 *    colormap with a small per-face light block (lightmap resolution, 1/256 the size of a
 *    mip 0 surface) that is rebuilt only when a light style or a dynamic light changes it
 *    (pdr_light.c, pdr_span.c).
 *  - The z buffer is only written where something will test against it: rows covered by
 *    entity bounding rectangles, or every row while particles are alive (pdr_main.c).
 *  - Alias models use compact triangle and texture-coordinate arrays and a rasterizer meant
 *    for the few-pixel triangles a half-resolution view produces (pdr_alias.c).
 *
 * Entry points keep Quake's names (render.h), so the rest of the engine is unchanged.
 */
#ifndef PDR_H
#define PDR_H

#include "quakedef.h"
#include "pdprof.h"

/* view buffer: the 3D view is rendered at half the screen resolution (PD_LOWRES_3D) */
#define PDR_MAXW		WARP_WIDTH
#define PDR_MAXH		WARP_HEIGHT
#define PDR_CWORDS		((PDR_MAXW + 31) / 32)	/* coverage words per row */

#define PDR_NEAR_CLIP	0.01f		/* as Quake's NEAR_CLIP */
#define PDR_BACKFACE_EPSILON	0.01f
#define PDR_MAXCLIPVERTS	32

#define PDR_SUBDIV_SHIFT	3		/* perspective-correct every 8 pixels, as D_DrawSpans8 */
#define PDR_SUBDIV			(1 << PDR_SUBDIV_SHIFT)

#define PDR_TURB_CYCLE		128
#define PDR_TURB_AMP		(8 * 0x10000)
#define PDR_TURB_AMP2		3
#define PDR_TURB_SPEED		20
#define PDR_SIN_SIZE		(PDR_MAXW + PDR_MAXH + PDR_TURB_CYCLE * 2)

/* ------------------------------------------------------------------ compact brush data */

/* face flags */
#define PF_PLANEBACK	1		/* (only used while building) */
#define PF_SKY			2
#define PF_TURB			4
#define PF_NOLIGHT		8		/* no lightmap samples (lit by ambient only) */
#define PF_POW2			16		/* texture width and height are powers of two */

typedef struct
{
	float			normal[3];
	float			dist;
	short			minmaxs[6];
	short			children[2];	/* >= 0: node; < 0: ~leaf */
} pdr_node_t;						/* 32 bytes: one cache line */

typedef struct
{
	unsigned short	first;			/* first face (pdr numbering); faces facing the plane's front, */
	unsigned short	nfront;			/* then those facing its back */
	unsigned short	nback;
	unsigned short	pad;
} pdr_nodefaces_t;

typedef struct
{
	short			minmaxs[6];
	unsigned short	firstmark;		/* into pdr_brush_t.marks */
	unsigned short	nummarks;
} pdr_leaf_t;						/* 16 bytes */

typedef struct
{
	byte			numverts;
	byte			flags;			/* PF_* */
	unsigned short	texinfo;		/* index into the model's mtexinfo_t array */
	short			texturemins[2];
	unsigned short	extents[2];
	byte			styles[MAXLIGHTMAPS];
	int				lightofs;		/* offset into lightdata, -1 = none */
	float			plane[4];		/* normal and dist, facing the drawn side */
	float			verts[][3];
} pdr_face_t;

typedef struct pdr_brush_s
{
	msurface_t		*surfaces;		/* key: the model's surface array (shared by a .bsp's submodels) */
	model_t			*model;			/* the model it was built from */
	int				numfaces;
	byte			*facedata;		/* face records, pdr order */
	unsigned		*faceofs;		/* pdr face index -> offset in facedata */
	unsigned short	*origtopdr;		/* msurface index -> pdr face index */
	int				numnodes;
	pdr_node_t		*nodes;
	pdr_nodefaces_t	*nodefaces;
	int				numleafs;		/* including leaf 0 */
	pdr_leaf_t		*leafs;
	unsigned short	*marks;
	float			(*spheres)[4];	/* per face: bounding sphere centre and radius */
	int				facebase;		/* first slot of this model's faces in the per-face state arrays */
} pdr_brush_t;

#define PDR_FACE(b, i)	((pdr_face_t *)((b)->facedata + (b)->faceofs[i]))

extern pdr_brush_t	*pdr_world;		/* cl.worldmodel's data */
extern int			pdr_totalfaces;	/* faces of all brush models (per-face state slots) */

pdr_brush_t *PDR_BrushForModel (model_t *m);
void PDR_BuildBrushes (void);

/* ------------------------------------------------------------------ view */

typedef struct
{
	vec3_t	org;					/* eye, in the space of the geometry being drawn */
	vec3_t	right, up, fwd;			/* view axes in that space */
} pdr_basis_t;

extern pdr_basis_t	pdr_wbasis;		/* world space */
extern float	pdr_xcenter, pdr_ycenter, pdr_xscale, pdr_yscale, pdr_xscaleinv, pdr_yscaleinv;
extern float	xscaleshrink, yscaleshrink;	/* particle projection (r_part.c's names) */
extern float	pdr_umin, pdr_umax, pdr_vmin, pdr_vmax;	/* clamp limits for projected points */
extern float	pdr_hw, pdr_hh;			/* half view width / height (frustum planes in view space) */
extern int		pdr_vx, pdr_vy, pdr_vw, pdr_vh;	/* view rectangle in the view buffer */
extern byte		*pdr_vbuf;			/* view buffer (row 0) */
extern short	*pdr_zbuf;			/* z buffer (row 0, same stride) */
extern int		pdr_stride;			/* bytes per row of pdr_vbuf, shorts per row of pdr_zbuf */
extern int		pdr_skip;			/* interlaced: rows with (y & 1) == pdr_skip are not drawn; 2 = none */
extern int		r_framecount;
extern float	pdr_mipscale[3];	/* d_scalemip */
extern int		pdr_minmip;
extern float	pdr_scale_for_mip;
extern float	pdr_maxdist2;
extern qboolean	pdr_fullbright;

/* PD_PDR_AB (profiling, cmake -DPD_PDR_EXP=n): code under `if (PDR_EXPERIMENT(n))` runs on the
   frames where pd_asm_on is 0, so one device run times both variants (scripts/pd-report.py --ab) */
#ifdef PD_PDR_AB
extern int	pd_asm_on;
#define PDR_EXPERIMENT(n)	(PDR_EXP == (n) && !pd_asm_on)
#else
#define PDR_EXPERIMENT(n)	0
#endif

/* work counters (pdr_c_*): profiling and host builds only; on the device each would be a store
   to slow memory in a hot loop */
#if defined(PD_PROFILE) || defined(TARGET_SIMULATOR)
#define PDR_CNT(x)	((void)(x))
#else
#define PDR_CNT(x)	((void)0)
#endif

#define PDR_ROW_SKIPPED(y)	(((y) & 1) == pdr_skip)

/* Per-frame copies of the client clock. cl.time is a double, and this FPU only does single
   precision: a double multiply or compare is a library call of several microseconds. */
extern float	pdr_time;			/* cl.time */
extern int		pdr_time10;			/* (int)(cl.time * 10): texture animation */
extern int		pdr_turbofs;		/* water and underwater warp phase */
extern int		pdr_numdlights;		/* the dynamic lights alive this frame */
extern int		pdr_dlightidx[MAX_DLIGHTS];

/* sinf and cosf take over a microsecond each here; this is a few dozen cycles */
void PDR_SinCos (float degrees, float *s, float *c);
void PDR_AngleVectors (const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up);

/* world-space frustum planes for box culling (unnormalised; inside >= 0) */
extern float	pdr_frustum[4][4];
extern float	pdr_frustum_len[4];	/* length of each plane's normal */
extern float	pdr_frustum_abs[4][3];	/* absolute values of the normals */
extern int		pdr_frustum_idx[4][6];

/* rows that need z (union of entity rectangles), per view row: [pdr_zx0, pdr_zx1) */
extern short	pdr_zx0[PDR_MAXH], pdr_zx1[PDR_MAXH];
extern qboolean	pdr_zany;

void PDR_TransformToView (const pdr_basis_t *b, const float *in, float *out);

/* ------------------------------------------------------------------ world (pdr_world.c) */

void PDR_DrawWorld (void);
void PDR_MarkLeavesNow (void);
qboolean PDR_LeafVisible (int leafnum);
void PDR_NewMapWorld (void);
void PDR_AddBmodel (entity_t *e, pdr_brush_t *b, model_t *m);
void PDR_ClearBmodels (void);
extern int	pdr_c_nodes, pdr_c_leafs, pdr_c_faces, pdr_c_drawn, pdr_c_occl, pdr_c_spans, pdr_c_pixels;

/* ------------------------------------------------------------------ spans (pdr_span.c) */

typedef struct
{
	/* screen-space gradients */
	float	zistepu, zistepv, ziorigin;
	float	sdivzstepu, sdivzstepv, sdivzorigin;
	float	tdivzstepu, tdivzstepv, tdivzorigin;
	int		sadjust, tadjust;		/* 16.16, relative to the texture mins (as Quake's cache) */
	int		bbextents, bbextentt;	/* clamp limits of s and t */
	/* texture */
	byte	*tex;
	int		tw, th;					/* texture size at this mip */
	int		soff, toff;				/* 16.16: cache-relative -> texture coordinates */
	int		tshift;					/* 16 - log2(tw) (power-of-two textures) */
	int		smask, tmask;			/* tw - 1, (th - 1) << log2(tw) */
	/* light */
	const byte	*light;				/* light block, NULL = constant */
	int		lw, lh;					/* light block size (samples) */
	int		lshift;					/* 4 - mip: 16.16 surface texels -> 16.16 light samples */
	int		lconst;					/* light (8.16) when light == NULL */
	int		lmaxs, lmaxt;			/* 16.16 clamp limits of the light coordinates */
	byte	*colormap;
	int		kind;					/* PDR_SPAN_* */
} pdr_spanctx_t;

enum { PDR_SPAN_LIT, PDR_SPAN_TURB, PDR_SPAN_SKY };

void PDR_DrawSpan (const pdr_spanctx_t *c, int y, int x0, int x1);
void PDR_ZSpan (const pdr_spanctx_t *c, int y, int x0, int x1);
void PDR_FillSpan (int y, int x0, int x1, int color);
void PDR_InitTurb (void);
void PDR_SetupSky (void);
extern int	pdr_sintable[PDR_SIN_SIZE];
extern int	pdr_intsintable[PDR_SIN_SIZE];

/* ------------------------------------------------------------------ light (pdr_light.c) */

const byte *PDR_FaceLight (pdr_brush_t *b, int fi, const pdr_face_t *f, int *lconst);
void PDR_InitLightCache (void *buf, int size);
void PDR_FlushLightCache (void);
void PDR_NewMapLight (void);
void PDR_AnimateLights (void);
void PDR_MarkBmodelLights (pdr_brush_t *b, int headnode);
int PDR_LightPoint (const vec3_t p);
extern int	pdr_lightstyle[MAX_LIGHTSTYLES];
extern int	*pdr_dlframe;			/* per face state, indexed by pdr_brush_t.facebase + face */
extern unsigned	*pdr_dlbits;
extern int	pdr_dlightframe;
extern int	pdr_c_lbuild;
extern byte	**pdr_lightptr;			/* per face slot: its light block, NULL = none */

/* ------------------------------------------------------------------ entities */

void PDR_DrawAliasModel (entity_t *e, qboolean viewmodel);
void PDR_ResetAliasSetups (void);
qboolean PDR_AliasRect (entity_t *e, int rect[4]);
qboolean PDR_AliasCulled (entity_t *e);
void PDR_DrawSprite (entity_t *e);
qboolean PDR_SpriteRect (entity_t *e, int rect[4]);
void PDR_NewMapAlias (void);
extern int	pdr_c_aliasmodels, pdr_c_atris, pdr_c_averts;

qboolean R_BoxBeyond (const vec3_t mins, const vec3_t maxs);

/* textures */
texture_t *PDR_TextureAnimation (texture_t *base, int frame);

#endif
