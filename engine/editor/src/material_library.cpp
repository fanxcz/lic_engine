#include <lic/editor/editor_ui.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace lic::editor {

void EditorUi::seed_material_library() {
    const auto shipped = executable_directory_ / "assets" / "materials";
    const auto destination = project_root_ / "Assets" / "Materials";
    std::error_code error;
    if (!std::filesystem::is_directory(shipped, error)) return;
    for (std::filesystem::directory_iterator it(shipped, error), end;
         !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error) || it->path().extension() != ".licmat") continue;
        const auto target = destination / it->path().filename();
        if (std::filesystem::exists(target, error)) continue;
        error.clear();
        std::filesystem::copy_file(it->path(), target, std::filesystem::copy_options::skip_existing, error);
        if (error) {
            log("Could not install material preset " + it->path().filename().string() + ": " + error.message());
            error.clear();
        }
    }
}

void EditorUi::load_project_materials() {
    const std::array<const char*, 5> filenames{{
        "VoidboundPlayer", "RiftStalker", "RiftBrute", "Pulse", "MemoryShard"
    }};
    const std::array<const char*, 5> names{{
        "Player", "Stalker", "Elite", "Projectile", "Pickup"
    }};
    const std::array<std::array<float, 4>, 5> colors{{
        {{0.25F, 0.92F, 0.98F, 1.0F}},
        {{0.94F, 0.29F, 0.24F, 1.0F}},
        {{0.94F, 0.31F, 0.54F, 1.0F}},
        {{0.24F, 0.80F, 1.0F, 1.0F}},
        {{0.96F, 0.76F, 0.27F, 1.0F}}
    }};
    const std::array<std::array<float, 3>, 5> surface{{
        {{0.45F, 0.22F, 0.20F}},
        {{0.22F, 0.38F, 0.08F}},
        {{0.58F, 0.22F, 0.32F}},
        {{0.05F, 0.18F, 1.15F}},
        {{0.62F, 0.24F, 0.55F}}
    }};
    for (std::size_t i = 0; i < filenames.size(); ++i) {
        game_materials_[i].name = names[i];
        game_materials_[i].surface.color = colors[i];
        game_materials_[i].surface.metallic = surface[i][0];
        game_materials_[i].surface.roughness = surface[i][1];
        game_materials_[i].surface.emission_strength = surface[i][2];
        const auto materialPath = project_root_ / "Assets" / "Materials" /
                                  (std::string(filenames[i]) + ".licmat");
        std::error_code error;
        if (!std::filesystem::is_regular_file(materialPath, error)) continue;
        try {
            game_materials_[i] = lic::world::load_material(materialPath);
        } catch (const std::exception& loadError) {
            log("Invalid material preset " + materialPath.filename().string() + ": " + loadError.what());
        }
    }
}

void EditorUi::apply_project_materials() {
    auto& world = game_.world();
    world.each<lic::world::NodeName>([&](lic::world::Entity entity, const lic::world::NodeName& name) {
        if (!world.has<lic::world::Appearance>(entity)) return;
        const lic::world::MaterialResource* material = nullptr;
        if (name.value == "Pilot") material = &game_materials_[0];
        else if (name.value.starts_with("Rift Stalker ")) material = &game_materials_[1];
        else if (name.value.starts_with("Rift Brute ")) material = &game_materials_[2];
        else if (name.value == "Pulse") material = &game_materials_[3];
        else if (name.value == "Memory Shard") material = &game_materials_[4];
        if (material == nullptr) return;
        world.get<lic::world::Appearance>(entity) = material->surface;
    });
}


} // namespace lic::editor
