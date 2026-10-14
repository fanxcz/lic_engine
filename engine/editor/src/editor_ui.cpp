#include <lic/editor/editor_ui.hpp>
#include <lic/scripting/process.hpp>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
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

constexpr float kToolbarHeight = 44.0F;
constexpr float kLeftPanelWidth = 238.0F;
constexpr float kRightPanelWidth = 288.0F;
constexpr float kBottomPanelHeight = 162.0F;
constexpr ImGuiWindowFlags kFixedPanelFlags =
    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

std::filesystem::path default_project_root() {
#ifdef _WIN32
    if (const char* local = std::getenv("LOCALAPPDATA"); local != nullptr && *local != '\0') {
        return std::filesystem::path(local) / "LIC Engine" / "Projects" / "Default";
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / "Library" / "Application Support" / "LIC Engine" / "Projects" / "Default";
    }
#else
    if (const char* data = std::getenv("XDG_DATA_HOME"); data != nullptr && *data != '\0') {
        return std::filesystem::path(data) / "lic-engine" / "Projects" / "Default";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".local" / "share" / "lic-engine" / "Projects" / "Default";
    }
#endif
    return std::filesystem::temp_directory_path() / "lic-engine-projects" / "Default";
}

const char* node_icon(lic::world::NodeKind kind) {
    switch (kind) {
    case lic::world::NodeKind::Node3D: return "[N]";
    case lic::world::NodeKind::MeshInstance3D: return "[M]";
    case lic::world::NodeKind::Camera3D: return "[C]";
    case lic::world::NodeKind::DirectionalLight3D: return "[L]";
    case lic::world::NodeKind::StaticBody3D: return "[B]";
    }
    return "[?]";
}

} // namespace

EditorUi::EditorUi(bool start_in_game, std::filesystem::path executable_directory)
    : project_root_(default_project_root()),
      scene_path_(project_root_ / "Scenes" / "Main.licscene"),
      script_path_(project_root_ / "Scripts" / "GameRules.nx"),
      executable_directory_(std::move(executable_directory)),
      game_mode_(start_in_game), standalone_game_(start_in_game) {
    if (executable_directory_.empty()) executable_directory_ = std::filesystem::current_path();
#ifdef _WIN32
    nexus_executable_ = executable_directory_ / "nexus.exe";
    nexus_rules_binary_ = project_root_ / "Build" / "Nexus" / "GameRules.exe";
#else
    nexus_executable_ = executable_directory_ / "nexus";
    nexus_rules_binary_ = project_root_ / "Build" / "Nexus" / "GameRules";
#endif
    create_default_scene();
}

float EditorUi::camera_yaw() const noexcept {
    return game_mode_ ? 0.0F : camera_yaw_;
}

float EditorUi::camera_pitch() const noexcept {
    return game_mode_ ? 0.88F : camera_pitch_;
}

float EditorUi::camera_zoom() const noexcept {
    return game_mode_ ? 0.23F : camera_zoom_;
}

void EditorUi::enter_game_mode(bool standalone) {
    playing_ = false;
    game_ = lic::game::Voidbound{};
    game_mode_ = true;
    standalone_game_ = standalone;
    game_escape_was_down_ = false;
    game_dash_was_down_ = false;
    status_message_ = "Voidbound";
    log("Entered Voidbound game mode");
}

void EditorUi::exit_game_mode() {
    game_mode_ = false;
    standalone_game_ = false;
    playing_ = false;
    game_escape_was_down_ = false;
    game_dash_was_down_ = false;
    camera_yaw_ = 0.68F;
    camera_pitch_ = 0.38F;
    camera_zoom_ = 1.0F;
    status_message_ = "Returned to editor";
    log("Returned to scene editor");
}

EditorUi::~EditorUi() {
    shutdown_platform();
}

void EditorUi::initialize_platform(GLFWwindow* window) {
    if (imgui_context_initialized_) {
        throw std::logic_error("Dear ImGui platform layer is already initialized");
    }
    if (window == nullptr) {
        throw std::invalid_argument("Editor UI requires a valid GLFW window");
    }
    window_ = window;
    std::filesystem::create_directories(project_root_ / "Scenes");
    std::filesystem::create_directories(project_root_ / "Assets");
    std::filesystem::create_directories(project_root_ / "Assets" / "Materials");
    std::filesystem::create_directories(script_path_.parent_path());
    std::filesystem::create_directories(nexus_rules_binary_.parent_path());
    seed_material_library();
    load_project_materials();
    if (!std::filesystem::exists(script_path_)) {
        const auto bundledScript = executable_directory_ / "assets" / "scripts" / "GameRules.nx";
        std::error_code copyError;
        if (std::filesystem::is_regular_file(bundledScript, copyError)) {
            std::filesystem::copy_file(bundledScript, script_path_,
                                       std::filesystem::copy_options::skip_existing, copyError);
        }
        if (copyError || !std::filesystem::exists(script_path_)) {
            std::ofstream sample(script_path_, std::ios::binary | std::ios::trunc);
            sample << "fn main() {\n"
                      "    print(\"LICCFG_PLAYER_HEALTH=100\")\n"
                      "    print(\"LICCFG_PLAYER_SPEED=230\")\n"
                      "    print(\"LICCFG_ENEMY_SPEED=68\")\n"
                      "    print(\"LICCFG_FIRE_INTERVAL_MS=240\")\n"
                      "    print(\"LICCFG_DASH_COOLDOWN_MS=1350\")\n"
                      "}\n";
        }
    }
    load_nexus_script();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    imgui_context_initialized_ = true;
    configure_style();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (!ImGui_ImplGlfw_InitForVulkan(window_, true)) {
        throw std::runtime_error("Dear ImGui could not initialize its GLFW input backend");
    }
    imgui_glfw_initialized_ = true;

    if (std::filesystem::exists(scene_path_)) {
        try {
            lic::world::load_scene(world_, scene_path_);
            scene_dirty_ = false;
            select_first_mesh();
            log("Loaded " + scene_path_.string());
        } catch (const std::exception& error) {
            log(std::string("Scene load failed; using the default scene: ") + error.what());
            create_default_scene();
            scene_dirty_ = false;
        }
    } else {
        log("Created a new project at " + project_root_.string());
        log("Press Save to write Scenes/Main.licscene");
    }
}

void EditorUi::initialize_renderer(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device,
                                   std::uint32_t queue_family, VkQueue queue, VkRenderPass render_pass,
                                   std::uint32_t image_count) {
    if (!imgui_context_initialized_ || !imgui_glfw_initialized_) {
        throw std::logic_error("Initialize the GLFW editor layer before the Vulkan backend");
    }
    if (imgui_vulkan_initialized_) {
        throw std::logic_error("Dear ImGui Vulkan backend is already initialized");
    }
    if (image_count < 2) {
        throw std::runtime_error("The editor UI requires at least two swapchain images");
    }

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_0;
    info.Instance = instance;
    info.PhysicalDevice = physical_device;
    info.Device = device;
    info.QueueFamily = queue_family;
    info.Queue = queue;
    info.DescriptorPool = VK_NULL_HANDLE;
    info.DescriptorPoolSize = 1000;
    info.RenderPass = render_pass;
    info.MinImageCount = std::min(image_count, std::max(2U, image_count - 1U));
    info.ImageCount = image_count;
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    if (!ImGui_ImplVulkan_Init(&info)) {
        throw std::runtime_error("Dear ImGui could not initialize its Vulkan renderer backend");
    }
    imgui_vulkan_initialized_ = true;
    if (!ImGui_ImplVulkan_CreateFontsTexture()) {
        throw std::runtime_error("Dear ImGui could not upload its font atlas to Vulkan");
    }
}

void EditorUi::shutdown_renderer() noexcept {
    if (imgui_vulkan_initialized_) {
        ImGui_ImplVulkan_Shutdown();
        imgui_vulkan_initialized_ = false;
    }
}

void EditorUi::shutdown_platform() noexcept {
    shutdown_renderer();
    if (imgui_glfw_initialized_) {
        ImGui_ImplGlfw_Shutdown();
        imgui_glfw_initialized_ = false;
    }
    if (imgui_context_initialized_) {
        ImGui::DestroyContext();
        imgui_context_initialized_ = false;
    }
    window_ = nullptr;
}

void EditorUi::configure_style() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(9.0F, 8.0F);
    style.FramePadding = ImVec2(7.0F, 4.0F);
    style.ItemSpacing = ImVec2(7.0F, 5.0F);
    style.ItemInnerSpacing = ImVec2(5.0F, 4.0F);
    style.WindowRounding = 0.0F;
    style.ChildRounding = 3.0F;
    style.FrameRounding = 3.0F;
    style.PopupRounding = 3.0F;
    style.ScrollbarRounding = 3.0F;
    style.GrabRounding = 3.0F;
    style.WindowBorderSize = 1.0F;
    style.FrameBorderSize = 0.0F;
    style.PopupBorderSize = 1.0F;
    auto& colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.88F, 0.89F, 0.91F, 1.00F);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.48F, 0.50F, 0.54F, 1.00F);
    colors[ImGuiCol_WindowBg] = ImVec4(0.105F, 0.112F, 0.124F, 1.00F);
    colors[ImGuiCol_ChildBg] = ImVec4(0.105F, 0.112F, 0.124F, 1.00F);
    colors[ImGuiCol_PopupBg] = ImVec4(0.12F, 0.13F, 0.145F, 0.99F);
    colors[ImGuiCol_Border] = ImVec4(0.20F, 0.21F, 0.23F, 1.00F);
    colors[ImGuiCol_FrameBg] = ImVec4(0.16F, 0.17F, 0.19F, 1.00F);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22F, 0.24F, 0.27F, 1.00F);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.25F, 0.28F, 0.32F, 1.00F);
    colors[ImGuiCol_TitleBg] = ImVec4(0.12F, 0.13F, 0.145F, 1.00F);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.12F, 0.13F, 0.145F, 1.00F);
    colors[ImGuiCol_Button] = ImVec4(0.19F, 0.20F, 0.22F, 1.00F);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.26F, 0.29F, 0.32F, 1.00F);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.30F, 0.34F, 0.38F, 1.00F);
    colors[ImGuiCol_Header] = ImVec4(0.18F, 0.24F, 0.30F, 1.00F);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.22F, 0.30F, 0.38F, 1.00F);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.25F, 0.35F, 0.44F, 1.00F);
    colors[ImGuiCol_Tab] = ImVec4(0.14F, 0.15F, 0.17F, 1.00F);
    colors[ImGuiCol_TabHovered] = ImVec4(0.22F, 0.29F, 0.36F, 1.00F);
    colors[ImGuiCol_TabActive] = ImVec4(0.18F, 0.22F, 0.27F, 1.00F);
    colors[ImGuiCol_CheckMark] = ImVec4(0.53F, 0.72F, 0.91F, 1.00F);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.42F, 0.61F, 0.79F, 1.00F);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.54F, 0.73F, 0.91F, 1.00F);
    colors[ImGuiCol_Separator] = ImVec4(0.22F, 0.23F, 0.25F, 1.00F);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.25F, 0.35F, 0.44F, 0.45F);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.42F, 0.61F, 0.79F, 0.75F);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.53F, 0.72F, 0.91F, 1.00F);
}

void EditorUi::create_default_scene() {
    world_ = lic::world::World{};
    (void)lic::world::create_node(world_, lic::world::NodeKind::Node3D, "World");
    lic::world::Appearance cubeAppearance;
    cubeAppearance.color = {0.36F, 0.66F, 0.89F, 1.0F};
    lic::world::Transform cubeTransform;
    cubeTransform.scale = {1.0F, 1.0F, 1.0F};
    selected_entity_ = lic::world::create_node(world_, lic::world::NodeKind::MeshInstance3D,
                                                "Cube", cubeTransform, cubeAppearance);

    lic::world::Transform cameraTransform;
    cameraTransform.position = {0.0F, 1.0F, 3.5F};
    (void)lic::world::create_node(world_, lic::world::NodeKind::Camera3D, "Camera3D", cameraTransform);

    lic::world::Transform lightTransform;
    lightTransform.rotation_degrees = {-35.0F, -35.0F, 0.0F};
    (void)lic::world::create_node(world_, lic::world::NodeKind::DirectionalLight3D,
                                  "DirectionalLight3D", lightTransform);
}

void EditorUi::select_first_mesh() {
    selected_entity_ = {};
    world_.each<lic::world::NodeName>([&](lic::world::Entity entity, const lic::world::NodeName&) {
        if (!selected_entity_.valid() && world_.has<lic::world::NodeKindComponent>(entity) &&
            lic::world::node_kind_renders_mesh(world_.get<lic::world::NodeKindComponent>(entity).value)) {
            selected_entity_ = entity;
        }
    });
    if (!selected_entity_.valid()) {
        world_.each<lic::world::NodeName>([&](lic::world::Entity entity, const lic::world::NodeName&) {
            if (!selected_entity_.valid()) selected_entity_ = entity;
        });
    }
}

void EditorUi::log(std::string message) {
    console_messages_.push_back(std::move(message));
    if (console_messages_.size() > 400) {
        console_messages_.erase(console_messages_.begin(), console_messages_.begin() + 100);
    }
}

void EditorUi::mark_dirty() {
    scene_dirty_ = true;
    status_message_ = "Unsaved changes";
}

void EditorUi::save_scene() {
    try {
        lic::world::save_scene(world_, scene_path_);
        scene_dirty_ = false;
        status_message_ = "Scene saved";
        log("Saved scene: " + scene_path_.string());
    } catch (const std::exception& error) {
        status_message_ = "Save failed";
        log(std::string("Save failed: ") + error.what());
    }
}

void EditorUi::load_scene() {
    try {
        lic::world::load_scene(world_, scene_path_);
        scene_dirty_ = false;
        playing_ = false;
        play_rotation_degrees_ = 0.0F;
        select_first_mesh();
        status_message_ = "Scene loaded";
        log("Loaded scene: " + scene_path_.string());
    } catch (const std::exception& error) {
        status_message_ = "Load failed";
        log(std::string("Load failed: ") + error.what());
    }
}

void EditorUi::request_new_scene() {
    if (scene_dirty_) {
        unsaved_new_scene_requested_ = true;
        ImGui::OpenPopup("Unsaved changes");
        return;
    }
    create_default_scene();
    scene_dirty_ = true;
    status_message_ = "New scene";
    log("Created a new scene; save to write it to disk");
}

void EditorUi::add_node(lic::world::NodeKind kind) {
    lic::world::Transform transform;
    if (kind == lic::world::NodeKind::Camera3D) {
        transform.position = {0.0F, 0.0F, 3.0F};
    } else if (kind == lic::world::NodeKind::DirectionalLight3D) {
        transform.rotation_degrees = {-35.0F, -35.0F, 0.0F};
    } else if (lic::world::node_kind_renders_mesh(kind)) {
        const float offset = static_cast<float>((world_.entity_count() % 5U)) * 0.35F;
        transform.position = {offset - 0.5F, 0.0F, 0.0F};
        transform.scale = {0.58F, 0.58F, 0.58F};
    }
    const auto entity = lic::world::create_node(world_, kind, std::string(lic::world::node_kind_name(kind)), transform);
    selected_entity_ = entity;
    mark_dirty();
    log("Added " + std::string(lic::world::node_kind_name(kind)));
}

void EditorUi::duplicate_selected() {
    if (!world_.alive(selected_entity_) || !world_.has<lic::world::NodeName>(selected_entity_)) return;
    const auto name = world_.get<lic::world::NodeName>(selected_entity_).value + " Copy";
    auto transform = world_.get<lic::world::Transform>(selected_entity_);
    const auto kind = world_.get<lic::world::NodeKindComponent>(selected_entity_).value;
    const auto appearance = world_.get<lic::world::Appearance>(selected_entity_);
    const bool enabled = world_.get<lic::world::NodeEnabled>(selected_entity_).value;
    transform.position[0] += 0.35F;
    const auto copy = lic::world::create_node(world_, kind, name, transform, appearance);
    world_.get<lic::world::NodeEnabled>(copy).value = enabled;
    selected_entity_ = copy;
    mark_dirty();
    log("Duplicated node: " + name);
}

void EditorUi::delete_selected() {
    if (!world_.alive(selected_entity_) || !world_.has<lic::world::NodeName>(selected_entity_)) return;
    const std::string name = world_.get<lic::world::NodeName>(selected_entity_).value;
    if (name == "World") {
        log("The World root cannot be deleted");
        return;
    }
    if (world_.destroy_entity(selected_entity_)) {
        selected_entity_ = {};
        select_first_mesh();
        mark_dirty();
        log("Deleted node: " + name);
    }
}

void EditorUi::toggle_play() {
    playing_ = !playing_;
    play_rotation_degrees_ = 0.0F;
    status_message_ = playing_ ? "Running preview" : "Preview stopped";
    log(playing_ ? "Started preview: meshes rotate temporarily; scene data is unchanged" : "Stopped preview");
}

void EditorUi::build_frame(float delta_seconds) {
    if (!imgui_context_initialized_ || !imgui_vulkan_initialized_) return;
    delta_seconds_ = std::clamp(delta_seconds, 0.0F, 0.1F);
    if (playing_) {
        play_rotation_degrees_ = std::fmod(play_rotation_degrees_ + delta_seconds_ * 45.0F, 360.0F);
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (game_mode_) {
        viewport_rect_ = {0.0F, 0.0F, std::max(1.0F, display.x), std::max(1.0F, display.y)};
        const bool escapeDown = glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        const bool escapePressed = escapeDown && !game_escape_was_down_;
        game_escape_was_down_ = escapeDown;
        const bool dashDown = glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                              glfwGetKey(window_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
        const bool dashPressed = dashDown && !game_dash_was_down_;
        game_dash_was_down_ = dashDown;

        if (escapePressed) {
            if (game_.state() == lic::game::RunState::Playing) game_.pause();
            else if (game_.state() == lic::game::RunState::Paused) game_.resume();
            else if (!standalone_game_) exit_game_mode();
            else if (game_.state() != lic::game::RunState::Upgrade) game_.show_menu();
        }

        if (game_mode_ && game_.state() == lic::game::RunState::Playing) {
            lic::game::Input input;
            input.move_x = static_cast<float>(glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS) -
                           static_cast<float>(glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS);
            input.move_z = static_cast<float>(glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS) -
                           static_cast<float>(glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS);
            input.dash_pressed = dashPressed;
            const ImGuiIO& inputIo = ImGui::GetIO();
            input.fire_held = (!inputIo.WantCaptureMouse &&
                               glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) ||
                              glfwGetKey(window_, GLFW_KEY_SPACE) == GLFW_PRESS;
            game_.update(delta_seconds_, input);
            apply_project_materials();
        }

        // Do not create a fullscreen SceneCanvas window behind the game HUD.
        // It could steal ImGui focus and make visible game buttons unclickable.
        draw_game_ui();
        ImGui::Render();
        return;
    }

    viewport_rect_.x = kLeftPanelWidth;
    viewport_rect_.y = kToolbarHeight;
    viewport_rect_.width = std::max(1.0F, display.x - kLeftPanelWidth - kRightPanelWidth);
    viewport_rect_.height = std::max(1.0F, display.y - kToolbarHeight - kBottomPanelHeight);

    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) save_scene();
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F5)) toggle_play();
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F6)) {
        if (playing_) toggle_play();
    }
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F7)) enter_game_mode(false);

    draw_scene_canvas();
    draw_toolbar();
    draw_hierarchy();
    draw_inspector();
    draw_bottom_panel();
    draw_nexus_panel();
    draw_new_scene_dialog();
    ImGui::Render();
}

void EditorUi::draw_toolbar() {
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, kToolbarHeight), ImGuiCond_Always);
    if (!ImGui::Begin("##LICToolbar", nullptr, kFixedPanelFlags | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55F, 0.74F, 0.92F, 1.0F));
    ImGui::TextUnformatted("LIC");
    ImGui::PopStyleColor();
    ImGui::SameLine(45.0F);
    ImGui::SetCursorPosY(9.0F);

    if (ImGui::Button("New")) request_new_scene();
    ImGui::SameLine();
    if (ImGui::Button("Save  Ctrl+S")) save_scene();
    ImGui::SameLine();
    if (ImGui::Button("Load")) load_scene();
    ImGui::SameLine();
    if (ImGui::Button("+ Add Node")) ImGui::OpenPopup("AddNodeToolbar");
    if (ImGui::BeginPopup("AddNodeToolbar")) {
        if (ImGui::MenuItem("Node3D")) add_node(lic::world::NodeKind::Node3D);
        if (ImGui::MenuItem("MeshInstance3D")) add_node(lic::world::NodeKind::MeshInstance3D);
        if (ImGui::MenuItem("Camera3D")) add_node(lic::world::NodeKind::Camera3D);
        if (ImGui::MenuItem("DirectionalLight3D")) add_node(lic::world::NodeKind::DirectionalLight3D);
        if (ImGui::MenuItem("StaticBody3D")) add_node(lic::world::NodeKind::StaticBody3D);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (playing_) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.42F, 0.19F, 0.18F, 1.0F));
        if (ImGui::Button("Stop  F6")) toggle_play();
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.17F, 0.36F, 0.27F, 1.0F));
        if (ImGui::Button("Play  F5")) toggle_play();
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    if (ImGui::Button("Play Game  F7")) enter_game_mode(false);
    ImGui::SameLine();
    if (ImGui::Button("NEXUS Rules")) {
        load_nexus_script();
        show_nexus_panel_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Camera")) {
        camera_yaw_ = 0.68F;
        camera_pitch_ = 0.38F;
        camera_zoom_ = 1.0F;
    }
    ImGui::SameLine();
    if (scene_dirty_) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92F, 0.67F, 0.34F, 1.0F));
        ImGui::TextUnformatted("* Unsaved");
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("%s", status_message_.c_str());
    }
    ImGui::End();
}

void EditorUi::draw_hierarchy() {
    ImGui::SetNextWindowPos(ImVec2(0.0F, kToolbarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kLeftPanelWidth, viewport_rect_.height), ImGuiCond_Always);
    if (!ImGui::Begin("##SceneTree", nullptr, kFixedPanelFlags | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("SCENE");
    ImGui::SameLine();
    ImGui::TextDisabled("%zu nodes", world_.entity_count());
    ImGui::Separator();

    lic::world::Entity root{};
    world_.each<lic::world::NodeName>([&](lic::world::Entity entity, const lic::world::NodeName& name) {
        if (name.value == "World") root = entity;
    });
    bool duplicateRequested = false;
    bool deleteRequested = false;
    if (root.valid()) {
        const bool expanded = ImGui::TreeNodeEx("World", ImGuiTreeNodeFlags_DefaultOpen |
            ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_FramePadding);
        if (ImGui::IsItemClicked()) selected_entity_ = root;
        if (expanded) {
            world_.each<lic::world::NodeName>([&](lic::world::Entity entity, const lic::world::NodeName& name) {
                if (entity == root || !world_.has<lic::world::NodeKindComponent>(entity)) return;
                const auto kind = world_.get<lic::world::NodeKindComponent>(entity).value;
                const std::string label = std::string(node_icon(kind)) + " " + name.value;
                ImGui::PushID(static_cast<int>(entity.index));
                ImGui::PushID(static_cast<int>(entity.generation));
                if (ImGui::Selectable(label.c_str(), selected_entity_ == entity, ImGuiSelectableFlags_AllowDoubleClick)) {
                    selected_entity_ = entity;
                }
                if (ImGui::BeginPopupContextItem("NodeContext")) {
                    selected_entity_ = entity;
                    const bool rowDuplicate = ImGui::MenuItem("Duplicate");
                    const bool rowDelete = ImGui::MenuItem("Delete");
                    duplicateRequested = duplicateRequested || rowDuplicate;
                    deleteRequested = deleteRequested || rowDelete;
                    ImGui::EndPopup();
                }
                ImGui::PopID();
                ImGui::PopID();
            });
            ImGui::TreePop();
        }
    } else {
        ImGui::TextDisabled("No nodes in this scene");
    }
    if (duplicateRequested) duplicate_selected();
    if (deleteRequested) delete_selected();

    ImGui::Separator();
    if (ImGui::Button("+ Add", ImVec2(68.0F, 0.0F))) ImGui::OpenPopup("AddNodeTree");
    ImGui::SameLine();
    if (ImGui::Button("Duplicate", ImVec2(82.0F, 0.0F))) duplicate_selected();
    ImGui::SameLine();
    if (ImGui::Button("Delete", ImVec2(55.0F, 0.0F))) delete_selected();
    if (ImGui::BeginPopup("AddNodeTree")) {
        if (ImGui::MenuItem("Node3D")) add_node(lic::world::NodeKind::Node3D);
        if (ImGui::MenuItem("MeshInstance3D")) add_node(lic::world::NodeKind::MeshInstance3D);
        if (ImGui::MenuItem("Camera3D")) add_node(lic::world::NodeKind::Camera3D);
        if (ImGui::MenuItem("DirectionalLight3D")) add_node(lic::world::NodeKind::DirectionalLight3D);
        if (ImGui::MenuItem("StaticBody3D")) add_node(lic::world::NodeKind::StaticBody3D);
        ImGui::EndPopup();
    }
    ImGui::End();
}

void EditorUi::draw_inspector() {
    const float displayWidth = ImGui::GetIO().DisplaySize.x;
    ImGui::SetNextWindowPos(ImVec2(displayWidth - kRightPanelWidth, kToolbarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kRightPanelWidth, viewport_rect_.height), ImGuiCond_Always);
    if (!ImGui::Begin("##Inspector", nullptr, kFixedPanelFlags | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("INSPECTOR");
    ImGui::Separator();
    if (!world_.alive(selected_entity_) || !world_.has<lic::world::NodeName>(selected_entity_)) {
        ImGui::TextDisabled("Select a node in the Scene panel.");
        ImGui::End();
        return;
    }

    auto& name = world_.get<lic::world::NodeName>(selected_entity_);
    auto& transform = world_.get<lic::world::Transform>(selected_entity_);
    auto& kind = world_.get<lic::world::NodeKindComponent>(selected_entity_);
    auto& appearance = world_.get<lic::world::Appearance>(selected_entity_);
    auto& enabled = world_.get<lic::world::NodeEnabled>(selected_entity_);

    std::array<char, 128> nameBuffer{};
    const std::size_t nameLength = std::min(name.value.size(), nameBuffer.size() - 1U);
    std::memcpy(nameBuffer.data(), name.value.data(), nameLength);
    if (ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size())) {
        name.value = nameBuffer.data();
        mark_dirty();
    }
    ImGui::TextDisabled("%s  |  ID %u:%u", lic::world::node_kind_name(kind.value).data(),
                        selected_entity_.index, selected_entity_.generation);
    if (ImGui::Checkbox("Enabled", &enabled.value)) mark_dirty();

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::DragFloat3("Position", transform.position.data(), 0.02F, -100.0F, 100.0F, "%.2f")) mark_dirty();
        if (ImGui::DragFloat3("Rotation", transform.rotation_degrees.data(), 0.5F, -360.0F, 360.0F, "%.1f deg")) mark_dirty();
        if (ImGui::DragFloat3("Scale", transform.scale.data(), 0.01F, 0.02F, 100.0F, "%.2f")) mark_dirty();
        if (ImGui::Button("Reset Transform")) {
            transform = lic::world::Transform{};
            mark_dirty();
        }
    }

    if (kind.value == lic::world::NodeKind::MeshInstance3D ||
        kind.value == lic::world::NodeKind::StaticBody3D) {
        if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::ColorEdit4("Color", appearance.color.data(),
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) mark_dirty();
            ImGui::TextDisabled("Primitive: cube");
        }
    } else if (kind.value == lic::world::NodeKind::Camera3D) {
        ImGui::Spacing();
        ImGui::TextDisabled("Camera controls are in development.");
    } else if (kind.value == lic::world::NodeKind::DirectionalLight3D) {
        ImGui::Spacing();
        ImGui::TextDisabled("Light affects the test scene in a future renderer update.");
    }

    ImGui::End();
}

void EditorUi::draw_bottom_panel() {
    ImGui::SetNextWindowPos(ImVec2(kLeftPanelWidth, kToolbarHeight + viewport_rect_.height), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(viewport_rect_.width, kBottomPanelHeight), ImGuiCond_Always);
    if (!ImGui::Begin("##BottomPanel", nullptr, kFixedPanelFlags)) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("LICBottomTabs")) {
        if (ImGui::BeginTabItem("Output")) {
            ImGui::Text("LIC Engine 0.1.0  |  C++23  |  Vulkan");
            ImGui::TextWrapped("Primitive cube preview, scene nodes, transform/material editing and versioned scene files.");
            ImGui::TextDisabled("%s", status_message_.c_str());
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Console")) {
            if (ImGui::SmallButton("Clear")) console_messages_.clear();
            ImGui::SameLine();
            ImGui::TextDisabled("%zu messages", console_messages_.size());
            ImGui::BeginChild("##ConsoleOutput", ImVec2(0.0F, 86.0F), false, ImGuiWindowFlags_HorizontalScrollbar);
            for (const auto& message : console_messages_) ImGui::TextUnformatted(message.c_str());
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 8.0F) ImGui::SetScrollHereY(1.0F);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("FileSystem")) {
            ImGui::TextDisabled("%s", project_root_.string().c_str());
            ImGui::BeginChild("##ProjectFiles", ImVec2(0.0F, 88.0F), false, ImGuiWindowFlags_HorizontalScrollbar);
            const std::array<std::filesystem::path, 3> folders{
                project_root_ / "Scenes", project_root_ / "Assets", project_root_ / "Scripts"};
            for (const auto& folder : folders) {
                ImGui::TextDisabled("%s/", folder.filename().string().c_str());
                std::error_code error;
                std::filesystem::directory_iterator it(folder, error);
                const std::filesystem::directory_iterator end;
                for (; !error && it != end; it.increment(error)) {
                    if (!it->is_regular_file(error)) continue;
                    const auto file = it->path();
                    const std::string label = "  " + file.filename().string();
                    if (ImGui::Selectable(label.c_str(), file == scene_path_) &&
                        file.extension() == ".licscene" && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        scene_path_ = file;
                        load_scene();
                    }
                }
                if (error) ImGui::TextDisabled("  (empty or unavailable)");
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void EditorUi::draw_scene_canvas() {
    ImGui::SetNextWindowPos(ImVec2(viewport_rect_.x, viewport_rect_.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(viewport_rect_.width, viewport_rect_.height), ImGuiCond_Always);
    const auto flags = kFixedPanelFlags | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::Begin("##SceneCanvas", nullptr, flags)) {
        ImGui::End();
        return;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 topLeft = ImGui::GetWindowPos();
    const ImVec2 bottomRight(topLeft.x + ImGui::GetWindowSize().x, topLeft.y + ImGui::GetWindowSize().y);

    if (show_grid_) {
        constexpr float spacing = 32.0F;
        const ImU32 gridColor = IM_COL32(86, 96, 109, 46);
        const ImU32 xAxisColor = IM_COL32(171, 112, 112, 120);
        const ImU32 zAxisColor = IM_COL32(112, 157, 189, 130);
        const float firstX = std::fmod(topLeft.x, spacing);
        for (float x = topLeft.x + firstX; x < bottomRight.x; x += spacing) {
            draw->AddLine(ImVec2(x, topLeft.y), ImVec2(x, bottomRight.y), gridColor);
        }
        const float firstY = std::fmod(topLeft.y, spacing);
        for (float y = topLeft.y + firstY; y < bottomRight.y; y += spacing) {
            draw->AddLine(ImVec2(topLeft.x, y), ImVec2(bottomRight.x, y), gridColor);
        }
        draw->AddLine(ImVec2(topLeft.x, topLeft.y + viewport_rect_.height * 0.52F),
                      ImVec2(bottomRight.x, topLeft.y + viewport_rect_.height * 0.52F), xAxisColor, 1.0F);
        draw->AddLine(ImVec2(topLeft.x + viewport_rect_.width * 0.5F, topLeft.y),
                      ImVec2(topLeft.x + viewport_rect_.width * 0.5F, bottomRight.y), zAxisColor, 1.0F);
    }
    if (!game_mode_) {
        draw->AddText(ImVec2(topLeft.x + 10.0F, topLeft.y + 7.0F), IM_COL32(160, 170, 181, 200), "3D VIEW");
        const char* hint = "MMB ORBIT   WHEEL ZOOM";
        const ImVec2 hintSize = ImGui::CalcTextSize(hint);
        draw->AddText(ImVec2(bottomRight.x - hintSize.x - 10.0F, topLeft.y + 7.0F),
                      IM_COL32(126, 136, 148, 180), hint);
    }

    if (!game_mode_ && ImGui::IsWindowHovered()) {
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            camera_yaw_ -= io.MouseDelta.x * 0.006F;
            camera_pitch_ = std::clamp(camera_pitch_ + io.MouseDelta.y * 0.006F, -1.15F, 1.15F);
        }
        if (io.MouseWheel != 0.0F) {
            camera_zoom_ = std::clamp(camera_zoom_ * (1.0F + io.MouseWheel * 0.08F), 0.3F, 3.5F);
        }
    }
    ImGui::End();
}

void EditorUi::draw_game_ui() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F), ImGuiCond_Always);
    ImGui::SetNextWindowSize(display, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0F);
    const auto flags = kFixedPanelFlags | ImGuiWindowFlags_NoBackground |
                       ImGuiWindowFlags_NoScrollbar;
    if (!ImGui::Begin("##VoidboundOverlay", nullptr, flags)) {
        ImGui::End();
        return;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const auto state = game_.state();
    const ImU32 white = IM_COL32(233, 242, 250, 255);
    const ImU32 muted = IM_COL32(158, 177, 194, 255);
    const ImU32 cyan = IM_COL32(78, 220, 230, 255);

    if (state == lic::game::RunState::Playing || state == lic::game::RunState::Paused ||
        state == lic::game::RunState::Upgrade) {
        draw->AddText(ImVec2(26.0F, 20.0F), cyan, "VOIDBOUND");
        draw->AddText(ImVec2(26.0F, 42.0F), muted, "RIFT SURVIVAL // RUN PROTOCOL");
        draw->AddText(ImVec2(26.0F, 70.0F), white, ("WAVE  " + std::to_string(game_.wave())).c_str());
        draw->AddText(ImVec2(150.0F, 70.0F), muted, ("KILLS  " + std::to_string(game_.kills())).c_str());
        draw->AddText(ImVec2(280.0F, 70.0F), muted, ("SCORE  " + std::to_string(game_.score())).c_str());

        const ImVec2 barPosition(26.0F, 94.0F);
        const float barWidth = std::min(280.0F, std::max(120.0F, display.x * 0.22F));
        draw->AddRectFilled(barPosition, ImVec2(barPosition.x + barWidth, barPosition.y + 12.0F),
                            IM_COL32(36, 48, 61, 230), 3.0F);
        const float healthRatio = game_.max_health() > 0.0F
            ? std::clamp(game_.health() / game_.max_health(), 0.0F, 1.0F) : 0.0F;
        draw->AddRectFilled(barPosition, ImVec2(barPosition.x + barWidth * healthRatio, barPosition.y + 12.0F),
                            healthRatio > 0.30F ? IM_COL32(70, 208, 166, 255) : IM_COL32(238, 83, 92, 255), 3.0F);
        draw->AddText(ImVec2(26.0F, 111.0F), muted,
                      ("HP  " + std::to_string(static_cast<int>(game_.health())) + " / " +
                       std::to_string(static_cast<int>(game_.max_health()))).c_str());
        draw->AddText(ImVec2(26.0F, display.y - 42.0F), muted,
                      "WASD MOVE    SHIFT DASH    MOUSE 1 / SPACE FIRE    ESC PAUSE");
        draw->AddText(ImVec2(display.x - 185.0F, 25.0F), muted,
                      ("LVL " + std::to_string(game_.level()) + "   XP " +
                       std::to_string(game_.experience()) + "/" +
                       std::to_string(game_.experience_to_next())).c_str());
    }

    if (state == lic::game::RunState::Playing) {
        ImGui::SetCursorScreenPos(ImVec2(display.x - 122.0F, 52.0F));
        if (ImGui::Button("PAUSE  [ESC]", ImVec2(96.0F, 28.0F))) game_.pause();
    } else {
        const int tintAlpha = state == lic::game::RunState::Menu ? 216 : 170;
        draw->AddRectFilled(ImVec2(0.0F, 0.0F), display, IM_COL32(5, 9, 15, tintAlpha));
        const float panelWidth = std::min(490.0F, display.x - 32.0F);
        const float panelHeight = state == lic::game::RunState::Upgrade ? 342.0F : 365.0F;
        const ImVec2 panelMin((display.x - panelWidth) * 0.5F, (display.y - panelHeight) * 0.5F);
        const ImVec2 panelMax(panelMin.x + panelWidth, panelMin.y + panelHeight);
        draw->AddRectFilled(panelMin, panelMax, IM_COL32(17, 25, 35, 248), 8.0F);
        draw->AddRect(panelMin, panelMax, IM_COL32(64, 104, 128, 220), 8.0F, 0, 1.0F);
        ImGui::SetCursorScreenPos(ImVec2(panelMin.x + 32.0F, panelMin.y + 25.0F));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.31F, 0.88F, 0.94F, 1.0F));
        ImGui::TextUnformatted("LIC / FIELD OPERATIONS");
        ImGui::PopStyleColor();
        ImGui::SetCursorScreenPos(ImVec2(panelMin.x + 30.0F, panelMin.y + 51.0F));
        if (state == lic::game::RunState::Menu) {
            ImGui::TextUnformatted("VOIDBOUND");
            ImGui::SetCursorScreenPos(ImVec2(panelMin.x + 32.0F, panelMin.y + 83.0F));
            ImGui::TextWrapped("The rift is spreading. Survive the waves, collect memory shards and build a stronger loadout.");
            ImGui::SetCursorScreenPos(ImVec2(panelMin.x + 32.0F, panelMin.y + 145.0F));
            if (ImGui::Button("START RUN", ImVec2(panelWidth - 64.0F, 38.0F))) start_game_run();
            ImGui::SetCursorScreenPos(ImVec2(panelMin.x + 32.0F, panelMin.y + 200.0F));
            ImGui::TextDisabled("WASD  move     SHIFT  dash");
            ImGui::SetCursorScreenPos(ImVec2(panelMin.x + 32.0F, panelMin.y + 222.0F));
            ImGui::TextDisabled("MOUSE 1 or SPACE  fire     ESC  back / pause");
            ImGui::SetCursorScreenPos(ImVec2(panelMin.x + 32.0F, panelMin.y + 282.0F));
            if (!standalone_game_ && ImGui::Button("BACK TO EDITOR", ImVec2(panelWidth - 64.0F, 32.0F))) {
                exit_game_mode();
            }
            if (standalone_game_) {
                ImGui::SetCursorScreenPos(ImVec2(panelMin.x + 32.0F, panelMin.y + 282.0F));
                if (ImGui::Button("QUIT", ImVec2(panelWidth - 64.0F, 32.0F))) {
                    glfwSetWindowShouldClose(window_, GLFW_TRUE);
                }
            }
        } else if (state == lic::game::RunState::Upgrade) {
            ImGui::TextUnformatted("CHOOSE AN UPGRADE");
            ImGui::SetCursorScreenPos(ImVec2(panelMin.x + 32.0F, panelMin.y + 90.0F));
            ImGui::TextWrapped("The rift is quiet for a moment. Choose one module before the next assault.");
            for (std::size_t i = 0; i < game_.upgrade_options().size(); ++i) {
                const auto& option = game_.upgrade_options()[i];
                ImGui::SetCursorScreenPos(ImVec2(panelMin.x + 32.0F, panelMin.y + 145.0F + static_cast<float>(i) * 58.0F));
                const std::string label = std::string(option.name) + "  —  " + std::string(option.description);
                if (ImGui::Button(label.c_str(), ImVec2(panelWidth - 64.0F, 43.0F))) game_.choose_upgrade(i);
            }
        } else if (state == lic::game::RunState::Paused) {
            ImGui::TextUnformatted("PAUSED");
            ImGui::SetCursorScreenPos(ImVec2(panelMin.x + 32.0F, panelMin.y + 104.0F));
            if (ImGui::Button("RESUME", ImVec2(panelWidth - 64.0F, 36.0F))) game_.resume();
            ImGui::SetCursorScreenPos(ImVec2(panelMin.x + 32.0F, panelMin.y + 154.0F));
            if (ImGui::Button("RESTART RUN", ImVec2(panelWidth - 64.0F, 36.0F))) start_game_run();
            ImGui::SetCursorScreenPos(ImVec2(panelMin.x + 32.0F, panelMin.y + 204.0F));
            if (!standalone_game_ && ImGui::Button("BACK TO EDITOR", ImVec2(panelWidth - 64.0F, 36.0F))) exit_game_mode();
            if (standalone_game_) {
                ImGui::SetCursorScreenPos(ImVec2(panelMin.x + 32.0F, panelMin.y + 204.0F));
                if (ImGui::Button("QUIT TO DESKTOP", ImVec2(panelWidth - 64.0F, 36.0F))) glfwSetWindowShouldClose(window_, GLFW_TRUE);
            }
        } else if (state == lic::game::RunState::GameOver) {
            ImGui::TextUnformatted("SIGNAL LOST");
            ImGui::SetCursorScreenPos(ImVec2(panelMin.x + 32.0F, panelMin.y + 95.0F));
            const std::string summary = "Wave " + std::to_string(game_.wave()) + "   |   Score " +
                                       std::to_string(game_.score()) + "   |   Kills " +
                                       std::to_string(game_.kills());
            ImGui::TextWrapped("%s", summary.c_str());
            ImGui::SetCursorScreenPos(ImVec2(panelMin.x + 32.0F, panelMin.y + 145.0F));
            if (ImGui::Button("TRY AGAIN", ImVec2(panelWidth - 64.0F, 36.0F))) start_game_run();
            ImGui::SetCursorScreenPos(ImVec2(panelMin.x + 32.0F, panelMin.y + 195.0F));
            if (ImGui::Button("MAIN MENU", ImVec2(panelWidth - 64.0F, 36.0F))) game_.show_menu();
            ImGui::SetCursorScreenPos(ImVec2(panelMin.x + 32.0F, panelMin.y + 245.0F));
            if (!standalone_game_ && ImGui::Button("BACK TO EDITOR", ImVec2(panelWidth - 64.0F, 32.0F))) exit_game_mode();
            if (standalone_game_) {
                ImGui::SetCursorScreenPos(ImVec2(panelMin.x + 32.0F, panelMin.y + 245.0F));
                if (ImGui::Button("QUIT TO DESKTOP", ImVec2(panelWidth - 64.0F, 32.0F))) glfwSetWindowShouldClose(window_, GLFW_TRUE);
            }
        }
    }
    ImGui::End();
}

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
               parse_setting_value(line, "LICCFG_MOBILITY_DASH_COOLDOWN_MS", settings.mobility_dash_cooldown_ms));
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

void EditorUi::draw_new_scene_dialog() {
    if (!unsaved_new_scene_requested_) return;
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextWrapped("The current scene has unsaved changes. Save before creating a new scene?");
        if (ImGui::Button("Save and New")) {
            save_scene();
            create_default_scene();
            scene_dirty_ = true;
            status_message_ = "New scene";
            unsaved_new_scene_requested_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard")) {
            create_default_scene();
            scene_dirty_ = true;
            status_message_ = "New scene";
            unsaved_new_scene_requested_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            unsaved_new_scene_requested_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorUi::render(VkCommandBuffer command_buffer) {
    if (imgui_vulkan_initialized_ && ImGui::GetDrawData() != nullptr) {
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);
    }
}

} // namespace lic::editor
