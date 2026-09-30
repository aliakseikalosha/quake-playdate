/*
 * R_GenerateLine_ARM: the span generation of one scan line of the array-based edge scan
 * (R_ScanEdges in r_edge.c, PD_FAST_EDGES) for the Playdate's Cortex-M7, in Thumb-2.
 *
 *	void R_GenerateLine_ARM (fe_line_t *g);
 *
 * Walks the active edge table act[1..] up to the tail edge, keeps the surface stack stk[] sorted
 * on key, emits a span whenever the top surface changes, then emits the top's last span and clears
 * the span state of the surfaces left on the stack; exactly what the C loop does, including the
 * 1/z sort of two bmodel surfaces with the same key (same fused multiply-adds, same comparisons).
 * The caller makes sure stk[] can hold every active edge (the C grows it on demand instead).
 *
 * fe_line_t (r_edge.c): act, stk, state, heads, surfaces, sp, iv, tail, head_u, tail_u, fv,
 * bmodelactive at 0, 4, ..., 44; sp and bmodelactive are written back.
 * aedge_t: u, u_step, s0 (16 bits), s1 (16 bits), e at 0, 4, 8, 10, 12 (16 bytes).
 * sentry_t: s, key, last_u (12 bytes). surf_t: spans at 8, key at 12, insubmodel (byte) at 40,
 * d_ziorigin, d_zistepu, d_zistepv at 44, 48, 52 (64 bytes). espan_t: u, v, count, pnext.
 */
	.syntax unified
	.cpu cortex-m7
	.fpu fpv5-sp-d16
	.thumb

#define G_ACT		0
#define G_STK		4
#define G_STATE		8
#define G_HEADS		12
#define G_SURFACES	16
#define G_SP		20
#define G_IV		24
#define G_TAIL		28
#define G_HEADU		32
#define G_TAILU		36
#define G_FV		40
#define G_BMODEL	44

@ stack frame
#define F_G		0
#define F_BMODEL	4
#define F_TAILU		8
#define F_SIZE		16

a	.req r0		@ current active edge
surfs	.req r1		@ surfaces
state	.req r2		@ span state per surface (signed char)
stk	.req r3		@ surface stack, stk[0] = nearest
nstk	.req r4
spp	.req r5		@ next free span
iv	.req r6
heads	.req r7		@ span list heads per surface, or 0: the lists hang off surf_t.spans
tail	.req r9		@ &edge_tail

@ If \ue > stk[0].last_u, emit the span [stk[0].last_u, \ue) of the top surface stk[0].s and put
@ it at the head of that surface's span list. Uses r10, r11.
.macro EMIT_TOP ue
	ldr	r10, [stk, #8]
	cmp	\ue, r10
	ble	9f
	str	r10, [spp, #0]			@ u
	sub	r10, \ue, r10
	str	iv, [spp, #4]			@ v
	str	r10, [spp, #8]			@ count
	ldr	r10, [stk, #0]
	cbz	heads, 8f
	sub	r10, r10, surfs
	lsr	r10, r10, #6			@ surface number
	ldr	r11, [heads, r10, lsl #2]
	str	r11, [spp, #12]
	str	spp, [heads, r10, lsl #2]
	add	spp, spp, #16
	b	9f
8:	ldr	r11, [r10, #8]
	str	r11, [spp, #12]
	str	spp, [r10, #8]
	add	spp, spp, #16
9:
.endm

@ The surface in lr (a bmodel) and the one of stack entry \entry have the same key: branch to
@ \front if lr's surface is in front at this edge (1/z at u, v, with the C's 1% slack and tie
@ break on d_zistepu). Uses r12 and s0-s7.
.macro ZTEST entry, front
	ldr	r12, [a, #0]
	add	r12, r12, #1
	sub	r12, r12, #0x100000		@ u - 0xFFFFF
	vmov	s0, r12
	vcvt.f32.s32	s0, s0
	vmul.f32	s0, s0, s19		@ fu
	ldr	r12, [\entry]			@ the other surface
	vldr	s1, [lr, #44]
	vldr	s2, [lr, #52]
	vldr	s3, [lr, #48]
	vldr	s4, [r12, #44]
	vldr	s5, [r12, #52]
	vldr	s6, [r12, #48]
	vfma.f32	s1, s2, s16
	vfma.f32	s4, s5, s16
	vfma.f32	s1, s3, s0		@ newzi
	vfma.f32	s4, s6, s0		@ testzi
	vmul.f32	s7, s1, s17		@ newzibottom = newzi * 0.99
	vcmpe.f32	s7, s4
	vmrs	APSR_nzcv, fpscr
	bge	\front
	vmul.f32	s7, s1, s18		@ newzitop = newzi * 1.01
	vcmpe.f32	s4, s7
	vmrs	APSR_nzcv, fpscr
	bhi	7f				@ testzi > newzitop: behind
	vcmpe.f32	s3, s6
	vmrs	APSR_nzcv, fpscr
	bge	\front				@ d_zistepu >= the other's
7:
.endm

	.section .text.R_GenerateLine_ARM,"ax",%progbits
	.align	4
	.global	R_GenerateLine_ARM
	.type	R_GenerateLine_ARM, %function
	.thumb_func
R_GenerateLine_ARM:
	push	{r4-r11, lr}
	vpush	{s16-s19}
	sub	sp, sp, #F_SIZE
	str	r0, [sp, #F_G]
	ldr	surfs, [r0, #G_SURFACES]
	ldr	state, [r0, #G_STATE]
	ldr	stk, [r0, #G_STK]
	ldr	spp, [r0, #G_SP]
	ldr	iv, [r0, #G_IV]
	ldr	heads, [r0, #G_HEADS]
	ldr	tail, [r0, #G_TAIL]
	ldr	r10, [r0, #G_TAILU]
	str	r10, [sp, #F_TAILU]
	mov	r10, #0
	str	r10, [sp, #F_BMODEL]
	vldr	s16, [r0, #G_FV]
	movw	r10, #0x70a4
	movt	r10, #0x3f7d
	vmov	s17, r10			@ 0.99f
	movw	r10, #0x47ae
	movt	r10, #0x3f81
	vmov	s18, r10			@ 1.01f
	mov	r10, #0x35800000
	vmov	s19, r10			@ 1.0f / 0x100000

	@ the stack holds just the background: stk[0] = {&surfaces[1], its key, head_u}
	add	r10, surfs, #64
	ldr	r11, [r10, #12]
	ldr	r12, [r0, #G_HEADU]
	stmia	stk, {r10, r11, r12}
	mov	nstk, #1
	ldr	a, [r0, #G_ACT]
	add	a, a, #16			@ act[1]

.Ledge:
	ldr	r10, [a, #12]
	cmp	r10, tail
	beq	.Ldone

	@ a trailing edge: its left surface goes away
	ldrh	r11, [a, #8]
	cmp	r11, #0
	beq	.Lleading
	ldrsb	r12, [state, r11]
	subs	r12, r12, #1
	strb	r12, [state, r11]
	bne	.Lleading
	add	lr, surfs, r11, lsl #6		@ surf
	ldrb	r12, [lr, #40]
	cmp	r12, #0
	beq	1f
	ldr	r12, [sp, #F_BMODEL]
	sub	r12, r12, #1
	str	r12, [sp, #F_BMODEL]
1:	ldr	r12, [stk, #0]
	cmp	r12, lr
	bne	.Lfind
	@ it is the top: emit its span up to here, the one below starts here
	ldr	r8, [a, #0]
	asr	r8, r8, #20
	EMIT_TOP r8
	str	r8, [stk, #20]			@ stk[1].last_u
	mov	r10, stk
	b	.Lremove
.Lfind:
	mov	r10, stk
1:	ldr	r12, [r10, #12]!
	cmp	r12, lr
	bne	1b
.Lremove:
	@ r10 = its entry: move the ones below it up
	sub	nstk, nstk, #1
	add	r11, nstk, nstk, lsl #1
	add	r11, stk, r11, lsl #2		@ &stk[nstk]
	cmp	r10, r11
	bhs	.Lleading
1:	ldrd	r8, r12, [r10, #12]
	ldr	lr, [r10, #20]
	strd	r8, r12, [r10, #0]
	str	lr, [r10, #8]
	add	r10, r10, #12
	cmp	r10, r11
	blo	1b

.Lleading:
	@ a leading edge: its right surface comes in
	ldrh	r11, [a, #10]
	cmp	r11, #0
	beq	.Lnext
	ldrsb	r12, [state, r11]
	add	r12, r12, #1
	strb	r12, [state, r11]
	cmp	r12, #1
	bne	.Lnext
	add	lr, surfs, r11, lsl #6		@ surf
	ldrb	r11, [lr, #40]			@ insubmodel, kept in r11
	cmp	r11, #0
	beq	1f
	ldr	r12, [sp, #F_BMODEL]
	add	r12, r12, #1
	str	r12, [sp, #F_BMODEL]
1:	ldr	r8, [lr, #12]			@ key
	mov	r10, stk			@ p = 0
	ldr	r12, [stk, #4]
	cmp	r8, r12
	blt	.Lnewtop
	bne	.Lsearch
	cmp	r11, #0
	beq	.Lsearch
	ZTEST	stk, .Lnewtop			@ two bmodels in the same leaf
.Lsearch:
	@ do p++ while key > stk[p].key (the background has the largest key)
	add	r10, r10, #12
	ldr	r12, [r10, #4]
	cmp	r8, r12
	bgt	.Lsearch
	bne	.Lgotpos
	cmp	r11, #0				@ same key: a world surface goes behind it,
	beq	.Lsearch			@ a bmodel is sorted on 1/z
	ZTEST	r10, .Lgotpos
	b	.Lsearch

.Lnewtop:
	@ it obscures the top: emit the top's span up to here
	ldr	r12, [a, #0]
	asr	r12, r12, #20
	EMIT_TOP r12
	mov	r10, stk

.Lgotpos:
	@ insert {surf, key, last_u} before stk[p] (r10); a new top starts its span here
	add	r11, nstk, nstk, lsl #1
	add	r11, stk, r11, lsl #2		@ &stk[nstk]
	cmp	r11, r10
	bls	2f
1:	ldr	r12, [r11, #-12]
	str	r12, [r11, #0]
	ldr	r12, [r11, #-8]
	str	r12, [r11, #4]
	ldr	r12, [r11, #-4]
	str	r12, [r11, #8]
	sub	r11, r11, #12
	cmp	r11, r10
	bhi	1b
2:	str	lr, [r10, #0]
	str	r8, [r10, #4]
	mov	r12, #0
	cmp	r10, stk
	bne	3f
	ldr	r12, [a, #0]
	asr	r12, r12, #20
3:	str	r12, [r10, #8]
	add	nstk, nstk, #1

.Lnext:
	add	a, a, #16
	b	.Ledge

.Ldone:
	@ the right edge of the screen: emit whatever is on top
	ldr	r12, [sp, #F_TAILU]
	EMIT_TOP r12

	@ clear the span state of the surfaces left in the stack
	mov	r10, stk
	mov	r12, #0
	cmp	nstk, #0
	ble	2f
1:	ldr	r11, [r10], #12
	sub	r11, r11, surfs
	lsr	r11, r11, #6
	strb	r12, [state, r11]
	subs	nstk, nstk, #1
	bne	1b
2:
	ldr	r0, [sp, #F_G]
	str	spp, [r0, #G_SP]
	ldr	r1, [sp, #F_BMODEL]
	str	r1, [r0, #G_BMODEL]
	add	sp, sp, #F_SIZE
	vpop	{s16-s19}
	pop	{r4-r11, pc}
	.size	R_GenerateLine_ARM, .-R_GenerateLine_ARM


/*
 * The active edge table upkeep of R_ScanEdges around the span generation. The C shifts table
 * entries with struct assignments that GCC turns into memmove calls, and newlib's memmove copies
 * an overlapping block that moves up one byte at a time; these move whole 16-byte entries.
 *
 * edge_t: u, u_step at 0, 4, next at 12, surfs[2] at 16, nextremove at 20.
 */

/*
 *	void R_InsertEdges_ARM (fe_ins_t *g);
 *
 * fe_ins_t: act, nact, cap, j, ne at 0, 4, 8, 12, 16. Merges the edge list ne (sorted on u) into
 * act[]: each edge goes before the first active edge with u >= its own, searching on from act[j]
 * (j = 1 at first, then just after the previous insertion). When act[] is full it stops with ne =
 * the next edge to insert and j its search position; the caller grows act[] and calls again.
 * Finishes with ne = NULL. nact, j and ne are written back.
 */
	.section .text.R_InsertEdges_ARM,"ax",%progbits
	.align	4
	.global	R_InsertEdges_ARM
	.type	R_InsertEdges_ARM, %function
	.thumb_func
R_InsertEdges_ARM:
	push	{r0, r4-r11, lr}
	ldmia	r0, {r1, r2, r3, r4, r5}	@ act, nact, cap, j, ne
	add	r4, r1, r4, lsl #4		@ &act[j]
.Lins_edge:
	ldr	r6, [r5, #0]			@ u
	ldr	r7, [r5, #4]			@ u_step
	ldr	r8, [r5, #16]			@ surfs[0], surfs[1]
	ldr	r9, [r5, #12]			@ next
1:	ldr	r10, [r4, #0]
	cmp	r10, r6
	bge	2f
	add	r4, r4, #16
	b	1b
2:	cmp	r2, r3
	beq	.Lins_full
	@ move act[j .. nact-1] up one
	add	r10, r1, r2, lsl #4
3:	cmp	r10, r4
	bls	4f
	ldmdb	r10, {r0, r11, r12, lr}
	stmia	r10, {r0, r11, r12, lr}
	sub	r10, r10, #16
	b	3b
4:	stmia	r4!, {r6, r7, r8}
	str	r5, [r4], #4			@ e
	add	r2, r2, #1
	movs	r5, r9
	bne	.Lins_edge
.Lins_full:
	ldr	r0, [sp, #0]
	str	r2, [r0, #4]
	sub	r4, r4, r1
	lsr	r4, r4, #4
	str	r4, [r0, #12]
	str	r5, [r0, #16]
	pop	{r0, r4-r11, pc}
	.size	R_InsertEdges_ARM, .-R_InsertEdges_ARM

/*
 *	int R_RemoveEdges_ARM (aedge_t *act, int nact, edge_t *list);
 *
 * Takes every edge of the nextremove list out of act[] (each is found from act[1] on) and
 * returns the new nact.
 */
	.section .text.R_RemoveEdges_ARM,"ax",%progbits
	.align	4
	.global	R_RemoveEdges_ARM
	.type	R_RemoveEdges_ARM, %function
	.thumb_func
R_RemoveEdges_ARM:
	push	{r4-r7, lr}
.Lrem_edge:
	add	r3, r0, #16
1:	ldr	r4, [r3, #12]
	cmp	r4, r2
	beq	2f
	add	r3, r3, #16
	b	1b
2:	sub	r1, r1, #1
	add	r4, r0, r1, lsl #4		@ &act[nact]
3:	cmp	r3, r4
	bhs	4f
	ldrd	r5, r6, [r3, #16]
	ldrd	r7, r12, [r3, #24]
	strd	r5, r6, [r3, #0]
	strd	r7, r12, [r3, #8]
	add	r3, r3, #16
	b	3b
4:	ldr	r2, [r2, #20]			@ nextremove
	cmp	r2, #0
	bne	.Lrem_edge
	mov	r0, r1
	pop	{r4-r7, pc}
	.size	R_RemoveEdges_ARM, .-R_RemoveEdges_ARM

/*
 *	void R_StepEdges_ARM (aedge_t *act, int nact);
 *
 * Steps every active edge after act[0] to the next scan line; an edge that ends up left of its
 * predecessor is moved back to after the last entry (from act[i-2] down to act[0]) whose u is
 * not greater than its own, or to act[0].
 */
	.section .text.R_StepEdges_ARM,"ax",%progbits
	.align	4
	.global	R_StepEdges_ARM
	.type	R_StepEdges_ARM, %function
	.thumb_func
R_StepEdges_ARM:
	cmp	r1, #2
	it	le
	bxle	lr
	push	{r4-r11, lr}
	add	r2, r0, #16			@ &act[i], i = 1
	add	r3, r0, r1, lsl #4		@ &act[nact]
	ldr	r4, [r0, #0]			@ act[i-1].u
.Lstep:
	ldrd	r5, r6, [r2]
	add	r5, r5, r6
	str	r5, [r2]
	cmp	r5, r4
	blt	.Lstep_move
	mov	r4, r5
	add	r2, r2, #16
	cmp	r2, r3
	blo	.Lstep
	pop	{r4-r11, pc}
.Lstep_move:
	ldrd	r7, r8, [r2, #8]		@ the moved entry is r5..r8
	sub	r9, r2, #32			@ act[j], j = i - 2
1:	cmp	r9, r0
	blo	2f
	ldr	r10, [r9, #0]
	cmp	r10, r5
	ble	2f
	sub	r9, r9, #16
	b	1b
2:	add	r9, r9, #16			@ act[j+1]
	mov	r10, r2
3:	cmp	r10, r9				@ act[k] = act[k-1] for k = i .. j+2
	bls	4f
	ldmdb	r10, {r1, r11, r12, lr}
	stmia	r10, {r1, r11, r12, lr}
	sub	r10, r10, #16
	b	3b
4:	stmia	r9, {r5, r6, r7, r8}
	ldr	r4, [r2, #0]			@ what is at act[i] now
	add	r2, r2, #16
	cmp	r2, r3
	blo	.Lstep
	pop	{r4-r11, pc}
	.size	R_StepEdges_ARM, .-R_StepEdges_ARM
