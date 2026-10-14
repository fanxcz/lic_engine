#include <lic/core/version.hpp>

#include <cstdlib>
#include <iostream>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    const auto current = lic::core::version();
    expect(current.major == 0, "major version should be zero during foundation stage");
    expect(current.minor == 1, "minor version should match project version");
    expect(current.patch == 0, "patch version should match project version");
    expect(lic::core::version_string() == "0.1.0", "version string should be stable");
    expect(lic::core::engine_name() == "LIC Engine", "engine name should be stable");

    std::cout << "LIC Engine core foundation tests passed.\n";
    return EXIT_SUCCESS;
}
