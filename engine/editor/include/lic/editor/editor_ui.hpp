#pragma once

#include <lic/world/scene.hpp>
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
    EditorUi();
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
    [[nodiscard]] lic::world::Entity selected_entity() const noexcept { return selected_entity_; }
    [[nodiscard]] ViewportRect viewport_rect() const noexcept { return viewport_rect_; }
    [[nodiscard]] float camera_yaw() const noexcept { return camera_yaw_; }
    [[nodiscard]] float camera_pitch() const noexcept { return camera_pitch_; }
    [[nodiscard]] float camera_zoom() const noexcept { return camera_zoom_; }
    [[nodiscard]] float play_rotation_degrees() const noexcept { return play_rotation_degrees_; }
    [[nodiscard]] bool playing() const noexcept { return playing_; }

private:
    std::filesystem::path project_root_;
    std::filesystem::path scene_path_;
    lic::world::World world_;
    lic::world::Entity selected_entity_{};
    ViewportRect viewport_rect_{};
    std::vector<std::string> console_messages_;
    GLFWwindow* window_{};
    bool imgui_context_initialized_{};
    bool imgui_glfw_initialized_{};
    bool imgui_vulkan_initialized_{};
    bool scene_dirty_{};
    bool playing_{};
    bool show_grid_{true};
    bool unsaved_new_scene_requested_{};
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
    void duplicate_selected();
    void delete_selected();
    void toggle_play();
    void draw_scene_canvas();
    void draw_toolbar();
    void draw_hierarchy();
    void draw_inspector();
    void draw_bottom_panel();
    void draw_new_scene_dialog();
};

} // namespace lic::editor
