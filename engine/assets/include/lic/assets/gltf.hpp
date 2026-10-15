#pragma once
#include <lic/world/scene.hpp>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace lic::assets {
struct MeshVertex final {
    std::array<float, 3> position{};
    std::array<float, 3> normal{0.0F, 1.0F, 0.0F};
    std::array<float, 2> texcoord{};
};
struct MeshPrimitive final {
    std::uint32_t first_index{};
    std::uint32_t index_count{};
    std::string material_name;
    lic::world::Appearance surface{};
};
struct GltfAsset final {
    std::string name;
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<MeshPrimitive> primitives;
};
// Imports triangle-list geometry and node transforms from glTF 2.0 (.gltf/.glb).
// Reads normals, UV0 and metallic-roughness material factors. Texture sampling is
// not yet part of the active viewport shader.
[[nodiscard]] GltfAsset load_gltf(const std::filesystem::path& path);
[[nodiscard]] std::vector<std::filesystem::path> gltf_external_dependencies(const std::filesystem::path& path);
[[nodiscard]] bool is_gltf_path(const std::filesystem::path& path) noexcept;
} // namespace lic::assets
