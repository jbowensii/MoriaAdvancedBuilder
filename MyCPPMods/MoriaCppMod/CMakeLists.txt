cmake_minimum_required(VERSION 3.22)

set(TARGET MoriaCppMod)
project(${TARGET})

add_library(${TARGET} SHARED
    "src/dllmain.cpp"
    "src/moria_overlay.cpp"
)
target_include_directories(${TARGET} PRIVATE . src)
target_link_libraries(${TARGET} PRIVATE UE4SS)
target_compile_options(${TARGET} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
