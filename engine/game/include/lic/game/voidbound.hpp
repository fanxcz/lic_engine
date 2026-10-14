#pragma once

#include <lic/world/scene.hpp>

#include <array>
#include <cstdint>
#include <string_view>

namespace lic::game {

enum class RunState : std::uint8_t { Menu, Playing, Paused, Upgrade, GameOver };
enum class ActorKind : std::uint8_t { Player, Enemy, Projectile, ExperienceOrb, ArenaDecoration };
enum class UpgradeKind : std::uint8_t { RapidFire, HeavyRounds, Vitality, Mobility };

struct GameActor final {
    ActorKind kind{ActorKind::ArenaDecoration};
    float health{};
    float max_health{};
    float speed{};
    float cooldown{};
    float velocity_x{};
    float velocity_z{};
    float lifetime{};
    int reward{};
};

struct Input final {
    float move_x{};
    float move_z{};
    bool dash_pressed{};
    bool fire_held{};
};

struct UpgradeOption final {
    UpgradeKind kind{};
    std::string_view name;
    std::string_view description;
};

class Voidbound final {
public:
    Voidbound();

    void start_run();
    void update(float delta_seconds, const Input& input);
    void pause();
    void resume();
    void toggle_pause();
    void choose_upgrade(std::size_t index);

    [[nodiscard]] const lic::world::World& world() const noexcept { return world_; }
    [[nodiscard]] lic::world::World& world() noexcept { return world_; }
    [[nodiscard]] RunState state() const noexcept { return state_; }
    [[nodiscard]] int wave() const noexcept { return wave_; }
    [[nodiscard]] int score() const noexcept { return score_; }
    [[nodiscard]] int kills() const noexcept { return kills_; }
    [[nodiscard]] int level() const noexcept { return level_; }
    [[nodiscard]] int experience() const noexcept { return experience_; }
    [[nodiscard]] int experience_to_next() const noexcept { return experience_to_next_; }
    [[nodiscard]] float health() const noexcept { return health_; }
    [[nodiscard]] float max_health() const noexcept { return max_health_; }
    [[nodiscard]] float dash_cooldown() const noexcept { return dash_cooldown_; }
    [[nodiscard]] float fire_interval() const noexcept { return fire_interval_; }
    [[nodiscard]] const std::array<UpgradeOption, 3>& upgrade_options() const noexcept { return upgrade_options_; }
    [[nodiscard]] lic::world::Entity player() const noexcept { return player_; }

private:
    lic::world::World world_;
    lic::world::Entity player_{};
    RunState state_{RunState::Menu};
    std::array<UpgradeOption, 3> upgrade_options_{};
    int wave_{};
    int score_{};
    int kills_{};
    int level_{};
    int experience_{};
    int experience_to_next_{};
    int spawn_remaining_{};
    int pending_upgrades_{};
    bool pending_wave_advance_{};
    float health_{};
    float max_health_{};
    float move_speed_{};
    float damage_{};
    float fire_interval_{};
    float fire_cooldown_{};
    float spawn_cooldown_{};
    float dash_cooldown_{};
    float invulnerability_time_{};
    float dash_time_{};

    [[nodiscard]] lic::world::Entity spawn_actor(std::string name, ActorKind actor_kind,
        lic::world::NodeKind node_kind, lic::world::Transform transform,
        lic::world::Appearance appearance, GameActor stats = {});
    void spawn_enemy();
    void spawn_projectile(float target_x, float target_z);
    void spawn_experience(float x, float z, int amount);
    void prepare_wave();
    void build_upgrade_options();
    void apply_upgrade(UpgradeKind kind);
    void collect_experience(int amount);
    [[nodiscard]] lic::world::Entity nearest_enemy(float x, float z) const;
    void finish_enemy(lic::world::Entity enemy);
};

} // namespace lic::game
