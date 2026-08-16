cmake_minimum_required(VERSION 3.20)

# AYEventSystem unit tests
project(AYEventSystem_Tests)

set(TEST_TARGET AYEventSystem_Tests)

add_executable(${TEST_TARGET}
    main.cpp
    Test_EventBus.cpp
)

target_link_libraries(${TEST_TARGET} PRIVATE
    AYEventSystem
    AYTest
)

target_include_directories(${TEST_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AYFoundation/AYTest
)

target_compile_definitions(${TEST_TARGET} PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_compile_options(${TEST_TARGET} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>: /W4 /wd4200>
    $<$<CXX_COMPILER_ID:GNU,Clang>: -Wall -Wextra -Wpedantic>
)

ay_add_test(${TEST_TARGET})
