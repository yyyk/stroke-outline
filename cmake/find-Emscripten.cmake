# Locate the Emscripten toolchain file so a WASM build "just works" without the
# caller having to pass -DCMAKE_TOOLCHAIN_FILE by hand. Only runs when the
# `wasm` preset (USE_EMSCRIPTEN=ON) is active; otherwise it's a no-op.
if (NOT USE_EMSCRIPTEN)
    return()
endif()

if (NOT CMAKE_TOOLCHAIN_FILE)
    # 1. The EMSDK env var, set by sourcing emsdk_env.sh.
    if (DEFINED ENV{EMSDK})
        set(CMAKE_TOOLCHAIN_FILE
            "$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
    # 2. Fall back to a Homebrew install (common on macOS).
    else()
        execute_process(
            COMMAND brew --prefix emscripten
            OUTPUT_VARIABLE HOMEBREW_EMSCRIPTEN_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if (HOMEBREW_EMSCRIPTEN_PREFIX)
            set(CMAKE_TOOLCHAIN_FILE
                "${HOMEBREW_EMSCRIPTEN_PREFIX}/libexec/cmake/Modules/Platform/Emscripten.cmake")
        endif()
    endif()
endif()

if (CMAKE_TOOLCHAIN_FILE)
    message(STATUS "Using Emscripten toolchain file: ${CMAKE_TOOLCHAIN_FILE}")
else()
    message(FATAL_ERROR
        "Emscripten toolchain not found. Install emsdk and `source emsdk_env.sh` "
        "(sets EMSDK), or install via Homebrew (`brew install emscripten`).")
endif()
