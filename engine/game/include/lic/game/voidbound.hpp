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

// Values are emitted by a NEXUS GameRules.nx script.
struct ScriptSettings final {
    int player_health{100};
    int player_speed{230};                   // hundredths of game units per second
    int enemy_speed{68};                     // hundredths of game units per second
    int fire_interval_ms{240};
    int dash_cooldown_ms{1350};
    int enemy_spawn_base{4};
    int enemy_spawn_per_wave{2};
    int enemy_spawn_cap{28};
    int enemy_health_base{200};              // hundredths of health
    int enemy_health_per_wave{20};           // hundredths of health
    int elite_health_bonus{200};             // hundredths of health
    int elite_start_wave{3};
    int elite_every_n{5};
    int score_per_reward{100};
    int experience_initial_threshold{5};
    int experience_threshold_base{4};
    int experience_threshold_per_level{2};
    int spawn_interval_base_ms{950};
    int spawn_interval_wave_reduction_ms{35};
    int spawn_interval_min_ms{320};
    int rapid_fire_interval_percent{75};
    int heavy_round_damage{1};
    int vitality_health_bonus{25};
    int vitality_heal_amount{45};
    int mobility_speed_bonus{28};            // hundredths of game units per second
    int mobility_dash_cooldown_ms{350};
    int experience_pickup_lifetime_seconds{18};
    int arena_limit_centi{335};
    int enemy_spawn_radius_centi{345};
    int enemy_speed_wave_bonus_centi{25};
    int enemy_speed_wave_bonus_cap_centi{42};
    int elite_speed_percent{82};
    int initial_spawn_delay_ms{450};
    int enemy_initial_contact_cooldown_ms{200};
    int player_dash_distance_centi{100};
    int player_dash_duration_ms{180};
    int dash_invulnerability_ms{340};
    int enemy_contact_damage{8};
    int elite_contact_damage{13};
    int enemy_contact_range_centi{52};
    int enemy_contact_cooldown_ms{720};
    int player_hit_invulnerability_ms{350};
    int projectile_speed_centi{680};
    int projectile_lifetime_ms{1100};
    int projectile_hit_radius_centi{40};
    int no_target_fire_delay_ms{50};
    int experience_attraction_range_centi{110};
    int experience_attraction_speed_centi{125};
    int experience_collect_range_centi{35};
};

class Voidbound final {
public:
    Voidbound();

    void show_menu();
    void apply_script_settings(const ScriptSettings& settings) noexcept;
    [[nodiscard]] ScriptSettings script_settings() const noexcept { return settings_; }
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
    ScriptSettings settings_{};
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
    int wave_spawn_total_{};
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
    float dash_cooldown_duration_{1.35F};
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
