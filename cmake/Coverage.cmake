# Clang source-based code coverage for the test suite.
#
#   cmake --preset coverage
#   cmake --build --preset coverage
#
# STROKE_OUTLINE_COVERAGE=ON instruments everything built in this project (the
# library and the tests). stroke_outline_add_coverage_target(<test target>)
# then defines a `coverage` target that runs the tests and writes:
#   - a per-file summary to the console (regions, functions, lines, branches)
#   - an annotated HTML report at <build>/coverage-report/index.html
# The tests themselves, fetched dependencies and the unused reference clipper
# (src/stroke/clip_weiler_atherton.cpp) are left out of the report.
#
# Small inline/constexpr helpers in headers can show as uncovered even when
# used: the compiler may inline or evaluate them at compile time.
option(STROKE_OUTLINE_COVERAGE "Instrument the build for clang source-based coverage" OFF)

if (NOT STROKE_OUTLINE_COVERAGE)
    return()
endif()

if (EMSCRIPTEN OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "STROKE_OUTLINE_COVERAGE needs a native Clang build (compiler: ${CMAKE_CXX_COMPILER_ID})")
endif()

add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
add_link_options(-fprofile-instr-generate)

# The profile tools must match the compiler's profile format. On macOS use
# Apple's through xcrun: a Homebrew llvm-cov can't read Apple clang's profiles.
if (APPLE)
    find_program(STROKE_OUTLINE_XCRUN xcrun REQUIRED)
    set(STROKE_OUTLINE_LLVM_PROFDATA ${STROKE_OUTLINE_XCRUN} llvm-profdata)
    set(STROKE_OUTLINE_LLVM_COV ${STROKE_OUTLINE_XCRUN} llvm-cov)
else()
    find_program(STROKE_OUTLINE_LLVM_PROFDATA_EXE NAMES llvm-profdata REQUIRED)
    find_program(STROKE_OUTLINE_LLVM_COV_EXE NAMES llvm-cov REQUIRED)
    set(STROKE_OUTLINE_LLVM_PROFDATA ${STROKE_OUTLINE_LLVM_PROFDATA_EXE})
    set(STROKE_OUTLINE_LLVM_COV ${STROKE_OUTLINE_LLVM_COV_EXE})
endif()

function(stroke_outline_add_coverage_target test_target)
    set(dir "${CMAKE_BINARY_DIR}/coverage-report")
    set(raw "${dir}/tests.profraw")
    set(data "${dir}/tests.profdata")
    set(ignore "(tests/|_deps/|clip_weiler_atherton)")
    set(binary "$<TARGET_FILE:${test_target}>")

    add_custom_target(coverage
        COMMAND ${CMAKE_COMMAND} -E make_directory "${dir}"
        COMMAND ${CMAKE_COMMAND} -E rm -f "${raw}"
        # 1. run the tests; the instrumented binary writes raw counts to `raw`
        COMMAND ${CMAKE_COMMAND} -E env "LLVM_PROFILE_FILE=${raw}" "${binary}"
        # 2. index them
        COMMAND ${STROKE_OUTLINE_LLVM_PROFDATA} merge -sparse "${raw}" -o "${data}"
        # 3. per-file summary
        COMMAND ${STROKE_OUTLINE_LLVM_COV} report "${binary}"
            "-instr-profile=${data}" "-ignore-filename-regex=${ignore}"
        # 4. annotated HTML (each line's hit count; 0 = never run)
        COMMAND ${STROKE_OUTLINE_LLVM_COV} show "${binary}"
            "-instr-profile=${data}" "-ignore-filename-regex=${ignore}"
            -format=html "-output-dir=${dir}"
        COMMAND ${CMAKE_COMMAND} -E echo "Coverage report: ${dir}/index.html"
        DEPENDS ${test_target}
        COMMENT "Running the tests with coverage"
        USES_TERMINAL
        VERBATIM)
endfunction()
