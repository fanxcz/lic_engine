#include <lic/game/voidbound.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace lic::game {
namespace {

[[nodiscard]] float squared_distance(float ax, float az, float bx, float bz) noexcept {
    const float dx = ax - bx;
    const float dz = az - bz;
    return dx * dx + dz * dz;
}

[[nodiscard]] float clamp_delta(float value) noexcept {
    return std::clamp(value, 0.0F, 0.05F);
}

} // namespace

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
