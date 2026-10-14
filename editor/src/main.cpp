#include <lic/core/diagnostics.hpp>
#include <lic/core/version.hpp>

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
            std::cout << "LIC Engine - initial foundation tools\n"
                         "Usage: lic_editor [--help] [--version] [--diagnostics]\n";
            return 0;
        }
        if (argument != "--diagnostics") {
            std::cerr << "Unknown option: " << argument << "\nUse --help to see available options.\n";
            return 2;
        }
    }

    lic::core::print_build_information(std::cout);
    return 0;
}
