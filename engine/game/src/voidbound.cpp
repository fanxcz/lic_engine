#include <lic/game/voidbound.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace lic::game {
namespace {

constexpr float kArenaLimit = 3.35F;
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

void Voidbound::start_run() {
    world_ = lic::world::World{};
    state_ = RunState::Playing;
    wave_ = 1;
    score_ = 0;
    kills_ = 0;
    level_ = 1;
    experience_ = 0;
    experience_to_next_ = 5;
    pending_upgrades_ = 0;
    pending_wave_advance_ = false;
    health_ = 100.0F;
    max_health_ = 100.0F;
    move_speed_ = 2.3F;
    damage_ = 1.0F;
    fire_interval_ = 0.24F;
    fire_cooldown_ = 0.0F;
    spawn_cooldown_ = 0.35F;
    dash_cooldown_ = 0.0F;
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
    spawn_remaining_ = std::min(4 + wave_ * 2, 28);
    spawn_cooldown_ = 0.45F;
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
    const int spawnIndex = 4 + wave_ * 2 - spawn_remaining_;
    const float angle = static_cast<float>(spawnIndex) * 2.39996323F +
                        static_cast<float>(wave_) * 0.41F;
    const float radius = 3.45F;
    const bool elite = wave_ >= 3 && ((spawnIndex + wave_) % 5 == 0);
    lic::world::Transform transform;
    transform.position = {std::cos(angle) * radius, 0.06F, std::sin(angle) * radius};
    transform.scale = elite ? std::array<float, 3>{0.39F, 0.48F, 0.39F}
                            : std::array<float, 3>{0.27F, 0.34F, 0.27F};
    lic::world::Appearance appearance;
    if (elite) appearance.color = {0.94F, 0.31F, 0.54F, 1.0F};
    else appearance.color = {0.94F, 0.29F, 0.24F, 1.0F};

    GameActor stats;
    stats.health = (elite ? 4.0F : 2.0F) + static_cast<float>(wave_) * 0.20F;
    stats.max_health = stats.health;
    stats.speed = (elite ? 0.55F : 0.68F) + std::min(static_cast<float>(wave_) * 0.025F, 0.42F);
    stats.cooldown = 0.2F;
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
    stats.velocity_x = dx * 6.8F;
    stats.velocity_z = dz * 6.8F;
    stats.lifetime = 1.1F;
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
    stats.lifetime = 18.0F;
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
        score_ += 100 * reward * wave_;
        spawn_experience(position[0], position[2], reward);
    }
}

void Voidbound::collect_experience(int amount) {
    if (amount <= 0) return;
    experience_ += amount;
    while (experience_ >= experience_to_next_) {
        experience_ -= experience_to_next_;
        ++level_;
        experience_to_next_ = 4 + level_ * 2;
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
        fire_interval_ = std::max(0.085F, fire_interval_ * 0.75F);
        break;
    case UpgradeKind::HeavyRounds:
        damage_ += 1.0F;
        break;
    case UpgradeKind::Vitality:
        max_health_ += 25.0F;
        health_ = std::min(max_health_, health_ + 45.0F);
        break;
    case UpgradeKind::Mobility:
        move_speed_ += 0.28F;
        dash_cooldown_ = std::min(dash_cooldown_, 0.35F);
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
        if (moveLength > 0.05F) {
            playerTransform.position[0] += moveX * 1.0F;
            playerTransform.position[2] += moveZ * 1.0F;
        } else {
            const auto target = nearest_enemy(playerTransform.position[0], playerTransform.position[2]);
            if (target.valid()) {
                const auto& targetPos = world_.get<lic::world::Transform>(target).position;
                float dx = playerTransform.position[0] - targetPos[0];
                float dz = playerTransform.position[2] - targetPos[2];
                const float length = std::max(0.001F, std::sqrt(dx * dx + dz * dz));
                playerTransform.position[0] += (dx / length) * 1.0F;
                playerTransform.position[2] += (dz / length) * 1.0F;
            }
        }
        dash_cooldown_ = 1.35F;
        dash_time_ = 0.18F;
        invulnerability_time_ = 0.34F;
    }
    playerTransform.position[0] = std::clamp(playerTransform.position[0], -kArenaLimit, kArenaLimit);
    playerTransform.position[2] = std::clamp(playerTransform.position[2], -kArenaLimit, kArenaLimit);
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
            fire_cooldown_ = 0.05F;
        }
    }

    spawn_cooldown_ -= dt;
    if (spawn_remaining_ > 0 && spawn_cooldown_ <= 0.0F) {
        spawn_enemy();
        spawn_cooldown_ = std::max(0.32F, 0.95F - static_cast<float>(wave_) * 0.035F);
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
        if (distance < 0.52F && enemy.cooldown <= 0.0F) {
            if (invulnerability_time_ <= 0.0F) {
                health_ = std::max(0.0F, health_ - (enemy.reward > 1 ? 13.0F : 8.0F));
                playerStats.health = health_;
                invulnerability_time_ = 0.35F;
            }
            enemy.cooldown = 0.72F;
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
            if (squared_distance(transform.position[0], transform.position[2], enemyPos[0], enemyPos[2]) < 0.16F) {
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
        if (distance < 1.1F && distance > 0.001F) {
            transform.position[0] += (dx / distance) * dt * 1.25F;
            transform.position[2] += (dz / distance) * dt * 1.25F;
        }
        if (distance < 0.35F) {
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
