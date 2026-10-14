#pragma once

#include <filesystem>

namespace lic::renderer {

// Opens the native Vulkan viewport and locates shaders beside the executable.
// Returns 0 on normal exit and a non-zero value if initialization or rendering fails.
int run_vulkan_viewport(const std::filesystem::path& executable_path, bool start_in_game = false);

} // namespace lic::renderer
