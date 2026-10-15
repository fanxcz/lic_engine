#pragma once

#include <lic/world/scene.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace lic::scripting {

// Applies safe declarative commands emitted by a compiled NEXUS program.
// Commands mutate only the supplied ECS world and never run shell commands.
struct SceneApiResult final {
    std::size_t commands_applied{};
    std::size_t commands_ignored{};
    std::vector<std::string> messages;
};

[[nodiscard]] SceneApiResult apply_scene_commands(std::string_view program_output,
                                                   lic::world::World& world);

} // namespace lic::scripting
