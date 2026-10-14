#pragma once

#include <lic/world/world.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lic::world {

enum class NodeKind : std::uint8_t {
    Node3D = 0,
    MeshInstance3D = 1,
    Camera3D = 2,
    DirectionalLight3D = 3,
    StaticBody3D = 4
};

struct NodeName final {
    std::string value;
};

struct Transform final {
    std::array<float, 3> position{0.0F, 0.0F, 0.0F};
    std::array<float, 3> rotation_degrees{0.0F, 0.0F, 0.0F};
    std::array<float, 3> scale{1.0F, 1.0F, 1.0F};
};

struct NodeKindComponent final {
    NodeKind value{NodeKind::Node3D};
};

struct Appearance final {
    std::array<float, 4> color{0.30F, 0.67F, 0.90F, 1.0F};
};

struct NodeEnabled final {
    bool value{true};
};

[[nodiscard]] inline std::string_view node_kind_name(NodeKind kind) noexcept {
    switch (kind) {
    case NodeKind::Node3D: return "Node3D";
    case NodeKind::MeshInstance3D: return "MeshInstance3D";
    case NodeKind::Camera3D: return "Camera3D";
    case NodeKind::DirectionalLight3D: return "DirectionalLight3D";
    case NodeKind::StaticBody3D: return "StaticBody3D";
    }
    return "Unknown";
}

[[nodiscard]] inline bool node_kind_renders_mesh(NodeKind kind) noexcept {
    return kind == NodeKind::MeshInstance3D || kind == NodeKind::StaticBody3D;
}

[[nodiscard]] inline Entity create_node(World& world, NodeKind kind, std::string name,
                                        Transform transform = {}, Appearance appearance = {}) {
    const Entity entity = world.create_entity();
    try {
        world.emplace<NodeName>(entity, NodeName{std::move(name)});
        world.emplace<Transform>(entity, std::move(transform));
        world.emplace<NodeKindComponent>(entity, NodeKindComponent{kind});
        world.emplace<Appearance>(entity, std::move(appearance));
        world.emplace<NodeEnabled>(entity, NodeEnabled{true});
    } catch (...) {
        (void)world.destroy_entity(entity);
        throw;
    }
    return entity;
}

inline void save_scene(const World& world, const std::filesystem::path& path) {
    if (path.empty()) {
        throw std::invalid_argument("Scene path is empty");
    }
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Cannot write scene file: " + temporary);
    }

    std::size_t count = 0;
    world.each<NodeName>([&](Entity entity, const NodeName&) {
        if (world.has<Transform>(entity) && world.has<NodeKindComponent>(entity) &&
            world.has<Appearance>(entity) && world.has<NodeEnabled>(entity)) {
            ++count;
        }
    });
    output << "LICSCENE 1\n" << "nodes " << count << '\n' << std::setprecision(9);
    world.each<NodeName>([&](Entity entity, const NodeName& name) {
        if (!world.has<Transform>(entity) || !world.has<NodeKindComponent>(entity) ||
            !world.has<Appearance>(entity) || !world.has<NodeEnabled>(entity)) {
            return;
        }
        const auto& transform = world.get<Transform>(entity);
        const auto& kind = world.get<NodeKindComponent>(entity);
        const auto& appearance = world.get<Appearance>(entity);
        const auto& enabled = world.get<NodeEnabled>(entity);
        output << "node " << static_cast<unsigned>(kind.value) << ' ' << std::quoted(name.value);
        for (const float value : transform.position) output << ' ' << value;
        for (const float value : transform.rotation_degrees) output << ' ' << value;
        for (const float value : transform.scale) output << ' ' << value;
        for (const float value : appearance.color) output << ' ' << value;
        output << ' ' << (enabled.value ? 1 : 0) << '\n';
    });
    output.flush();
    if (!output) {
        output.close();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw std::runtime_error("Failed while writing scene file: " + temporary);
    }
    output.close();

    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        // Windows cannot replace an existing destination with rename; use a backup to avoid truncation.
        const auto backup = path.string() + ".bak";
        std::error_code backupError;
        const bool hadOriginal = std::filesystem::exists(path);
        if (hadOriginal) {
            std::filesystem::remove(backup, backupError);
            backupError.clear();
            std::filesystem::rename(path, backup, backupError);
            if (backupError) {
                std::filesystem::remove(temporary, backupError);
                throw std::runtime_error("Cannot replace scene file: " + path.string());
            }
        }
        error.clear();
        std::filesystem::rename(temporary, path, error);
        if (error) {
            if (hadOriginal) {
                std::error_code restoreError;
                std::filesystem::rename(backup, path, restoreError);
            }
            std::filesystem::remove(temporary, backupError);
            throw std::runtime_error("Cannot finish saving scene file: " + path.string());
        }
        if (hadOriginal) std::filesystem::remove(backup, backupError);
    }
}

inline void load_scene(World& destination, const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open scene file: " + path.string());
    }

    std::string magic;
    unsigned version = 0;
    std::string nodesLabel;
    std::size_t count = 0;
    if (!(input >> magic >> version) || magic != "LICSCENE" || version != 1 ||
        !(input >> nodesLabel >> count) || nodesLabel != "nodes" || count > 4096) {
        throw std::runtime_error("Invalid or unsupported LIC scene header: " + path.string());
    }

    struct NodeRecord final {
        NodeKind kind{};
        std::string name;
        Transform transform{};
        Appearance appearance{};
        bool enabled{true};
    };
    std::vector<NodeRecord> records;
    records.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::string recordLabel;
        unsigned rawKind = 0;
        NodeRecord record;
        int enabled = 0;
        if (!(input >> recordLabel >> rawKind >> std::quoted(record.name)) ||
            recordLabel != "node" || rawKind > static_cast<unsigned>(NodeKind::StaticBody3D) ||
            record.name.empty() || record.name.size() > 4096) {
            throw std::runtime_error("Invalid scene node at index " + std::to_string(i));
        }
        record.kind = static_cast<NodeKind>(rawKind);
        for (float& value : record.transform.position) {
            if (!(input >> value) || !std::isfinite(value)) throw std::runtime_error("Invalid node position");
        }
        for (float& value : record.transform.rotation_degrees) {
            if (!(input >> value) || !std::isfinite(value)) throw std::runtime_error("Invalid node rotation");
        }
        for (float& value : record.transform.scale) {
            if (!(input >> value) || !std::isfinite(value) || std::abs(value) < 0.0001F || std::abs(value) > 10000.0F) {
                throw std::runtime_error("Invalid node scale");
            }
        }
        for (float& value : record.appearance.color) {
            if (!(input >> value) || !std::isfinite(value) || value < 0.0F || value > 1.0F) {
                throw std::runtime_error("Invalid node material color");
            }
        }
        if (!(input >> enabled) || (enabled != 0 && enabled != 1)) {
            throw std::runtime_error("Invalid node enabled state");
        }
        record.enabled = enabled == 1;
        records.push_back(std::move(record));
    }
    input >> std::ws;
    if (!input.eof()) {
        throw std::runtime_error("Unexpected data after last node in scene file");
    }

    // Build a temporary world first; malformed input never destroys the active scene.
    World replacement;
    for (auto& record : records) {
        const Entity entity = create_node(replacement, record.kind, std::move(record.name),
                                          std::move(record.transform), std::move(record.appearance));
        replacement.get<NodeEnabled>(entity).value = record.enabled;
    }
    destination = std::move(replacement);
}

} // namespace lic::world
