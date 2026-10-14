#include <lic/game/voidbound.hpp>

#include <algorithm>

namespace lic::game {

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


} // namespace lic::game
