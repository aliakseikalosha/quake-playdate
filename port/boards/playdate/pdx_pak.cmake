# Chooses which game data ends up in a built .pdx.
#
#   cmake -DPDX=<game>.pdx -DRELEASE=<ON|OFF> -P pdx_pak.cmake
#
# Runs after pdc (POST_BUILD), which bundles everything in Source/, so Source/id1/ can hold both the
# full pak0.pak and the shareware pak0_demo.pak:
#   RELEASE on   pak0_demo.pak, if there is one, becomes id1/pak0.pak in the .pdx (replacing any
#                pak0.pak) and id1/music/ is left out of it (the shareware has no music, and the
#                re-release's tracks are not redistributable), and so is every other game folder
#                (mods and mission packs in Source/: the shareware does not run mods, and the
#                mission packs are not redistributable either); without a pak0_demo.pak the .pdx
#                keeps whatever pak0.pak, music and game folders Source/ has.
#   RELEASE off  pak0_demo.pak is dropped from the .pdx, so only pak0.pak ships.
# Source/id1/ itself is never touched.
if(NOT PDX)
	message(FATAL_ERROR "usage: cmake -DPDX=<dir> [-DRELEASE=ON] -P pdx_pak.cmake")
endif()

set(id1 "${PDX}/id1")
set(demo "${id1}/pak0_demo.pak")

if(NOT EXISTS "${demo}")
	if(RELEASE)
		message(STATUS "release: no pak0_demo.pak, keeping id1/pak0.pak")
	endif()
	return()
endif()

if(RELEASE)
	file(REMOVE "${id1}/pak0.pak")
	file(RENAME "${demo}" "${id1}/pak0.pak")
	message(STATUS "release: pak0_demo.pak is id1/pak0.pak")
	file(REMOVE_RECURSE "${id1}/music")
	message(STATUS "release: id1/music left out of the .pdx")
	# game folders as the Mods menu sees them (winquake/menu.c, M_ModsCheckEntry)
	file(GLOB entries LIST_DIRECTORIES true "${PDX}/*")
	foreach(dir ${entries})
		get_filename_component(name "${dir}" NAME)
		# (by path: in this script's policy a quoted "id1" would read the variable id1)
		if(NOT IS_DIRECTORY "${dir}" OR "${dir}" STREQUAL "${id1}")
			continue()
		endif()
		file(GLOB game_files "${dir}/*.pak" "${dir}/progs.dat" "${dir}/maps" "${dir}/progs" "${dir}/gfx" "${dir}/sound")
		if(game_files)
			file(REMOVE_RECURSE "${dir}")
			message(STATUS "release: ${name}/ left out of the .pdx")
		endif()
	endforeach()
else()
	file(REMOVE "${demo}")
	message(STATUS "pak0_demo.pak left out of the .pdx")
endif()
