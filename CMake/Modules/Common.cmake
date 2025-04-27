set (FASTIPC_COMPILE_OPTIONS "-Wall" "-Wextra" "-Wpedantic" "-Werror" "-Wcast-align")

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    list (APPEND FASTIPC_COMPILE_OPTIONS "-fcolor-diagnostics")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    list (APPEND FASTIPC_COMPILE_OPTIONS "-fdiagnostics-color=always")
endif ()
