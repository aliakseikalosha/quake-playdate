# Chooses which game data ends up in a built .pdx.
#
#   cmake -DPDX=<game>.pdx -DRELEASE=<ON|OFF> -P pdx_pak.cmake
#
# Runs after pdc (POST_BUILD), which bundles everything in Source/, so Source/id1/ can hold both the
# full pak0.pak and the shareware pak0_demo.pak:
#   RELEASE on   pak0_demo.pak, if there is one, becomes id1/pak0.pak in the .pdx (replacing any
#                pak0.pak) and id1/music/ is left out of it (the shareware has no music, and the
#                re-release's tracks are not redistributable); without a pak0_demo.pak the .pdx keeps
#                whatever pak0.pak and music Source/id1/ has.
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
else()
	file(REMOVE "${demo}")
	message(STATUS "pak0_demo.pak left out of the .pdx")
endif()
