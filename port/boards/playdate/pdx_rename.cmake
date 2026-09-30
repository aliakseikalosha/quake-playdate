# Gives a built .pdx its own title in the Playdate launcher. The game and the profiling builds are
# made from the same Source/pdxinfo, so without this they look alike on the device.
#
#   cmake -DPDXINFO=<game>.pdx/pdxinfo -DTAGS="profile, demo1" -P pdx_rename.cmake
#
# appends " (TAGS)" to the name= line; running it again on the same file changes nothing.
if(NOT PDXINFO OR NOT TAGS)
	message(FATAL_ERROR "usage: cmake -DPDXINFO=<file> -DTAGS=<text> -P pdx_rename.cmake")
endif()

file(READ "${PDXINFO}" info)
string(FIND "${info}" " (${TAGS})" already)
if(already EQUAL -1)
	string(REGEX REPLACE "(^|\n)name=([^\n]*)" "\\1name=\\2 (${TAGS})" info "${info}")
	file(WRITE "${PDXINFO}" "${info}")
endif()
