cmake_minimum_required(VERSION 3.25)

project(LICEngine VERSION 0.1.0 DESCRIPTION "LIC Engine - modular C++ game engine" LANGUAGES CXX)

include(CTest)
option(LIC_ENGINE_BUILD_TESTS "Build LIC Engine tests" ON)
option(LIC_ENGINE_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)

add_library(lic_build_options INTERFACE)
target_compile_features(lic_build_options INTERFACE cxx_std_23)
set_target_properties(lic_build_options PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(lic_build_options INTERFACE /W4 /permissive- /Zc:preprocessor)
    if(LIC_ENGINE_WARNINGS_AS_ERRORS)
        target_compile_options(lic_build_options INTERFACE /WX)
    endif()
else()
    target_compile_options(lic_build_options INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(LIC_ENGINE_WARNINGS_AS_ERRORS)
        target_compile_options(lic_build_options INTERFACE -Werror)
    endif()
endif()

add_library(lic_core engine/core/src/version.cpp engine/core/src/diagnostics.cpp)
add_library(LIC::Core ALIAS lic_core)
target_include_directories(lic_core PUBLIC engine/core/include)
target_link_libraries(lic_core PUBLIC lic_build_options)

add_executable(lic_editor editor/src/main.cpp)
target_link_libraries(lic_editor PRIVATE LIC::Core lic_build_options)

if(BUILD_TESTING AND LIC_ENGINE_BUILD_TESTS)
    add_executable(lic_core_tests tests/core_tests.cpp)
    target_link_libraries(lic_core_tests PRIVATE LIC::Core lic_build_options)
    add_test(NAME lic_core_tests COMMAND lic_core_tests)
endif()

message(STATUS "LIC Engine ${PROJECT_VERSION}")
message(STATUS "C++ standard: C++23")
message(STATUS "Build tests: ${BUILD_TESTING} / ${LIC_ENGINE_BUILD_TESTS}")
message(STATUS "Renderer: not implemented yet (foundation milestone)")
