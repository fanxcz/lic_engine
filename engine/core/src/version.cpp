#include <lic/core/version.hpp>

namespace lic::core {

std::string_view version_string() noexcept {
    return "0.1.0";
}

std::string_view engine_name() noexcept {
    return "LIC Engine";
}

} // namespace lic::core
