/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// d_edge.c

#include "quakedef.h"
#include "d_local.h"
#include "pdprof.h"
#include "pd_asm.h"
#include "pd_stack.h"

static int	miplevel;

float		scale_for_mip;
extern int	screenwidth;
int			ubasestep, errorterm, erroradjustup, erroradjustdown;
int			vstartscan;

// FIXME: should go away
extern void			R_RotateBmodel (void);
extern void			R_TransformFrustum (void);

vec3_t		transformed_modelorg;

#ifdef PD_FAST_EDGES
extern espan_t	**r_spanheads;
#endif

/*
==============
D_DrawPoly

==============
*/
void D_DrawPoly (void)
{
// this driver takes spans, not polygons
}


/*
=============
D_MipLevelForScale
=============
*/
int D_MipLevelForScale (float scale)
{
	int		lmiplevel;

	if (scale >= d_scalemip[0] )
		lmiplevel = 0;
	else if (scale >= d_scalemip[1] )
		lmiplevel = 1;
	else if (scale >= d_scalemip[2] )
		lmiplevel = 2;
	else
		lmiplevel = 3;

	if (lmiplevel < d_minmip)
		lmiplevel = d_minmip;

	return lmiplevel;
}


/*
==============
D_DrawSolidSurface
==============
*/

// FIXME: clean this up

void D_DrawSolidSurface (surf_t *surf, int color)
{
	espan_t	*span;
	byte	*pdest;
	int		u, u2, pix;
	
	pix = (color<<24) | (color<<16) | (color<<8) | color;
	for (span=surf->spans ; span ; span=span->pnext)
	{
		pdest = (byte *)d_viewbuffer + screenwidth*span->v;
		u = span->u;
		u2 = span->u + span->count - 1;
		((byte *)pdest)[u] = pix;

		if (u2 - u < 8)
		{
			for (u++ ; u <= u2 ; u++)
				((byte *)pdest)[u] = pix;
		}
		else
		{
			for (u++ ; u & 3 ; u++)
				((byte *)pdest)[u] = pix;

			u2 -= 4;
			for ( ; u <= u2 ; u+=4)
				*(int *)((byte *)pdest + u) = pix;
			u2 += 4;
			for ( ; u <= u2 ; u++)
				((byte *)pdest)[u] = pix;
		}
	}
}


/*
==============
D_CalcGradients
==============
*/
void D_CalcGradientsTo (msurface_t *pface, int mip, d_spanparms_t *p)
{
	float		mipscale;
	vec3_t		p_temp1;
	vec3_t		p_saxis, p_taxis;
	float		t;

	mipscale = 1.0F / (float)(1 << mip);

	TransformVector (pface->texinfo->vecs[0], p_saxis);
	TransformVector (pface->texinfo->vecs[1], p_taxis);

	t = xscaleinv * mipscale;
	p->sdivzstepu = p_saxis[0] * t;
	p->tdivzstepu = p_taxis[0] * t;

	t = yscaleinv * mipscale;
	p->sdivzstepv = -p_saxis[1] * t;
	p->tdivzstepv = -p_taxis[1] * t;

	p->sdivzorigin = p_saxis[2] * mipscale - xcenter * p->sdivzstepu -
			ycenter * p->sdivzstepv;
	p->tdivzorigin = p_taxis[2] * mipscale - xcenter * p->tdivzstepu -
			ycenter * p->tdivzstepv;

	VectorScale (transformed_modelorg, mipscale, p_temp1);

	t = 0x10000*mipscale;
	p->sadjust = ((fixed16_t)(DotProduct (p_temp1, p_saxis) * 0x10000 + 0.5f)) -
			((pface->texturemins[0] << 16) >> mip)
			+ pface->texinfo->vecs[0][3]*t;
	p->tadjust = ((fixed16_t)(DotProduct (p_temp1, p_taxis) * 0x10000 + 0.5f)) -
			((pface->texturemins[1] << 16) >> mip)
			+ pface->texinfo->vecs[1][3]*t;

//
// -1 (-epsilon) so we never wander off the edge of the texture
//
	p->bbextents = ((pface->extents[0] << 16) >> mip) - 1;
	p->bbextentt = ((pface->extents[1] << 16) >> mip) - 1;
}

void D_CalcGradients (msurface_t *pface)
{
	d_spanparms_t	p;

	D_CalcGradientsTo (pface, miplevel, &p);
	d_sdivzstepu = p.sdivzstepu;
	d_tdivzstepu = p.tdivzstepu;
	d_sdivzstepv = p.sdivzstepv;
	d_tdivzstepv = p.tdivzstepv;
	d_sdivzorigin = p.sdivzorigin;
	d_tdivzorigin = p.tdivzorigin;
	sadjust = p.sadjust;
	tadjust = p.tadjust;
	bbextents = p.bbextents;
	bbextentt = p.bbextentt;
}


/*
==============
D_DrawSurfaces
==============
*/
void D_DrawSurfaces (void)
{
	surf_t			*s;
	msurface_t		*pface;
	surfcache_t		*pcurrentcache;
	vec3_t			world_transformed_modelorg;
	vec3_t			local_modelorg;
	PROF_STK(K_DSURF);

	PROF_BEGINF(P_DSURF);
	currententity = &cl_entities[0];
	TransformVector (modelorg, transformed_modelorg);
	VectorCopy (transformed_modelorg, world_transformed_modelorg);

// TODO: could preset a lot of this at mode set time
	if (r_drawflat.value)
	{
		for (s = &surfaces[1] ; s<surface_p ; s++)
		{
#ifdef PD_FAST_EDGES
			if (r_spanheads)
				s->spans = r_spanheads[s - surfaces];
#endif
			if (!s->spans)
				continue;

			d_zistepu = s->d_zistepu;
			d_zistepv = s->d_zistepv;
			d_ziorigin = s->d_ziorigin;

			D_DrawSolidSurface (s, (int)s->data & 0xFF);
			D_DrawZSpans (s->spans);
		}
	}
	else
	{
		for (s = &surfaces[1] ; s<surface_p ; s++)
		{
#ifdef PD_FAST_EDGES
		// the scan keeps the span list heads on its stack (see FE_EMIT in r_edge.c)
			if (r_spanheads)
			{
				espan_t		*head = r_spanheads[s - surfaces];

				if (!head)
					continue;
				s->spans = head;
			}
#endif
			if (!s->spans)
				continue;

			r_drawnpolycount++;

			d_zistepu = s->d_zistepu;
			d_zistepv = s->d_zistepv;
			d_ziorigin = s->d_ziorigin;

			if (s->flags & SURF_DRAWSKY)
			{
				if (!r_skymade)
				{
					R_MakeSky ();
				}

				PROF_BEGINF(P_OTHER);
				D_DrawSkyScans8 (s->spans);
				PROF_ENDF(P_OTHER);
				D_DrawZSpans (s->spans);
			}
			else if (s->flags & SURF_DRAWBACKGROUND)
			{
			// set up a gradient for the background surface that places it
			// effectively at infinity distance from the viewpoint
				d_zistepu = 0;
				d_zistepv = 0;
				d_ziorigin = -0.9F;

				PROF_BEGINF(P_OTHER);
				D_DrawSolidSurface (s, (int)r_clearcolor.value & 0xFF);
				PROF_ENDF(P_OTHER);
				D_DrawZSpans (s->spans);
			}
			else if (s->flags & SURF_DRAWTURB)
			{
				pface = s->data;
				miplevel = 0;
				cacheblock = (pixel_t *)
						((byte *)pface->texinfo->texture +
						pface->texinfo->texture->offsets[0]);
				cachewidth = 64;

				if (s->insubmodel)
				{
				// FIXME: we don't want to do all this for every polygon!
				// TODO: store once at start of frame
					currententity = s->entity;	//FIXME: make this passed in to
												// R_RotateBmodel ()
					VectorSubtract (r_origin, currententity->origin,
							local_modelorg);
					TransformVector (local_modelorg, transformed_modelorg);

					R_RotateBmodel ();	// FIXME: don't mess with the frustum,
										// make entity passed in
				}

				PROF_BEGINF(P_GRAD);
				D_CalcGradients (pface);
				PROF_ENDF(P_GRAD);
				PROF_BEGINF(P_OTHER);
				Turbulent8 (s->spans);
				PROF_ENDF(P_OTHER);
				D_DrawZSpans (s->spans);

				if (s->insubmodel)
				{
				//
				// restore the old drawing state
				// FIXME: we don't want to do this every time!
				// TODO: speed up
				//
					currententity = &cl_entities[0];
					VectorCopy (world_transformed_modelorg,
								transformed_modelorg);
					VectorCopy (base_vpn, vpn);
					VectorCopy (base_vup, vup);
					VectorCopy (base_vright, vright);
					VectorCopy (base_modelorg, modelorg);
					R_TransformFrustum ();
				}
			}
			else
			{
				if (s->insubmodel)
				{
				// FIXME: we don't want to do all this for every polygon!
				// TODO: store once at start of frame
					currententity = s->entity;	//FIXME: make this passed in to
												// R_RotateBmodel ()
					VectorSubtract (r_origin, currententity->origin, local_modelorg);
					TransformVector (local_modelorg, transformed_modelorg);

					R_RotateBmodel ();	// FIXME: don't mess with the frustum,
										// make entity passed in
				}

				pface = s->data;
#ifdef PD_USE_ASM
				if (PD_ASM_ACTIVE() && PD_STACK_ACTIVE() && d_drawspans == D_DrawSpans8)
				{
				// what the span drawers need goes straight into a block on the stack
				// instead of a dozen globals that D_DrawSpans8 would read back
					d_spanparms_t	p;
					int				mip = D_MipLevelForScale (s->nearzi * scale_for_mip
									* pface->texinfo->mipadjust);

					PROF_BEGINF(P_CACHE);
					pcurrentcache = D_CacheSurface (pface, mip);
					PROF_ENDF(P_CACHE);

					PROF_BEGINF(P_GRAD);
					D_CalcGradientsTo (pface, mip, &p);
					PROF_ENDF(P_GRAD);
					p.ziorigin = s->d_ziorigin;
					p.zistepv = s->d_zistepv;
					p.zistepu = s->d_zistepu;
					p.cacheblock = (pixel_t *)pcurrentcache->data;
					p.cachewidth = pcurrentcache->width;
					p.viewbuffer = (pixel_t *)d_viewbuffer;
					p.screenwidth = screenwidth;

					PROF_BEGINF(P_SPANS);
					D_DrawSpans8_ARM (s->spans, &p);
					PROF_ENDF(P_SPANS);
					PROF_SPANSF (s->spans);

					D_DrawZSpansP (s->spans, p.ziorigin, p.zistepu, p.zistepv);
#ifdef PD_ASM_CHECK
				// compare with the C drawers fed the way the other branch feeds them
					miplevel = mip;
					cacheblock = p.cacheblock;
					cachewidth = p.cachewidth;
					D_CalcGradients (pface);
					D_AsmCheckSpans ("spans-p", s->spans, D_DrawSpans8_C, (byte *)d_viewbuffer, screenwidth, 1);
					D_AsmCheckSpans ("zspans-p", s->spans, D_DrawZSpans, (byte *)d_pzbuffer, d_zwidth * 2, 2);
#endif
				}
				else
#endif
				{
					miplevel = D_MipLevelForScale (s->nearzi * scale_for_mip
					* pface->texinfo->mipadjust);

				// FIXME: make this passed in to D_CacheSurface
					PROF_BEGINF(P_CACHE);
					pcurrentcache = D_CacheSurface (pface, miplevel);
					PROF_ENDF(P_CACHE);

					cacheblock = (pixel_t *)pcurrentcache->data;
					cachewidth = pcurrentcache->width;

					PROF_BEGINF(P_GRAD);
					D_CalcGradients (pface);
					PROF_ENDF(P_GRAD);

					PROF_BEGINF(P_SPANS);
					(*d_drawspans) (s->spans);
					PROF_ENDF(P_SPANS);
					PROF_SPANSF (s->spans);

					D_DrawZSpans (s->spans);
				}

				if (s->insubmodel)
				{
				//
				// restore the old drawing state
				// FIXME: we don't want to do this every time!
				// TODO: speed up
				//
					currententity = &cl_entities[0];
					VectorCopy (world_transformed_modelorg,
								transformed_modelorg);
					VectorCopy (base_vpn, vpn);
					VectorCopy (base_vup, vup);
					VectorCopy (base_vright, vright);
					VectorCopy (base_modelorg, modelorg);
					R_TransformFrustum ();
				}
			}
		}
	}
	PROF_ENDF(P_DSURF);
}

