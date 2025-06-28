#
# CMakeLists.txt Copyright 2025 ItJustWorksTM
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the
# specific language governing permissions and limitations under the License.
#

cmake_minimum_required (VERSION 3.20)

project (fastipc)

include (CMake/Modules/Common.cmake)

add_library (fastipc STATIC)
target_include_directories (fastipc PUBLIC include)
target_sources (
    fastipc
    PRIVATE include/fastipc.hxx
            src/io/result.hxx
            src/io/fd.hxx
            src/io/addr.hxx
            src/io/addr.cxx
            src/io/endian.hxx
            src/io/polled_io.hxx
            src/io/reactor.hxx
            src/io/reactor.cxx
            src/io/io_env.hxx
            src/io/context.hxx
            src/co/scheduler.hxx
            src/co/coroutine.hxx
            src/co/task.hxx
            src/fastipc.cxx
            src/visitor.hxx
            src/channel.hxx
)
if (NOT DEFINED CMAKE_CXX_CLANG_TIDY OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    target_precompile_headers (fastipc PUBLIC include/fastipc.hxx)
endif ()

target_compile_features (fastipc INTERFACE cxx_std_17)
target_compile_features (fastipc PRIVATE cxx_std_23)
target_compile_options (fastipc PRIVATE ${FASTIPC_COMPILE_OPTIONS} -Wno-zero-length-array)

add_library (tower OBJECT)
target_include_directories (tower PUBLIC src)
target_sources (tower PRIVATE src/tower.hxx src/tower.cxx)
target_compile_features (tower PUBLIC cxx_std_23)
target_compile_options (tower PRIVATE ${FASTIPC_COMPILE_OPTIONS} "-Wno-zero-length-array")
target_link_libraries (tower PRIVATE fastipc)

add_executable (fastipcd)
target_sources (fastipcd PRIVATE src/main.cxx)
target_compile_options (fastipcd PRIVATE ${FASTIPC_COMPILE_OPTIONS})
target_link_libraries (fastipcd PRIVATE tower)

enable_testing ()
add_subdirectory (test)
