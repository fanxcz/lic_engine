#include <lic/core/version.hpp>
#include <lic/game/voidbound.hpp>
#include <lic/world/scene.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

struct Position final {
    float x{};
    float y{};
};

struct Name final {
    std::string value;
};

void test_version() {
    const auto current = lic::core::version();
    expect(current.major == 0, "major version should be zero during foundation stage");
    expect(current.minor == 1, "minor version should match project version");
    expect(current.patch == 0, "patch version should match project version");
    expect(lic::core::version_string() == "0.1.0", "version string should be stable");
    expect(lic::core::engine_name() == "LIC Engine", "engine name should be stable");
}

void test_world_entity_lifecycle() {
    lic::world::World world;
    const auto first = world.create_entity();
    const auto second = world.create_entity();

    expect(first.valid(), "created entity handle should be valid");
    expect(world.alive(first), "new entity should be alive");
    expect(world.entity_count() == 2, "world should count live entities");

    world.emplace<Position>(first, Position{1.0F, 2.0F});
    world.emplace<Position>(second, Position{4.0F, 5.0F});
    world.emplace<Name>(first, Name{"player"});
    expect(world.get<Position>(first).x == 1.0F, "component lookup should return the right entity");
    expect(world.get<Position>(second).y == 5.0F, "sparse-set swap should preserve other components");
    expect(world.get<Name>(first).value == "player", "non-trivial movable component should be supported");

    float position_x_sum = 0.0F;
    std::size_t visited_positions = 0;
    world.each<Position>([&](lic::world::Entity entity, Position& position) {
        expect(world.alive(entity), "iteration should expose live entity handles");
        position_x_sum += position.x;
        ++visited_positions;
    });
    expect(visited_positions == 2, "each should visit every component exactly once");
    expect(position_x_sum == 5.0F, "each should expose mutable component references");

    const auto& const_world = world;
    std::size_t const_visited_positions = 0;
    const_world.each<Position>([&](lic::world::Entity, const Position& position) {
        expect(position.x > 0.0F, "const iteration should expose a read-only component");
        ++const_visited_positions;
    });
    expect(const_visited_positions == 2, "const each should visit every component");

    expect(world.has<Position>(first), "has should detect a component");
    expect(!world.has<Name>(second), "has should reject a missing component");

    bool duplicate_rejected = false;
    try {
        (void)world.emplace<Position>(first, Position{8.0F, 9.0F});
    } catch (const std::logic_error&) {
        duplicate_rejected = true;
    }
    expect(duplicate_rejected, "adding the same component twice should fail clearly");

    expect(world.remove<Name>(first), "remove should report an existing component");
    expect(!world.remove<Name>(first), "removing an absent component should return false");
    expect(!world.has<Name>(first), "removed component should no longer be visible");

    expect(world.destroy_entity(first), "destroy should remove a live entity");
    expect(!world.destroy_entity(first), "destroying a stale handle should be harmless");
    expect(!world.alive(first), "destroyed entity handle should be invalidated");
    expect(!world.has<Position>(first), "destroy should remove all entity components");
    expect(world.entity_count() == 1, "destroy should update the live entity count");

    const auto recycled = world.create_entity();
    expect(recycled.index == first.index, "registry should recycle freed indices");
    expect(recycled.generation != first.generation, "recycled entity should get a new generation");
    expect(!world.has<Position>(recycled), "recycled entity must not inherit old components");

    bool stale_get_rejected = false;
    try {
        (void)world.get<Position>(first);
    } catch (const std::invalid_argument&) {
        stale_get_rejected = true;
    }
    expect(stale_get_rejected, "component access through a stale handle should fail clearly");

    world.emplace<Position>(recycled, Position{9.0F, 10.0F});
    expect(world.get<Position>(recycled).x == 9.0F, "recycled entity should accept new components");
    expect(world.get<Position>(second).x == 4.0F, "unrelated entities should remain unchanged");
}

} // namespace

void test_scene_serialization() {
    const auto folder = std::filesystem::temp_directory_path() / "lic-engine-scene-tests";
    const auto scenePath = folder / "roundtrip.licscene";
    const auto brokenPath = folder / "broken.licscene";
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
    std::filesystem::create_directories(folder);

    lic::world::World source;
    (void)lic::world::create_node(source, lic::world::NodeKind::Node3D, "World");
    lic::world::Transform transform;
    transform.position = {1.25F, -2.0F, 3.5F};
    transform.rotation_degrees = {10.0F, 20.0F, -30.0F};
    transform.scale = {0.5F, 0.75F, 1.25F};
    lic::world::Appearance appearance;
    appearance.color = {0.2F, 0.4F, 0.8F, 1.0F};
    appearance.metallic = 0.72F;
    appearance.roughness = 0.24F;
    appearance.emission_strength = 0.35F;
    const auto cube = lic::world::create_node(source, lic::world::NodeKind::MeshInstance3D,
                                               "Cube with spaces", transform, appearance);
    source.get<lic::world::NodeEnabled>(cube).value = false;
    lic::world::save_scene(source, scenePath);

    lic::world::World loaded;
    (void)lic::world::create_node(loaded, lic::world::NodeKind::Node3D, "Old scene");
    lic::world::load_scene(loaded, scenePath);
    expect(loaded.entity_count() == 2, "scene load should replace the active world atomically");

    bool foundCube = false;
    loaded.each<lic::world::NodeName>([&](lic::world::Entity entity, const lic::world::NodeName& name) {
        if (name.value != "Cube with spaces") return;
        foundCube = true;
        const auto& restored = loaded.get<lic::world::Transform>(entity);
        const auto& restoredAppearance = loaded.get<lic::world::Appearance>(entity);
        expect(restored.position[0] == 1.25F && restored.position[1] == -2.0F,
               "scene round trip should preserve positions");
        expect(restored.rotation_degrees[2] == -30.0F, "scene round trip should preserve rotations");
        expect(restored.scale[1] == 0.75F, "scene round trip should preserve scales");
        expect(restoredAppearance.color[2] == 0.8F, "scene round trip should preserve material color");
        expect(restoredAppearance.metallic == 0.72F, "scene round trip should preserve metallic");
        expect(restoredAppearance.roughness == 0.24F, "scene round trip should preserve roughness");
        expect(restoredAppearance.emission_strength == 0.35F,
               "scene round trip should preserve emission strength");
        expect(!loaded.get<lic::world::NodeEnabled>(entity).value, "scene round trip should preserve enabled state");
    });
    expect(foundCube, "scene round trip should preserve names with spaces");

    {
        std::ofstream broken(brokenPath, std::ios::binary | std::ios::trunc);
        broken << "LICSCENE 1\nnodes 1\nnode 99 \"bad\" 0 0 0 0 0 0 1 1 1 1 1 1 1 1\n";
    }
    const auto entityCountBeforeBrokenLoad = loaded.entity_count();
    bool invalidFileRejected = false;
    try {
        lic::world::load_scene(loaded, brokenPath);
    } catch (const std::runtime_error&) {
        invalidFileRejected = true;
    }
    expect(invalidFileRejected, "malformed scene files should be rejected");
    expect(loaded.entity_count() == entityCountBeforeBrokenLoad,
           "a failed load must leave the current scene unchanged");
    std::filesystem::remove_all(folder, ignored);
}

void test_legacy_scene_compatibility() {
    const auto folder = std::filesystem::temp_directory_path() / "lic-engine-legacy-scene-tests";
    const auto path = folder / "v1.licscene";
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
    std::filesystem::create_directories(folder);
    {
        std::ofstream scene(path, std::ios::binary | std::ios::trunc);
        scene << "LICSCENE 1\nnodes 1\n"
                 "node 1 \"Legacy cube\" 0 0 0 0 0 0 1 1 1 0.1 0.2 0.3 1 1\n";
    }
    lic::world::World loaded;
    lic::world::load_scene(loaded, path);
    expect(loaded.entity_count() == 1, "legacy LICSCENE v1 should remain loadable");
    loaded.each<lic::world::NodeName>([&](lic::world::Entity entity, const lic::world::NodeName& name) {
        if (name.value != "Legacy cube") return;
        const auto& material = loaded.get<lic::world::Appearance>(entity);
        expect(material.color[0] == 0.1F, "legacy scenes should preserve base color");
        expect(material.metallic == 0.0F, "legacy scenes should default metallic to zero");
        expect(material.roughness == 0.68F, "legacy scenes should receive the default roughness");
        expect(material.emission_strength == 0.0F, "legacy scenes should default emission to zero");
    });
    std::filesystem::remove_all(folder, ignored);
}

void test_gameplay_settings_validation() {
    lic::game::Voidbound game;
    lic::game::ScriptSettings settings;
    settings.player_health = 900;
    settings.player_speed = -5;
    settings.enemy_spawn_cap = 1000;
    settings.elite_every_n = 0;
    settings.projectile_lifetime_ms = 999999;
    game.apply_script_settings(settings);
    const auto validated = game.script_settings();
    expect(validated.player_health == 500, "NEXUS health settings should be clamped");
    expect(validated.player_speed == 80, "NEXUS movement speed should be clamped");
    expect(validated.enemy_spawn_cap == 60, "NEXUS wave cap should be clamped");
    expect(validated.elite_every_n == 1, "elite cadence must never divide by zero");
    expect(validated.projectile_lifetime_ms == 10000, "projectile lifetime should be clamped");
    game.start_run();
    expect(game.max_health() == 500.0F, "validated health should drive the actual run");
}

void test_material_resources() {
    const auto folder = std::filesystem::temp_directory_path() / "lic-engine-material-tests";
    const auto path = folder / "Test Material.licmat";
    const auto brokenPath = folder / "broken.licmat";
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);

    lic::world::MaterialResource expected;
    expected.name = "Rift alloy";
    expected.surface.color = {0.12F, 0.72F, 0.91F, 1.0F};
    expected.surface.metallic = 0.8F;
    expected.surface.roughness = 0.21F;
    expected.surface.emission_strength = 0.6F;
    lic::world::save_material(expected, path);
    const auto loaded = lic::world::load_material(path);
    expect(loaded.name == expected.name, ".licmat round trip should preserve the material name");
    expect(loaded.surface.color[1] == expected.surface.color[1],
           ".licmat round trip should preserve base color");
    expect(loaded.surface.metallic == expected.surface.metallic,
           ".licmat round trip should preserve metallic");
    expect(loaded.surface.roughness == expected.surface.roughness,
           ".licmat round trip should preserve roughness");
    expect(loaded.surface.emission_strength == expected.surface.emission_strength,
           ".licmat round trip should preserve emission");

    {
        std::ofstream bad(brokenPath, std::ios::binary | std::ios::trunc);
        bad << "LICMAT 1\nname \"Broken\"\nbase_color 1 1 1 1\nmetallic 8\nroughness .5\nemission 0\n";
    }
    bool invalidRejected = false;
    try {
        (void)lic::world::load_material(brokenPath);
    } catch (const std::runtime_error&) {
        invalidRejected = true;
    }
    expect(invalidRejected, "invalid material property ranges must be rejected");
    std::filesystem::remove_all(folder, ignored);
}

void test_voidbound_gameplay() {
    lic::game::Voidbound game;
    expect(game.state() == lic::game::RunState::Menu, "Voidbound should open to a main menu");
    game.start_run();
    expect(game.state() == lic::game::RunState::Playing, "starting a run should enter gameplay");
    expect(game.world().alive(game.player()), "gameplay should create a player entity");
    expect(game.world().entity_count() >= 24, "gameplay should create an arena with markers and pillars");

    const auto start = game.world().get<lic::world::Transform>(game.player()).position;
    for (int i = 0; i < 30; ++i) {
        game.update(0.01F, lic::game::Input{1.0F, 0.0F, false, false});
    }
    const auto moved = game.world().get<lic::world::Transform>(game.player()).position;
    expect(moved[0] > start[0] + 0.5F, "WASD-style input should move the player");
    expect(game.wave() == 1, "the first run should start at wave one");

    game.pause();
    expect(game.state() == lic::game::RunState::Paused, "pause should suspend active gameplay");
    const auto pausedPosition = game.world().get<lic::world::Transform>(game.player()).position;
    game.update(0.05F, lic::game::Input{0.0F, 1.0F, true, true});
    const auto stillPaused = game.world().get<lic::world::Transform>(game.player()).position;
    expect(pausedPosition == stillPaused, "paused gameplay should not advance entities");
    game.resume();
    expect(game.state() == lic::game::RunState::Playing, "resume should return to gameplay");
    game.show_menu();
    expect(game.state() == lic::game::RunState::Menu, "main menu should be reachable after a run");
    expect(game.world().entity_count() == 0, "returning to the menu should release gameplay entities");
}

int main() {
    test_version();
    test_world_entity_lifecycle();
    test_scene_serialization();
    test_legacy_scene_compatibility();
    test_material_resources();
    test_gameplay_settings_validation();
    test_voidbound_gameplay();
    std::cout << "LIC Engine core, world, scenes, materials, NEXUS settings and gameplay tests passed.\n";
    return EXIT_SUCCESS;
}
