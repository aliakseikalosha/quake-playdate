/*
 * pdr_lowres.c -- hands the Playdate renderer's half-resolution view to the display
 * (port/boards/playdate/display.c), as d_scan.c did for the old refresh.
 */
#include "pdr.h"

/*
==============================================================================

LOW-RESOLUTION VIEW HAND-OVER (as d_scan.c in the old refresh)

The 3D view is rendered at half resolution into warpbuffer. Expanding it into vid.buffer
costs 96 KB of writes (and the display pass then reads them back), so instead each row pair
of the view is marked pending and the display layer dithers pending pairs straight from the
half-resolution buffer. Only row pairs that something draws over (console, menu, HUD text;
see DRAW_TOUCH in draw.h) are expanded into vid.buffer, so the overlay has its background.
==============================================================================
*/

int			qembd_lowres_rect[4];	/* view rectangle in full-resolution screen pixels */
int			qembd_lowres_active;
const byte	*qembd_lowres_src;
int			qembd_lowres_stride;
byte		qembd_lowres_pending[PD_RENDER_HEIGHT / 2];	/* 1 = drawn this frame, 2 = kept (interlaced) */
byte		qembd_lowres_shown[PD_RENDER_HEIGHT / 2];

static void D_UpscaleRow (int p)
{
	int		u;
	int		w = qembd_lowres_rect[2] >> 1;
	const byte	*src = qembd_lowres_src + p * qembd_lowres_stride + (qembd_lowres_rect[0] >> 1);
	byte	*dest = vid.buffer + (p * 2) * vid.rowbytes + qembd_lowres_rect[0];
	unsigned int	*d0 = (unsigned int *)dest;
	unsigned int	*d1 = (unsigned int *)(dest + vid.rowbytes);

	for (u=0 ; u+4<=w ; u+=4)
	{
		unsigned int	s, lo, hi;

		memcpy (&s, src + u, sizeof(s));
		lo = s & 0xffff;
		hi = s >> 16;
		lo = ((lo | (lo << 8)) & 0x00ff00ff) * 0x101;
		hi = ((hi | (hi << 8)) & 0x00ff00ff) * 0x101;
		d0[0] = d1[0] = lo;
		d0[1] = d1[1] = hi;
		d0 += 2;
		d1 += 2;
	}
	for ( ; u<w ; u++)
	{
		unsigned short	*e0 = (unsigned short *)d0;
		unsigned short	*e1 = (unsigned short *)d1;

		e0[0] = e1[0] = (unsigned short)(src[u] * 0x0101);
		d0 = (unsigned int *)(e0 + 1);
		d1 = (unsigned int *)(e1 + 1);
	}
}

void D_LowresTouch (int y0, int y1)
{
	int		p, p0, p1;

	if (y0 < qembd_lowres_rect[1])
		y0 = qembd_lowres_rect[1];
	if (y1 > qembd_lowres_rect[1] + qembd_lowres_rect[3])
		y1 = qembd_lowres_rect[1] + qembd_lowres_rect[3];
	if (y0 >= y1)
		return;
	p0 = y0 >> 1;
	p1 = (y1 + 1) >> 1;
	for (p=p0 ; p<p1 ; p++)
	{
		if (qembd_lowres_pending[p])
		{
			D_UpscaleRow (p);
			qembd_lowres_pending[p] = 0;
		}
	}
}

void D_LowresEndFrame (void)
{
	qembd_lowres_active = 0;
}

void D_UpscaleScreen (void)
{
	int		p;

	qembd_lowres_rect[0] = r_refdef.vrect.x * 2;
	qembd_lowres_rect[1] = r_refdef.vrect.y * 2;
	qembd_lowres_rect[2] = r_refdef.vrect.width * 2;
	qembd_lowres_rect[3] = r_refdef.vrect.height * 2;
	qembd_lowres_src = pdr_vbuf;
	qembd_lowres_stride = pdr_stride;
	memset (qembd_lowres_pending, 0, sizeof(qembd_lowres_pending));
	for (p=r_refdef.vrect.y ; p<r_refdef.vrect.y + r_refdef.vrect.height ; p++)
	{
		if (pdr_skip != 2 && PDR_ROW_SKIPPED(p))
			qembd_lowres_pending[p] = 2;
		else
		{
			qembd_lowres_pending[p] = 1;
			qembd_lowres_shown[p] = 0;
		}
	}
	qembd_lowres_active = 1;
}

