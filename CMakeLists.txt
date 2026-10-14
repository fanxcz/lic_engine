cmake_minimum_required(VERSION 3.25)

project(LICEngine VERSION 0.1.0 DESCRIPTION "LIC Engine - modular C++ game engine" LANGUAGES CXX)

include(CTest)
option(LIC_ENGINE_BUILD_TESTS "Build LIC Engine tests" ON)
option(LIC_ENGINE_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)
option(LIC_ENGINE_ENABLE_VULKAN "Build the native Vulkan viewport when Vulkan and glslc are available" ON)
option(LIC_ENGINE_FETCH_GLFW "Fetch GLFW 3.4 if it is not installed and the Vulkan backend can be built" OFF)

# Use the static MSVC runtime so the Windows archive does not depend on a
# separately installed Visual C++ redistributable.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

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

# The world module is header-only and remains independent of graphics APIs.
add_library(lic_world INTERFACE)
add_library(LIC::World ALIAS lic_world)
target_include_directories(lic_world INTERFACE engine/world/include)
target_link_libraries(lic_world INTERFACE lic_build_options)

add_executable(lic_editor editor/src/main.cpp)
target_link_libraries(lic_editor PRIVATE LIC::Core lic_build_options)
if(WIN32)
    target_link_libraries(lic_editor PRIVATE user32)
endif()

if(LIC_ENGINE_ENABLE_VULKAN)
    find_package(Vulkan QUIET)
    find_package(glfw3 CONFIG QUIET)
    find_program(LIC_GLSLC_EXECUTABLE NAMES glslc
        HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")

    if(Vulkan_FOUND AND LIC_GLSLC_EXECUTABLE
       AND NOT TARGET glfw AND NOT TARGET glfw3::glfw
       AND LIC_ENGINE_FETCH_GLFW)
        include(FetchContent)
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "Disable GLFW documentation builds" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "Disable GLFW test builds" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Disable GLFW example builds" FORCE)
        set(GLFW_INSTALL OFF CACHE BOOL "Do not install GLFW from the nested build" FORCE)
        # Linux CI builds use GLFW X11 here so configuration does not require wayland-scanner.
        set(BUILD_SHARED_LIBS OFF)
        FetchContent_Declare(
            glfw
            GIT_REPOSITORY https://github.com/glfw/glfw.git
            GIT_TAG 3.4
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(glfw)
    endif()

    if(Vulkan_FOUND AND (TARGET glfw OR TARGET glfw3::glfw) AND LIC_GLSLC_EXECUTABLE)
        set(LIC_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/renderer_shaders")
        set(LIC_VERTEX_SPIRV "${LIC_SHADER_OUTPUT_DIR}/viewport.vert.spv")
        set(LIC_FRAGMENT_SPIRV "${LIC_SHADER_OUTPUT_DIR}/viewport.frag.spv")

        add_custom_command(
            OUTPUT "${LIC_VERTEX_SPIRV}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${LIC_SHADER_OUTPUT_DIR}"
            COMMAND "${LIC_GLSLC_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/engine/renderer/shaders/viewport.vert"
                    -o "${LIC_VERTEX_SPIRV}"
            DEPENDS engine/renderer/shaders/viewport.vert
            VERBATIM
            COMMENT "Compile LIC viewport vertex shader"
        )
        add_custom_command(
            OUTPUT "${LIC_FRAGMENT_SPIRV}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${LIC_SHADER_OUTPUT_DIR}"
            COMMAND "${LIC_GLSLC_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/engine/renderer/shaders/viewport.frag"
                    -o "${LIC_FRAGMENT_SPIRV}"
            DEPENDS engine/renderer/shaders/viewport.frag
            VERBATIM
            COMMENT "Compile LIC viewport fragment shader"
        )
        add_custom_target(lic_renderer_shaders DEPENDS "${LIC_VERTEX_SPIRV}" "${LIC_FRAGMENT_SPIRV}")

        add_library(lic_renderer engine/renderer/src/vulkan_viewport.cpp)
        add_library(LIC::Renderer ALIAS lic_renderer)
        add_dependencies(lic_renderer lic_renderer_shaders)
        target_include_directories(lic_renderer PUBLIC engine/renderer/include)
        if(TARGET glfw)
            target_link_libraries(lic_renderer PUBLIC Vulkan::Vulkan glfw)
        else()
            target_link_libraries(lic_renderer PUBLIC Vulkan::Vulkan glfw3::glfw)
        endif()
        target_link_libraries(lic_renderer PUBLIC lic_build_options)
        target_compile_definitions(lic_editor PRIVATE LIC_HAS_VULKAN_VIEWPORT=1)
        target_link_libraries(lic_editor PRIVATE LIC::Renderer)
        add_custom_command(TARGET lic_editor POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                    "${LIC_SHADER_OUTPUT_DIR}" "$<TARGET_FILE_DIR:lic_editor>/shaders"
            VERBATIM
            COMMENT "Copy LIC viewport shaders beside the executable"
        )
        message(STATUS "LIC Engine Vulkan viewport: enabled (GLFW + Vulkan + glslc)")
    else()
        message(STATUS "LIC Engine Vulkan viewport: disabled (requires Vulkan dev files, GLFW 3, and glslc)")
    endif()
else()
    message(STATUS "LIC Engine Vulkan viewport: disabled by LIC_ENGINE_ENABLE_VULKAN=OFF")
endif()

if(BUILD_TESTING AND LIC_ENGINE_BUILD_TESTS)
    add_executable(lic_core_tests tests/core_tests.cpp)
    target_link_libraries(lic_core_tests PRIVATE LIC::Core LIC::World lic_build_options)
    add_test(NAME lic_core_tests COMMAND lic_core_tests)
    add_test(NAME lic_editor_help COMMAND lic_editor --help)
    add_test(NAME lic_editor_version COMMAND lic_editor --version)
    add_test(NAME lic_editor_diagnostics COMMAND lic_editor --diagnostics)
endif()

message(STATUS "LIC Engine ${PROJECT_VERSION}")
message(STATUS "C++ standard: C++23")
message(STATUS "Build tests: ${BUILD_TESTING} / ${LIC_ENGINE_BUILD_TESTS}")
