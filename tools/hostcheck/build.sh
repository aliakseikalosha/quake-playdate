#!/bin/bash
# Builds tools/hostcheck/hostcheck.c against a source tree. Needs clang and the Playdate SDK
# (PLAYDATE_SDK_PATH) for pd_api.h only.
#
#   tools/hostcheck/build.sh                 build against this repo -> tools/hostcheck/out/hostcheck
#   TREE=/path/to/other/checkout OUT=ref tools/hostcheck/build.sh
#   NO_LAZY_CHECK=1 ...                      for trees that predate the lazy low-res upscale
#   NO_FAST_ALIAS=1 / NO_FAST_FACES=1 / NO_FAST_SURFACES=1   build the original alias rasterizer / world face code / surface builder
#   NO_STACK=1                               keep the scratch buffers in static memory (see winquake/pd_stack.h)
#   NEW=1                                    build the new renderer (winquake/pdr_*.c, PD_NEW_RENDERER) instead of r_*/d_*
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=${TREE:-$(cd "$HERE/../.." && pwd)}
OUT=${OUT:-hostcheck}
SDK=${PLAYDATE_SDK_PATH:-$(egrep '^\s*SDKRoot' "$HOME/.Playdate/config" | head -n 1 | cut -c9-)}
[ -f "$SDK/C_API/pd_api.h" ] || { echo "Playdate SDK not found; set PLAYDATE_SDK_PATH"; exit 1; }
O=$HERE/out/obj_$OUT
rm -rf "$O"; mkdir -p "$O"

DEFS=(-DWINQUAKE_ENABLE_LOGGING -DWINQUAKE_LOGGING_EXTERNAL -DQEMBD_PLAYDATE=1 -DTARGET_SIMULATOR=1
      -DTARGET_EXTENSION=1 -DPD_LOWRES_3D=1 -DPD_RENDER_WIDTH=400 -DPD_RENDER_HEIGHT=240
      -DPD_REFRESH_RATE=30 "-DDEFAULT_MEM_SIZE=(7*1024*1024)" "-DDEFAULT_MIN_MEM_SIZE=(4*1024*1024)")
grep -q "PD_FAST_EDGES" "$TREE/winquake/r_edge.c" 2>/dev/null && DEFS+=(-DPD_FAST_EDGES=1)
[ -z "$NO_FAST_ALIAS" ] && grep -q "PD_FAST_ALIAS" "$TREE/winquake/d_polyse.c" 2>/dev/null && DEFS+=(-DPD_FAST_ALIAS=1)
[ -z "$NO_FAST_FACES" ] && grep -q "PD_FAST_FACES" "$TREE/winquake/r_draw.c" 2>/dev/null && DEFS+=(-DPD_FAST_FACES=1)
[ -z "$NO_FAST_SURFACES" ] && grep -q "PD_FAST_SURFACES" "$TREE/winquake/r_surf.c" 2>/dev/null && DEFS+=(-DPD_FAST_SURFACES=1)
# stack buffers (pd_stack.h) with a budget big enough for the host's larger frames, so they are used
[ -z "$NO_STACK" ] && [ -f "$TREE/winquake/pd_stack.h" ] && DEFS+=(-DPD_STACK=1 "-DPD_STACK_BUDGET=(64*1024)")
[ -n "$NO_LAZY_CHECK" ] && DEFS+=(-DNO_LAZY_CHECK=1)
[ -n "$EXTRA_DEFS" ] && DEFS+=($EXTRA_DEFS)
INC=(-I"$TREE/include" -I"$TREE/winquake" -I"$TREE/port/boards/playdate" -I"$SDK/C_API" -I"$HERE")
CFLAGS=(${OPT:--O1} -g -w -fno-common -fcommon "${DEFS[@]}" "${INC[@]}")

WQ="chase cmd common console crc cvar draw host host_cmd keys mathlib menu model nonintel screen sbar zone view wad world
    cl_demo cl_input cl_main cl_parse cl_tent net_loop net_main pr_cmds pr_edict pr_exec r_efrag r_part
    sv_main sv_phys sv_move sv_user net_none"
if [ -n "$NEW" ]; then
  WQ="$WQ pdr_main pdr_world pdr_span pdr_light pdr_alias pdr_sprite pdr_lowres"
  DEFS+=(-DPD_NEW_RENDERER=1)
  CFLAGS=(${OPT:--O1} -g -w -fno-common -fcommon "${DEFS[@]}" "${INC[@]}")
else
  WQ="$WQ d_edge d_fill d_init d_modech d_part d_polyse d_scan d_sky d_sprite d_surf d_vars d_zpoint
    r_aclip r_alias r_bsp r_light r_draw r_edge r_misc r_main r_sky r_sprite r_surf r_vars"
fi
pids=()
for f in $WQ; do
  extra=()
  if [ -z "$NO_LAZY_CHECK" ] && { [ $f = d_scan ] || [ $f = pdr_lowres ]; }; then extra=(-DD_UpscaleScreen=real_D_UpscaleScreen); fi
  clang "${CFLAGS[@]}" "${extra[@]}" -c "$TREE/winquake/$f.c" -o "$O/$f.o" & pids+=($!)
done
for f in sys_port vid_port in_port cd_null; do clang "${CFLAGS[@]}" -c "$TREE/port/$f.c" -o "$O/$f.o" & pids+=($!); done
clang "${CFLAGS[@]}" -c "$TREE/port/fio/fio_posix.c" -o "$O/fio_posix.o" & pids+=($!)

# display.c as tree_* ; and, for the lazy-upscale check, a copy that always dithers the expanded buffer as old_*
ren() { echo "-Dqembd_fillrect=$1_fillrect -Dqembd_vidinit=$1_vidinit -Dqembd_get_width=$1_get_width -Dqembd_get_height=$1_get_height -Dqembd_refresh=$1_refresh -Dqembd_display_invalidate=$1_display_invalidate"; }
clang "${CFLAGS[@]}" $(ren tree) -c "$TREE/port/boards/playdate/display.c" -o "$O/display_tree.o" & pids+=($!)
if [ -z "$NO_LAZY_CHECK" ]; then
  python3 - "$TREE/port/boards/playdate/display.c" "$O/display_old.c" <<'PY'
import sys
s = open(sys.argv[1]).read()
a = "\t\t\tif (qembd_lowres_active && qembd_lowres_pending[qy >> 1]"
i = s.index(a)
j = s.index("\t\t\telse\n\t\t\t\tlowres_pair(", i)
s = s[:i] + "\t\t\t" + s[j + len("\t\t\telse\n\t\t\t\t"):]
# the interlace bookkeeping (which LCD rows still show their row) belongs to the tree copy only
e = "extern uint8_t qembd_lowres_shown[];"
if e in s:
    s = s.replace(e, "static uint8_t old_lowres_shown[PD_RENDER_HEIGHT / 2];").replace("qembd_lowres_shown", "old_lowres_shown")
open(sys.argv[2], "w").write(s)
PY
  clang "${CFLAGS[@]}" $(ren old) -c "$O/display_old.c" -o "$O/display_old.o" & pids+=($!)
fi
clang "${CFLAGS[@]}" -c "$HERE/hostcheck.c" -o "$O/hostcheck.o" & pids+=($!)
for p in "${pids[@]}"; do wait $p || exit 1; done
clang -o "$HERE/out/$OUT" "$O"/*.o -lm
echo "built $HERE/out/$OUT"
