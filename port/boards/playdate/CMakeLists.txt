set(PLAYDATE_GAME_NAME quake)
set(PLAYDATE_GAME_DEVICE quake_DEVICE)

set(PD_SOURCES
	main.c
	keyqueue.c
	autofire.c
	weapons.c
	display.c
	fio.c
	pd_stdio.c
	snd.c
)

if(PD_PROFILE)
	list(APPEND PD_SOURCES pdprof.c)
endif()

if(TOOLCHAIN STREQUAL "armgcc")
	add_executable(${PLAYDATE_GAME_DEVICE} ${PD_SOURCES})
	target_link_libraries(${PLAYDATE_GAME_DEVICE} winquake port)
else()
	add_library(${PLAYDATE_GAME_NAME} SHARED ${PD_SOURCES})
	target_link_libraries(${PLAYDATE_GAME_NAME} winquake port)
endif()

# Put the .pdx in the build tree instead of next to the sources.
set(PRODUCT_DIR ${CMAKE_BINARY_DIR} CACHE STRING "Built PDX directory" FORCE)
include(${SDK}/C_API/buildsupport/playdate_game.cmake)

# Every build gets the next build number in its .pdx (pdx_buildnumber.cmake; the counter is
# port/boards/playdate/.build_number, shared by all build directories and not in git).
if(TOOLCHAIN STREQUAL "armgcc")
	set(_pd_target ${PLAYDATE_GAME_DEVICE})
else()
	set(_pd_target ${PLAYDATE_GAME_NAME})
endif()
add_custom_command(
	TARGET ${_pd_target} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -DPDXINFO=${PRODUCT_DIR}/${_pd_target}.pdx/pdxinfo
		-DSOURCE=${CMAKE_CURRENT_LIST_DIR}/Source/pdxinfo
		-DCOUNTER=${CMAKE_CURRENT_LIST_DIR}/.build_number
		-P ${CMAKE_CURRENT_LIST_DIR}/pdx_buildnumber.cmake
	VERBATIM
)

# A profiling build gets its own title in the launcher, e.g. "Quake Playdate (profile, demo1,
# stack A/B)", so it is not mistaken for the game on the device (scripts/pd-bench.sh installs it
# next to the game as Games/quake_PROF.pdx).
if(PD_PROFILE AND TOOLCHAIN STREQUAL "armgcc")
	set(_pd_tags "profile")
	if(PD_BENCH)
		math(EXPR _pd_last "${PD_BENCH_FIRST} + ${PD_BENCH_COUNT} - 1")
		if(_pd_last EQUAL PD_BENCH_FIRST)
			string(APPEND _pd_tags ", demo${PD_BENCH_FIRST}")
		else()
			string(APPEND _pd_tags ", demos ${PD_BENCH_FIRST}-${_pd_last}")
		endif()
	endif()
	if(PD_PROFILE_FINE)
		string(APPEND _pd_tags ", fine")
	endif()
	if(PD_ASM_BUILD AND PD_ASM_AB)
		string(APPEND _pd_tags ", asm A/B")
	endif()
	if(PD_ASM_BUILD AND PD_ASM_CHECK)
		string(APPEND _pd_tags ", asm check")
	endif()
	if(PD_STACK AND PD_STACK_AB)
		string(APPEND _pd_tags ", stack A/B")
	endif()
	add_custom_command(
		TARGET ${PLAYDATE_GAME_DEVICE} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -DPDXINFO=${PRODUCT_DIR}/${PLAYDATE_GAME_DEVICE}.pdx/pdxinfo
			"-DTAGS=${_pd_tags}" -P ${CMAKE_CURRENT_LIST_DIR}/pdx_rename.cmake
		VERBATIM
	)
endif()
