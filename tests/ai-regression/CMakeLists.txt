add_library(sts_regression_core OBJECT ${sts_lightspeed_SOURCES})
target_include_directories(sts_regression_core PRIVATE
    "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/json/single_include")
file(GLOB regression_sources CONFIGURE_DEPENDS "native/*.cpp")
set(regression_targets)
set(regression_runtime_dlls)
if(MINGW)
    get_filename_component(compiler_real "${CMAKE_CXX_COMPILER}" REALPATH)
    get_filename_component(compiler_bin "${compiler_real}" DIRECTORY)
    foreach(dll libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
        execute_process(COMMAND "${CMAKE_CXX_COMPILER}" "-print-file-name=${dll}"
            OUTPUT_VARIABLE dll_path OUTPUT_STRIP_TRAILING_WHITESPACE
            COMMAND_ERROR_IS_FATAL ANY)
        if(NOT IS_ABSOLUTE "${dll_path}" OR NOT EXISTS "${dll_path}")
            set(dll_path "${compiler_bin}/${dll}")
        endif()
        if(NOT EXISTS "${dll_path}")
            message(FATAL_ERROR "Missing compiler runtime: ${dll}")
        endif()
        list(APPEND regression_runtime_dlls "${dll_path}")
    endforeach()
endif()
foreach(source ${regression_sources})
    get_filename_component(name "${source}" NAME_WE)
    set(target "regression_${name}")
    add_executable(${target} "${source}" $<TARGET_OBJECTS:sts_regression_core>)
    target_include_directories(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/json/single_include")
    add_test(NAME "ai.${name}" COMMAND ${target})
    set_tests_properties("ai.${name}" PROPERTIES TIMEOUT 30)
    list(APPEND regression_targets ${target})
endforeach()

if(MINGW AND regression_targets)
    # All regression executables share the runtime directory for their configuration.
    list(GET regression_targets 0 first_regression_target)
    set(regression_runtime_dir "$<TARGET_FILE_DIR:${first_regression_target}>")
    add_custom_target(ai-regression-runtime
        COMMAND ${CMAKE_COMMAND} -E make_directory "${regression_runtime_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${regression_runtime_dlls}
            "${regression_runtime_dir}"
        VERBATIM)
    foreach(target IN LISTS regression_targets)
        add_dependencies(${target} ai-regression-runtime)
    endforeach()
endif()

add_custom_target(ai-regressions DEPENDS ${regression_targets})
