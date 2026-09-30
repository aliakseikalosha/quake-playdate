/*
 * D_DrawSpans8_ARM: D_DrawSpans8 (d_scan.c) for the Playdate's Cortex-M7, in Thumb-2.
 *
 *	void D_DrawSpans8_ARM (espan_t *pspan, const d_spanparms_t *p);
 *
 * Produces exactly the pixels of the C version as the device compiler builds it: the same
 * float operations in the same order (the start-of-span sums are two fused multiply-adds each,
 * as GCC contracts them; each whole 16-pixel segment adds the x16 steps; the last partial
 * segment is one fused multiply-add), the same truncating conversions, the same clamps and
 * integer steps. What is different:
 *   - everything lives in registers (the C spills most of its state to the stack);
 *   - the 1/z division of the next segment is started before the 16 pixels of the current one
 *     are drawn, so the 14-cycle divider runs while the pixels are fetched and stored;
 *   - a texel address is one SMLATB, pbase + (t >> 16) * cachewidth, plus (s >> 16);
 *   - the last partial segment jumps into an unrolled run instead of a switch.
 *
 * espan_t: u, v, count, pnext at 0, 4, 8, 12. d_spanparms_t: see d_scan.c.
 */
	.syntax unified
	.cpu cortex-m7
	.fpu fpv5-sp-d16
	.thumb

@ stack frame (the stack is fast internal RAM)
#define F_SADJUST	0
#define F_TADJUST	4
#define F_BBEXTS	8
#define F_BBEXTT	12
#define F_VIEW		16
#define F_SCRW		20
#define F_PNEXT		24
#define F_SNEXT		28
#define F_TNEXT		32
#define F_SIZE		40

@ registers in the pixel loops
pdest	.req r0
sfix	.req r1
tfix	.req r2
sstep	.req r3
tstep	.req r4
pbase	.req r5
cw	.req r6
nseg	.req r9
pspan	.req r10
nrem	.req r11

@ one texel: \d = pbase[(s >> 16) + (t >> 16) * cachewidth], then step s and t
.macro TEXEL d
	smlatb	\d, tfix, cw, pbase
	add	\d, \d, sfix, asr #16
	add	sfix, sfix, sstep
	add	tfix, tfix, tstep
	ldrb	\d, [\d]
.endm

@ one pixel of the last partial segment, as a fixed 24-byte block (6 wide instructions) so the
@ entry point can be computed; \k = distance from the end of the span
.macro REMPIX k
	smlatb	r7, tfix, cw, pbase
	add.w	r7, r7, sfix, asr #16
	ldrb.w	r7, [r7, #0]
	add.w	sfix, sfix, sstep
	add.w	tfix, tfix, tstep
	strb.w	r7, [pdest, #-\k]
.endm

@ \d = max(min(\d, \max), 16): bound (16, \d, \max) as the C macro evaluates it
.macro BOUND16 d, max
	cmp	\d, \max
	it	gt
	movgt	\d, \max
	cmp	\d, #16
	it	lt
	movlt	\d, #16
.endm

@ r7/r8 = bound (16, (int)(sdivz * z) + sadjust, bbextents) and the same for t; uses r3, r4
.macro SNEXT_TNEXT
	vmul.f32	s4, s0, s3
	vmul.f32	s5, s1, s3
	vcvt.s32.f32	s4, s4
	vcvt.s32.f32	s5, s5
	ldrd	r3, r4, [sp, #F_SADJUST]
	vmov	r7, s4
	vmov	r8, s5
	add	r7, r7, r3
	add	r8, r8, r4
	ldrd	r3, r4, [sp, #F_BBEXTS]
	BOUND16	r7, r3
	BOUND16	r8, r4
.endm

@ the partial segment's end: sdivz/tdivz/zi += step * (nrem - 1) (fused), and start z = 65536 / zi
.macro REM_ZI
	sub	lr, nrem, #1
	vmov	s8, lr
	vcvt.f32.s32	s8, s8
	vfma.f32	s2, s24, s8
	vfma.f32	s0, s18, s8
	vfma.f32	s1, s21, s8
	vdiv.f32	s3, s28, s2
.endm

	.section .text.D_DrawSpans8_ARM,"ax",%progbits
	.align	4
	.global	D_DrawSpans8_ARM
	.type	D_DrawSpans8_ARM, %function
	.thumb_func
D_DrawSpans8_ARM:
	push	{r4-r11, lr}
	vpush	{s16-s28}
	sub	sp, sp, #F_SIZE

	@ s16..s24: s/z, t/z and 1/z gradients (origin, stepv, stepu each)
	vldmia	r1!, {s16-s24}
	ldmia	r1, {r2, r3, r4, r7, r8, r9, r11, r12}
	stmia	sp, {r2, r3, r4, r7}		@ sadjust, tadjust, bbextents, bbextentt
	mov	pbase, r8
	mov	cw, r9
	strd	r11, r12, [sp, #F_VIEW]		@ viewbuffer, screenwidth
	vmov.f32	s4, #16.0
	vmul.f32	s25, s18, s4		@ s/z step per 16 pixels
	vmul.f32	s26, s21, s4		@ t/z step
	vmul.f32	s27, s24, s4		@ 1/z step
	mov	r2, #0x47800000
	vmov	s28, r2			@ 65536.0
	mov	pspan, r0

.Lspan:
	ldr	lr, [pspan, #12]		@ pnext
	ldmia	pspan, {r7, r8, r9}		@ u, v, count
	cmp	lr, #0
	it	ne
	ldrne	r12, [lr]		@ request the next span record now (r12 is not written again)
	str	lr, [sp, #F_PNEXT]
	cmp	r9, #0
	ble	.Lnext

	@ s/z, t/z and 1/z at the first pixel
	vmov	s6, r7
	vmov	s7, r8
	vcvt.f32.s32	s6, s6		@ du
	vcvt.f32.s32	s7, s7		@ dv
	vmov.f32	s2, s22
	vmov.f32	s0, s16
	vmov.f32	s1, s19
	vfma.f32	s2, s23, s7
	vfma.f32	s0, s17, s7
	vfma.f32	s1, s20, s7
	vfma.f32	s2, s24, s6		@ zi
	vfma.f32	s0, s18, s6		@ sdivz
	vfma.f32	s1, s21, s6		@ tdivz
	vdiv.f32	s3, s28, s2		@ z = 65536 / zi

	ldrd	r0, r1, [sp, #F_VIEW]
	and	nrem, r9, #15			@ pixels in the partial segment
	asr	nseg, r9, #4			@ whole 16-pixel segments
	mla	r0, r1, r8, r0
	add	pdest, r0, r7			@ &viewbuffer[screenwidth * v + u]

	@ s = bound (0, (int)(sdivz * z) + sadjust, bbextents), t the same
	vmul.f32	s4, s0, s3
	vmul.f32	s5, s1, s3
	vcvt.s32.f32	s4, s4
	vcvt.s32.f32	s5, s5
	vmov	r7, s4
	vmov	r8, s5

	@ z0 is used up: start the next division now, so it runs during the clamps below
	cmp	nseg, #0
	ble	1f
	vadd.f32	s2, s2, s27
	vadd.f32	s0, s0, s25
	vadd.f32	s1, s1, s26
	vdiv.f32	s3, s28, s2		@ z at the end of the first segment
	b	2f
1:	cmp	nrem, #1
	beq	2f
	REM_ZI					@ z at the last pixel
2:
	ldmia	sp, {r1, r2, r3, r4}		@ sadjust, tadjust, bbextents, bbextentt
	add	r7, r7, r1
	add	r8, r8, r2
	cmp	r7, r3
	it	gt
	movgt	r7, r3
	bic	sfix, r7, r7, asr #31		@ max (min (x, bbextents), 0)
	cmp	r8, r4
	it	gt
	movgt	r8, r4
	bic	tfix, r8, r8, asr #31

	cmp	nseg, #0
	ble	.Lrem_only

.Lseg:
	@ s3 = z at the end of this segment
	SNEXT_TNEXT
	strd	r7, r8, [sp, #F_SNEXT]
	sub	sstep, r7, sfix
	sub	tstep, r8, tfix
	asr	sstep, sstep, #4
	asr	tstep, tstep, #4

	@ start the division for what comes after this segment; it runs while the pixels are drawn
	subs	nseg, nseg, #1
	beq	1f
	vadd.f32	s2, s2, s27
	vadd.f32	s0, s0, s25
	vadd.f32	s1, s1, s26
	vdiv.f32	s3, s28, s2
	b	2f
1:	cmp	nrem, #1
	ble	2f				@ no step needed for 0 or 1 pixel
	REM_ZI
2:
	TEXEL	r7
	TEXEL	r8
	strb	r7, [pdest, #0]
	TEXEL	r7
	strb	r8, [pdest, #1]
	TEXEL	r8
	strb	r7, [pdest, #2]
	TEXEL	r7
	strb	r8, [pdest, #3]
	TEXEL	r8
	strb	r7, [pdest, #4]
	TEXEL	r7
	strb	r8, [pdest, #5]
	TEXEL	r8
	strb	r7, [pdest, #6]
	TEXEL	r7
	strb	r8, [pdest, #7]
	TEXEL	r8
	strb	r7, [pdest, #8]
	TEXEL	r7
	strb	r8, [pdest, #9]
	TEXEL	r8
	strb	r7, [pdest, #10]
	TEXEL	r7
	strb	r8, [pdest, #11]
	TEXEL	r8
	strb	r7, [pdest, #12]
	TEXEL	r7
	strb	r8, [pdest, #13]
	TEXEL	r8
	strb	r7, [pdest, #14]
	strb	r8, [pdest, #15]
	add	pdest, pdest, #16
	ldrd	sfix, tfix, [sp, #F_SNEXT]
	cmp	nseg, #0
	bgt	.Lseg

	cmp	nrem, #1
	blt	.Lnext
	beq	.Lrem_draw
	b	.Lrem_steps

.Lrem_only:
	@ fewer than 16 pixels (nrem = count >= 1); the division was started above
	cmp	nrem, #1
	beq	.Lrem_draw

.Lrem_steps:
	@ steps by division, biased low (nrem >= 2; s3 = z at the last pixel)
	SNEXT_TNEXT
	sub	lr, nrem, #1
	sub	r3, r7, sfix
	sub	r4, r8, tfix
	sdiv	sstep, r3, lr
	sdiv	tstep, r4, lr

.Lrem_draw:
	@ nrem pixels (1..15): jump to the block of pixel -nrem
	add	pdest, pdest, nrem
	adr.w	lr, .Lrem_end
	sub	lr, lr, nrem, lsl #4
	sub	lr, lr, nrem, lsl #3		@ 24 bytes per pixel
	orr	lr, lr, #1
	bx	lr
	REMPIX	15
	REMPIX	14
	REMPIX	13
	REMPIX	12
	REMPIX	11
	REMPIX	10
	REMPIX	9
	REMPIX	8
	REMPIX	7
	REMPIX	6
	REMPIX	5
	REMPIX	4
	REMPIX	3
	REMPIX	2
	REMPIX	1
.Lrem_end:

.Lnext:
	ldr	pspan, [sp, #F_PNEXT]
	cmp	pspan, #0
	bne	.Lspan

	add	sp, sp, #F_SIZE
	vpop	{s16-s28}
	pop	{r4-r11, pc}
	.size	D_DrawSpans8_ARM, .-D_DrawSpans8_ARM
