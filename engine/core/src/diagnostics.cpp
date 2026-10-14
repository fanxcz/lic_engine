#include <lic/core/diagnostics.hpp>

#include <lic/core/version.hpp>

#include <ostream>

namespace lic::core {

void print_build_information(std::ostream& output) {
    const auto current = version();
    output << engine_name() << ' ' << version_string() << '\n';
    output << "Status: foundation milestone; 3D renderer is not implemented yet.\n";
    output << "Version components: " << current.major << '.' << current.minor << '.' << current.patch << '\n';
    output << "Build language: C++23\n";
}

} // namespace lic::core
