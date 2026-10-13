# Writes build_info.cpp with "<git hash>[+] <HH:MM>" so every screenshot or
# photo from the PSP tells exactly which build is running.
# Invoked at build time: cmake -DSRC=<repo> -DOUT=<file> -P build_info.cmake
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY ${SRC}
                OUTPUT_VARIABLE hash OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git status --porcelain --untracked-files=no WORKING_DIRECTORY ${SRC}
                OUTPUT_VARIABLE dirty OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT hash)
    set(hash "nogit")
endif()
if(dirty)
    set(hash "${hash}+")  # uncommitted changes
endif()
string(TIMESTAMP stamp "%H:%M")
file(WRITE ${OUT}.tmp "namespace core { extern const char* const kBuildId; const char* const kBuildId = \"${hash} ${stamp}\"; }\n")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT}.tmp ${OUT})
