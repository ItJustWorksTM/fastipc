cmake_minimum_required (VERSION 3.20)

project (fastipc)

add_library (fastipc STATIC)
target_include_directories (fastipc PUBLIC include)
target_sources (fastipc PRIVATE
    include/fastipc.hxx
    src/io/result.hxx
    src/io/fd.hxx
    src/fastipc.cxx
    src/channel.hxx
)
target_precompile_headers (fastipc PUBLIC include/fastipc.hxx)
target_compile_features (fastipc INTERFACE cxx_std_17)
target_compile_features (fastipc PRIVATE cxx_std_23)
target_compile_options (fastipc PRIVATE "-Wall" "-Wextra" "-Werror" "-Wcast-align" "-Wno-zero-length-array")

add_executable (tower)
target_sources (tower PRIVATE src/tower.cxx)
target_compile_features (tower PUBLIC cxx_std_23)
target_compile_options (tower PRIVATE "-Wall" "-Wextra" "-Werror" "-Wcast-align" "-Wno-zero-length-array")
target_link_libraries (tower PRIVATE fastipc)

add_subdirectory (test)
