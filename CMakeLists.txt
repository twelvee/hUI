cmake_minimum_required(VERSION 3.10)
project(hUI C)

set(CMAKE_C_STANDARD 11)
include(GNUInstallDirs)

option(HUI_ENABLE_COVERAGE "Enable coverage instrumentation" OFF)
option(HUI_BUILD_RAYLIB_EXAMPLE "Build the raylib integration example" ON)

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
target_link_libraries(hui_tests hui)
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
    font_size_application
    ctx_pipeline
)
foreach(test_name IN LISTS HUI_TEST_CASES)
    add_test(NAME hui_${test_name} COMMAND hui_tests ${test_name})
endforeach()

if (HUI_ENABLE_COVERAGE)
    if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(--coverage -O0)
        add_link_options(--coverage)
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
