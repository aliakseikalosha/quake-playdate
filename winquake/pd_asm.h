/*
 * Hand-written Thumb-2 versions of hot loops for the Playdate's Cortex-M7 (winquake/*_arm.S),
 * used by the device build with -DPD_ASM=ON (the default there). Each one keeps its C twin,
 * which stays the reference:
 *
 *  PD_ASM_AB (with PD_PROFILE): each frame runs either the assembly or the C (picked by a hash
 *      of the frame number) and prof.csv records which; `scripts/pd-report.py --ab` then compares
 *      the two halves of one run (same binary layout, same scenes).
 *  PD_ASM_CHECK (with PD_PROFILE): every call runs the assembly, keeps what it wrote, runs the
 *      C over the same pixels and counts the ones that differ (prof.csv column asm_bad, plus
 *      ASMBAD lines with the first differences). Must stay 0 over the demos.
 */
#ifndef PD_ASM_H
#define PD_ASM_H

#if defined(PD_ASM) && defined(__arm__)
#define PD_USE_ASM 1

#if defined(PD_ASM_AB) || defined(PD_ASM_CHECK)
extern int		pd_asm_on;		// 1: use the assembly this frame
extern unsigned	pd_asm_bad;		// PD_ASM_CHECK: pixels that differed this frame
#define PD_ASM_ACTIVE()	(pd_asm_on)
void pd_asm_mismatch (const char *what, int u, int v, int got, int want);
#else
#define PD_ASM_ACTIVE()	1
#endif

#endif
#endif
