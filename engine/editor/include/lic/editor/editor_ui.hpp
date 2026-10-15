#pragma once

#include <lic/game/voidbound.hpp>
#include <lic/world/scene.hpp>

#include <array>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct GLFWwindow;

namespace lic::editor {

struct ViewportRect final {
    float x{};
    float y{};
    float width{};
    float height{};
};

class EditorUi final {
public:
    explicit EditorUi(bool start_in_game = false, std::filesystem::path executable_directory = {});
    ~EditorUi();

    EditorUi(const EditorUi&) = delete;
    EditorUi& operator=(const EditorUi&) = delete;

    void initialize_platform(GLFWwindow* window);
    void initialize_renderer(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device,
                             std::uint32_t queue_family, VkQueue queue, VkRenderPass render_pass,
                             std::uint32_t image_count);
    void shutdown_renderer() noexcept;
    void shutdown_platform() noexcept;

    void build_frame(float delta_seconds);
    void render(VkCommandBuffer command_buffer);

    [[nodiscard]] lic::world::World& world() noexcept { return world_; }
    [[nodiscard]] const lic::world::World& world() const noexcept { return world_; }
    [[nodiscard]] lic::world::World& render_world() noexcept { return game_mode_ ? game_.world() : world_; }
    [[nodiscard]] const lic::world::World& render_world() const noexcept { return game_mode_ ? game_.world() : world_; }
    [[nodiscard]] const std::filesystem::path& project_root_path() const noexcept { return project_root_; }
    [[nodiscard]] lic::world::Entity selected_entity() const noexcept { return selected_entity_; }
    [[nodiscard]] ViewportRect viewport_rect() const noexcept { return viewport_rect_; }
    [[nodiscard]] float camera_yaw() const noexcept;
    [[nodiscard]] float camera_pitch() const noexcept;
    [[nodiscard]] float camera_zoom() const noexcept;
    [[nodiscard]] float play_rotation_degrees() const noexcept { return play_rotation_degrees_; }
    [[nodiscard]] bool playing() const noexcept { return playing_; }
    [[nodiscard]] bool game_mode() const noexcept { return game_mode_; }
    [[nodiscard]] bool standalone_game() const noexcept { return standalone_game_; }

private:
    std::filesystem::path project_root_;
    std::filesystem::path scene_path_;
    std::filesystem::path script_path_;
    std::filesystem::path executable_directory_;
    std::filesystem::path nexus_executable_;
    std::filesystem::path nexus_rules_binary_;
    lic::world::World world_;
    lic::game::Voidbound game_;
    lic::world::Entity selected_entity_{};
    ViewportRect viewport_rect_{};
    std::vector<std::string> console_messages_;
    std::array<char, 32768> nexus_script_buffer_{};
    std::array<char, 1024> model_import_path_{};
    std::array<lic::world::MaterialResource, 5> game_materials_{};
    std::string nexus_output_;
    std::string nexus_status_{"NEXUS rules not loaded"};
    GLFWwindow* window_{};
    bool imgui_context_initialized_{};
    bool imgui_glfw_initialized_{};
    bool imgui_vulkan_initialized_{};
    bool scene_dirty_{};
    bool playing_{};
    bool show_grid_{true};
    bool unsaved_new_scene_requested_{};
    bool game_mode_{};
    bool standalone_game_{};
    bool game_escape_was_down_{};
    bool game_dash_was_down_{};
    bool nexus_script_loaded_{};
    bool show_nexus_panel_{};
    bool show_model_import_dialog_{};
    float camera_yaw_{0.68F};
    float camera_pitch_{0.38F};
    float camera_zoom_{1.0F};
    float play_rotation_degrees_{};
    float delta_seconds_{};
    std::string status_message_{"Ready"};

    void configure_style();
    void create_default_scene();
    void select_first_mesh();
    void log(std::string message);
    void mark_dirty();
    void save_scene();
    void load_scene();
    void request_new_scene();
    void add_node(lic::world::NodeKind kind);
    void import_model_from_path();
    void draw_model_import_dialog();
    void duplicate_selected();
    void delete_selected();
    void toggle_play();
    void load_nexus_script();
    void save_nexus_script();
    void execute_nexus_action(int action);
    void apply_nexus_game_rules();
    void load_project_materials();
    void seed_material_library();
    void apply_project_materials();
    void start_game_run();
    void draw_nexus_panel();
    void enter_game_mode(bool standalone = false);
    void exit_game_mode();
    void draw_game_ui();
    void draw_scene_canvas();
    void draw_toolbar();
    void draw_hierarchy();
    void draw_inspector();
    void draw_bottom_panel();
    void draw_new_scene_dialog();
};

} // namespace lic::editor
