#pragma once

namespace lic::renderer {

// Opens the native Vulkan viewport and keeps it alive until the window closes.
// Returns 0 on normal exit and a non-zero value if initialization or rendering fails.
int run_vulkan_viewport();

} // namespace lic::renderer
