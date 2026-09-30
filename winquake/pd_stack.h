/*
 * Scratch buffers on the stack instead of in static memory (-DPD_STACK=ON, the default for the
 * Playdate). The stack is fast internal RAM; static data and the heap are slow memory behind a
 * write-through cache, where a buffer that is written and then read back costs ~26 ns per byte
 * stored plus a miss per line read. But the game task's stack is only ~10 KB, so a buffer only
 * goes on the stack while the whole call chain, the buffer included, stays within PD_STACK_BUDGET
 * of where the frame started (pd_stack_top, set by the port for every frame); otherwise the
 * caller uses its static buffer, as before. The pictures are the same either way.
 *
 * PD_STACK_AB (with PD_PROFILE): each frame uses either the stack buffers or the static ones,
 * picked by a hash of the frame number; compare with `scripts/pd-report.py --ab`.
 */
#ifndef PD_STACK_H
#define PD_STACK_H

#include <stdint.h>

#ifdef PD_STACK
// the world traversal of a PD_PROFILE_FINE build goes ~7 KB below the frame start every frame
// (release builds ~6 KB), so stay within 6.5 KB
#ifndef PD_STACK_BUDGET
#define PD_STACK_BUDGET		(6 * 1024 + 512)
#endif

extern uintptr_t	pd_stack_top;	// stack pointer when the frame started, 0 = not in a frame

static inline uintptr_t PD_StackPointer (void)
{
	uintptr_t	sp;

#if defined(__arm__)
	__asm__ volatile ("mov %0, sp" : "=r"(sp));
#else
	volatile char	probe;

	sp = (uintptr_t)&probe;
#endif
	return sp;
}

#ifdef PD_STACK_AB
extern int	pd_asm_on;	// the A/B switch of pd_asm.h
#define PD_STACK_ACTIVE()	(pd_asm_on)
#else
#define PD_STACK_ACTIVE()	1
#endif

// is there room below the caller's frame for a buffer of this many bytes?
static inline int PD_StackRoom (unsigned bytes)
{
	return PD_STACK_ACTIVE() && pd_stack_top
		&& pd_stack_top - PD_StackPointer () + bytes <= PD_STACK_BUDGET;
}
#else
#define PD_STACK_ACTIVE()	0
#define PD_StackRoom(bytes)	0
#endif

#endif
