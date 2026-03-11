cmake_minimum_required(VERSION 3.16)
project(react_cpp_demo LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Skia + SDL3 Demo
add_executable(react_cpp_sdl_skia_demo
    src/skia_runtime.hpp
    src/skia_runtime.cpp
    src/skia_demo_main.cpp
)

# SDL discovery: pkg-config first, then CMake package targets
set(REACTCPP_SDL_FOUND OFF)
set(REACTCPP_SDL_INCLUDE_DIRS "")
set(REACTCPP_SDL_CFLAGS_OTHER "")
set(REACTCPP_SDL_LIBRARIES "")
set(REACTCPP_SDL_IMPORTED_TARGET "")

find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
    pkg_check_modules(SDL3 QUIET sdl3)
    if (SDL3_FOUND)
        set(REACTCPP_SDL_FOUND ON)
        set(REACTCPP_SDL_INCLUDE_DIRS ${SDL3_INCLUDE_DIRS})
        set(REACTCPP_SDL_CFLAGS_OTHER ${SDL3_CFLAGS_OTHER})
        set(REACTCPP_SDL_LIBRARIES ${SDL3_LIBRARIES})
    else()
        pkg_check_modules(SDL2 QUIET sdl2)
        if (SDL2_FOUND)
            set(REACTCPP_SDL_FOUND ON)
            set(REACTCPP_SDL_INCLUDE_DIRS ${SDL2_INCLUDE_DIRS})
            set(REACTCPP_SDL_CFLAGS_OTHER ${SDL2_CFLAGS_OTHER})
            set(REACTCPP_SDL_LIBRARIES ${SDL2_LIBRARIES})
        endif()
    endif()
endif()

if (NOT REACTCPP_SDL_FOUND)
    find_package(SDL3 QUIET CONFIG)
    if (TARGET SDL3::SDL3)
        set(REACTCPP_SDL_FOUND ON)
        set(REACTCPP_SDL_IMPORTED_TARGET SDL3::SDL3)
    endif()
endif()

if (NOT REACTCPP_SDL_FOUND)
    find_package(SDL2 QUIET CONFIG)
    if (TARGET SDL2::SDL2)
        set(REACTCPP_SDL_FOUND ON)
        set(REACTCPP_SDL_IMPORTED_TARGET SDL2::SDL2)
    endif()
endif()

if (NOT REACTCPP_SDL_FOUND)
    # Manual fallback (common on distros without pkg-config .pc files)
    find_path(SDL2_INCLUDE_DIR NAMES SDL2/SDL.h)
    find_library(SDL2_LIBRARY NAMES SDL2 SDL2-2.0)
    if (SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
        set(REACTCPP_SDL_FOUND ON)
        set(REACTCPP_SDL_INCLUDE_DIRS "${SDL2_INCLUDE_DIR}")
        set(REACTCPP_SDL_LIBRARIES "${SDL2_LIBRARY}")
    endif()
endif()

if (NOT REACTCPP_SDL_FOUND)
    message(FATAL_ERROR "Neither SDL3 nor SDL2 was found (tried pkg-config, CMake package config, and manual find_path/find_library). Install SDL2 development headers/libs or set CMAKE_PREFIX_PATH/PKG_CONFIG_PATH accordingly.")
endif()

if (REACTCPP_SDL_INCLUDE_DIRS)
    target_include_directories(react_cpp_sdl_skia_demo
        PRIVATE
            ${REACTCPP_SDL_INCLUDE_DIRS}
    )
endif()

if (REACTCPP_SDL_CFLAGS_OTHER)
    target_compile_options(react_cpp_sdl_skia_demo
        PRIVATE
            ${REACTCPP_SDL_CFLAGS_OTHER}
    )
endif()

if (REACTCPP_SDL_IMPORTED_TARGET)
    target_link_libraries(react_cpp_sdl_skia_demo
        PRIVATE
            ${REACTCPP_SDL_IMPORTED_TARGET}
    )
else()
    target_link_libraries(react_cpp_sdl_skia_demo
        PRIVATE
            ${REACTCPP_SDL_LIBRARIES}
    )
endif()

# Skia: headers + static library
set(SKIA_BUNDLE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/skia")
set(SKIA_HEADERS_ROOT "${SKIA_BUNDLE_ROOT}" CACHE PATH "Path to Skia source tree containing include/ and modules/")
set(SKIA_LIB_PATH "${SKIA_BUNDLE_ROOT}/libskia.a" CACHE FILEPATH "Path to Skia static library (libskia.a)")

if (NOT EXISTS "${SKIA_LIB_PATH}")
    message(FATAL_ERROR "Skia static library not found at ${SKIA_LIB_PATH}")
endif()

if (NOT EXISTS "${SKIA_HEADERS_ROOT}/include/core/SkCanvas.h")
    message(FATAL_ERROR "Skia headers not found: expected ${SKIA_HEADERS_ROOT}/include/core/SkCanvas.h")
endif()

if (NOT EXISTS "${SKIA_HEADERS_ROOT}/modules/skparagraph/include/Paragraph.h")
    message(FATAL_ERROR "SkParagraph headers not found: expected ${SKIA_HEADERS_ROOT}/modules/skparagraph/include/Paragraph.h")
endif()

target_include_directories(react_cpp_sdl_skia_demo
    PRIVATE
        "${SKIA_HEADERS_ROOT}/include"  # for core/SkCanvas.h
        "${SKIA_HEADERS_ROOT}"          # for modules/skparagraph/...
)

target_link_libraries(react_cpp_sdl_skia_demo
    PRIVATE
        "${SKIA_LIB_PATH}"
)

# Some Skia builds require skcms as a separate static lib.
if (EXISTS "${SKIA_BUNDLE_ROOT}/libskcms.a")
    target_link_libraries(react_cpp_sdl_skia_demo
        PRIVATE
            "${SKIA_BUNDLE_ROOT}/libskcms.a"
    )
endif()

find_package(Threads REQUIRED)

target_link_libraries(react_cpp_sdl_skia_demo
    PRIVATE
        Threads::Threads
)
