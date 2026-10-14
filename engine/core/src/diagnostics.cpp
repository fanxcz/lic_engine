#include <lic/core/diagnostics.hpp>

#include <lic/core/version.hpp>

#include <ostream>

namespace lic::core {

void print_build_information(std::ostream& output) {
    const auto current = version();
    output << engine_name() << ' ' << version_string() << '\n';
    output << "Status: early foundation; the optional Vulkan viewport renders a test cube.\n";
    output << "World model: generation-safe entities and typed sparse-set components.\n";
    output << "Version components: " << current.major << '.' << current.minor << '.' << current.patch << '\n';
    output << "Build language: C++23\n";
}

} // namespace lic::core
