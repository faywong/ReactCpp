cmake_minimum_required(VERSION 3.16)
project(react_cpp_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 控制台 PoC
add_executable(react_cpp_demo
    src/main.cpp
    src/runtime.hpp
    src/runtime.cpp
)

# Skia + SDL2 Demo
add_executable(react_cpp_sdl_skia_demo
    src/skia_runtime.hpp
    src/skia_runtime.cpp
    src/skia_demo_main.cpp
)

# SDL2 via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

target_include_directories(react_cpp_sdl_skia_demo
    PRIVATE
        ${SDL2_INCLUDE_DIRS}
)

target_compile_definitions(react_cpp_sdl_skia_demo
    PRIVATE
        ${SDL2_CFLAGS_OTHER}
)

target_link_libraries(react_cpp_sdl_skia_demo
    PRIVATE
        ${SDL2_LIBRARIES}
)

# Skia 静态库
set(SKIA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/skia")
set(SKIA_EXT_INCLUDE "/home/faywong/repo/skia/include")

# Skia headers from external path + local skia
target_include_directories(react_cpp_sdl_skia_demo
    PRIVATE
        "${SKIA_EXT_INCLUDE}"
)

# Link local CPU-only Skia static library
# (assumes libskia.a is built with compatible settings)
target_link_libraries(react_cpp_sdl_skia_demo
    PRIVATE
        "${SKIA_ROOT}/libskia.a"
)

find_package(Threads REQUIRED)

target_link_libraries(react_cpp_sdl_skia_demo
    PRIVATE
        Threads::Threads
)
