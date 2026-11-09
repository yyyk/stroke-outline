set(TARGET_NAME stroke_outline_sandbox)

add_executable(${TARGET_NAME} main.cpp)
target_link_libraries(${TARGET_NAME} PRIVATE ${PROJECT_NAME})
