cmake_minimum_required(VERSION 3.10)
project(hUI C)

set(CMAKE_C_STANDARD 11)
include(GNUInstallDirs)

option(HUI_ENABLE_COVERAGE "Enable coverage instrumentation" OFF)
option(HUI_BUILD_RAYLIB_EXAMPLE "Build the raylib integration example" ON)
option(HUI_COVERAGE_FORCE_GCOV "Link gcov coverage runtime explicitly" OFF)

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
target_link_libraries(example PRIVATE hui)

if (HUI_BUILD_RAYLIB_EXAMPLE)
    include(FetchContent)
    set(RAYLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(RAYLIB_INSTALL OFF CACHE BOOL "" FORCE)
    set(RAYLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        raylib
        URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
    )
    FetchContent_MakeAvailable(raylib)

    add_executable(hui_raylib_example examples/raylib_example.c)
    target_link_libraries(hui_raylib_example PRIVATE hui raylib)
endif()

enable_testing()
add_executable(hui_tests tests/test_runner.c)
target_link_libraries(hui_tests PRIVATE hui)
target_include_directories(hui_tests PRIVATE src include)
set(HUI_TEST_CASES
    intern
    html_builder
    css_parser
    style_layout_paint
    filter_spec
    dom_mutations
    queries
    ir_serialization
    button_text_color
    input_hover_interaction
    font_size_application
    ctx_pipeline
)
foreach(test_name IN LISTS HUI_TEST_CASES)
    add_test(NAME hui_${test_name} COMMAND hui_tests ${test_name})
endforeach()

if (HUI_ENABLE_COVERAGE)
    if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(hui PRIVATE --coverage -O0)
        target_link_options(hui PRIVATE --coverage)
        target_compile_options(hui_tests PRIVATE --coverage -O0)
        target_link_options(hui_tests PRIVATE --coverage)
        if (TARGET hui_raylib_example)
            target_compile_options(hui_raylib_example PRIVATE --coverage -O0)
            target_link_options(hui_raylib_example PRIVATE --coverage)
        endif ()
        if (HUI_COVERAGE_FORCE_GCOV)
            target_link_libraries(hui PRIVATE gcov)
            target_link_libraries(hui_tests PRIVATE gcov)
            if (TARGET hui_raylib_example)
                target_link_libraries(hui_raylib_example PRIVATE gcov)
            endif ()
        endif ()
    else ()
        message(WARNING "Coverage requested, but compiler ${CMAKE_C_COMPILER_ID} is not supported for coverage flags.")
    endif ()
endif ()

install(TARGETS hui
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
