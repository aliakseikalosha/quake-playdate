set(WQ_SRCS
	chase.c
	cmd.c
	common.c
	console.c
	crc.c
	cvar.c
	draw.c
	host.c
	host_cmd.c
	keys.c
	mathlib.c
	menu.c
	model.c
	nonintel.c
	screen.c
	sbar.c
	zone.c
	view.c
	wad.c
	world.c
	cl_demo.c
	cl_input.c
	cl_main.c
	cl_parse.c
	cl_tent.c
	net_loop.c
	net_main.c
	pr_cmds.c
	pr_edict.c
	pr_exec.c
	r_efrag.c
	r_part.c
	sv_main.c
	sv_phys.c
	sv_move.c
	sv_user.c
)

# The 3D renderer (see PD_NEW_RENDERER in port/boards/playdate/platform.cmake)
if(PD_NEW_RENDERER)
	list(APPEND WQ_SRCS
		pdr_main.c
		pdr_world.c
		pdr_span.c
		pdr_light.c
		pdr_alias.c
		pdr_sprite.c
		pdr_lowres.c
	)
else()
	list(APPEND WQ_SRCS
		d_edge.c
		d_fill.c
		d_init.c
		d_modech.c
		d_part.c
		d_polyse.c
		d_scan.c
		d_sky.c
		d_sprite.c
		d_surf.c
		d_vars.c
		d_zpoint.c
		r_aclip.c
		r_alias.c
		r_bsp.c
		r_light.c
		r_draw.c
		r_edge.c
		r_misc.c
		r_main.c
		r_sky.c
		r_sprite.c
		r_surf.c
		r_vars.c
	)
endif()

# Always warn for unnecessary use of double-precision.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang")
	list(APPEND OPTS
		-Wdouble-promotion
	)
endif()

if(CMAKE_SYSTEM_NAME MATCHES "(Darwin|Linux)" AND NOT BOARD_NAME STREQUAL "playdate")
	list(APPEND WQ_SRCS
		net_dgrm.c
		net_udp.c
		net_bsd.c
	)
else()
	list(APPEND WQ_SRCS
		net_none.c
	)
endif()

# Thumb-2 versions of hot loops for the Playdate (see pd_asm.h)
if(PD_ASM_BUILD)
	list(APPEND WQ_ASM_SRCS
		d_scan_arm.S
		r_edge_arm.S
	)
endif()

add_library(winquake OBJECT ${WQ_SRCS} ${WQ_ASM_SRCS})

target_compile_options(winquake PRIVATE ${OPTS})
target_include_directories(winquake PUBLIC
	${PROJECT_SOURCE_DIR}/winquake
)
