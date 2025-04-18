cmake_minimum_required (VERSION 3.20)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS OFF)

project (fastipc)

add_library (fastipc STATIC)
target_include_directories (fastipc PUBLIC include)
target_sources (fastipc PRIVATE
    include/fastipc.hxx
    include/io/result.hxx
    include/io/fd.hxx
    src/fastipc.cxx
    src/channel.hxx
)

add_executable (tower src/tower.cxx)
target_link_libraries (tower PRIVATE fastipc)

add_subdirectory (test)
