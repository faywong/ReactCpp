cmake_minimum_required(VERSION 3.16)
project(react_cpp_demo LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

include(FetchContent)

option(REACTCPP_BUILD_DEMO "Build SDL+Skia demo" ON)

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(REACTCPP_HOST_PLATFORM "macos")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(REACTCPP_HOST_PLATFORM "windows")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(REACTCPP_HOST_PLATFORM "linux")
else()
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" REACTCPP_HOST_PLATFORM)
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" REACTCPP_HOST_ARCH_RAW)
if (REACTCPP_HOST_ARCH_RAW MATCHES "^(x86_64|amd64)$")
    set(REACTCPP_HOST_ARCH "x64")
elseif (REACTCPP_HOST_ARCH_RAW MATCHES "^(aarch64|arm64)$")
    set(REACTCPP_HOST_ARCH "arm64")
elseif (REACTCPP_HOST_ARCH_RAW MATCHES "^(i386|i686|x86)$")
    set(REACTCPP_HOST_ARCH "x86")
else()
    set(REACTCPP_HOST_ARCH "${REACTCPP_HOST_ARCH_RAW}")
endif()

set(SKIA_SDK_ROOT_DEFAULT
    "${CMAKE_CURRENT_SOURCE_DIR}/.reactcpp/skia-sdk/${REACTCPP_HOST_PLATFORM}-${REACTCPP_HOST_ARCH}"
)
set(SKIA_SDK_ROOT "${SKIA_SDK_ROOT_DEFAULT}" CACHE PATH "Path to Skia SDK")

if (REACTCPP_BUILD_DEMO
        AND NOT EXISTS "${SKIA_SDK_ROOT}/skia/include/core/SkCanvas.h")
    message(FATAL_ERROR
        "Skia SDK not found at ${SKIA_SDK_ROOT}. "
        "Run: python3 scripts/skia/setup_skia_sdk.py, or pass -DSKIA_SDK_ROOT=/path/to/skia-sdk"
    )
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
    add_library(reactcpp
        SHARED
            src/skia_runtime.cpp
    )

    target_include_directories(reactcpp
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )

    set_target_properties(reactcpp
        PROPERTIES
            OUTPUT_NAME reactcpp
            WINDOWS_EXPORT_ALL_SYMBOLS ON
    )

    add_executable(demo
        src/main.cpp
    )

    target_link_libraries(demo PRIVATE reactcpp)

    # Rendering backends:
    # - Ganesh GL is always attempted first and is not user-disableable.
    # - If GL init fails at runtime, we fall back to CPU raster with a loud warning.
    find_package(OpenGL REQUIRED)
    target_link_libraries(demo PRIVATE OpenGL::GL)
    target_link_libraries(reactcpp PRIVATE OpenGL::GL)

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

    add_executable(react_cpp_skia_runtime_ime_area_scroll_tests
        tests/skia_runtime_ime_area_scroll_tests.cpp
    )

    # This test includes src/skia_runtime.cpp directly and therefore needs GL symbols.
    target_link_libraries(react_cpp_skia_runtime_ime_area_scroll_tests PRIVATE OpenGL::GL)

    target_include_directories(react_cpp_skia_runtime_ime_area_scroll_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )

    add_test(NAME skia_runtime_ime_area_scroll_tests COMMAND react_cpp_skia_runtime_ime_area_scroll_tests)

    add_executable(react_cpp_skia_runtime_frame_retention_tests
        tests/skia_runtime_frame_retention_tests.cpp
    )

    # This test includes src/skia_runtime.cpp directly and therefore needs GL symbols.
    target_link_libraries(react_cpp_skia_runtime_frame_retention_tests PRIVATE OpenGL::GL)

    target_include_directories(react_cpp_skia_runtime_frame_retention_tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )

    add_test(NAME skia_runtime_frame_retention_tests COMMAND react_cpp_skia_runtime_frame_retention_tests)



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
        pkg_check_modules(SDL3 QUIET IMPORTED_TARGET sdl3)
        if (SDL3_FOUND)
            set(REACTCPP_SDL_FOUND ON)
            set(REACTCPP_SDL_INCLUDE_DIRS ${SDL3_INCLUDE_DIRS})
            set(REACTCPP_SDL_CFLAGS_OTHER ${SDL3_CFLAGS_OTHER})
            if (TARGET PkgConfig::SDL3)
                set(REACTCPP_SDL_IMPORTED_TARGET PkgConfig::SDL3)
            else()
                set(REACTCPP_SDL_LIBRARIES ${SDL3_LIBRARIES})
            endif()
        else()
            pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
            if (SDL2_FOUND)
                set(REACTCPP_SDL_FOUND ON)
                set(REACTCPP_SDL_INCLUDE_DIRS ${SDL2_INCLUDE_DIRS})
                set(REACTCPP_SDL_CFLAGS_OTHER ${SDL2_CFLAGS_OTHER})
                if (TARGET PkgConfig::SDL2)
                    set(REACTCPP_SDL_IMPORTED_TARGET PkgConfig::SDL2)
                else()
                    set(REACTCPP_SDL_LIBRARIES ${SDL2_LIBRARIES})
                endif()
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
        # Manual fallback (common on distros without pkg-config/.pc or CMake package config files)
        set(REACTCPP_SDL3_PREFIXES
            /opt/homebrew
            /usr/local
            /usr
            ${CMAKE_PREFIX_PATH}
        )

        find_path(SDL3_INCLUDE_DIR
            NAMES SDL3/SDL.h
            HINTS
            ${REACTCPP_SDL3_PREFIXES}
            PATH_SUFFIXES include
        )
        find_library(SDL3_LIBRARY
            NAMES SDL3 SDL3-0 SDL3-1.0
            HINTS
            ${REACTCPP_SDL3_PREFIXES}
            PATH_SUFFIXES lib
        )
        if (NOT SDL3_LIBRARY)
            find_library(SDL3_LIBRARY
                NAMES SDL3
                HINTS
                ${REACTCPP_SDL3_PREFIXES}
                PATH_SUFFIXES Frameworks
            )
        endif()
        if (NOT SDL3_LIBRARY)
            find_library(SDL3_LIBRARY
                NAMES SDL3
                PATHS
                /opt/homebrew/Frameworks
                /Library/Frameworks
                /System/Library/Frameworks
            )
        endif()
        if (SDL3_INCLUDE_DIR AND SDL3_LIBRARY)
            set(REACTCPP_SDL_FOUND ON)
            set(REACTCPP_SDL_INCLUDE_DIRS "${SDL3_INCLUDE_DIR}")
            set(REACTCPP_SDL_LIBRARIES "${SDL3_LIBRARY}")
        else()
            find_path(SDL2_INCLUDE_DIR
                NAMES SDL2/SDL.h
                HINTS
                ${REACTCPP_SDL3_PREFIXES}
                PATH_SUFFIXES include
            )
            find_library(SDL2_LIBRARY
                NAMES SDL2 SDL2-2.0
                HINTS
                ${REACTCPP_SDL3_PREFIXES}
                PATH_SUFFIXES lib
            )
            if (SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
                set(REACTCPP_SDL_FOUND ON)
                set(REACTCPP_SDL_INCLUDE_DIRS "${SDL2_INCLUDE_DIR}")
                set(REACTCPP_SDL_LIBRARIES "${SDL2_LIBRARY}")
            endif()
        endif()
    endif()

    if (NOT REACTCPP_SDL_IMPORTED_TARGET AND REACTCPP_SDL_FOUND)
        set(_SDL_LIBRARY_PATHS
            /opt/homebrew/lib
            /usr/local/lib
            /usr/lib
            ${CMAKE_PREFIX_PATH}
        )
        if (REACTCPP_SDL_INCLUDE_DIRS MATCHES "/SDL3/")
            find_library(_SDL3_ABS_LIBRARY
                NAMES SDL3 SDL3.0 SDL3.0.0 SDL3-0 SDL3-1.0
                PATHS ${_SDL_LIBRARY_PATHS}
                PATH_SUFFIXES lib Frameworks
            )
            if (_SDL3_ABS_LIBRARY)
                set(REACTCPP_SDL_LIBRARIES "${_SDL3_ABS_LIBRARY}")
            endif()
        elseif (REACTCPP_SDL_INCLUDE_DIRS MATCHES "/SDL2/")
            find_library(_SDL2_ABS_LIBRARY
                NAMES SDL2 SDL2-2.0
                PATHS ${_SDL_LIBRARY_PATHS}
                PATH_SUFFIXES lib Frameworks
            )
            if (_SDL2_ABS_LIBRARY)
                set(REACTCPP_SDL_LIBRARIES "${_SDL2_ABS_LIBRARY}")
            endif()
        endif()
    endif()

    if (NOT REACTCPP_SDL_FOUND)
        message(FATAL_ERROR "Neither SDL3 nor SDL2 was found (tried pkg-config, CMake package config, and manual find_path/find_library). Install SDL2 development headers/libs or set CMAKE_PREFIX_PATH/PKG_CONFIG_PATH accordingly.")
    endif()

if (REACTCPP_SDL_INCLUDE_DIRS)
    target_include_directories(reactcpp
        PRIVATE
            ${REACTCPP_SDL_INCLUDE_DIRS}
    )

    target_include_directories(demo
        PRIVATE
            ${REACTCPP_SDL_INCLUDE_DIRS}
    )

    target_include_directories(react_cpp_skia_runtime_input_area_enter_tests
        PRIVATE
            ${REACTCPP_SDL_INCLUDE_DIRS}
    )

    target_include_directories(react_cpp_skia_runtime_frame_retention_tests
        PRIVATE
            ${REACTCPP_SDL_INCLUDE_DIRS}
    )

    target_include_directories(react_cpp_skia_runtime_ime_area_scroll_tests
        PRIVATE
            ${REACTCPP_SDL_INCLUDE_DIRS}
    )
endif()

if (REACTCPP_SDL_CFLAGS_OTHER)
    target_compile_options(reactcpp
        PRIVATE
            ${REACTCPP_SDL_CFLAGS_OTHER}
    )

    target_compile_options(demo
        PRIVATE
            ${REACTCPP_SDL_CFLAGS_OTHER}
    )

    target_compile_options(react_cpp_skia_runtime_input_area_enter_tests
        PRIVATE
            ${REACTCPP_SDL_CFLAGS_OTHER}
    )

    target_compile_options(react_cpp_skia_runtime_frame_retention_tests
        PRIVATE
            ${REACTCPP_SDL_CFLAGS_OTHER}
    )

    target_compile_options(react_cpp_skia_runtime_ime_area_scroll_tests
        PRIVATE
            ${REACTCPP_SDL_CFLAGS_OTHER}
    )
endif()

if (REACTCPP_SDL_IMPORTED_TARGET)
    target_link_libraries(reactcpp
        PRIVATE
            ${REACTCPP_SDL_IMPORTED_TARGET}
    )

    target_link_libraries(demo
        PRIVATE
            ${REACTCPP_SDL_IMPORTED_TARGET}
    )

    target_link_libraries(react_cpp_skia_runtime_input_area_enter_tests
        PRIVATE
            ${REACTCPP_SDL_IMPORTED_TARGET}
    )

    target_link_libraries(react_cpp_skia_runtime_frame_retention_tests
        PRIVATE
            ${REACTCPP_SDL_IMPORTED_TARGET}
    )

    target_link_libraries(react_cpp_skia_runtime_ime_area_scroll_tests
        PRIVATE
            ${REACTCPP_SDL_IMPORTED_TARGET}
    )
else()
    target_link_libraries(reactcpp
        PRIVATE
            ${REACTCPP_SDL_LIBRARIES}
    )

    target_link_libraries(demo
        PRIVATE
            ${REACTCPP_SDL_LIBRARIES}
    )

    target_link_libraries(react_cpp_skia_runtime_input_area_enter_tests
        PRIVATE
            ${REACTCPP_SDL_LIBRARIES}
    )

    target_link_libraries(react_cpp_skia_runtime_frame_retention_tests
        PRIVATE
            ${REACTCPP_SDL_LIBRARIES}
    )

    target_link_libraries(react_cpp_skia_runtime_ime_area_scroll_tests
        PRIVATE
            ${REACTCPP_SDL_LIBRARIES}
    )
endif()

    # Skia SDK produced by scripts/skia/build_skia_sdk.py or installed from CI archive.
    set(SKIA_ROOT "${SKIA_SDK_ROOT}/skia")
    set(SKIA_LIB_DIR "${SKIA_SDK_ROOT}/lib")
    message(STATUS "Using Skia SDK: ${SKIA_SDK_ROOT}")

    if (WIN32)
        set(SKIA_LIB_PATH_DEFAULT "${SKIA_LIB_DIR}/skia.lib")
    else()
        set(SKIA_LIB_PATH_DEFAULT "${SKIA_LIB_DIR}/libskia.a")
    endif()

    set(SKIA_LIB_PATH "${SKIA_LIB_PATH_DEFAULT}")
    set(SKIA_SKCMS_LIB_PATH "${SKIA_LIB_DIR}/libskcms.a")

if (NOT EXISTS "${SKIA_LIB_PATH}")
    message(FATAL_ERROR "Skia static library not found at ${SKIA_LIB_PATH}")
endif()

if (NOT EXISTS "${SKIA_ROOT}/include/core/SkCanvas.h")
    message(FATAL_ERROR "Skia headers not found: expected ${SKIA_ROOT}/include/core/SkCanvas.h")
endif()

target_include_directories(demo
    PRIVATE
        "${SKIA_ROOT}/include"  # for core/SkCanvas.h
        "${SKIA_ROOT}"
)

target_include_directories(reactcpp
    PRIVATE
        "${SKIA_ROOT}/include"  # for core/SkCanvas.h
        "${SKIA_ROOT}"
)

target_include_directories(react_cpp_skia_runtime_input_area_enter_tests
    PRIVATE
        "${SKIA_ROOT}/include"
        "${SKIA_ROOT}"
)

target_include_directories(react_cpp_skia_runtime_frame_retention_tests
    PRIVATE
        "${SKIA_ROOT}/include"
        "${SKIA_ROOT}"
)

target_include_directories(react_cpp_skia_runtime_ime_area_scroll_tests
    PRIVATE
        "${SKIA_ROOT}/include"
        "${SKIA_ROOT}"
)

set(RIVE_RUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/rive-runtime")
if (NOT EXISTS "${RIVE_RUNTIME_ROOT}/include/rive/file.hpp")
    message(FATAL_ERROR
        "Rive runtime submodule is missing at ${RIVE_RUNTIME_ROOT}. "
        "Run: git submodule update --init --recursive"
    )
endif()

file(GLOB_RECURSE REACTCPP_RIVE_RUNTIME_SOURCES CONFIGURE_DEPENDS
    "${RIVE_RUNTIME_ROOT}/src/*.cpp"
)
list(FILTER REACTCPP_RIVE_RUNTIME_SOURCES EXCLUDE REGEX "/src/lua/")
list(FILTER REACTCPP_RIVE_RUNTIME_SOURCES EXCLUDE REGEX "/src/audio/")
list(FILTER REACTCPP_RIVE_RUNTIME_SOURCES EXCLUDE REGEX "/src/text/font_hb")
list(FILTER REACTCPP_RIVE_RUNTIME_SOURCES EXCLUDE REGEX "/src/text/line_breaker")
list(FILTER REACTCPP_RIVE_RUNTIME_SOURCES EXCLUDE REGEX "/src/text/fully_shaped_text")

set(REACTCPP_RIVE_SKIA_COMPAT_DIR "${CMAKE_CURRENT_BINARY_DIR}/rive-skia-compat")
file(MAKE_DIRECTORY "${REACTCPP_RIVE_SKIA_COMPAT_DIR}")

file(READ "${RIVE_RUNTIME_ROOT}/skia/renderer/include/to_skia.hpp" REACTCPP_RIVE_TO_SKIA_HPP)
string(REPLACE
    "return SkPath::Make((const SkPoint*)pts.data(), pts.size(),\n                                vbs.data(), math::lossless_numeric_cast<int>(vbs.size()),\n                                nullptr, 0, SkPathFillType::kWinding);"
    "return SkPath::Make({(const SkPoint*)pts.data(), pts.size()},\n                                {vbs.data(), vbs.size()},\n                                {}, SkPathFillType::kWinding);"
    REACTCPP_RIVE_TO_SKIA_HPP
    "${REACTCPP_RIVE_TO_SKIA_HPP}"
)
file(WRITE "${REACTCPP_RIVE_SKIA_COMPAT_DIR}/to_skia.hpp" "${REACTCPP_RIVE_TO_SKIA_HPP}")

file(READ "${RIVE_RUNTIME_ROOT}/skia/renderer/src/skia_factory.cpp" REACTCPP_RIVE_SKIA_FACTORY_CPP)
string(REPLACE
    "#include \"include/core/SkPath.h\""
    "#include \"include/core/SkPath.h\"\n#include \"include/core/SkPathBuilder.h\""
    REACTCPP_RIVE_SKIA_FACTORY_CPP
    "${REACTCPP_RIVE_SKIA_FACTORY_CPP}"
)
string(REPLACE
    "private:\n    SkPath m_Path;\n\npublic:\n    SkiaRenderPath() {}\n    SkiaRenderPath(SkPath&& path) : m_Path(std::move(path)) {}\n\n    const SkPath& path() const { return m_Path; }"
    "private:\n    SkPathBuilder m_Builder;\n    mutable SkPath m_Path;\n\npublic:\n    SkiaRenderPath() {}\n    SkiaRenderPath(SkPath&& path) : m_Builder(path), m_Path(std::move(path)) {}\n\n    const SkPath& path() const { m_Path = m_Builder.snapshot(); return m_Path; }"
    REACTCPP_RIVE_SKIA_FACTORY_CPP
    "${REACTCPP_RIVE_SKIA_FACTORY_CPP}"
)
string(REPLACE
    "m_Path.setFillType(ToSkia::convert(value));"
    "m_Builder.setFillType(ToSkia::convert(value));"
    REACTCPP_RIVE_SKIA_FACTORY_CPP
    "${REACTCPP_RIVE_SKIA_FACTORY_CPP}"
)
string(REPLACE
    "auto fillType = m_Path.getFillType();\n    m_Path.rewind();\n    m_Path.setFillType(fillType);"
    "auto fillType = m_Builder.fillType();\n    m_Builder.reset();\n    m_Builder.setFillType(fillType);"
    REACTCPP_RIVE_SKIA_FACTORY_CPP
    "${REACTCPP_RIVE_SKIA_FACTORY_CPP}"
)
string(REPLACE
    "m_Path.addPath(skPath->m_Path, ToSkia::convert(transform));"
    "m_Builder.addPath(skPath->path(), ToSkia::convert(transform));"
    REACTCPP_RIVE_SKIA_FACTORY_CPP
    "${REACTCPP_RIVE_SKIA_FACTORY_CPP}"
)
string(REPLACE
    "SkPath::Make(reinterpret_cast<const SkPoint*>(path.points().data()),\n                     path.points().size(),\n                     (uint8_t*)path.verbs().data(),\n                     path.verbs().size(),\n                     conicWeights,\n                     conicWeightCount,\n                     m_Path.getFillType(),\n                     isVolatile);"
    "SkPath::Make({reinterpret_cast<const SkPoint*>(path.points().data()), path.points().size()},\n                     {(uint8_t*)path.verbs().data(), path.verbs().size()},\n                     {conicWeights, static_cast<size_t>(conicWeightCount)},\n                     m_Path.getFillType(),\n                     isVolatile);"
    REACTCPP_RIVE_SKIA_FACTORY_CPP
    "${REACTCPP_RIVE_SKIA_FACTORY_CPP}"
)
string(REPLACE
    "m_Path.getFillType(),"
    "m_Builder.fillType(),"
    REACTCPP_RIVE_SKIA_FACTORY_CPP
    "${REACTCPP_RIVE_SKIA_FACTORY_CPP}"
)
string(REPLACE
    "m_Path.addPath(skPath);"
    "m_Builder.addPath(skPath);"
    REACTCPP_RIVE_SKIA_FACTORY_CPP
    "${REACTCPP_RIVE_SKIA_FACTORY_CPP}"
)
string(REPLACE
    "void SkiaRenderPath::moveTo(float x, float y) { m_Path.moveTo(x, y); }\nvoid SkiaRenderPath::lineTo(float x, float y) { m_Path.lineTo(x, y); }"
    "void SkiaRenderPath::moveTo(float x, float y) { m_Builder.moveTo(x, y); }\nvoid SkiaRenderPath::lineTo(float x, float y) { m_Builder.lineTo(x, y); }"
    REACTCPP_RIVE_SKIA_FACTORY_CPP
    "${REACTCPP_RIVE_SKIA_FACTORY_CPP}"
)
string(REPLACE
    "m_Path.cubicTo(ox, oy, ix, iy, x, y);"
    "m_Builder.cubicTo(ox, oy, ix, iy, x, y);"
    REACTCPP_RIVE_SKIA_FACTORY_CPP
    "${REACTCPP_RIVE_SKIA_FACTORY_CPP}"
)
string(REPLACE
    "void SkiaRenderPath::close() { m_Path.close(); }"
    "void SkiaRenderPath::close() { m_Builder.close(); }"
    REACTCPP_RIVE_SKIA_FACTORY_CPP
    "${REACTCPP_RIVE_SKIA_FACTORY_CPP}"
)
string(REPLACE
    "SkPath::Make(reinterpret_cast<const SkPoint*>(rawPath.points().data()),\n                     rawPath.points().size(),\n                     (uint8_t*)rawPath.verbs().data(),\n                     rawPath.verbs().size(),\n                     conicWeights,\n                     conicWeightCount,\n                     ToSkia::convert(fillRule),\n                     isVolatile));"
    "SkPath::Make({reinterpret_cast<const SkPoint*>(rawPath.points().data()), rawPath.points().size()},\n                     {(uint8_t*)rawPath.verbs().data(), rawPath.verbs().size()},\n                     {conicWeights, static_cast<size_t>(conicWeightCount)},\n                     ToSkia::convert(fillRule),\n                     isVolatile));"
    REACTCPP_RIVE_SKIA_FACTORY_CPP
    "${REACTCPP_RIVE_SKIA_FACTORY_CPP}"
)
string(REPLACE
    "SkImage::MakeFromEncoded(data)"
    "SkImages::DeferredFromEncodedData(data)"
    REACTCPP_RIVE_SKIA_FACTORY_CPP
    "${REACTCPP_RIVE_SKIA_FACTORY_CPP}"
)
string(REPLACE
    "SkImage::MakeRasterCopy({skinfo, pixels.data(), info.rowBytes})"
    "SkImages::RasterFromPixmapCopy(SkPixmap(skinfo, pixels.data(), info.rowBytes))"
    REACTCPP_RIVE_SKIA_FACTORY_CPP
    "${REACTCPP_RIVE_SKIA_FACTORY_CPP}"
)
file(WRITE "${REACTCPP_RIVE_SKIA_COMPAT_DIR}/skia_factory.cpp" "${REACTCPP_RIVE_SKIA_FACTORY_CPP}")

set(REACTCPP_RIVE_SKIA_RENDERER_SOURCES
    "${REACTCPP_RIVE_SKIA_COMPAT_DIR}/skia_factory.cpp"
)

add_library(reactcpp_rive_objects OBJECT
    ${REACTCPP_RIVE_RUNTIME_SOURCES}
    ${REACTCPP_RIVE_SKIA_RENDERER_SOURCES}
)

set_target_properties(reactcpp_rive_objects
    PROPERTIES
        POSITION_INDEPENDENT_CODE ON
)

target_compile_definitions(reactcpp_rive_objects
    PRIVATE
        _RIVE_INTERNAL_
        YOGA_EXPORT=
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(reactcpp_rive_objects
        PRIVATE
            -Wno-deprecated-declarations
    )
endif()

target_include_directories(reactcpp_rive_objects
    PRIVATE
        "${REACTCPP_RIVE_SKIA_COMPAT_DIR}"
        "${RIVE_RUNTIME_ROOT}/include"
        "${RIVE_RUNTIME_ROOT}/dependencies"
        "${RIVE_RUNTIME_ROOT}/skia/renderer/include"
        "${RIVE_RUNTIME_ROOT}/cg_renderer/include"
        "${SKIA_ROOT}"
        "${SKIA_ROOT}/include"
)

function(reactcpp_attach_rive_sources target_name)
    target_sources(${target_name}
        PRIVATE
            $<TARGET_OBJECTS:reactcpp_rive_objects>
    )

    target_compile_definitions(${target_name}
        PRIVATE
            _RIVE_INTERNAL_
            YOGA_EXPORT=
    )

    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target_name}
            PRIVATE
                -Wno-deprecated-declarations
        )
    endif()

    target_include_directories(${target_name}
        PRIVATE
            "${REACTCPP_RIVE_SKIA_COMPAT_DIR}"
            "${RIVE_RUNTIME_ROOT}/include"
            "${RIVE_RUNTIME_ROOT}/dependencies"
            "${RIVE_RUNTIME_ROOT}/skia/renderer/include"
            "${RIVE_RUNTIME_ROOT}/cg_renderer/include"
            "${SKIA_ROOT}"
            "${SKIA_ROOT}/include"
    )
endfunction()

reactcpp_attach_rive_sources(reactcpp)
reactcpp_attach_rive_sources(react_cpp_skia_runtime_input_area_enter_tests)
reactcpp_attach_rive_sources(react_cpp_skia_runtime_frame_retention_tests)
reactcpp_attach_rive_sources(react_cpp_skia_runtime_ime_area_scroll_tests)

target_link_libraries(demo
    PRIVATE
        "${SKIA_LIB_PATH}"
)

target_link_libraries(reactcpp
    PRIVATE
        "${SKIA_LIB_PATH}"
)

target_link_libraries(react_cpp_skia_runtime_input_area_enter_tests
    PRIVATE
        "${SKIA_LIB_PATH}"
)

target_link_libraries(react_cpp_skia_runtime_frame_retention_tests
    PRIVATE
        "${SKIA_LIB_PATH}"
)

target_link_libraries(react_cpp_skia_runtime_ime_area_scroll_tests
    PRIVATE
        "${SKIA_LIB_PATH}"
)

# Some Skia builds require skcms as a separate static lib.
if (EXISTS "${SKIA_SKCMS_LIB_PATH}")
    target_link_libraries(demo
        PRIVATE
            "${SKIA_SKCMS_LIB_PATH}"
    )

    target_link_libraries(react_cpp_skia_runtime_input_area_enter_tests
        PRIVATE
            "${SKIA_SKCMS_LIB_PATH}"
    )

    target_link_libraries(react_cpp_skia_runtime_frame_retention_tests
        PRIVATE
            "${SKIA_SKCMS_LIB_PATH}"
    )

    target_link_libraries(react_cpp_skia_runtime_ime_area_scroll_tests
        PRIVATE
            "${SKIA_SKCMS_LIB_PATH}"
    )

    target_link_libraries(reactcpp
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
        set(REACTCPP_FONT_LINK_LIBS)
        set(REACTCPP_FONT_LINK_TARGETS)

        if (TARGET PkgConfig::FONTCONFIG)
            list(APPEND REACTCPP_FONT_LINK_TARGETS PkgConfig::FONTCONFIG)
        else()
            if (NOT FONTCONFIG_LIBRARY_DIRS)
                set(FONTCONFIG_LIBRARY_DIRS
                    /opt/homebrew/lib
                    /usr/local/lib
                    /usr/lib
                )
            endif()
            find_library(_FONTCONFIG_LIBRARY NAMES fontconfig PATHS ${FONTCONFIG_LIBRARY_DIRS} NO_DEFAULT_PATH)
            if (NOT _FONTCONFIG_LIBRARY)
                find_library(_FONTCONFIG_LIBRARY NAMES fontconfig PATHS /opt/homebrew/lib /usr/local/lib /usr/lib)
            endif()
            if (_FONTCONFIG_LIBRARY)
                list(APPEND REACTCPP_FONT_LINK_LIBS "${_FONTCONFIG_LIBRARY}")
            else()
                message(WARNING "fontconfig library not resolved to absolute path, falling back to pkg-config token list.")
                list(APPEND REACTCPP_FONT_LINK_LIBS "${FONTCONFIG_LIBRARIES}")
            endif()
        endif()

        if (TARGET PkgConfig::FREETYPE2)
            list(APPEND REACTCPP_FONT_LINK_TARGETS PkgConfig::FREETYPE2)
        else()
            if (NOT FREETYPE2_LIBRARY_DIRS)
                set(FREETYPE2_LIBRARY_DIRS
                    /opt/homebrew/lib
                    /usr/local/lib
                    /usr/lib
                )
            endif()
            find_library(_FREETYPE_LIBRARY NAMES freetype freetype2 PATHS ${FREETYPE2_LIBRARY_DIRS} NO_DEFAULT_PATH)
            if (NOT _FREETYPE_LIBRARY)
                find_library(_FREETYPE_LIBRARY NAMES freetype freetype2 PATHS /opt/homebrew/lib /usr/local/lib /usr/lib)
            endif()
            if (_FREETYPE_LIBRARY)
                list(APPEND REACTCPP_FONT_LINK_LIBS "${_FREETYPE_LIBRARY}")
            else()
                list(APPEND REACTCPP_FONT_LINK_LIBS "${FREETYPE2_LIBRARIES}")
            endif()
        endif()

        if (TARGET PkgConfig::HARFBUZZ)
            list(APPEND REACTCPP_FONT_LINK_TARGETS PkgConfig::HARFBUZZ)
        else()
            if (NOT HARFBUZZ_LIBRARY_DIRS)
                set(HARFBUZZ_LIBRARY_DIRS
                    /opt/homebrew/lib
                    /usr/local/lib
                    /usr/lib
                )
            endif()
            find_library(_HARFBUZZ_LIBRARY NAMES harfbuzz PATHS ${HARFBUZZ_LIBRARY_DIRS} NO_DEFAULT_PATH)
            if (NOT _HARFBUZZ_LIBRARY)
                find_library(_HARFBUZZ_LIBRARY NAMES harfbuzz PATHS /opt/homebrew/lib /usr/local/lib /usr/lib)
            endif()
            if (_HARFBUZZ_LIBRARY)
                list(APPEND REACTCPP_FONT_LINK_LIBS "${_HARFBUZZ_LIBRARY}")
            else()
                list(APPEND REACTCPP_FONT_LINK_LIBS "${HARFBUZZ_LIBRARIES}")
            endif()
        endif()

        if (REACTCPP_FONT_LINK_TARGETS)
            target_link_libraries(demo PRIVATE ${REACTCPP_FONT_LINK_TARGETS})
            target_link_libraries(react_cpp_skia_runtime_input_area_enter_tests PRIVATE ${REACTCPP_FONT_LINK_TARGETS})
            target_link_libraries(react_cpp_skia_runtime_frame_retention_tests PRIVATE ${REACTCPP_FONT_LINK_TARGETS})
            target_link_libraries(react_cpp_skia_runtime_ime_area_scroll_tests PRIVATE ${REACTCPP_FONT_LINK_TARGETS})
            target_link_libraries(reactcpp PRIVATE ${REACTCPP_FONT_LINK_TARGETS})
        endif()

        if (REACTCPP_FONT_LINK_LIBS)
            target_link_libraries(demo PRIVATE ${REACTCPP_FONT_LINK_LIBS})
            target_link_libraries(react_cpp_skia_runtime_input_area_enter_tests PRIVATE ${REACTCPP_FONT_LINK_LIBS})
            target_link_libraries(react_cpp_skia_runtime_frame_retention_tests PRIVATE ${REACTCPP_FONT_LINK_LIBS})
            target_link_libraries(react_cpp_skia_runtime_ime_area_scroll_tests PRIVATE ${REACTCPP_FONT_LINK_LIBS})
            target_link_libraries(reactcpp PRIVATE ${REACTCPP_FONT_LINK_LIBS})
        endif()

        target_include_directories(demo
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

        target_include_directories(react_cpp_skia_runtime_frame_retention_tests
            PRIVATE
                ${FREETYPE2_INCLUDE_DIRS}
                ${FONTCONFIG_INCLUDE_DIRS}
                ${HARFBUZZ_INCLUDE_DIRS}
        )

        target_include_directories(react_cpp_skia_runtime_ime_area_scroll_tests
            PRIVATE
                ${FREETYPE2_INCLUDE_DIRS}
                ${FONTCONFIG_INCLUDE_DIRS}
                ${HARFBUZZ_INCLUDE_DIRS}
        )

        target_include_directories(reactcpp
            PRIVATE
                ${FREETYPE2_INCLUDE_DIRS}
                ${FONTCONFIG_INCLUDE_DIRS}
                ${HARFBUZZ_INCLUDE_DIRS}
        )

        target_compile_options(demo
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

        target_compile_options(react_cpp_skia_runtime_frame_retention_tests
            PRIVATE
                ${FREETYPE2_CFLAGS_OTHER}
                ${FONTCONFIG_CFLAGS_OTHER}
                ${HARFBUZZ_CFLAGS_OTHER}
        )

        target_compile_options(react_cpp_skia_runtime_ime_area_scroll_tests
            PRIVATE
                ${FREETYPE2_CFLAGS_OTHER}
                ${FONTCONFIG_CFLAGS_OTHER}
                ${HARFBUZZ_CFLAGS_OTHER}
        )

        target_compile_options(reactcpp
            PRIVATE
                ${FREETYPE2_CFLAGS_OTHER}
                ${FONTCONFIG_CFLAGS_OTHER}
                ${HARFBUZZ_CFLAGS_OTHER}
        )
        list(APPEND REACTCPP_FONT_LINK_LIBS m dl)
        target_link_libraries(demo PRIVATE m dl)
        target_link_libraries(react_cpp_skia_runtime_input_area_enter_tests PRIVATE m dl)
        target_link_libraries(react_cpp_skia_runtime_frame_retention_tests PRIVATE m dl)
        target_link_libraries(react_cpp_skia_runtime_ime_area_scroll_tests PRIVATE m dl)
        target_link_libraries(reactcpp PRIVATE m dl)
    else()
        message(WARNING "pkg-config did not find all Skia font deps (freetype2/fontconfig/harfbuzz). If your Skia build has text enabled, linking may fail.")
    endif()
endif()

if (APPLE)
    set(REACTCPP_APPLE_FRAMEWORK_LINKS
        "-framework Foundation"
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework CoreText"
    )

    target_link_libraries(reactcpp PRIVATE ${REACTCPP_APPLE_FRAMEWORK_LINKS})
    target_link_libraries(demo PRIVATE ${REACTCPP_APPLE_FRAMEWORK_LINKS})
    target_link_libraries(react_cpp_skia_runtime_input_area_enter_tests PRIVATE ${REACTCPP_APPLE_FRAMEWORK_LINKS})
    target_link_libraries(react_cpp_skia_runtime_frame_retention_tests PRIVATE ${REACTCPP_APPLE_FRAMEWORK_LINKS})
    target_link_libraries(react_cpp_skia_runtime_ime_area_scroll_tests PRIVATE ${REACTCPP_APPLE_FRAMEWORK_LINKS})
endif()

target_link_libraries(demo
    PRIVATE
        Threads::Threads
        yogacore
)

target_link_libraries(reactcpp
    PRIVATE
        Threads::Threads
        yogacore
)

target_link_libraries(react_cpp_skia_runtime_input_area_enter_tests
    PRIVATE
        Threads::Threads
        yogacore
)

target_link_libraries(react_cpp_skia_runtime_frame_retention_tests
    PRIVATE
        Threads::Threads
        yogacore
)

target_link_libraries(react_cpp_skia_runtime_ime_area_scroll_tests
    PRIVATE
        Threads::Threads
        yogacore
)

endif()
