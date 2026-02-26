cmake_minimum_required(VERSION 3.22)
project(MoriaCppModTests CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest
    GIT_TAG v1.15.2
)
# Prevent GoogleTest from overriding our compiler/linker options on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(MoriaCppModTests
    test_loc.cpp
    test_string_helpers.cpp
    test_key_helpers.cpp
    test_file_io.cpp
    test_memory.cpp
)

target_include_directories(MoriaCppModTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(MoriaCppModTests PRIVATE GTest::gtest_main)
target_compile_options(MoriaCppModTests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

include(GoogleTest)
gtest_discover_tests(MoriaCppModTests)
