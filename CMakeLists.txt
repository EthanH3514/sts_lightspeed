cmake_minimum_required(VERSION 3.19)
project(
        sts_lightspeed
        VERSION 0.1
        DESCRIPTION "High Performance, rng accurate implementation of Slay the Spire with tools for tree search and ml."
        LANGUAGES CXX)

file (GLOB_RECURSE sts_lightspeed_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

set(CMAKE_CXX_STANDARD 17)
set(STS_OPTIMIZATION "-O3" CACHE STRING "Compiler optimization level")
set(CMAKE_CXX_FLAGS  "-Wno-shift-count-overflow ${STS_OPTIMIZATION}")

add_subdirectory(json)
add_subdirectory(pybind11)

pybind11_add_module(slaythespire bindings/slaythespire.cpp bindings/bindings-util.cpp ${sts_lightspeed_SOURCES})
target_include_directories(slaythespire PUBLIC include)
target_include_directories(slaythespire PUBLIC json/single_include)
target_include_directories(slaythespire PUBLIC bindings)

# Optional first-party no-oracle search, maintained by the consuming AI project.
# The fork still builds on its own with no external source path or policy change.
set(SPIRE_AI_SEARCH_SOURCE_ROOT "" CACHE PATH "Directory containing combat_search/Engine.h")
option(SPIRE_AI_SEARCH_TEST_FIXTURES "Enable private constructed-world differential tests" OFF)
option(SPIRE_AI_SESSION_SEARCH "Enable the consumer's public-history session search and draw hooks" OFF)
if(SPIRE_AI_SESSION_SEARCH AND NOT SPIRE_AI_SEARCH_SOURCE_ROOT)
    message(FATAL_ERROR "Session search requires an explicit SPIRE_AI_SEARCH_SOURCE_ROOT")
endif()
if(SPIRE_AI_SEARCH_SOURCE_ROOT)
    target_include_directories(slaythespire PRIVATE "${SPIRE_AI_SEARCH_SOURCE_ROOT}")
    target_compile_definitions(slaythespire PRIVATE SPIRE_AI_PUBLIC_SEARCH=1)
    target_compile_options(slaythespire PRIVATE -ffp-contract=off)
    if(SPIRE_AI_SESSION_SEARCH)
        target_compile_definitions(slaythespire PRIVATE SPIRE_AI_SESSION_SEARCH=1 SPIRE_AI_DRAW_EVENTS=1)
    endif()
    if(SPIRE_AI_SEARCH_TEST_FIXTURES)
        target_include_directories(slaythespire PRIVATE "${SPIRE_AI_SEARCH_SOURCE_ROOT}/../tests")
        target_compile_definitions(slaythespire PRIVATE SPIRE_AI_SEARCH_TEST_FIXTURES=1)
    endif()
endif()


# Build the standalone console and legacy developer tools only on request.
add_executable(main EXCLUDE_FROM_ALL apps/main.cpp ${sts_lightspeed_SOURCES})
target_link_libraries(main PRIVATE nlohmann_json::nlohmann_json)
target_include_directories(main PUBLIC include)

add_executable(legacy-test EXCLUDE_FROM_ALL apps/test.cpp apps/simple-agent.cpp ${sts_lightspeed_SOURCES})
target_include_directories(legacy-test PUBLIC include)
target_link_libraries(legacy-test PRIVATE nlohmann_json::nlohmann_json)

option(STS_BUILD_AI_REGRESSIONS "Build the maintained fork's scoped regression suite" OFF)
if(STS_BUILD_AI_REGRESSIONS)
    enable_testing()
    add_subdirectory(tests/ai-regression)
endif()
