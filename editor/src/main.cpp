#include <lic/core/diagnostics.hpp>
#include <lic/core/version.hpp>

#ifdef LIC_HAS_VULKAN_VIEWPORT
#include <lic/renderer/vulkan_viewport.hpp>
#endif

#include <filesystem>
#include <iostream>
#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

int main(int argc, char** argv) {
    if (argc > 1) {
        const std::string_view argument{argv[1]};
        if (argument == "--version" || argument == "-v") {
            std::cout << lic::core::engine_name() << ' ' << lic::core::version_string() << '\n';
            return 0;
        }
        if (argument == "--help" || argument == "-h") {
            std::cout << "LIC Engine - native game-engine foundation\n"
                         "Usage: lic_editor [--help] [--version] [--diagnostics] [--window]\n"
                         "  --diagnostics  Print build and platform information\n"
                         "  --window       Open the native Vulkan viewport (when built with Vulkan + GLFW)\n";
            return 0;
        }
        if (argument == "--window") {
#ifdef LIC_HAS_VULKAN_VIEWPORT
            return lic::renderer::run_vulkan_viewport(std::filesystem::absolute(std::filesystem::path(argv[0])));
#else
            std::cerr << "This build has no Vulkan viewport. Install Vulkan development files, GLFW 3, and glslc, "
                         "then reconfigure with LIC_ENGINE_ENABLE_VULKAN=ON.\n";
            return 3;
#endif
        }
        if (argument != "--diagnostics") {
            std::cerr << "Unknown option: " << argument << "\nUse --help to see available options.\n";
            return 2;
        }
    }

#ifdef LIC_HAS_VULKAN_VIEWPORT
    // Launch the graphical viewport only when the executable is opened without CLI arguments.
    if (argc == 1) {
        return lic::renderer::run_vulkan_viewport(std::filesystem::absolute(std::filesystem::path(argv[0])));
    }
#else
#ifdef _WIN32
    // Explain core-only builds instead of closing silently after a double-click.
    if (argc == 1) {
        lic::core::print_build_information(std::cout);
        MessageBoxW(nullptr,
                    L"This build contains the engine core only, without the 3D viewport.\n\n"
                    L"Download and extract the complete Windows ZIP package from the LIC Engine website.",
                    L"LIC Engine - viewport unavailable",
                    MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
        return 3;
    }
#endif
#endif
    lic::core::print_build_information(std::cout);
    return 0;
}
