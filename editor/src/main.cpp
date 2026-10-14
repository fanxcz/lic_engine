#include <lic/core/diagnostics.hpp>
#include <lic/core/version.hpp>

#ifdef LIC_HAS_VULKAN_VIEWPORT
#include <lic/renderer/vulkan_viewport.hpp>
#endif

#include <filesystem>
#include <iostream>
#include <string_view>

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
    // Launch the graphical viewport when the executable is opened without CLI arguments.
    return lic::renderer::run_vulkan_viewport(std::filesystem::absolute(std::filesystem::path(argv[0])));
#else
    lic::core::print_build_information(std::cout);
    return 0;
#endif
}
