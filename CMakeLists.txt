cmake_minimum_required(VERSION 3.21)

# Define the library name once; everything below references ${TARGET_NAME}.
# Set before project() so project(${TARGET_NAME} ...) picks it up too.
set(TARGET_NAME stroke_outline)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Resolve the Emscripten toolchain before project() — it must be known before
# the compiler is probed. No-op unless USE_EMSCRIPTEN is on (the `wasm` preset).
if (USE_EMSCRIPTEN)
    include(find-Emscripten)
endif()

project(${TARGET_NAME} VERSION 0.1.3 LANGUAGES CXX)

# Guard against being added twice (e.g. two consumers FetchContent this lib).
if (TARGET ${TARGET_NAME})
    return()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optional clang coverage instrumentation (STROKE_OUTLINE_COVERAGE, top-level
# builds only). Included before any target so the flags reach the library too.
if (PROJECT_IS_TOP_LEVEL)
    include(Coverage)
endif()

# ---------------------------------------------------------------------------
# The core library: plain, portable C++. No platform-specific code lives here,
# so the exact same sources compile for native and for WebAssembly.
# ---------------------------------------------------------------------------
add_library(${TARGET_NAME}
    src/stroke_outline.cpp
	src/bezier/arc.cpp
	src/bezier/arc_length.cpp
	src/bezier/bounds.cpp
	# src/bezier/flatten.cpp
	src/bezier/intersect.cpp
	src/bezier/offset.cpp
	src/bezier/utils.cpp
	src/io/path.cpp
	src/io/svg_arc.cpp
	src/io/svg_path.cpp
	src/stroke/cap.cpp
	src/stroke/clip.cpp
	src/stroke/clip_weiler_atherton.cpp  # original clipper, kept for reference; unused
	src/stroke/dash.cpp
	src/stroke/join.cpp
	src/stroke/utils.cpp
)
target_include_directories(${TARGET_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        # internal headers (src/math, src/bezier) — not exposed to consumers
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
# The public headers and the library are C++20; consumers inherit that.
target_compile_features(${TARGET_NAME} PUBLIC cxx_std_20)
# The same name whether the library comes from find_package (installed) or
# add_subdirectory/FetchContent.
add_library(${TARGET_NAME}::${TARGET_NAME} ALIAS ${TARGET_NAME})

# `install` rules and the find_package(stroke_outline) package files.
include(Install)

# When consumed as a dependency (add_subdirectory / FetchContent), stop here:
# hand back only the library, not our example, tests, or WASM bindings.
if (NOT PROJECT_IS_TOP_LEVEL)
    return()
endif()

# `format` / `format-check` targets (clang-format over all first-party C++).
include(Format)

# EMSCRIPTEN is defined by Emscripten's toolchain file when building for WASM.
if(EMSCRIPTEN)
    # ----- WASM target: wrap the core lib with JS bindings -----
    add_executable(${TARGET_NAME}_wasm bindings/wasm_bindings.cpp)
    target_link_libraries(${TARGET_NAME}_wasm PRIVATE ${TARGET_NAME})

    # --bind links Embind (the <emscripten/bind.h> API). It's a link flag only;
    # passing it at compile time just produces an "ignored" warning.
    target_link_options(${TARGET_NAME}_wasm PRIVATE
        --bind
        -sMODULARIZE=1
        -sEXPORT_ES6=1
        # No `node`: its startup code imports node:module, which every browser
        # bundler (e.g. Vite) warns about. The package still runs in Node 20+,
        # which has the web APIs the module uses; the .wasm is embedded
        # (SINGLE_FILE), so there is no file to read from disk. CI runs the JS
        # tests on Node 20, 22 and 24 to keep it that way.
        -sENVIRONMENT=web,worker
        -sALLOW_MEMORY_GROWTH=1
        # Embed the .wasm in the .js (base64), so the package needs no bundler
        # or server setup to find it (Vite's dev server, for one, can't locate a
        # separate .wasm in a pre-bundled dependency).
        -sSINGLE_FILE=1
        # Emit TypeScript declarations alongside the module for typed JS/TS use.
        --emit-tsd "${TARGET_NAME}.d.ts"
    )
    set_target_properties(${TARGET_NAME}_wasm PROPERTIES OUTPUT_NAME "${TARGET_NAME}")

    # Publish the built module (.js with the .wasm embedded, .d.ts) into the npm package (js/),
    # which imports it with full TypeScript types. The web sandbox and the docs
    # site use the package from source, so they get it from there.
    set(PACKAGE_WASM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/js/src/wasm")
    set(WASM_OUTPUTS
        "$<TARGET_FILE_DIR:${TARGET_NAME}_wasm>/${TARGET_NAME}.js"
        "$<TARGET_FILE_DIR:${TARGET_NAME}_wasm>/${TARGET_NAME}.d.ts")
    # Release builds only: a Debug module is ~30x larger and unoptimized, and
    # must never end up in the package. (`cmake -E true` does nothing.)
    set(IF_RELEASE "$<IF:$<CONFIG:Release>")
    add_custom_command(TARGET ${TARGET_NAME}_wasm POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E ${IF_RELEASE},make_directory,true> "${PACKAGE_WASM_DIR}"
        COMMAND ${CMAKE_COMMAND} -E ${IF_RELEASE},copy_if_different,true> ${WASM_OUTPUTS} "${PACKAGE_WASM_DIR}/"
        # A separate .wasm from a build before SINGLE_FILE would be stale.
        COMMAND ${CMAKE_COMMAND} -E ${IF_RELEASE},rm,true> -f "${PACKAGE_WASM_DIR}/${TARGET_NAME}.wasm"
        COMMENT "Publishing WASM module into js/src/wasm (Release builds only)")
else()
    # ----- Native build: sandbox demo + tests -----
    add_subdirectory(sandbox/native)

    enable_testing()
    add_subdirectory(tests)
endif()
