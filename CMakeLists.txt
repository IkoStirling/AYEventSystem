cmake_minimum_required(VERSION 3.20)

# AYEventSystem — engine-wide typed event bus
# See design.md for the full spec.
project(AYEventSystem)

set(MODULE_NAME AYEventSystem)

set(SOURCES
    src/AYEventBus.cpp
)

set(HEADERS
    include/ayevent/EventPriority.h
    include/ayevent/Connection.h
    include/ayevent/EventBus.h
    include/ayevent/Events/WindowEvents.h
)

add_library(${MODULE_NAME} STATIC ${SOURCES} ${HEADERS})

target_include_directories(${MODULE_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Phase 1 dependency surface is intentionally tiny: standard library only.
# Listeners may be functions/lambdas with captures; no engine modules pulled in
# so AYEventSystem can sit at the bottom of the runtime DAG (alongside AYCore).

target_compile_options(${MODULE_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>: /W4>
    $<$<CXX_COMPILER_ID:GNU,Clang>: -Wall -Wextra -Wpedantic>
)

enable_testing()
add_subdirectory(unittest)