#include <lic/game/voidbound.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace lic::game {
namespace {

constexpr float kPi = 3.14159265358979323846F;

[[nodiscard]] float squared_distance(float ax, float az, float bx, float bz) noexcept {
    const float dx = ax - bx;
    const float dz = az - bz;
    return dx * dx + dz * dz;
}

[[nodiscard]] float clamp_delta(float value) noexcept {
    return std::clamp(value, 0.0F, 0.05F);
}

} // namespace

Voidbound::Voidbound() {
    build_upgrade_options();
}

lic::world::Entity Voidbound::spawn_actor(std::string name, ActorKind actor_kind,
    lic::world::NodeKind node_kind, lic::world::Transform transform,
    lic::world::Appearance appearance, GameActor stats) {
    const auto entity = lic::world::create_node(world_, node_kind, std::move(name),
                                                std::move(transform), std::move(appearance));
    stats.kind = actor_kind;
    world_.emplace<GameActor>(entity, stats);
    return entity;
}

void Voidbound::show_menu() {
    world_ = lic::world::World{};
    player_ = {};
    state_ = RunState::Menu;
    pending_upgrades_ = 0;
    pending_wave_advance_ = false;
}

void Voidbound::apply_script_settings(const ScriptSettings& settings) noexcept {
    settings_.player_health = std::clamp(settings.player_health, 25, 500);
    settings_.player_speed = std::clamp(settings.player_speed, 80, 600);
    settings_.enemy_speed = std::clamp(settings.enemy_speed, 20, 250);
    settings_.fire_interval_ms = std::clamp(settings.fire_interval_ms, 80, 2000);
    settings_.dash_cooldown_ms = std::clamp(settings.dash_cooldown_ms, 250, 5000);
    settings_.enemy_spawn_base = std::clamp(settings.enemy_spawn_base, 1, 20);
    settings_.enemy_spawn_per_wave = std::clamp(settings.enemy_spawn_per_wave, 0, 8);
    settings_.enemy_spawn_cap = std::clamp(settings.enemy_spawn_cap, 4, 60);
    settings_.enemy_health_base = std::clamp(settings.enemy_health_base, 50, 1500);
    settings_.enemy_health_per_wave = std::clamp(settings.enemy_health_per_wave, 0, 300);
    settings_.elite_health_bonus = std::clamp(settings.elite_health_bonus, 0, 1500);
    settings_.elite_start_wave = std::clamp(settings.elite_start_wave, 1, 30);
    settings_.elite_every_n = std::clamp(settings.elite_every_n, 1, 20);
    settings_.score_per_reward = std::clamp(settings.score_per_reward, 1, 10000);
    settings_.experience_initial_threshold = std::clamp(settings.experience_initial_threshold, 1, 50);
    settings_.experience_threshold_base = std::clamp(settings.experience_threshold_base, 1, 50);
    settings_.experience_threshold_per_level = std::clamp(settings.experience_threshold_per_level, 0, 20);
    settings_.spawn_interval_base_ms = std::clamp(settings.spawn_interval_base_ms, 350, 3000);
    settings_.spawn_interval_wave_reduction_ms = std::clamp(settings.spawn_interval_wave_reduction_ms, 0, 100);
    settings_.spawn_interval_min_ms = std::clamp(settings.spawn_interval_min_ms, 200, 1000);
    settings_.rapid_fire_interval_percent = std::clamp(settings.rapid_fire_interval_percent, 40, 95);
    settings_.heavy_round_damage = std::clamp(settings.heavy_round_damage, 1, 10);
    settings_.vitality_health_bonus = std::clamp(settings.vitality_health_bonus, 5, 200);
    settings_.vitality_heal_amount = std::clamp(settings.vitality_heal_amount, 5, 250);
    settings_.mobility_speed_bonus = std::clamp(settings.mobility_speed_bonus, 5, 200);
    settings_.mobility_dash_cooldown_ms = std::clamp(settings.mobility_dash_cooldown_ms, 100, 2000);
    settings_.experience_pickup_lifetime_seconds =
        std::clamp(settings.experience_pickup_lifetime_seconds, 3, 120);
    settings_.arena_limit_centi = std::clamp(settings.arena_limit_centi, 150, 1000);
    settings_.enemy_spawn_radius_centi = std::clamp(settings.enemy_spawn_radius_centi, 160, 1200);
    settings_.enemy_speed_wave_bonus_centi = std::clamp(settings.enemy_speed_wave_bonus_centi, 0, 50);
    settings_.enemy_speed_wave_bonus_cap_centi = std::clamp(settings.enemy_speed_wave_bonus_cap_centi, 0, 150);
    settings_.elite_speed_percent = std::clamp(settings.elite_speed_percent, 40, 100);
    settings_.initial_spawn_delay_ms = std::clamp(settings.initial_spawn_delay_ms, 100, 2500);
    settings_.enemy_initial_contact_cooldown_ms = std::clamp(settings.enemy_initial_contact_cooldown_ms, 0, 2000);
    settings_.player_dash_distance_centi = std::clamp(settings.player_dash_distance_centi, 20, 300);
    settings_.player_dash_duration_ms = std::clamp(settings.player_dash_duration_ms, 50, 1000);
    settings_.dash_invulnerability_ms = std::clamp(settings.dash_invulnerability_ms, 50, 1500);
    settings_.enemy_contact_damage = std::clamp(settings.enemy_contact_damage, 1, 100);
    settings_.elite_contact_damage = std::clamp(settings.elite_contact_damage, 1, 200);
    settings_.enemy_contact_range_centi = std::clamp(settings.enemy_contact_range_centi, 20, 150);
    settings_.enemy_contact_cooldown_ms = std::clamp(settings.enemy_contact_cooldown_ms, 100, 5000);
    settings_.player_hit_invulnerability_ms = std::clamp(settings.player_hit_invulnerability_ms, 50, 2000);
    settings_.projectile_speed_centi = std::clamp(settings.projectile_speed_centi, 100, 2000);
    settings_.projectile_lifetime_ms = std::clamp(settings.projectile_lifetime_ms, 100, 10000);
    settings_.projectile_hit_radius_centi = std::clamp(settings.projectile_hit_radius_centi, 10, 100);
    settings_.no_target_fire_delay_ms = std::clamp(settings.no_target_fire_delay_ms, 0, 1000);
    settings_.experience_attraction_range_centi = std::clamp(settings.experience_attraction_range_centi, 20, 500);
    settings_.experience_attraction_speed_centi = std::clamp(settings.experience_attraction_speed_centi, 0, 1000);
    settings_.experience_collect_range_centi = std::clamp(settings.experience_collect_range_centi, 10, 100);
}

void Voidbound::start_run() {
    world_ = lic::world::World{};
    state_ = RunState::Playing;
    wave_ = 1;
    score_ = 0;
    kills_ = 0;
    level_ = 1;
    experience_ = 0;
    experience_to_next_ = settings_.experience_initial_threshold;
    pending_upgrades_ = 0;
    pending_wave_advance_ = false;
    health_ = static_cast<float>(settings_.player_health);
    max_health_ = health_;
    move_speed_ = static_cast<float>(settings_.player_speed) / 100.0F;
    damage_ = 1.0F;
    fire_interval_ = static_cast<float>(settings_.fire_interval_ms) / 1000.0F;
    fire_cooldown_ = 0.0F;
    spawn_cooldown_ = 0.35F;
    dash_cooldown_ = 0.0F;
    dash_cooldown_duration_ = static_cast<float>(settings_.dash_cooldown_ms) / 1000.0F;
    invulnerability_time_ = 0.0F;
    dash_time_ = 0.0F;

    (void)lic::world::create_node(world_, lic::world::NodeKind::Node3D, "World");
    lic::world::Appearance playerAppearance;
    playerAppearance.color = {0.25F, 0.92F, 0.98F, 1.0F};
    lic::world::Transform playerTransform;
    playerTransform.position = {0.0F, 0.08F, 0.0F};
    playerTransform.scale = {0.34F, 0.44F, 0.34F};
    GameActor playerStats;
    playerStats.health = health_;
    playerStats.max_health = max_health_;
    playerStats.speed = move_speed_;
    player_ = spawn_actor("Pilot", ActorKind::Player, lic::world::NodeKind::MeshInstance3D,
                          playerTransform, playerAppearance, playerStats);

    // Small emissive-looking markers make the playable area readable without textures.
    lic::world::Appearance beaconColor;
    beaconColor.color = {0.20F, 0.39F, 0.62F, 1.0F};
    for (int i = 0; i < 20; ++i) {
        const float angle = (static_cast<float>(i) / 20.0F) * 2.0F * kPi;
        lic::world::Transform marker;
        marker.position = {std::cos(angle) * 3.75F, -0.16F, std::sin(angle) * 3.75F};
        marker.rotation_degrees[1] = -angle * 57.29578F;
        marker.scale = {0.075F, 0.075F, 0.075F};
        (void)spawn_actor("Arena Beacon " + std::to_string(i + 1), ActorKind::ArenaDecoration,
                          lic::world::NodeKind::StaticBody3D, marker, beaconColor);
    }

    lic::world::Appearance pillarColor;
    pillarColor.color = {0.16F, 0.20F, 0.29F, 1.0F};
    for (int i = 0; i < 4; ++i) {
        lic::world::Transform pillar;
        pillar.position = {(i % 2 == 0) ? -1.65F : 1.65F, -0.13F,
                           (i < 2) ? -1.55F : 1.55F};
        pillar.scale = {0.22F, 0.42F, 0.22F};
        (void)spawn_actor("Ruined Pillar " + std::to_string(i + 1), ActorKind::ArenaDecoration,
                          lic::world::NodeKind::StaticBody3D, pillar, pillarColor);
    }

    prepare_wave();
    build_upgrade_options();
}

void Voidbound::prepare_wave() {
    spawn_remaining_ = std::min(settings_.enemy_spawn_base + wave_ * settings_.enemy_spawn_per_wave,
                                settings_.enemy_spawn_cap);
    wave_spawn_total_ = spawn_remaining_;
    spawn_cooldown_ = static_cast<float>(settings_.initial_spawn_delay_ms) / 1000.0F;
    pending_wave_advance_ = false;
}

lic::world::Entity Voidbound::nearest_enemy(float x, float z) const {
    lic::world::Entity nearest{};
    float bestDistance = std::numeric_limits<float>::max();
    world_.each<GameActor>([&](lic::world::Entity entity, const GameActor& actor) {
        if (actor.kind != ActorKind::Enemy || !world_.has<lic::world::Transform>(entity)) {
            return;
        }
        const auto& pos = world_.get<lic::world::Transform>(entity).position;
        const float distance = squared_distance(x, z, pos[0], pos[2]);
        if (distance < bestDistance) {
            bestDistance = distance;
            nearest = entity;
        }
    });
    return nearest;
}

void Voidbound::spawn_enemy() {
    const int spawnIndex = wave_spawn_total_ - spawn_remaining_;
    const float angle = static_cast<float>(spawnIndex) * 2.39996323F +
                        static_cast<float>(wave_) * 0.41F;
    const float radius = static_cast<float>(settings_.enemy_spawn_radius_centi) / 100.0F;
    const bool elite = wave_ >= settings_.elite_start_wave &&
                       ((spawnIndex + wave_) % settings_.elite_every_n == 0);
    lic::world::Transform transform;
    transform.position = {std::cos(angle) * radius, 0.06F, std::sin(angle) * radius};
    transform.scale = elite ? std::array<float, 3>{0.39F, 0.48F, 0.39F}
                            : std::array<float, 3>{0.27F, 0.34F, 0.27F};
    lic::world::Appearance appearance;
    if (elite) appearance.color = {0.94F, 0.31F, 0.54F, 1.0F};
    else appearance.color = {0.94F, 0.29F, 0.24F, 1.0F};

    GameActor stats;
    stats.health = static_cast<float>(settings_.enemy_health_base +
        wave_ * settings_.enemy_health_per_wave +
        (elite ? settings_.elite_health_bonus : 0)) / 100.0F;
    stats.max_health = stats.health;
    const float scriptedSpeed = static_cast<float>(settings_.enemy_speed) / 100.0F;
    stats.speed = (elite ? scriptedSpeed *
        static_cast<float>(settings_.elite_speed_percent) / 100.0F : scriptedSpeed) +
        std::min(static_cast<float>(wave_ * settings_.enemy_speed_wave_bonus_centi) / 100.0F,
                 static_cast<float>(settings_.enemy_speed_wave_bonus_cap_centi) / 100.0F);
    stats.cooldown = static_cast<float>(settings_.enemy_initial_contact_cooldown_ms) / 1000.0F;
    stats.reward = elite ? 3 : 1;
    (void)spawn_actor((elite ? "Rift Brute " : "Rift Stalker ") + std::to_string(kills_ + spawnIndex + 1),
                      ActorKind::Enemy, lic::world::NodeKind::MeshInstance3D,
                      transform, appearance, stats);
    --spawn_remaining_;
}

void Voidbound::spawn_projectile(float targetX, float targetZ) {
    if (!world_.alive(player_)) return;
    const auto& playerPosition = world_.get<lic::world::Transform>(player_).position;
    float dx = targetX - playerPosition[0];
    float dz = targetZ - playerPosition[2];
    const float length = std::sqrt(dx * dx + dz * dz);
    if (length < 0.0001F) return;
    dx /= length;
    dz /= length;

    lic::world::Transform transform;
    transform.position = {playerPosition[0], 0.14F, playerPosition[2]};
    transform.rotation_degrees[1] = std::atan2(dx, dz) * 57.29578F;
    transform.scale = {0.075F, 0.075F, 0.075F};
    lic::world::Appearance appearance;
    appearance.color = {0.43F, 1.0F, 0.68F, 1.0F};
    GameActor stats;
    const float projectileSpeed = static_cast<float>(settings_.projectile_speed_centi) / 100.0F;
    stats.velocity_x = dx * projectileSpeed;
    stats.velocity_z = dz * projectileSpeed;
    stats.lifetime = static_cast<float>(settings_.projectile_lifetime_ms) / 1000.0F;
    stats.health = damage_;
    (void)spawn_actor("Pulse", ActorKind::Projectile, lic::world::NodeKind::MeshInstance3D,
                      transform, appearance, stats);
}

void Voidbound::spawn_experience(float x, float z, int amount) {
    lic::world::Transform transform;
    transform.position = {x, 0.13F, z};
    transform.scale = {0.105F, 0.105F, 0.105F};
    lic::world::Appearance appearance;
    appearance.color = {0.96F, 0.76F, 0.27F, 1.0F};
    GameActor stats;
    stats.reward = amount;
    stats.lifetime = static_cast<float>(settings_.experience_pickup_lifetime_seconds);
    (void)spawn_actor("Memory Shard", ActorKind::ExperienceOrb, lic::world::NodeKind::MeshInstance3D,
                      transform, appearance, stats);
}

void Voidbound::finish_enemy(lic::world::Entity enemy) {
    if (!world_.alive(enemy) || !world_.has<GameActor>(enemy) ||
        world_.get<GameActor>(enemy).kind != ActorKind::Enemy) return;
    const auto position = world_.get<lic::world::Transform>(enemy).position;
    const auto reward = world_.get<GameActor>(enemy).reward;
    if (world_.destroy_entity(enemy)) {
        ++kills_;
        score_ += settings_.score_per_reward * reward * wave_;
        spawn_experience(position[0], position[2], reward);
    }
}

void Voidbound::collect_experience(int amount) {
    if (amount <= 0) return;
    experience_ += amount;
    while (experience_ >= experience_to_next_) {
        experience_ -= experience_to_next_;
        ++level_;
        experience_to_next_ = settings_.experience_threshold_base +
                               level_ * settings_.experience_threshold_per_level;
        ++pending_upgrades_;
    }
    if (pending_upgrades_ > 0 && state_ == RunState::Playing) {
        build_upgrade_options();
        state_ = RunState::Upgrade;
    }
}

void Voidbound::build_upgrade_options() {
    constexpr std::array<UpgradeOption, 4> allOptions{{
        {UpgradeKind::RapidFire, "OVERCHARGE", "Fire pulses 25% faster"},
        {UpgradeKind::HeavyRounds, "HEAVY ROUNDS", "Deal +1 damage per pulse"},
        {UpgradeKind::Vitality, "REPAIR MATRIX", "Restore health and increase maximum health"},
        {UpgradeKind::Mobility, "ION DASH", "Move faster and recover dash sooner"}
    }};
    const std::size_t first = static_cast<std::size_t>(std::max(0, wave_ + level_ - 2)) % allOptions.size();
    for (std::size_t i = 0; i < upgrade_options_.size(); ++i) {
        upgrade_options_[i] = allOptions[(first + i) % allOptions.size()];
    }
}

void Voidbound::apply_upgrade(UpgradeKind kind) {
    switch (kind) {
    case UpgradeKind::RapidFire:
        fire_interval_ = std::max(0.085F, fire_interval_ *
            static_cast<float>(settings_.rapid_fire_interval_percent) / 100.0F);
        break;
    case UpgradeKind::HeavyRounds:
        damage_ += static_cast<float>(settings_.heavy_round_damage);
        break;
    case UpgradeKind::Vitality:
        max_health_ += static_cast<float>(settings_.vitality_health_bonus);
        health_ = std::min(max_health_, health_ + static_cast<float>(settings_.vitality_heal_amount));
        break;
    case UpgradeKind::Mobility:
        move_speed_ += static_cast<float>(settings_.mobility_speed_bonus) / 100.0F;
        dash_cooldown_ = std::min(dash_cooldown_,
            static_cast<float>(settings_.mobility_dash_cooldown_ms) / 1000.0F);
        break;
    }
    if (world_.alive(player_) && world_.has<GameActor>(player_)) {
        auto& stats = world_.get<GameActor>(player_);
        stats.health = health_;
        stats.max_health = max_health_;
        stats.speed = move_speed_;
    }
}

void Voidbound::choose_upgrade(std::size_t index) {
    if (state_ != RunState::Upgrade || index >= upgrade_options_.size()) return;
    apply_upgrade(upgrade_options_[index].kind);
    if (pending_upgrades_ > 0) --pending_upgrades_;
    build_upgrade_options();
    if (pending_upgrades_ > 0) return;
    if (pending_wave_advance_) {
        ++wave_;
        prepare_wave();
    }
    state_ = RunState::Playing;
}

void Voidbound::pause() {
    if (state_ == RunState::Playing) state_ = RunState::Paused;
}

void Voidbound::resume() {
    if (state_ == RunState::Paused) state_ = RunState::Playing;
}

void Voidbound::toggle_pause() {
    if (state_ == RunState::Playing) pause();
    else if (state_ == RunState::Paused) resume();
}

void Voidbound::update(float delta_seconds, const Input& input) {
    if (state_ != RunState::Playing) return;
    const float dt = clamp_delta(delta_seconds);
    if (dt <= 0.0F || !world_.alive(player_)) return;

    fire_cooldown_ = std::max(0.0F, fire_cooldown_ - dt);
    dash_cooldown_ = std::max(0.0F, dash_cooldown_ - dt);
    invulnerability_time_ = std::max(0.0F, invulnerability_time_ - dt);
    dash_time_ = std::max(0.0F, dash_time_ - dt);

    float moveX = std::clamp(input.move_x, -1.0F, 1.0F);
    float moveZ = std::clamp(input.move_z, -1.0F, 1.0F);
    const float moveLength = std::sqrt(moveX * moveX + moveZ * moveZ);
    if (moveLength > 1.0F) {
        moveX /= moveLength;
        moveZ /= moveLength;
    }

    auto& playerTransform = world_.get<lic::world::Transform>(player_);
    playerTransform.position[0] += moveX * move_speed_ * dt;
    playerTransform.position[2] += moveZ * move_speed_ * dt;
    if (input.dash_pressed && dash_cooldown_ <= 0.0F) {
        const float dashDistance = static_cast<float>(settings_.player_dash_distance_centi) / 100.0F;
        if (moveLength > 0.05F) {
            playerTransform.position[0] += moveX * dashDistance;
            playerTransform.position[2] += moveZ * dashDistance;
        } else {
            const auto target = nearest_enemy(playerTransform.position[0], playerTransform.position[2]);
            if (target.valid()) {
                const auto& targetPos = world_.get<lic::world::Transform>(target).position;
                float dx = playerTransform.position[0] - targetPos[0];
                float dz = playerTransform.position[2] - targetPos[2];
                const float length = std::max(0.001F, std::sqrt(dx * dx + dz * dz));
                playerTransform.position[0] += (dx / length) * dashDistance;
                playerTransform.position[2] += (dz / length) * dashDistance;
            }
        }
        dash_cooldown_ = dash_cooldown_duration_;
        dash_time_ = static_cast<float>(settings_.player_dash_duration_ms) / 1000.0F;
        invulnerability_time_ = static_cast<float>(settings_.dash_invulnerability_ms) / 1000.0F;
    }
    const float arenaLimit = static_cast<float>(settings_.arena_limit_centi) / 100.0F;
    playerTransform.position[0] = std::clamp(playerTransform.position[0], -arenaLimit, arenaLimit);
    playerTransform.position[2] = std::clamp(playerTransform.position[2], -arenaLimit, arenaLimit);
    playerTransform.rotation_degrees[1] = std::atan2(moveX, moveZ) * 57.29578F;

    auto& playerStats = world_.get<GameActor>(player_);
    playerStats.health = health_;
    playerStats.max_health = max_health_;

    if (input.fire_held && fire_cooldown_ <= 0.0F) {
        const auto target = nearest_enemy(playerTransform.position[0], playerTransform.position[2]);
        if (target.valid()) {
            const auto& targetPosition = world_.get<lic::world::Transform>(target).position;
            spawn_projectile(targetPosition[0], targetPosition[2]);
            fire_cooldown_ = fire_interval_;
        } else {
            fire_cooldown_ = static_cast<float>(settings_.no_target_fire_delay_ms) / 1000.0F;
        }
    }

    spawn_cooldown_ -= dt;
    if (spawn_remaining_ > 0 && spawn_cooldown_ <= 0.0F) {
        spawn_enemy();
        const int intervalMs = std::max(settings_.spawn_interval_min_ms,
            settings_.spawn_interval_base_ms - wave_ * settings_.spawn_interval_wave_reduction_ms);
        spawn_cooldown_ = static_cast<float>(intervalMs) / 1000.0F;
    }

    std::vector<lic::world::Entity> actors;
    actors.reserve(world_.entity_count());
    world_.each<GameActor>([&](lic::world::Entity entity, const GameActor&) { actors.push_back(entity); });

    for (const auto entity : actors) {
        if (!world_.alive(entity) || !world_.has<GameActor>(entity) ||
            world_.get<GameActor>(entity).kind != ActorKind::Enemy) continue;
        auto& enemy = world_.get<GameActor>(entity);
        auto& transform = world_.get<lic::world::Transform>(entity);
        enemy.cooldown = std::max(0.0F, enemy.cooldown - dt);
        const float dx = playerTransform.position[0] - transform.position[0];
        const float dz = playerTransform.position[2] - transform.position[2];
        const float distance = std::sqrt(dx * dx + dz * dz);
        if (distance > 0.0001F) {
            const float speed = enemy.speed * (dash_time_ > 0.0F ? 0.60F : 1.0F);
            transform.position[0] += (dx / distance) * speed * dt;
            transform.position[2] += (dz / distance) * speed * dt;
            transform.rotation_degrees[1] = std::atan2(dx, dz) * 57.29578F;
        }
        if (distance < static_cast<float>(settings_.enemy_contact_range_centi) / 100.0F &&
            enemy.cooldown <= 0.0F) {
            if (invulnerability_time_ <= 0.0F) {
                const int contactDamage = enemy.reward > 1
                    ? settings_.elite_contact_damage : settings_.enemy_contact_damage;
                health_ = std::max(0.0F, health_ - static_cast<float>(contactDamage));
                playerStats.health = health_;
                invulnerability_time_ =
                    static_cast<float>(settings_.player_hit_invulnerability_ms) / 1000.0F;
            }
            enemy.cooldown = static_cast<float>(settings_.enemy_contact_cooldown_ms) / 1000.0F;
        }
    }

    std::vector<lic::world::Entity> projectiles;
    for (const auto entity : actors) {
        if (world_.alive(entity) && world_.has<GameActor>(entity) &&
            world_.get<GameActor>(entity).kind == ActorKind::Projectile) {
            projectiles.push_back(entity);
        }
    }
    for (const auto projectile : projectiles) {
        if (!world_.alive(projectile)) continue;
        auto& stats = world_.get<GameActor>(projectile);
        auto& transform = world_.get<lic::world::Transform>(projectile);
        stats.lifetime -= dt;
        transform.position[0] += stats.velocity_x * dt;
        transform.position[2] += stats.velocity_z * dt;
        if (stats.lifetime <= 0.0F) {
            (void)world_.destroy_entity(projectile);
            continue;
        }
        lic::world::Entity hit{};
        for (const auto candidate : actors) {
            if (candidate == projectile || !world_.alive(candidate) || !world_.has<GameActor>(candidate) ||
                world_.get<GameActor>(candidate).kind != ActorKind::Enemy) continue;
            const auto& enemyPos = world_.get<lic::world::Transform>(candidate).position;
            const float hitRadius = static_cast<float>(settings_.projectile_hit_radius_centi) / 100.0F;
            if (squared_distance(transform.position[0], transform.position[2], enemyPos[0], enemyPos[2]) <
                hitRadius * hitRadius) {
                hit = candidate;
                break;
            }
        }
        if (hit.valid()) {
            world_.get<GameActor>(hit).health -= stats.health;
            (void)world_.destroy_entity(projectile);
            if (world_.alive(hit) && world_.get<GameActor>(hit).health <= 0.0F) finish_enemy(hit);
        }
    }

    std::vector<lic::world::Entity> pickups;
    for (const auto entity : actors) {
        if (world_.alive(entity) && world_.has<GameActor>(entity) &&
            world_.get<GameActor>(entity).kind == ActorKind::ExperienceOrb) {
            pickups.push_back(entity);
        }
    }
    for (const auto pickup : pickups) {
        if (!world_.alive(pickup)) continue;
        auto& stats = world_.get<GameActor>(pickup);
        auto& transform = world_.get<lic::world::Transform>(pickup);
        stats.lifetime -= dt;
        const float dx = playerTransform.position[0] - transform.position[0];
        const float dz = playerTransform.position[2] - transform.position[2];
        const float distance = std::sqrt(dx * dx + dz * dz);
        const float attractionRange = static_cast<float>(settings_.experience_attraction_range_centi) / 100.0F;
        const float attractionSpeed = static_cast<float>(settings_.experience_attraction_speed_centi) / 100.0F;
        const float collectRange = static_cast<float>(settings_.experience_collect_range_centi) / 100.0F;
        if (distance < attractionRange && distance > 0.001F) {
            transform.position[0] += (dx / distance) * dt * attractionSpeed;
            transform.position[2] += (dz / distance) * dt * attractionSpeed;
        }
        if (distance < collectRange) {
            const int reward = stats.reward;
            (void)world_.destroy_entity(pickup);
            collect_experience(reward);
        } else if (stats.lifetime <= 0.0F) {
            (void)world_.destroy_entity(pickup);
        }
    }

    if (health_ <= 0.0F) {
        state_ = RunState::GameOver;
        return;
    }

    int livingEnemies = 0;
    world_.each<GameActor>([&](lic::world::Entity, const GameActor& actor) {
        if (actor.kind == ActorKind::Enemy) ++livingEnemies;
    });
    if (spawn_remaining_ == 0 && livingEnemies == 0 && state_ == RunState::Playing) {
        pending_wave_advance_ = true;
        ++pending_upgrades_;
        build_upgrade_options();
        state_ = RunState::Upgrade;
    }
}

} // namespace lic::game
