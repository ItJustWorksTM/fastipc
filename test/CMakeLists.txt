
add_executable (writer writer.cxx)
target_link_libraries (writer PRIVATE fastipc)

add_executable (reader reader.cxx)
target_link_libraries (reader PRIVATE fastipc)
