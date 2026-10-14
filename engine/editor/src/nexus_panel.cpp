#include <lic/editor/editor_ui.hpp>
#include <lic/scripting/process.hpp>

#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace lic::editor {

namespace {

std::string path_utf8(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

bool parse_setting_value(const std::string& line, const char* name, int& value) {
    const std::string prefix = std::string(name) + "=";
    if (!line.starts_with(prefix)) return false;
    try {
        std::size_t parsed = 0;
        const int candidate = std::stoi(line.substr(prefix.size()), &parsed);
        if (parsed != line.size() - prefix.size()) return false;
        value = candidate;
        return true;
    } catch (...) {
        return false;
    }
}

void parse_nexus_settings(const std::string& output, lic::game::ScriptSettings& settings) {
    std::istringstream input(output);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        (void)(parse_setting_value(line, "LICCFG_PLAYER_HEALTH", settings.player_health) ||
               parse_setting_value(line, "LICCFG_PLAYER_SPEED", settings.player_speed) ||
               parse_setting_value(line, "LICCFG_ENEMY_SPEED", settings.enemy_speed) ||
               parse_setting_value(line, "LICCFG_FIRE_INTERVAL_MS", settings.fire_interval_ms) ||
               parse_setting_value(line, "LICCFG_DASH_COOLDOWN_MS", settings.dash_cooldown_ms) ||
               parse_setting_value(line, "LICCFG_ENEMY_SPAWN_BASE", settings.enemy_spawn_base) ||
               parse_setting_value(line, "LICCFG_ENEMY_SPAWN_PER_WAVE", settings.enemy_spawn_per_wave) ||
               parse_setting_value(line, "LICCFG_ENEMY_SPAWN_CAP", settings.enemy_spawn_cap) ||
               parse_setting_value(line, "LICCFG_ENEMY_HEALTH_BASE", settings.enemy_health_base) ||
               parse_setting_value(line, "LICCFG_ENEMY_HEALTH_PER_WAVE", settings.enemy_health_per_wave) ||
               parse_setting_value(line, "LICCFG_ELITE_HEALTH_BONUS", settings.elite_health_bonus) ||
               parse_setting_value(line, "LICCFG_ELITE_START_WAVE", settings.elite_start_wave) ||
               parse_setting_value(line, "LICCFG_ELITE_EVERY_N", settings.elite_every_n) ||
               parse_setting_value(line, "LICCFG_SCORE_PER_REWARD", settings.score_per_reward) ||
               parse_setting_value(line, "LICCFG_EXPERIENCE_INITIAL_THRESHOLD", settings.experience_initial_threshold) ||
               parse_setting_value(line, "LICCFG_EXPERIENCE_THRESHOLD_BASE", settings.experience_threshold_base) ||
               parse_setting_value(line, "LICCFG_EXPERIENCE_THRESHOLD_PER_LEVEL", settings.experience_threshold_per_level) ||
               parse_setting_value(line, "LICCFG_EXPERIENCE_PICKUP_LIFETIME_SECONDS", settings.experience_pickup_lifetime_seconds) ||
               parse_setting_value(line, "LICCFG_SPAWN_INTERVAL_BASE_MS", settings.spawn_interval_base_ms) ||
               parse_setting_value(line, "LICCFG_SPAWN_INTERVAL_WAVE_REDUCTION_MS", settings.spawn_interval_wave_reduction_ms) ||
               parse_setting_value(line, "LICCFG_SPAWN_INTERVAL_MIN_MS", settings.spawn_interval_min_ms) ||
               parse_setting_value(line, "LICCFG_RAPID_FIRE_INTERVAL_PERCENT", settings.rapid_fire_interval_percent) ||
               parse_setting_value(line, "LICCFG_HEAVY_ROUND_DAMAGE", settings.heavy_round_damage) ||
               parse_setting_value(line, "LICCFG_VITALITY_HEALTH_BONUS", settings.vitality_health_bonus) ||
               parse_setting_value(line, "LICCFG_VITALITY_HEAL_AMOUNT", settings.vitality_heal_amount) ||
               parse_setting_value(line, "LICCFG_MOBILITY_SPEED_BONUS", settings.mobility_speed_bonus) ||
               parse_setting_value(line, "LICCFG_MOBILITY_DASH_COOLDOWN_MS", settings.mobility_dash_cooldown_ms) ||
               parse_setting_value(line, "LICCFG_ARENA_LIMIT_CENTI", settings.arena_limit_centi) ||
               parse_setting_value(line, "LICCFG_ENEMY_SPAWN_RADIUS_CENTI", settings.enemy_spawn_radius_centi) ||
               parse_setting_value(line, "LICCFG_ENEMY_SPEED_WAVE_BONUS_CENTI", settings.enemy_speed_wave_bonus_centi) ||
               parse_setting_value(line, "LICCFG_ENEMY_SPEED_WAVE_BONUS_CAP_CENTI", settings.enemy_speed_wave_bonus_cap_centi) ||
               parse_setting_value(line, "LICCFG_ELITE_SPEED_PERCENT", settings.elite_speed_percent) ||
               parse_setting_value(line, "LICCFG_INITIAL_SPAWN_DELAY_MS", settings.initial_spawn_delay_ms) ||
               parse_setting_value(line, "LICCFG_ENEMY_INITIAL_CONTACT_COOLDOWN_MS", settings.enemy_initial_contact_cooldown_ms) ||
               parse_setting_value(line, "LICCFG_PLAYER_DASH_DISTANCE_CENTI", settings.player_dash_distance_centi) ||
               parse_setting_value(line, "LICCFG_PLAYER_DASH_DURATION_MS", settings.player_dash_duration_ms) ||
               parse_setting_value(line, "LICCFG_DASH_INVULNERABILITY_MS", settings.dash_invulnerability_ms) ||
               parse_setting_value(line, "LICCFG_ENEMY_CONTACT_DAMAGE", settings.enemy_contact_damage) ||
               parse_setting_value(line, "LICCFG_ELITE_CONTACT_DAMAGE", settings.elite_contact_damage) ||
               parse_setting_value(line, "LICCFG_ENEMY_CONTACT_RANGE_CENTI", settings.enemy_contact_range_centi) ||
               parse_setting_value(line, "LICCFG_ENEMY_CONTACT_COOLDOWN_MS", settings.enemy_contact_cooldown_ms) ||
               parse_setting_value(line, "LICCFG_PLAYER_HIT_INVULNERABILITY_MS", settings.player_hit_invulnerability_ms) ||
               parse_setting_value(line, "LICCFG_PROJECTILE_SPEED_CENTI", settings.projectile_speed_centi) ||
               parse_setting_value(line, "LICCFG_PROJECTILE_LIFETIME_MS", settings.projectile_lifetime_ms) ||
               parse_setting_value(line, "LICCFG_PROJECTILE_HIT_RADIUS_CENTI", settings.projectile_hit_radius_centi) ||
               parse_setting_value(line, "LICCFG_NO_TARGET_FIRE_DELAY_MS", settings.no_target_fire_delay_ms) ||
               parse_setting_value(line, "LICCFG_EXPERIENCE_ATTRACTION_RANGE_CENTI", settings.experience_attraction_range_centi) ||
               parse_setting_value(line, "LICCFG_EXPERIENCE_ATTRACTION_SPEED_CENTI", settings.experience_attraction_speed_centi) ||
               parse_setting_value(line, "LICCFG_EXPERIENCE_COLLECT_RANGE_CENTI", settings.experience_collect_range_centi));
    }
}

} // namespace

void EditorUi::load_nexus_script() {
    nexus_script_buffer_.fill('\0');
    std::ifstream input(script_path_, std::ios::binary);
    if (!input) {
        nexus_script_loaded_ = false;
        nexus_status_ = "Cannot open " + script_path_.string();
        return;
    }
    const std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const std::size_t length = std::min(source.size(), nexus_script_buffer_.size() - 1U);
    std::copy_n(source.data(), length, nexus_script_buffer_.data());
    nexus_script_loaded_ = true;
    nexus_status_ = "Loaded " + script_path_.filename().string();
}

void EditorUi::save_nexus_script() {
    try {
        std::filesystem::create_directories(script_path_.parent_path());
        std::ofstream output(script_path_, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot write " + script_path_.string());
        const auto end = std::find(nexus_script_buffer_.begin(), nexus_script_buffer_.end(), '\0');
        output.write(nexus_script_buffer_.data(),
                     static_cast<std::streamsize>(std::distance(nexus_script_buffer_.begin(), end)));
        if (!output) throw std::runtime_error("write failed for " + script_path_.string());
        nexus_script_loaded_ = true;
        nexus_status_ = "Saved " + script_path_.filename().string();
    } catch (const std::exception& error) {
        nexus_status_ = std::string("Save failed: ") + error.what();
        log(nexus_status_);
    }
}

void EditorUi::execute_nexus_action(int action) {
    save_nexus_script();
    if (!std::filesystem::is_regular_file(nexus_executable_)) {
        nexus_status_ = "NEXUS compiler not found beside the LIC Engine executable";
        nexus_output_ = "Expected compiler: " + nexus_executable_.string() +
            "\nDownload the complete editor ZIP; it includes nexus.exe and runtime/.";
        log(nexus_status_);
        return;
    }

    std::error_code error;
    std::filesystem::create_directories(nexus_rules_binary_.parent_path(), error);
    if (error) {
        nexus_status_ = "Cannot create NEXUS build directory: " + error.message();
        nexus_output_ = nexus_status_;
        return;
    }
    const auto logFile = nexus_rules_binary_.parent_path() / "nexus-process.log";
    const std::string scriptArgument = path_utf8(script_path_);
    const std::string outputArgument = path_utf8(nexus_rules_binary_);

    std::vector<std::string> command;
    if (action == 0) {
        command = {"check", scriptArgument};
    } else {
        command = {"build", scriptArgument, "-o", outputArgument, "--backend", "native"};
    }

    auto result = lic::scripting::run_process_capture(nexus_executable_, command, project_root_, logFile);
    nexus_output_ = result.output;
    if (!result.started || result.exit_code != 0) {
        nexus_status_ = action == 0 ? "NEXUS check failed" : "NEXUS build failed";
        if (nexus_output_.empty()) nexus_output_ = "NEXUS process failed to start or returned no diagnostics.";
        log(nexus_status_);
        return;
    }
    if (action == 0) {
        nexus_status_ = "NEXUS syntax and semantic checks passed";
        log(nexus_status_);
        return;
    }
    if (action == 1) {
        nexus_status_ = "Built " + nexus_rules_binary_.filename().string();
        log(nexus_status_);
        return;
    }

    const auto builtOutput = nexus_output_;
    auto runResult = lic::scripting::run_process_capture(
        nexus_rules_binary_, {}, project_root_, logFile);
    nexus_output_ = builtOutput + "\n--- GameRules.nx output ---\n" + runResult.output;
    if (!runResult.started || runResult.exit_code != 0) {
        nexus_status_ = "NEXUS rules executable failed";
        log(nexus_status_);
        return;
    }

    lic::game::ScriptSettings settings{};
    parse_nexus_settings(runResult.output, settings);
    game_.apply_script_settings(settings);
    nexus_status_ = "NEXUS rules loaded: HP " + std::to_string(settings.player_health) +
        ", speed " + std::to_string(settings.player_speed) + "/100, enemies " +
        std::to_string(settings.enemy_spawn_cap) + " max per wave";
    log(nexus_status_);
}

void EditorUi::apply_nexus_game_rules() {
    execute_nexus_action(2);
}

void EditorUi::start_game_run() {
    apply_nexus_game_rules();
    game_.start_run();
    apply_project_materials();
    if (nexus_status_.find("NEXUS rules loaded:") == std::string::npos) {
        log("Started with built-in game defaults because the NEXUS rules script did not load");
    }
}

void EditorUi::draw_nexus_panel() {
    if (!show_nexus_panel_) return;
    ImGui::SetNextWindowSize(ImVec2(760.0F, 570.0F), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("NEXUS Game Rules", &show_nexus_panel_)) {
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("%s", script_path_.string().c_str());
    ImGui::TextWrapped("This is real NEXUS source. The game compiles and runs it when a new run starts; "
                       "print lines beginning with LICCFG_ configure Voidbound.");
    ImGui::InputTextMultiline("##NEXUSGameRules", nexus_script_buffer_.data(),
        nexus_script_buffer_.size(), ImVec2(-1.0F, 280.0F), ImGuiInputTextFlags_AllowTabInput);
    if (ImGui::Button("Save")) save_nexus_script();
    ImGui::SameLine();
    if (ImGui::Button("Check")) execute_nexus_action(0);
    ImGui::SameLine();
    if (ImGui::Button("Build")) execute_nexus_action(1);
    ImGui::SameLine();
    if (ImGui::Button("Test Rules")) execute_nexus_action(2);
    ImGui::SameLine();
    if (ImGui::Button("Reload")) load_nexus_script();
    ImGui::TextDisabled("%s", nexus_status_.c_str());
    ImGui::Separator();
    ImGui::BeginChild("##NexusOutput", ImVec2(0.0F, 116.0F), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (nexus_output_.empty()) {
        ImGui::TextDisabled("NEXUS compiler output will appear here.");
    } else {
        ImGui::TextUnformatted(nexus_output_.c_str());
    }
    ImGui::EndChild();
    ImGui::End();
}


} // namespace lic::editor
