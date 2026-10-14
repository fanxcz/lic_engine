#include <lic/editor/editor_ui.hpp>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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

EditorUi::EditorUi()
    : project_root_(default_project_root()), scene_path_(project_root_ / "Scenes" / "Main.licscene") {
    create_default_scene();
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

    draw_scene_canvas();
    draw_toolbar();
    draw_hierarchy();
    draw_inspector();
    draw_bottom_panel();
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
            const std::array<std::filesystem::path, 2> folders{project_root_ / "Scenes", project_root_ / "Assets"};
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
    draw->AddText(ImVec2(topLeft.x + 10.0F, topLeft.y + 7.0F), IM_COL32(160, 170, 181, 200), "3D VIEW");
    const char* hint = "MMB ORBIT   WHEEL ZOOM";
    const ImVec2 hintSize = ImGui::CalcTextSize(hint);
    draw->AddText(ImVec2(bottomRight.x - hintSize.x - 10.0F, topLeft.y + 7.0F),
                  IM_COL32(126, 136, 148, 180), hint);

    if (ImGui::IsWindowHovered()) {
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
