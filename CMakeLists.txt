cmake_minimum_required(VERSION 3.10)
project(hUI C)

set(CMAKE_C_STANDARD 11)

file(GLOB SRC
    src/*.c
    src/html/*.c
    src/css/*.c
    src/style/*.c
    src/layout/*.c
    src/paint/*.c
    src/ir/*.c
)

add_library(hui STATIC ${SRC})
target_include_directories(hui PUBLIC include src)

add_executable(example examples/minimal.c)
target_link_libraries(example hui)

enable_testing()
add_executable(hui_tests tests/test_runner.c)
target_link_libraries(hui_tests hui)
target_include_directories(hui_tests PRIVATE src include)
add_test(NAME hui_tests COMMAND hui_tests)
