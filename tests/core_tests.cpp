#include <lic/core/version.hpp>
#include <lic/world/world.hpp>

#include <cstdlib>
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

int main() {
    test_version();
    test_world_entity_lifecycle();
    std::cout << "LIC Engine core and world tests passed.\n";
    return EXIT_SUCCESS;
}
