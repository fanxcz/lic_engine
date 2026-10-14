cmake_minimum_required(VERSION 3.25)

project(LICEngine VERSION 0.1.0 DESCRIPTION "LIC Engine - modular C++ game engine" LANGUAGES CXX)

include(CTest)
option(LIC_ENGINE_BUILD_TESTS "Build LIC Engine tests" ON)
option(LIC_ENGINE_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)
option(LIC_ENGINE_ENABLE_VULKAN "Build the native Vulkan viewport when Vulkan and glslc are available" ON)
option(LIC_ENGINE_FETCH_GLFW "Fetch GLFW 3.4 if it is not installed and the Vulkan backend can be built" OFF)
option(LIC_ENGINE_ENABLE_NEXUS "Build the NEXUS language compiler for LIC Engine scripting" ON)

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

add_library(lic_game engine/game/src/voidbound.cpp)
add_library(LIC::Game ALIAS lic_game)
target_include_directories(lic_game PUBLIC engine/game/include)
target_link_libraries(lic_game PUBLIC LIC::World lic_build_options)

add_library(lic_scripting engine/scripting/src/process.cpp)
add_library(LIC::Scripting ALIAS lic_scripting)
target_include_directories(lic_scripting PUBLIC engine/scripting/include)
target_link_libraries(lic_scripting PUBLIC lic_build_options)

if(LIC_ENGINE_ENABLE_NEXUS)
    include(FetchContent)
    FetchContent_Declare(
        lic_nexus_source
        GIT_REPOSITORY https://github.com/fanxcz/nexus-lang.git
        GIT_TAG 7cfe12b451883497323d5021309e2ff57bcd202d
        GIT_SHALLOW FALSE
    )
    FetchContent_GetProperties(lic_nexus_source)
    if(NOT lic_nexus_source_POPULATED)
        FetchContent_Populate(lic_nexus_source)
    endif()

    add_executable(lic_nexus
        ${lic_nexus_source_SOURCE_DIR}/src/cli/main.cpp
        ${lic_nexus_source_SOURCE_DIR}/src/compiler/format/formatter.cpp
        ${lic_nexus_source_SOURCE_DIR}/src/compiler/frontend/lexer/lexer.cpp
        ${lic_nexus_source_SOURCE_DIR}/src/compiler/frontend/parser/parser.cpp
        ${lic_nexus_source_SOURCE_DIR}/src/compiler/semantic/semantic.cpp
        ${lic_nexus_source_SOURCE_DIR}/src/compiler/backend/codegen/llvm_ir.cpp
        ${lic_nexus_source_SOURCE_DIR}/src/compiler/backend/native/native.cpp
        ${lic_nexus_source_SOURCE_DIR}/src/compiler/backend/native/target.cpp
        ${lic_nexus_source_SOURCE_DIR}/src/compiler/backend/native/pe_elf.cpp
        ${lic_nexus_source_SOURCE_DIR}/src/compiler/driver/driver.cpp
    )
    set_target_properties(lic_nexus PROPERTIES OUTPUT_NAME nexus)
    target_include_directories(lic_nexus PRIVATE
        ${lic_nexus_source_SOURCE_DIR}/src/compiler
        ${lic_nexus_source_SOURCE_DIR}/src/compiler/frontend
        ${lic_nexus_source_SOURCE_DIR}/src/compiler/backend
    )
    target_compile_features(lic_nexus PRIVATE cxx_std_20)
    target_compile_definitions(lic_nexus PRIVATE
        NEXUS_SOURCE_ROOT="${lic_nexus_source_SOURCE_DIR}"
        NEXUS_VERSION="1.1.0"
    )
    if(MSVC)
        target_compile_options(lic_nexus PRIVATE /W4 /permissive- /utf-8)
        set_property(TARGET lic_nexus PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        target_compile_options(lic_nexus PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion
            -Wno-misleading-indentation -Wno-shadow -Wno-unused-parameter)
    endif()
    add_custom_command(TARGET lic_nexus POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:lic_nexus>/runtime"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${lic_nexus_source_SOURCE_DIR}/src/runtime/runtime.cpp"
            "$<TARGET_FILE_DIR:lic_nexus>/runtime/runtime.cpp"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${lic_nexus_source_SOURCE_DIR}/stdlib" "$<TARGET_FILE_DIR:lic_nexus>/stdlib"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${lic_nexus_source_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:lic_nexus>/NEXUS-LICENSE"
        VERBATIM
        COMMENT "Bundle NEXUS runtime and standard library"
    )
endif()

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

    include(FetchContent)
    if(Vulkan_FOUND AND LIC_GLSLC_EXECUTABLE
       AND NOT TARGET glfw AND NOT TARGET glfw3::glfw
       AND LIC_ENGINE_FETCH_GLFW)
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
        if(TARGET glfw)
            set(LIC_GLFW_TARGET glfw)
        else()
            set(LIC_GLFW_TARGET glfw3::glfw)
        endif()

        FetchContent_Declare(
            imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG v1.91.9b
            GIT_SHALLOW TRUE
        )
        FetchContent_GetProperties(imgui)
        if(NOT imgui_POPULATED)
            FetchContent_Populate(imgui)
        endif()

        add_library(lic_imgui STATIC
            ${imgui_SOURCE_DIR}/imgui.cpp
            ${imgui_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_SOURCE_DIR}/imgui_widgets.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
        )
        add_library(LIC::ImGui ALIAS lic_imgui)
        target_include_directories(lic_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
        target_link_libraries(lic_imgui PUBLIC Vulkan::Vulkan ${LIC_GLFW_TARGET})

        add_library(lic_editor_ui engine/editor/src/editor_ui.cpp)
        add_library(LIC::EditorUI ALIAS lic_editor_ui)
        target_include_directories(lic_editor_ui PUBLIC engine/editor/include)
        target_link_libraries(lic_editor_ui PUBLIC LIC::World LIC::Game LIC::ImGui LIC::Scripting lic_build_options)
        if(TARGET lic_nexus)
            target_compile_definitions(lic_editor_ui PRIVATE LIC_ENGINE_HAS_NEXUS=1)
        endif()

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
        target_link_libraries(lic_renderer PUBLIC LIC::EditorUI)
        if(WIN32)
            target_link_libraries(lic_renderer PUBLIC user32)
        endif()
        if(TARGET glfw)
            target_link_libraries(lic_renderer PUBLIC Vulkan::Vulkan glfw)
        else()
            target_link_libraries(lic_renderer PUBLIC Vulkan::Vulkan glfw3::glfw)
        endif()
        target_link_libraries(lic_renderer PUBLIC lic_build_options)
        target_compile_definitions(lic_editor PRIVATE LIC_HAS_VULKAN_VIEWPORT=1)
        target_link_libraries(lic_editor PRIVATE LIC::Renderer)

        add_executable(voidbound editor/src/main.cpp)
        target_compile_definitions(voidbound PRIVATE LIC_HAS_VULKAN_VIEWPORT=1 LIC_START_IN_GAME=1)
        target_link_libraries(voidbound PRIVATE LIC::Core LIC::Renderer lic_build_options)
        # Share the shader folder copied by lic_editor's post-build step; avoid racing two copies.
        add_dependencies(voidbound lic_editor)

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
    if(TARGET lic_nexus)
        add_test(NAME lic_nexus_version COMMAND lic_nexus --version)
        set_tests_properties(lic_nexus_version PROPERTIES PASS_REGULAR_EXPRESSION "Nexus 1.1.0")
        add_test(NAME lic_nexus_rules_check
            COMMAND lic_nexus check "${CMAKE_CURRENT_SOURCE_DIR}/examples/nexus/GameRules.nx")
        if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
            set(LIC_NEXUS_RULES_SMOKE "${CMAKE_CURRENT_BINARY_DIR}/nexus-game-rules-smoke${CMAKE_EXECUTABLE_SUFFIX}")
            add_test(NAME lic_nexus_rules_build
                COMMAND lic_nexus build
                    "${CMAKE_CURRENT_SOURCE_DIR}/examples/nexus/GameRules.nx"
                    -o "${LIC_NEXUS_RULES_SMOKE}" --backend native)
            add_test(NAME lic_nexus_rules_run COMMAND "${LIC_NEXUS_RULES_SMOKE}")
            set_tests_properties(lic_nexus_rules_run PROPERTIES
                DEPENDS lic_nexus_rules_build
                PASS_REGULAR_EXPRESSION "LICCFG_PLAYER_HEALTH=100")
        endif()
    endif()
    add_executable(lic_core_tests tests/core_tests.cpp)
    target_link_libraries(lic_core_tests PRIVATE LIC::Core LIC::World LIC::Game lic_build_options)
    add_test(NAME lic_core_tests COMMAND lic_core_tests)
    add_test(NAME lic_editor_help COMMAND lic_editor --help)
    add_test(NAME lic_editor_version COMMAND lic_editor --version)
    add_test(NAME lic_editor_diagnostics COMMAND lic_editor --diagnostics)
endif()

message(STATUS "LIC Engine ${PROJECT_VERSION}")
message(STATUS "C++ standard: C++23")
message(STATUS "Build tests: ${BUILD_TESTING} / ${LIC_ENGINE_BUILD_TESTS}")
