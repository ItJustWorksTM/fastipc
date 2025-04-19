
add_executable (writer writer.cxx)
target_compile_features (writer PRIVATE cxx_std_23)
target_compile_options (writer PRIVATE "-Wall" "-Wextra" "-Wpedantic" "-Werror" "-Wcast-align")
target_link_libraries (writer PRIVATE fastipc)

add_executable (reader reader.cxx)
target_compile_features (reader PRIVATE cxx_std_23)
target_compile_options (reader PRIVATE "-Wall" "-Wextra" "-Wpedantic" "-Werror" "-Wcast-align")
target_link_libraries (reader PRIVATE fastipc)
