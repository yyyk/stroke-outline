set(TARGET_NAME stroke_outline_tests)

# doctest is the test framework (same one prism uses). Pull it in with
# FetchContent so there's nothing to vendor or install by hand.
include(FetchContent)
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.5.3
)
# doctest is only for building the tests: keep it out of `cmake --install`.
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(doctest)

# One executable gathers every *.test.cpp file. Test files mirror src/ (a test
# lives at the path of the source file whose behaviour it covers); shared
# helpers are in support/. Add new test files here.
# doctest_main.cpp provides main(); every *.test.cpp only includes <doctest/doctest.h>.
add_executable(${TARGET_NAME}
    doctest_main.cpp
    stroke_outline.test.cpp
    bezier/arc.test.cpp
    bezier/arc_length.test.cpp
    bezier/bounds.test.cpp
    bezier/intersect.test.cpp
    bezier/offset.test.cpp
    bezier/utils.test.cpp
    io/path.test.cpp
    io/svg_arc.test.cpp
    io/svg_path.test.cpp
    math/geom.test.cpp
    math/polynomial.test.cpp
    stroke/cap.test.cpp
    stroke/clip.test.cpp
    stroke/dash.test.cpp
    stroke/join.test.cpp
)

target_compile_features(${TARGET_NAME} PRIVATE cxx_std_20)
target_link_libraries(${TARGET_NAME} PRIVATE ${PROJECT_NAME} doctest::doctest)
# Internal headers (src/math, src/bezier, src/stroke) so tests can exercise
# header-declared internal helpers directly. The library's src/ include is PRIVATE.
target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})

# Register each TEST_CASE with ctest so `ctest` can list, filter, and run them
# individually. This subdir is only added on native builds, so it never runs
# against the WASM test binary (which couldn't execute on the host anyway).
include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(${TARGET_NAME})

# `coverage` target (see cmake/Coverage.cmake): only with STROKE_OUTLINE_COVERAGE.
if (STROKE_OUTLINE_COVERAGE)
    stroke_outline_add_coverage_target(${TARGET_NAME})
endif()
