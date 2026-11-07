cmake_minimum_required(VERSION 3.19)
project(
        sts_lightspeed
        VERSION 0.1
        DESCRIPTION "High Performance, rng accurate implementation of Slay the Spire with tools for tree search and ml."
        LANGUAGES CXX)

file (GLOB_RECURSE sts_lightspeed_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS  "-Wno-shift-count-overflow -O3")

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


add_executable(main apps/main.cpp ${sts_lightspeed_SOURCES})
target_link_directories(main PRIVATE json::nlohmann_json)
target_include_directories(main PUBLIC include)
target_include_directories(main PUBLIC json/include)

add_executable(legacy-test apps/test.cpp ${sts_lightspeed_SOURCES})
target_include_directories(legacy-test PUBLIC include)
target_link_directories(legacy-test PUBLIC json::nlohmann_json)
target_include_directories(legacy-test PUBLIC json/include)

add_executable(small-test apps/small-test.cpp bindings/bindings-util.cpp ${sts_lightspeed_SOURCES})
target_link_directories(small-test PRIVATE json::nlohmann_json)
target_include_directories(small-test PUBLIC include)
target_include_directories(small-test PUBLIC json/include)
target_include_directories(small-test PUBLIC bindings)

option(STS_BUILD_AI_REGRESSIONS "Build the maintained fork's scoped regression suite" OFF)
if(STS_BUILD_AI_REGRESSIONS)
    enable_testing()
    add_subdirectory(tests/ai-regression)
endif()
