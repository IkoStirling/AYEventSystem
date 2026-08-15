cmake_minimum_required(VERSION 3.20)

# AYEventSystem — engine-wide typed event bus
# See design.md for the full spec.
project(AYEventSystem
    VERSION 0.1.0
    DESCRIPTION "AYEventSystem — typed engine-wide event bus + facade migration"
)

set(MODULE_NAME AYEventSystem)

set(SOURCES
    src/AYEventBus.cpp
)

set(HEADERS
    AYEventSystem.h
    include/AYEventSystem/EventPriority.h
    include/AYEventSystem/Connection.h
    include/AYEventSystem/EventBus.h
    include/AYEventSystem/Events/WindowEvents.h
    include/AYEventSystem/Events/SceneEvents.h
    include/AYEventSystem/Events/DeviceEvents.h
    include/AYEventSystem/Events/ResourceEvents.h
    include/AYEventSystem/Events/PhysicsEvents.h
    include/AYEventSystem/Events/ScriptTestEvents.h
    include/AYEventSystem/Events/TaskEvents.h
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

message(STATUS "AYEventSystem v0.1.0 (typed event bus + GameLoopEventBus facade migration) — see ${CMAKE_CURRENT_SOURCE_DIR}/design.md")
