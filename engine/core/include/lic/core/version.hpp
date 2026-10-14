#pragma once

#include <string_view>

namespace lic::core {

struct Version final {
    int major;
    int minor;
    int patch;
};

[[nodiscard]] constexpr Version version() noexcept {
    return {0, 1, 0};
}

[[nodiscard]] std::string_view version_string() noexcept;
[[nodiscard]] std::string_view engine_name() noexcept;

} // namespace lic::core
