# The native benchmark (bench/native/main.cpp). Not part of normal builds:
#   cmake --build --preset native-release --target stroke_outline_bench
#   bin/native/stroke_outline_bench
set(TARGET_NAME stroke_outline_bench)

add_executable(${TARGET_NAME} main.cpp)
target_link_libraries(${TARGET_NAME} PRIVATE ${PROJECT_NAME})
target_compile_definitions(${TARGET_NAME} PRIVATE
    STROKE_OUTLINE_BENCH_INPUTS="${PROJECT_SOURCE_DIR}/bench/inputs")
