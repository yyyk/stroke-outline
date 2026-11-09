# Provides two targets that run clang-format over all first-party C++ sources:
#   format        - rewrite files in place (clang-format -i)
#   format-check  - fail if anything is unformatted (CI-friendly)
# The WASM sandbox (sandbox/web) is TypeScript and handled by Prettier instead.
find_program(CLANG_FORMAT_EXE NAMES clang-format)

if (NOT CLANG_FORMAT_EXE)
    message(STATUS "clang-format not found; `format`/`format-check` targets unavailable")
    return()
endif()

# CONFIGURE_DEPENDS re-globs on each build, so new files are picked up without
# re-running cmake by hand.
file(GLOB_RECURSE PB_FORMAT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/include/*.hpp"
    "${CMAKE_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/bindings/*.cpp"
    "${CMAKE_SOURCE_DIR}/sandbox/native/*.cpp"
    "${CMAKE_SOURCE_DIR}/tests/*.cpp"
    "${CMAKE_SOURCE_DIR}/tests/*.hpp"
)

add_custom_target(format
    COMMAND ${CLANG_FORMAT_EXE} -i ${PB_FORMAT_SOURCES}
    COMMENT "clang-format -i (${CLANG_FORMAT_EXE})"
    VERBATIM)

add_custom_target(format-check
    COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror ${PB_FORMAT_SOURCES}
    COMMENT "clang-format --dry-run --Werror"
    VERBATIM)
