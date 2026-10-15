#include <lic/scripting/scene_api.hpp>

#include <cstdint>
#include <sstream>
#include <string>
#include <utility>

namespace lic::scripting {
namespace {
constexpr std::int64_t kMaxObjectId = 1'000'000;
constexpr std::int64_t kCoordinateLimit = 1'000'000;

[[nodiscard]] std::string object_name(std::int64_t id) {
    return "NEXUS_" + std::to_string(id);
}
[[nodiscard]] bool valid_id(std::int64_t id) noexcept {
    return id >= 0 && id <= kMaxObjectId;
}
[[nodiscard]] bool read_id(std::istringstream& input, std::int64_t& id) {
    return static_cast<bool>(input >> id) && valid_id(id);
}
[[nodiscard]] lic::world::Entity find_entity(lic::world::World& world, std::int64_t id) {
    const std::string expected = object_name(id);
    lic::world::Entity found{};
    world.each<lic::world::NodeName>([&](lic::world::Entity entity, const lic::world::NodeName& name) {
        if (!found.valid() && name.value == expected) found = entity;
    });
    return found;
}
[[nodiscard]] bool read_three(std::istringstream& input, std::int64_t& a,
                              std::int64_t& b, std::int64_t& c) {
    return static_cast<bool>(input >> a >> b >> c) &&
           a >= -kCoordinateLimit && a <= kCoordinateLimit &&
           b >= -kCoordinateLimit && b <= kCoordinateLimit &&
           c >= -kCoordinateLimit && c <= kCoordinateLimit;
}
[[nodiscard]] bool has_trailing_token(std::istringstream& input) {
    std::string extra;
    return static_cast<bool>(input >> extra);
}

void apply_command(const std::string& command, std::istringstream& input,
                   lic::world::World& world, SceneApiResult& result) {
    std::int64_t id{};
    if (!read_id(input, id)) {
        result.messages.emplace_back("NEXUS API: invalid or missing object ID");
        ++result.commands_ignored;
        return;
    }
    const std::string name = object_name(id);

    if (command == "SPAWN_BOX") {
        std::int64_t x{}, y{}, z{}, scale{}, red{}, green{}, blue{};
        if (!(input >> x >> y >> z >> scale >> red >> green >> blue) ||
            x < -kCoordinateLimit || x > kCoordinateLimit ||
            y < -kCoordinateLimit || y > kCoordinateLimit ||
            z < -kCoordinateLimit || z > kCoordinateLimit ||
            scale < 2 || scale > 10'000 ||
            red < 0 || red > 1000 || green < 0 || green > 1000 ||
            blue < 0 || blue > 1000 || has_trailing_token(input)) {
            result.messages.emplace_back("NEXUS API: SPAWN_BOX arguments are outside supported ranges");
            ++result.commands_ignored;
            return;
        }
        const auto previous = find_entity(world, id);
        if (previous.valid()) (void)world.destroy_entity(previous);

        lic::world::Transform transform;
        transform.position = {static_cast<float>(x) / 100.0F,
                              static_cast<float>(y) / 100.0F,
                              static_cast<float>(z) / 100.0F};
        const float scalar = static_cast<float>(scale) / 100.0F;
        transform.scale = {scalar, scalar, scalar};
        lic::world::Appearance appearance;
        appearance.color = {static_cast<float>(red) / 1000.0F,
                            static_cast<float>(green) / 1000.0F,
                            static_cast<float>(blue) / 1000.0F, 1.0F};
        (void)lic::world::create_node(world, lic::world::NodeKind::MeshInstance3D,
                                      name, transform, appearance);
        ++result.commands_applied;
        result.messages.emplace_back("NEXUS API: spawned " + name);
        return;
    }

    const auto entity = find_entity(world, id);
    if (!entity.valid()) {
        result.messages.emplace_back("NEXUS API: object not found: " + name);
        ++result.commands_ignored;
        return;
    }
    if (command == "DESTROY") {
        if (has_trailing_token(input)) {
            ++result.commands_ignored;
            result.messages.emplace_back("NEXUS API: DESTROY expects only an object ID");
        } else if (world.destroy_entity(entity)) {
            ++result.commands_applied;
            result.messages.emplace_back("NEXUS API: destroyed " + name);
        } else {
            ++result.commands_ignored;
        }
        return;
    }
    if (command == "SET_ENABLED") {
        int enabled{};
        if (!(input >> enabled) || (enabled != 0 && enabled != 1) || has_trailing_token(input)) {
            result.messages.emplace_back("NEXUS API: SET_ENABLED expects 0 or 1");
            ++result.commands_ignored;
            return;
        }
        world.get<lic::world::NodeEnabled>(entity).value = enabled == 1;
        ++result.commands_applied;
        return;
    }

    std::int64_t a{}, b{}, c{};
    if (!read_three(input, a, b, c) || has_trailing_token(input)) {
        result.messages.emplace_back("NEXUS API: " + command + " expects three in-range integers");
        ++result.commands_ignored;
        return;
    }
    if (command == "SET_POSITION") {
        world.get<lic::world::Transform>(entity).position = {
            static_cast<float>(a) / 100.0F, static_cast<float>(b) / 100.0F, static_cast<float>(c) / 100.0F};
    } else if (command == "SET_ROTATION") {
        if (a < -360 || a > 360 || b < -360 || b > 360 || c < -360 || c > 360) {
            result.messages.emplace_back("NEXUS API: rotation must be between -360 and 360 degrees");
            ++result.commands_ignored;
            return;
        }
        world.get<lic::world::Transform>(entity).rotation_degrees = {
            static_cast<float>(a), static_cast<float>(b), static_cast<float>(c)};
    } else if (command == "SET_SCALE") {
        if (a < 2 || a > 10'000 || b < 2 || b > 10'000 || c < 2 || c > 10'000) {
            result.messages.emplace_back("NEXUS API: scale must be in [0.02, 100]");
            ++result.commands_ignored;
            return;
        }
        world.get<lic::world::Transform>(entity).scale = {
            static_cast<float>(a) / 100.0F, static_cast<float>(b) / 100.0F, static_cast<float>(c) / 100.0F};
    } else if (command == "SET_COLOR") {
        if (a < 0 || a > 1000 || b < 0 || b > 1000 || c < 0 || c > 1000) {
            result.messages.emplace_back("NEXUS API: RGB values must be in [0, 1000]");
            ++result.commands_ignored;
            return;
        }
        world.get<lic::world::Appearance>(entity).color = {
            static_cast<float>(a) / 1000.0F, static_cast<float>(b) / 1000.0F, static_cast<float>(c) / 1000.0F, 1.0F};
    } else if (command == "SET_SURFACE") {
        if (a < 0 || a > 1000 || b < 40 || b > 1000 || c < 0 || c > 4000) {
            result.messages.emplace_back("NEXUS API: surface values outside supported ranges");
            ++result.commands_ignored;
            return;
        }
        auto& appearance = world.get<lic::world::Appearance>(entity);
        appearance.metallic = static_cast<float>(a) / 1000.0F;
        appearance.roughness = static_cast<float>(b) / 1000.0F;
        appearance.emission_strength = static_cast<float>(c) / 1000.0F;
    } else {
        result.messages.emplace_back("NEXUS API: unknown command: " + command);
        ++result.commands_ignored;
        return;
    }
    ++result.commands_applied;
}
} // namespace

SceneApiResult apply_scene_commands(std::string_view program_output, lic::world::World& world) {
    SceneApiResult result;
    std::istringstream output{std::string(program_output)};
    std::string line;
    while (std::getline(output, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.starts_with("LICAPI ")) continue;
        std::istringstream input(line.substr(7));
        std::string command;
        if (!(input >> command)) {
            ++result.commands_ignored;
            continue;
        }
        apply_command(command, input, world, result);
    }
    return result;
}
} // namespace lic::scripting
