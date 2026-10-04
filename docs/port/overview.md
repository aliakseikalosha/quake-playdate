# Shared platform layer

[← Documentation index](../README.md) · [Source index](../source-index.md)

Quake expects an operating system. This port replaces it with two small layers:

1. **Engine-facing functions** in `port/*.c` that implement the classic WinQuake platform
   interface (`Sys_*`, `VID_*`, `IN_*`, `CDAudio_*`). They do not depend on the hardware.
2. **Board hooks**, the `qembd_*` functions declared in [`include/quakembd.h`](../../include/quakembd.h),
   which a board implements for its own hardware.

The only board in this tree is the [Playdate](playdate.md). (The upstream project's desktop, RISC-V and STM32 boards were
removed, see [Removed boards](../build-system.md#removed-boards); the hook interface they used is kept, so another board
could still be written, see the [skeleton](#minimal-board-skeleton).)

```
 winquake/ (engine)
     │  Sys_FloatTime(), VID_Update(), IN_Move(), Sys_FileOpenRead() ...
     ▼
 port/*.c  (this page)         ← independent of the hardware
     │  qembd_get_us_time(), qembd_fillrect(), qembd_dequeue_key_event() ...
     ▼
 port/boards/playdate/*.c      ← the board: one implementation of the hooks
```

Files covered here:

| File | Implements |
| --- | --- |
| [`include/quakembd.h`](#includequakembdh) | The board interface and logging macros |
| [`port/sys_port.c`](#portsys_portc) | `Sys_*`, start-up and the frame loop |
| [`port/vid_port.c`](#portvid_portc) | `VID_*`, glue to `qembd_fillrect` |
| [`port/in_port.c`](#portin_portc) | `IN_*` (mouse look) |
| [`port/cd_null.c`](#portcd_nullc) | `CDAudio_*` as no-ops (not in the Playdate build; the host check uses it) |
| [`port/fio/fio_posix.c`](#portfiofio_posixc) | `Sys_File*` on POSIX (used by the host check) |

---

## `include/quakembd.h`

The contract between the engine-side port and a board. It also holds the logging macros and
the small input structs.

### Logging

```c
qembd_info("Quake heap: %d KiB", parms.memsize / 1024);   // green  [INFO]
qembd_warn("Cannot f_stat %s", path);                      // yellow [WARN]
qembd_error("Memory cannot be allocated");                 // red    [ERROR]
qembd_debug("only with -DQEMBD_ENABLE_DEBUG");             // cyan   [DEBUG]
```

Output goes through `QEMBD_PRINTF` (default `printf`; the Playdate build redefines it to
`pdq_printf`, see [Playdate board](playdate.md)). `QEMBD_LOGGING_TAG` (default `"QUAKEMBD"`)
prefixes each line.

Three helpers implement a "log and jump to cleanup" pattern. Nothing in the Playdate build uses them (they came from the removed STM32 board):

```c
static int open_things(void)
{
	FILE *f = fopen("id1/pak0.pak", "rb");

	bail_if_null(f, "Cannot open pak0.pak");      // logs the message and goes to bail
	return 0;
bail:
	return -1;
}
```

`bail_if_error(X, COND, msg)`, `bail_if_null(X, msg)` and `bail(msg)` all `goto bail`.

### Types

```c
typedef struct { uint32_t keycode; uint8_t state; } key_event_t;       // state 1 = pressed
typedef struct { int32_t x, y, xrel, yrel; }        mouse_motion_t;    // not used by the Playdate board
typedef struct { int32_t x, y; }                    mouse_movement_t;  // delta since last call
```

### Functions a board must provide

| Function | Purpose |
| --- | --- |
| `int qembd_get_width()` / `qembd_get_height()` | Size of Quake's 8-bit frame buffer. |
| `void qembd_vidinit()` | Called from `VID_Init` once the engine has allocated its buffers. |
| `void qembd_fillrect(uint8_t *src, uint32_t *clut, x, y, xsize, ysize)` | Convert the dirty rectangle of the 8-bit buffer (`src`, stride = width) to the display, using the 256-entry `0x00RRGGBB` palette `clut`. |
| `void qembd_refresh()` | Push the finished frame to the screen. |
| `uint64_t qembd_get_us_time()` | Monotonic microseconds. |
| `void qembd_udelay(uint32_t us)` | Busy or timed delay. |
| `void *qembd_allocmain(size_t size)` | The block Quake uses as its whole heap (the "hunk"). May return `NULL`. |
| `int qembd_dequeue_key_event(key_event_t *e)` | `0` and fills `*e` if an event is waiting, `-1` otherwise. |
| `int qembd_get_mouse_movement(mouse_movement_t *m)` | `0` and fills `*m` (and clears it) when there is movement. |

### Entry points the port provides *to* a board

| Function | Defined in | Purpose |
| --- | --- | --- |
| `int qembd_main(int argc, char **argv)` | `sys_port.c` | `qembd_init` then `qembd_frame` forever. For a board that owns the process; the Playdate does not use it (it is called once per frame by the system). |
| `int qembd_init(int argc, char **argv)` | `sys_port.c` | Allocate the heap and run `Host_Init`. |
| `void qembd_frame(void)` | `sys_port.c` | Run one `Host_Frame`. For boards that are called once per frame (Playdate). |

### Hooks for boards that cannot own the process

When `QEMBD_PLAYDATE` is defined `Sys_Error`/`Sys_Quit` cannot call `exit`, so they call:

```c
void qembd_log(const char *text);                       // Sys_Printf output
void qembd_fatal(const char *msg) __attribute__((noreturn));  // Sys_Error
void qembd_quit(void) __attribute__((noreturn));              // Sys_Quit
```

### Minimal board skeleton

An illustration (not a file in the tree) of what a new board has to provide. The real example is [`port/boards/playdate`](playdate.md).

```c
// main.c of a hypothetical board that owns the process
#include <quakembd.h>
#include <stdlib.h>
#include <time.h>

uint64_t qembd_get_us_time() {
	struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t) ts.tv_sec * 1000000u + ts.tv_nsec / 1000;
}
void  qembd_udelay(uint32_t us)       { /* usleep(us) */ }
void *qembd_allocmain(size_t size)    { return malloc(size); }
int   qembd_dequeue_key_event(key_event_t *e)       { return -1; }  // no keys
int   qembd_get_mouse_movement(mouse_movement_t *m) { return -1; }  // no mouse

// display.c
int  qembd_get_width()  { return 640; }
int  qembd_get_height() { return 360; }
void qembd_vidinit()    { /* open a window */ }
void qembd_fillrect(uint8_t *src, uint32_t *clut, uint16_t x, uint16_t y,
                    uint16_t w, uint16_t h)
{
	for (int py = 0; py < h; py++)
		for (int px = 0; px < w; px++)
			framebuffer[(y + py) * 640 + x + px] = clut[src[(y + py) * 640 + x + px]];
}
void qembd_refresh()    { /* present framebuffer */ }

int main(int c, char **v) { return qembd_main(c, v); }
```

Plus the `Sys_File*` functions of `sys.h`: [`fio_posix.c`](#portfiofio_posixc) is a POSIX implementation, and the Playdate board has its own ([`fio.c`](playdate.md#fioc)).

---

## `port/sys_port.c`

Implements the engine's `sys.h` interface on top of the `qembd_*` hooks, and owns start-up.

| Function | What it does |
| --- | --- |
| `_Sys_Printf(fmt, ...)` | Built only with `WINQUAKE_LOGGING_EXTERNAL` (always, see [build system](../build-system.md)). Formats to a 1 KiB buffer, then either `qembd_log` (Playdate) or stdout with control characters escaped as `[xx]`. |
| `Sys_Error(fmt, ...)` | Formats the message; Playdate: `qembd_fatal`; others: print to stderr, `Host_Shutdown`, `exit(1)`. |
| `Sys_Quit()` | `Host_Shutdown`, then `qembd_quit` or `exit(0)`. |
| `Sys_FloatTime()` | `qembd_get_us_time() / 1000000.0` |
| `Sys_SendKeyEvents()` | Drains `qembd_dequeue_key_event` into `Key_Event`. |
| `Sys_ConsoleInput`, `Sys_Sleep`, `Sys_HighFPPrecision`, `Sys_LowFPPrecision`, `Sys_MakeCodeWriteable` | Stubs (`NULL` / nothing). |
| `qembd_init(c, v)` | Parses `-mem`, allocates the heap, calls `Host_Init`. |
| `qembd_frame()` | Computes the frame time, wraps `Host_Frame` in `pdprof_frame_begin/end`. |
| `qembd_main(c, v)` | `qembd_init` then `while (1) qembd_frame();` |

Heap size: `DEFAULT_MEM_SIZE` (8 MiB if the board does not define it) unless `-mem <MB>` is
given. With `QEMBD_PLAYDATE` the size backs off by 512 KiB while the allocation fails, down to
`DEFAULT_MIN_MEM_SIZE`:

```c
parms.memsize = DEFAULT_MEM_SIZE;
j = COM_CheckParm("-mem");
if (j)
	parms.memsize = (int) (Q_atof(com_argv[j+1]) * 1024 * 1024);
parms.membase = qembd_allocmain(parms.memsize);
#ifdef QEMBD_PLAYDATE
while (!parms.membase && !j && parms.memsize > DEFAULT_MIN_MEM_SIZE) {
	parms.memsize -= 512 * 1024;
	parms.membase = qembd_allocmain(parms.memsize);
}
#endif
```

The frame loop clamps long frames the same way the original does (`sys_ticrate`):

```c
newtime = Sys_FloatTime();
time = newtime - oldtime;
if (time > sys_ticrate.value*2)
	oldtime = newtime;      // a hitch: don't try to catch up
else
	oldtime += time;
Host_Frame(time);
```

**Using it:** a board that owns the process would call `qembd_main(argc, argv)` from `main`;
the Playdate calls `qembd_init` once and `qembd_frame` from its update callback. The non-Playdate branches of `Sys_Error` / `Sys_Quit`
(`fprintf` to stderr, `exit`) are what such a board would use; they are kept from the upstream code.

## `port/vid_port.c`

Implements `VID_Init`, `VID_SetPalette`, `VID_Update`, `VID_Shutdown` from `vid.h`.

- `VID_Init` allocates three buffers from the **high** end of the hunk
  (`Hunk_HighAllocName`): the 8-bit colour buffer (`vid_main`), the 16-bit z-buffer
  (`zbuffer`), and the surface cache. It fills in Quake's global `vid` struct
  (`vid.buffer`, `vid.rowbytes`, `vid.colormap`, ...), calls `D_InitCaches`, then `qembd_vidinit()`.
- `VID_SetPalette` converts the engine's 768-byte RGB palette to 256 `0x00RRGGBB` values
  (`clut_argb8888`).
- `VID_Update(rects)` copies the `pd_dither` setting to `qembd_dither_mode`, bumps the frame
  counter `qembd_frame_no`, calls `qembd_fillrect` for each dirty rectangle, then
  `qembd_refresh`, then (with `PD_LOWRES_3D`) `D_LowresEndFrame`.
- `D_BeginDirectRect` / `D_EndDirectRect` are empty (used for the loading disc in the original).

```c
void VID_Update(vrect_t *rects)
{
	qembd_dither_mode = (int)pd_dither.value;
	qembd_frame_no++;
	while (rects) {
		qembd_fillrect(vid_buffer, clut_argb8888, rects->x, rects->y, rects->width, rects->height);
		rects = rects->pnext;
	}
	qembd_refresh();
#ifdef PD_LOWRES_3D
	D_LowresEndFrame();
#endif
}
```

`qembd_dither_mode` and `qembd_frame_no` exist for the Playdate display layer
([`display.c`](playdate.md#displayc)); another board would ignore them.

## `port/in_port.c`

`IN_Init`, `IN_Shutdown` and `IN_Commands` are empty. `IN_Move(cmd)` applies relative mouse
movement from `qembd_get_mouse_movement` to the view angles (pitch clamped to -70°…80°):

```c
movement.x *= sensitivity.value;
movement.y *= sensitivity.value;
V_StopPitchDrift();
cl.viewangles[YAW]   -= m_yaw.value   * movement.x;
cl.viewangles[PITCH] += m_pitch.value * movement.y;
```

Keyboard events never go through this file; they arrive via `Sys_SendKeyEvents`.

## `port/cd_null.c`

The `CDAudio_*` interface (`Play`, `Stop`, `Pause`, `Resume`, `Update`, `Shutdown`) as empty
functions; `CDAudio_Init` returns `0`. The Playdate game does not link it: its music is
[`port/boards/playdate/cd_pd.c`](playdate.md#cd_pdc). `tools/hostcheck/build.sh` compiles it, so the host check runs without music.

## `port/fio/fio_posix.c`

The file half of `sys.h` (`Sys_FileOpenRead`, `Sys_FileOpenWrite`, `Sys_FileClose`,
`Sys_FileSeek`, `Sys_FileRead`, `Sys_FileWrite`, `Sys_FileTime`, `Sys_mkdir`, `Sys_FileSync`,
`Sys_File_gets`) on top of `open/read/write/lseek`. Handles are real file descriptors.

```c
int Sys_FileOpenRead(char *path, int *handle)
{
	int h = open(path, O_RDONLY, 0666);
	*handle = h;
	if (h == -1)
		return -1;                 // Quake treats -1 as "not found"
	struct stat fileinfo;
	if (fstat(h, &fileinfo) == -1)
		qembd_error("Error fstating %s", path);
	return fileinfo.st_size;       // the return value is the file length
}
```

The Playdate board does not use it (see [`fio.c`](playdate.md#fioc)). It is compiled by the
[host check](../tools.md#toolshostcheckbuildsh), which runs the engine on a computer.

---

## What the Playdate board provides instead

The Playdate board supplies the rest of the platform: `fio.c` and `pd_stdio.c` (files and stdio on the Playdate file API), `snd.c` (sound on the system's
`SamplePlayer`s), `keyqueue.c` (key events), and a `display.c` that does the 1-bit conversion and dithering. See [Playdate board](playdate.md).
