cmake_minimum_required(VERSION 3.16)
project(react_cpp_demo LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

include(FetchContent)

option(REACTCPP_BUILD_DEMO "Build SDL+Skia demo (requires external skia-builder)" ON)

set(SKIA_BUILDER_ROOT_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/../skia-builder")
set(SKIA_BUILDER_ROOT "${SKIA_BUILDER_ROOT_DEFAULT}" CACHE PATH "Path to skia-builder directory")

if (REACTCPP_BUILD_DEMO AND NOT EXISTS "${SKIA_BUILDER_ROOT}")
    message(WARNING "skia-builder not found at ${SKIA_BUILDER_ROOT}; demo target will be skipped")
    set(REACTCPP_BUILD_DEMO OFF)
endif()

add_executable(react_cpp_text_edit_tests
    tests/text_edit_tests.cpp
)

target_include_directories(react_cpp_text_edit_tests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_test(NAME text_edit_tests COMMAND react_cpp_text_edit_tests)

add_executable(react_cpp_text_wrap_tests
    tests/text_wrap_tests.cpp
)

target_include_directories(react_cpp_text_wrap_tests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_test(NAME text_wrap_tests COMMAND react_cpp_text_wrap_tests)

if (REACTCPP_BUILD_DEMO)
    add_executable(reactcpp_demo
        src/skia_runtime.hpp
        src/skia_runtime.cpp
        src/main.cpp
    )

    # Rendering backends:
    # - Ganesh GL is always attempted first and is not user-disableable.
    # - If GL init fails at runtime, we fall back to CPU raster with a loud warning.
    find_package(OpenGL REQUIRED)
    target_link_libraries(reactcpp_demo PRIVATE OpenGL::GL)

    add_executable(react_cpp_skia_runtime_input_area_enter_tests
        tests/skia_runtime_input_area_enter_tests.cpp
    )

    # This test includes src/skia_runtime.cpp directly and therefore needs GL symbols.
    target_link_libraries(react_cpp_skia_runtime_input_area_enter_tests PRIVATE OpenGL::GL)

    target_include_directories(react_cpp_skia_runtime_input_area_enter_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )

    add_test(NAME skia_runtime_input_area_enter_tests COMMAND react_cpp_skia_runtime_input_area_enter_tests)



    # Yoga (Flexbox layout)
    FetchContent_Declare(
        yoga
        GIT_REPOSITORY https://github.com/facebook/yoga.git
        GIT_TAG v3.2.1
        GIT_SHALLOW TRUE
    )
FetchContent_GetProperties(yoga)
if (NOT yoga_POPULATED)
    FetchContent_Populate(yoga)
endif()

# Yoga's root CMakeLists always builds tests; add only the core subdir.
add_subdirectory("${yoga_SOURCE_DIR}/yoga" "${yoga_BINARY_DIR}/yoga")

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
    target_include_directories(reactcpp_demo
        PRIVATE
            ${REACTCPP_SDL_INCLUDE_DIRS}
    )

    target_include_directories(react_cpp_skia_runtime_input_area_enter_tests
        PRIVATE
            ${REACTCPP_SDL_INCLUDE_DIRS}
    )
endif()

if (REACTCPP_SDL_CFLAGS_OTHER)
    target_compile_options(reactcpp_demo
        PRIVATE
            ${REACTCPP_SDL_CFLAGS_OTHER}
    )

    target_compile_options(react_cpp_skia_runtime_input_area_enter_tests
        PRIVATE
            ${REACTCPP_SDL_CFLAGS_OTHER}
    )
endif()

if (REACTCPP_SDL_IMPORTED_TARGET)
    target_link_libraries(reactcpp_demo
        PRIVATE
            ${REACTCPP_SDL_IMPORTED_TARGET}
    )

    target_link_libraries(react_cpp_skia_runtime_input_area_enter_tests
        PRIVATE
            ${REACTCPP_SDL_IMPORTED_TARGET}
    )
else()
    target_link_libraries(reactcpp_demo
        PRIVATE
            ${REACTCPP_SDL_LIBRARIES}
    )

    target_link_libraries(react_cpp_skia_runtime_input_area_enter_tests
        PRIVATE
            ${REACTCPP_SDL_LIBRARIES}
    )
endif()

    # Skia: headers + static libraries
    set(SKIA_ROOT "${SKIA_BUILDER_ROOT}/skia")
    set(SKIA_LIB_DIR "${SKIA_BUILDER_ROOT}/build")
    set(SKIA_LIB_PATH "${SKIA_LIB_DIR}/libskia.a" CACHE FILEPATH "Path to Skia static library (libskia.a)")
    set(SKIA_SKCMS_LIB_PATH "${SKIA_LIB_DIR}/libskcms.a" CACHE FILEPATH "Path to skcms static library (libskcms.a), if present")

if (NOT EXISTS "${SKIA_LIB_PATH}")
    message(FATAL_ERROR "Skia static library not found at ${SKIA_LIB_PATH}")
endif()

if (NOT EXISTS "${SKIA_ROOT}/include/core/SkCanvas.h")
    message(FATAL_ERROR "Skia headers not found: expected ${SKIA_ROOT}/include/core/SkCanvas.h")
endif()

target_include_directories(reactcpp_demo
    PRIVATE
        "${SKIA_ROOT}/include"  # for core/SkCanvas.h
        "${SKIA_ROOT}"
)

target_include_directories(react_cpp_skia_runtime_input_area_enter_tests
    PRIVATE
        "${SKIA_ROOT}/include"
        "${SKIA_ROOT}"
)

target_link_libraries(reactcpp_demo
    PRIVATE
        "${SKIA_LIB_PATH}"
)

target_link_libraries(react_cpp_skia_runtime_input_area_enter_tests
    PRIVATE
        "${SKIA_LIB_PATH}"
)

# Some Skia builds require skcms as a separate static lib.
if (EXISTS "${SKIA_SKCMS_LIB_PATH}")
    target_link_libraries(reactcpp_demo
        PRIVATE
            "${SKIA_SKCMS_LIB_PATH}"
    )

    target_link_libraries(react_cpp_skia_runtime_input_area_enter_tests
        PRIVATE
            "${SKIA_SKCMS_LIB_PATH}"
    )
endif()

find_package(Threads REQUIRED)

# Font dependencies for Skia text rendering (Linux)
# If Skia was built with freetype/fontconfig/harfbuzz enabled, the final link
# step must include these system libraries.
if (PkgConfig_FOUND)
    pkg_check_modules(FREETYPE2 QUIET freetype2)
    pkg_check_modules(FONTCONFIG QUIET fontconfig)
    pkg_check_modules(HARFBUZZ QUIET harfbuzz)

    if (FREETYPE2_FOUND AND FONTCONFIG_FOUND AND HARFBUZZ_FOUND)
        target_include_directories(reactcpp_demo
            PRIVATE
                ${FREETYPE2_INCLUDE_DIRS}
                ${FONTCONFIG_INCLUDE_DIRS}
                ${HARFBUZZ_INCLUDE_DIRS}
        )

        target_include_directories(react_cpp_skia_runtime_input_area_enter_tests
            PRIVATE
                ${FREETYPE2_INCLUDE_DIRS}
                ${FONTCONFIG_INCLUDE_DIRS}
                ${HARFBUZZ_INCLUDE_DIRS}
        )

        target_compile_options(reactcpp_demo
            PRIVATE
                ${FREETYPE2_CFLAGS_OTHER}
                ${FONTCONFIG_CFLAGS_OTHER}
                ${HARFBUZZ_CFLAGS_OTHER}
        )

        target_compile_options(react_cpp_skia_runtime_input_area_enter_tests
            PRIVATE
                ${FREETYPE2_CFLAGS_OTHER}
                ${FONTCONFIG_CFLAGS_OTHER}
                ${HARFBUZZ_CFLAGS_OTHER}
        )

        # NOTE: link order matters for static libs: dependents first, dependencies after.
        # fontconfig can depend on freetype, so keep fontconfig before freetype.
        target_link_libraries(reactcpp_demo
            PRIVATE
                ${FONTCONFIG_LIBRARIES}
                ${FREETYPE2_LIBRARIES}
                ${HARFBUZZ_LIBRARIES}
                m
                dl
        )

        target_link_libraries(react_cpp_skia_runtime_input_area_enter_tests
            PRIVATE
                ${FONTCONFIG_LIBRARIES}
                ${FREETYPE2_LIBRARIES}
                ${HARFBUZZ_LIBRARIES}
                m
                dl
        )
    else()
        message(WARNING "pkg-config did not find all Skia font deps (freetype2/fontconfig/harfbuzz). If your Skia build has text enabled, linking may fail.")
    endif()
endif()

target_link_libraries(reactcpp_demo
    PRIVATE
        Threads::Threads
        yogacore
)

target_link_libraries(react_cpp_skia_runtime_input_area_enter_tests
    PRIVATE
        Threads::Threads
        yogacore
)

endif()
