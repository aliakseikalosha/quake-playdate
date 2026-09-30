# Gives every build of the game its own buildNumber in the built .pdx's pdxinfo.
#
#   cmake -DPDXINFO=<game>.pdx/pdxinfo -DSOURCE=<Source/pdxinfo> -DCOUNTER=<file> -P pdx_buildnumber.cmake
#
# Runs after pdc (POST_BUILD, so only when the game was actually rebuilt). COUNTER holds the last
# number handed out; it is shared by every build directory (device, simulator, profiling) and is
# not in git. The next number is one more than the larger of the counter and the buildNumber in
# SOURCE, so raising buildNumber in Source/pdxinfo (e.g. for a release) moves the count up.
if(NOT PDXINFO OR NOT SOURCE OR NOT COUNTER)
	message(FATAL_ERROR "usage: cmake -DPDXINFO=<file> -DSOURCE=<file> -DCOUNTER=<file> -P pdx_buildnumber.cmake")
endif()

set(last 0)
if(EXISTS "${COUNTER}")
	file(STRINGS "${COUNTER}" last LIMIT_COUNT 1 REGEX "^[0-9]+$")
	if(NOT last)
		set(last 0)
	endif()
endif()

file(STRINGS "${SOURCE}" base REGEX "^buildNumber=[0-9]+$")
string(REGEX REPLACE "^buildNumber=" "" base "${base}")
if(base AND base GREATER last)
	set(last ${base})
endif()

math(EXPR next "${last} + 1")
file(WRITE "${COUNTER}" "${next}\n")

file(READ "${PDXINFO}" info)
if(info MATCHES "(^|\n)buildNumber=")
	string(REGEX REPLACE "(^|\n)buildNumber=[^\n]*" "\\1buildNumber=${next}" info "${info}")
else()
	string(REGEX REPLACE "\n?$" "\nbuildNumber=${next}\n" info "${info}")
endif()
file(WRITE "${PDXINFO}" "${info}")
message(STATUS "pdxinfo buildNumber=${next}")
