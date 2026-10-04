# Other boards

[← Documentation index](../README.md) · Directory: [`port/boards/`](../../port/boards/)

This project began as [sysprog21/quake-embedded](https://github.com/sysprog21/quake-embedded),
a Quake port for several targets. The Playdate board is the one this fork develops
([documented separately](playdate.md)); the others are inherited. Each board implements the
`qembd_*` hooks from [`include/quakembd.h`](../../include/quakembd.h) (see the
[shared platform layer](overview.md#functions-a-board-must-provide)).

| Board | Target | Window / display | Keys | File I/O | Status in this tree |
| --- | --- | --- | --- | --- | --- |
| [`emulator`](#emulator) | Desktop (macOS/Linux/Windows) | MiniFB window, 800×480 | none yet | `fio_posix.c` | Not updated for the current header (see below) |
| [`rv32emu`](#rv32emu) | RISC-V RV32IMF guest in [rv32emu](https://github.com/sysprog21/rv32emu) | 640×360 via `scall` | via event queue | own `fio.c` | Matches the current header |
| [`stm32h747i_disco`](#stm32h747i_disco) | STM32H747I-DISCO (Cortex-M7, 800×480 DSI LCD) | LTDC + DSI + DMA2D | joystick, button | `fio_fatfs.c` (SD card) | Not updated for the current header (see below) |

> **Header drift.** The current `quakembd.h` uses `key_event_t { keycode, state }` and
> `qembd_get_mouse_movement(mouse_movement_t *)`. `emulator/main.c` and `stm32h747i_disco/main.c`
> still define `qembd_get_current_position(mouse_position_t *)` and (STM32) use `.code`/`.down`.
> They need that small update before they build against this tree. They are kept as references.

---

## `emulator`

[`port/boards/emulator/`](../../port/boards/emulator/): run Quake in a desktop window using the
bundled [MiniFB](https://github.com/emoon/minifb) library (`lib/minifb`, a git submodule; not
documented here).

| File | Content |
| --- | --- |
| `CMakeLists.txt` | Adds `lib/minifb`, builds the `quakembd` executable from `main.c`, `display.c`, `fio_posix.c`, links `winquake port minifb`. |
| `main.c` | `main` → `qembd_main`; `gettimeofday` for `qembd_get_us_time`; `usleep` for `qembd_udelay`; `malloc` for `qembd_allocmain`; key/mouse stubs. |
| `display.c` | 800×480 `uint32_t` buffer. `qembd_fillrect` maps palette indices through the CLUT; `qembd_refresh` calls `mfb_update`. |

```c
// emulator/display.c: the simplest possible display backend
void qembd_fillrect(uint8_t *src, uint32_t *clut, uint16_t x, uint16_t y, uint16_t xsize, uint16_t ysize)
{
	for (int py = 0; py < ysize; py++) {
		int offset = (y + py) * DISPLAY_WIDTH + x;
		for (int px = 0; px < xsize; px++)
			buffer[offset + px] = clut[src[offset + px]];   // 8-bit index → 0x00RRGGBB
	}
}
void qembd_refresh() { if (window) mfb_update(window, buffer); }
```

```shell
mkdir build && cd build
cmake -DBOARD_NAME=emulator .. && make
```

---

## `rv32emu`

[`port/boards/rv32emu/`](../../port/boards/rv32emu/): Quake compiled for 32-bit RISC-V
(`-march=rv32imf -mabi=ilp32 -Ofast -flto`, see `toolchain.cmake`) and run inside the
[rv32emu](https://github.com/sysprog21/rv32emu) emulator, which supplies graphics, input and
sound through custom `scall` system calls.

| File | Content |
| --- | --- |
| `CMakeLists.txt` | Builds the `quake` executable from `main.c`, `display.c`, `fio.c`; links `winquake port m`. |
| `toolchain.cmake` | RV32IMF flags, `--gc-sections`, `-u _printf_float -u _scanf_float`. |
| `main.c` | Shared-memory event queue (128 entries) with the emulator; key code remapping from SDL scancodes to Quake keys; mouse motion accumulation; `main` registers the queues with the host. |
| `display.c` | 640×360 buffer; `qembd_refresh` issues the `0xbeef` "draw frame" syscall. |
| `fio.c` | `Sys_File*` on `FILE*` (`fopen`/`fread`), 10 handles. |

```c
// rv32emu/display.c: hand a finished frame to the emulator
register int a0 asm("a0") = (uintptr_t) buffer;
register int a1 asm("a1") = DISPLAY_WIDTH;
register int a2 asm("a2") = DISPLAY_HEIGHT;
register int a7 asm("a7") = syscall_draw_frame;      // 0xbeef
asm volatile("scall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7));
```

```c
// rv32emu/main.c: SDL scancodes → Quake keys
case 0x40000052: e->keycode = K_UPARROW; break;
case 0x400000E0:
case 0x400000E4: e->keycode = K_CTRL;    break;
```

Sound is provided by the shared [`port/snd.c`](overview.md#portsndc), which also uses `scall`.

Known issues in `fio.c`: `Sys_FileWrite` calls `fread` instead of `fwrite`, so writing does not
work; `Sys_File_gets` passes the handle-table index to `fdopen`, which expects a file descriptor.

---

## `stm32h747i_disco`

[`port/boards/stm32h747i_disco/`](../../port/boards/stm32h747i_disco/): the STM32H747I-DISCO
discovery board (dual-core; Quake runs on the Cortex-M7), built against STM32CubeH7
(`CUBE_HOME` defaults to `~/STM32CubeH7`, verified upstream with v1.9.0). Game data is on the SD card
(FatFs); the frame buffer lives in external SDRAM at `0xD0400000`.

### Application files

| File | Content |
| --- | --- |
| `CMakeLists.txt` | Lists the Cube HAL/BSP/FatFs sources, builds `quakembd`, attaches the linker script, runs `add_embedded_binary` (→ `.bin`, `.hex`). |
| `gcc/toolchain.cmake` | `arm-none-eabi-gcc` settings (`-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard --specs=nano.specs`, `-Os`), `add_embedded_binary()` helper. |
| `main.c` | `main()`: MPU region (SDRAM write-through), I/D cache, `SystemClock_Config` (400 MHz from 25 MHz HSE via PLL), timer, LEDs, wake button and joystick EXTI, touch screen, UART (115200), SDRAM, SD card + FatFs mount, then `qembd_main`. `qembd_allocmain` returns the fixed SDRAM address. Joystick → Quake keys (select 13, up 128, down 129, left `,` right `.`). |
| `display.c` | LTDC + DSI + OTM8009A panel bring-up (PLL3, DSI PHY timers, layer config). `qembd_fillrect` lets the DMA2D engine do the palette lookup: it loads Quake's 256-entry palette as a CLUT and copies the dirty rectangle from the 8-bit (L8) buffer into the ARGB8888 LTDC frame buffer. `qembd_refresh` starts a DSI refresh and waits for the end-of-refresh callback. |
| `timer.c` / `timer.h` | `TIM7` at 1 MHz with an update interrupt each millisecond; `qembd_get_us_time()` = `tick * 1000 + counter`. `qembd_udelay` is a stub (`HAL_Delay(1)`, marked FIXME). |
| `interrupt_handlers.c` | Cortex-M fault handlers (infinite loops), `SysTick_Handler` → `HAL_IncTick`, EXTI handlers → BSP joystick/button, `SDMMC1_IRQHandler`, `MDMA_IRQHandler`. |
| `syscalls.c`, `sysmem.c` | newlib syscall stubs and `_sbrk` (generated by STM32CubeIDE). |
| `sd_diskio.h`, `sd_diskio_template_bspv2.c` | FatFs low-level SD driver (ST template for BSP v2). |
| `common.h`, `timer.h` | Shared includes, `DISPLAY_WIDTH/HEIGHT` (800×480), `error_loop()`. |

### Vendor files

These are ST-supplied and configure the Cube libraries; treat them as vendored.

| File | Content |
| --- | --- |
| `system_stm32h7xx.c` | CMSIS system init (`SystemInit`, `SystemCoreClock`). |
| `gcc/startup_stm32h747xx.s` | Reset handler and vector table. |
| `gcc/stm32h747xx_flash_cm7.ld` | Linker script: FLASH 1 MiB at `0x08000000`, RAM 512 KiB at `0x24000000`, ITCM 64 KiB, IRAM2 64 KiB. |
| `inc/stm32h7xx_hal_conf.h` | Which HAL modules are enabled. |
| `inc/ffconf.h` | FatFs configuration. |
| `inc/stm32h747i_discovery_conf.h`, `inc/ft6x06_conf.h`, `inc/is42s32800j_conf.h` | BSP, touch-controller and SDRAM configuration. |

```c
// main.c: Quake's heap is a fixed region of external SDRAM
void *qembd_allocmain(size_t size)
{
	return (void *) (0xD0400000U);
}

// main.c: joystick → Quake keys
case JOY_SEL:   bind = 13;   break;   // Enter
case JOY_UP:    bind = 128;  break;   // K_UPARROW
case JOY_LEFT:  bind = 0x2c; break;   // ','
```

```shell
cmake -DBOARD_NAME=stm32h747i_disco \
      -DCMAKE_TOOLCHAIN_FILE=port/boards/stm32h747i_disco/gcc/toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release ..
make          # → quakembd.bin / quakembd.hex
```
