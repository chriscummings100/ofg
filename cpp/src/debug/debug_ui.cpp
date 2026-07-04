// Implements the renderer-owned Dear ImGui debug overlay.
//
// The overlay creates a real ImGui context, feeds raw browser input into ImGuiIO, renders the DebugMenu registry into
// the final target, and exposes diagnostics so tests and the browser shell can observe the integration.
#include "ofg/debug/debug_ui.hpp"

#include "ofg/core/engine_error.hpp"
#include "ofg/debug/debug_menu.hpp"
#include "ofg/gpu/common.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <string>
#include <string_view>

#include <imgui.h>
#include <backends/imgui_impl_wgpu.h>

namespace ofg {

DEBUG_BOOL("debug/ui/show_metrics", g_debug_ui_show_metrics, false);

namespace {

constexpr float k_default_window_x = 12.0F;
constexpr float k_default_window_y = 12.0F;
constexpr float k_default_window_width = 280.0F;
constexpr float k_default_window_height = 180.0F;
constexpr float k_window_background_alpha = 0.72F;
constexpr std::uint32_t k_vertex_capacity_slack = 5000;
constexpr std::uint32_t k_index_capacity_slack = 10000;

[[nodiscard]] float sanitized_delta_seconds(float delta_seconds) noexcept {
    if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0F) {
        return 1.0F / 60.0F;
    }
    return delta_seconds;
}

[[nodiscard]] float sanitized_pixel_ratio(float device_pixel_ratio) noexcept {
    if (!std::isfinite(device_pixel_ratio) || device_pixel_ratio <= 0.0F) {
        return 1.0F;
    }
    return device_pixel_ratio;
}

[[nodiscard]] std::uint32_t count_draw_commands(const ImDrawData& draw_data) noexcept {
    std::uint32_t command_count = 0;
    for (int list_index = 0; list_index < draw_data.CmdListsCount; ++list_index) {
        const ImDrawList* draw_list = draw_data.CmdLists[list_index];
        if (draw_list != nullptr) {
            command_count += static_cast<std::uint32_t>(draw_list->CmdBuffer.Size);
        }
    }
    return command_count;
}

[[nodiscard]] std::uint32_t capacity_with_slack(int used_count, std::uint32_t slack) noexcept {
    if (used_count <= 0) {
        return 0;
    }
    const auto used = static_cast<std::uint32_t>(used_count);
    if (used > std::numeric_limits<std::uint32_t>::max() - slack) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return used + slack;
}

[[nodiscard]] ImGuiKey imgui_key_from_dom_code(std::string_view code) noexcept {
    if (code.size() == 4 && code.substr(0, 3) == "Key" && code[3] >= 'A' && code[3] <= 'Z') {
        return static_cast<ImGuiKey>(static_cast<int>(ImGuiKey_A) + (code[3] - 'A'));
    }
    if (code.size() == 6 && code.substr(0, 5) == "Digit" && code[5] >= '0' && code[5] <= '9') {
        return static_cast<ImGuiKey>(static_cast<int>(ImGuiKey_0) + (code[5] - '0'));
    }
    if (code.size() == 2 && code[0] == 'F' && code[1] >= '1' && code[1] <= '9') {
        return static_cast<ImGuiKey>(static_cast<int>(ImGuiKey_F1) + (code[1] - '1'));
    }
    if (code.size() == 3 && code[0] == 'F' && code[1] == '1' && code[2] >= '0' && code[2] <= '2') {
        return static_cast<ImGuiKey>(static_cast<int>(ImGuiKey_F10) + (code[2] - '0'));
    }
    if (code.size() == 7 && code.substr(0, 6) == "Numpad" && code[6] >= '0' && code[6] <= '9') {
        return static_cast<ImGuiKey>(static_cast<int>(ImGuiKey_Keypad0) + (code[6] - '0'));
    }

    if (code == "Tab") {
        return ImGuiKey_Tab;
    }
    if (code == "ArrowLeft") {
        return ImGuiKey_LeftArrow;
    }
    if (code == "ArrowRight") {
        return ImGuiKey_RightArrow;
    }
    if (code == "ArrowUp") {
        return ImGuiKey_UpArrow;
    }
    if (code == "ArrowDown") {
        return ImGuiKey_DownArrow;
    }
    if (code == "PageUp") {
        return ImGuiKey_PageUp;
    }
    if (code == "PageDown") {
        return ImGuiKey_PageDown;
    }
    if (code == "Home") {
        return ImGuiKey_Home;
    }
    if (code == "End") {
        return ImGuiKey_End;
    }
    if (code == "Insert") {
        return ImGuiKey_Insert;
    }
    if (code == "Delete") {
        return ImGuiKey_Delete;
    }
    if (code == "Backspace") {
        return ImGuiKey_Backspace;
    }
    if (code == "Space") {
        return ImGuiKey_Space;
    }
    if (code == "Enter") {
        return ImGuiKey_Enter;
    }
    if (code == "Escape") {
        return ImGuiKey_Escape;
    }
    if (code == "ShiftLeft") {
        return ImGuiKey_LeftShift;
    }
    if (code == "ShiftRight") {
        return ImGuiKey_RightShift;
    }
    if (code == "ControlLeft") {
        return ImGuiKey_LeftCtrl;
    }
    if (code == "ControlRight") {
        return ImGuiKey_RightCtrl;
    }
    if (code == "AltLeft") {
        return ImGuiKey_LeftAlt;
    }
    if (code == "AltRight") {
        return ImGuiKey_RightAlt;
    }
    if (code == "MetaLeft") {
        return ImGuiKey_LeftSuper;
    }
    if (code == "MetaRight") {
        return ImGuiKey_RightSuper;
    }
    if (code == "ContextMenu") {
        return ImGuiKey_Menu;
    }
    if (code == "Minus") {
        return ImGuiKey_Minus;
    }
    if (code == "Equal") {
        return ImGuiKey_Equal;
    }
    if (code == "BracketLeft") {
        return ImGuiKey_LeftBracket;
    }
    if (code == "BracketRight") {
        return ImGuiKey_RightBracket;
    }
    if (code == "Backslash") {
        return ImGuiKey_Backslash;
    }
    if (code == "Semicolon") {
        return ImGuiKey_Semicolon;
    }
    if (code == "Quote") {
        return ImGuiKey_Apostrophe;
    }
    if (code == "Comma") {
        return ImGuiKey_Comma;
    }
    if (code == "Period") {
        return ImGuiKey_Period;
    }
    if (code == "Slash") {
        return ImGuiKey_Slash;
    }
    if (code == "Backquote") {
        return ImGuiKey_GraveAccent;
    }
    if (code == "CapsLock") {
        return ImGuiKey_CapsLock;
    }
    if (code == "ScrollLock") {
        return ImGuiKey_ScrollLock;
    }
    if (code == "NumLock") {
        return ImGuiKey_NumLock;
    }
    if (code == "PrintScreen") {
        return ImGuiKey_PrintScreen;
    }
    if (code == "Pause") {
        return ImGuiKey_Pause;
    }
    if (code == "NumpadDecimal") {
        return ImGuiKey_KeypadDecimal;
    }
    if (code == "NumpadDivide") {
        return ImGuiKey_KeypadDivide;
    }
    if (code == "NumpadMultiply") {
        return ImGuiKey_KeypadMultiply;
    }
    if (code == "NumpadSubtract") {
        return ImGuiKey_KeypadSubtract;
    }
    if (code == "NumpadAdd") {
        return ImGuiKey_KeypadAdd;
    }
    if (code == "NumpadEnter") {
        return ImGuiKey_KeypadEnter;
    }
    if (code == "NumpadEqual") {
        return ImGuiKey_KeypadEqual;
    }
    return ImGuiKey_None;
}

[[nodiscard]] bool contains_dom_code(const std::vector<std::string>& codes, std::string_view needle) noexcept {
    return std::find(codes.begin(), codes.end(), needle) != codes.end();
}

void add_key_events(ImGuiIO& io, const std::vector<std::string>& codes, bool down) {
    for (const std::string& code : codes) {
        const ImGuiKey key = imgui_key_from_dom_code(code);
        if (key != ImGuiKey_None) {
            io.AddKeyEvent(key, down);
        }
    }
}

void apply_debug_ui_input(ImGuiIO& io, const DebugUiInput& input) {
    io.AddFocusEvent(input.m_has_focus);
    if (input.m_mouse_position_valid && !input.m_pointer_locked) {
        io.AddMousePosEvent(input.m_mouse_x, input.m_mouse_y);
    } else {
        const float no_mouse = -std::numeric_limits<float>::max();
        io.AddMousePosEvent(no_mouse, no_mouse);
    }
    for (std::size_t index = 0; index < input.m_mouse_down.size(); ++index) {
        io.AddMouseButtonEvent(static_cast<int>(index), input.m_mouse_down[index]);
    }
    if (input.m_wheel_x != 0.0F || input.m_wheel_y != 0.0F) {
        io.AddMouseWheelEvent(input.m_wheel_x, input.m_wheel_y);
    }

    add_key_events(io, input.m_key_released_codes, false);
    add_key_events(io, input.m_key_pressed_codes, true);
    add_key_events(io, input.m_key_down_codes, true);
    io.AddKeyEvent(ImGuiMod_Ctrl,
        contains_dom_code(input.m_key_down_codes, "ControlLeft") ||
            contains_dom_code(input.m_key_down_codes, "ControlRight"));
    io.AddKeyEvent(ImGuiMod_Shift,
        contains_dom_code(input.m_key_down_codes, "ShiftLeft") ||
            contains_dom_code(input.m_key_down_codes, "ShiftRight"));
    io.AddKeyEvent(ImGuiMod_Alt,
        contains_dom_code(input.m_key_down_codes, "AltLeft") || contains_dom_code(input.m_key_down_codes, "AltRight"));
    io.AddKeyEvent(ImGuiMod_Super,
        contains_dom_code(input.m_key_down_codes, "MetaLeft") ||
            contains_dom_code(input.m_key_down_codes, "MetaRight"));

    if (!input.m_text_input_utf8.empty()) {
        io.AddInputCharactersUTF8(input.m_text_input_utf8.c_str());
    }
}

template <typename TEntry> void render_debug_entry_value(const TEntry& entry, const char* label);

template <> void render_debug_entry_value<DebugBoolEntry>(const DebugBoolEntry& entry, const char* label) {
    if (entry.m_variable == nullptr) {
        return;
    }

    bool value = entry.m_variable->value();
    if (ImGui::Checkbox(label, &value)) {
        entry.m_variable->set(value);
    }
}

template <> void render_debug_entry_value<DebugIntEntry>(const DebugIntEntry& entry, const char* label) {
    if (entry.m_variable == nullptr) {
        return;
    }

    int value = entry.m_variable->value();
    ImGui::SetNextItemWidth(160.0F);
    if (ImGui::InputInt(label, &value)) {
        entry.m_variable->set(value);
    }
}

template <> void render_debug_entry_value<DebugFloatEntry>(const DebugFloatEntry& entry, const char* label) {
    if (entry.m_variable == nullptr) {
        return;
    }

    float value = entry.m_variable->value();
    ImGui::SetNextItemWidth(160.0F);
    if (ImGui::InputFloat(label, &value)) {
        entry.m_variable->set(value);
    }
}

void render_debug_entry(const DebugMenu& menu, const DebugMenuTreeEntry& entry) {
    ImGui::PushID(entry.m_path.c_str());

    switch (entry.m_type) {
    case DebugScalarType::Bool: {
        const auto entries = menu.bool_entries();
        if (entry.m_entry_index < entries.size()) {
            render_debug_entry_value(entries[entry.m_entry_index], entry.m_label.c_str());
        }
        break;
    }
    case DebugScalarType::Int: {
        const auto entries = menu.int_entries();
        if (entry.m_entry_index < entries.size()) {
            render_debug_entry_value(entries[entry.m_entry_index], entry.m_label.c_str());
        }
        break;
    }
    case DebugScalarType::Float: {
        const auto entries = menu.float_entries();
        if (entry.m_entry_index < entries.size()) {
            render_debug_entry_value(entries[entry.m_entry_index], entry.m_label.c_str());
        }
        break;
    }
    }

    ImGui::PopID();
}

// Finds a cached menu-tree node by its slash-separated path.
const DebugMenuTreeNode* find_debug_node(std::span<const DebugMenuTreeNode> nodes, std::string_view path) noexcept {
    for (const DebugMenuTreeNode& node : nodes) {
        if (std::string_view{node.m_path} == path) {
            return &node;
        }
        const DebugMenuTreeNode* child = find_debug_node(node.m_children, path);
        if (child != nullptr) {
            return child;
        }
    }
    return nullptr;
}

// Returns the parent path used by the drill-down menu back button.
std::string parent_menu_path(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    if (slash == std::string_view::npos) {
        return {};
    }
    return std::string(path.substr(0, slash));
}

// Renders one submenu row and updates the current path when the row is clicked.
void render_debug_submenu_row(const DebugMenuTreeNode& node, std::string& current_path) {
    ImGui::PushID(node.m_path.c_str());
    const std::string label = node.m_label + " >";
    if (ImGui::Selectable(label.c_str(), false)) {
        current_path = node.m_path;
    }
    ImGui::PopID();
}

// Renders the current drill-down menu level instead of recursive foldout nodes.
void render_debug_menu_level(const DebugMenu& menu, const DebugMenuTree& tree, std::string& current_path) {
    const DebugMenuTreeNode* current_node = nullptr;
    if (!current_path.empty()) {
        current_node = find_debug_node(tree.m_nodes, current_path);
        if (current_node == nullptr) {
            current_path.clear();
        }
    }

    if (!current_path.empty()) {
        if (ImGui::Button("< Back")) {
            current_path = parent_menu_path(current_path);
            current_node = current_path.empty() ? nullptr : find_debug_node(tree.m_nodes, current_path);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(current_path.c_str());
        ImGui::Separator();
    }

    const std::span<const DebugMenuTreeNode> nodes = current_node == nullptr
                                                         ? std::span<const DebugMenuTreeNode>(tree.m_nodes)
                                                         : std::span<const DebugMenuTreeNode>(current_node->m_children);
    const std::span<const DebugMenuTreeEntry> entries =
        current_node == nullptr ? std::span<const DebugMenuTreeEntry>(tree.m_entries)
                                : std::span<const DebugMenuTreeEntry>(current_node->m_entries);

    for (const DebugMenuTreeNode& child : nodes) {
        render_debug_submenu_row(child, current_path);
    }
    for (const DebugMenuTreeEntry& entry : entries) {
        render_debug_entry(menu, entry);
    }
}

bool render_debug_menu_window(std::string& current_path) {
    DebugMenu& menu = DebugMenu::instance();
    const bool rebuilt = menu.refresh_tree_if_dirty();

    ImGui::SetNextWindowPos(ImVec2(k_default_window_x, k_default_window_y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(k_default_window_width, k_default_window_height), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(k_window_background_alpha);
    if (ImGui::Begin("OFG Debug", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        const DebugMenuTree& tree = menu.tree();
        render_debug_menu_level(menu, tree, current_path);

        if (g_debug_ui_show_metrics) {
            ImGui::Separator();
            ImGui::Text("tree generation: %llu", static_cast<unsigned long long>(tree.m_generation));
            ImGui::Text("rebuilt this frame: %s", rebuilt ? "yes" : "no");
        }
    }
    ImGui::End();
    return rebuilt;
}

} // namespace

std::unique_ptr<DebugUi> DebugUi::create(const GpuContext& gpu, WGPUTextureFormat target_format) {
    auto debug_ui = std::unique_ptr<DebugUi>(new DebugUi(gpu, target_format));
    debug_ui->initialize();
    return debug_ui;
}

DebugUi::DebugUi(const GpuContext& gpu, WGPUTextureFormat target_format) : m_gpu(gpu), m_target_format(target_format) {}

DebugUi::~DebugUi() {
    if (m_context == nullptr) {
        return;
    }

    ImGui::SetCurrentContext(m_context);
    if (m_backend_initialized) {
        ImGui_ImplWGPU_Shutdown();
        m_backend_initialized = false;
    }
    ImGui::DestroyContext(m_context);
    if (ImGui::GetCurrentContext() == m_context) {
        ImGui::SetCurrentContext(nullptr);
    }
    m_context = nullptr;
}

void DebugUi::initialize() {
    if (m_gpu.m_device == nullptr || m_gpu.m_queue == nullptr) {
        throw EngineError("DebugUi requires a valid WebGPU device and queue");
    }
    if (m_target_format == WGPUTextureFormat_Undefined) {
        throw EngineError("DebugUi requires a concrete render target format");
    }

    IMGUI_CHECKVERSION();
    m_context = ImGui::CreateContext();
    if (m_context == nullptr) {
        throw EngineError("Failed to create ImGui context");
    }

    ImGui::SetCurrentContext(m_context);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    ImGui::StyleColorsDark();

    ImGui_ImplWGPU_InitInfo init_info{};
    init_info.Device = m_gpu.m_device;
    init_info.NumFramesInFlight = 3;
    init_info.RenderTargetFormat = m_target_format;
    init_info.DepthStencilFormat = WGPUTextureFormat_Undefined;

    if (!ImGui_ImplWGPU_Init(&init_info)) {
        ImGui::DestroyContext(m_context);
        m_context = nullptr;
        throw EngineError("Failed to initialize ImGui WebGPU backend");
    }

    m_backend_initialized = true;
    m_status.m_visible = true;
}

void DebugUi::render(WGPUCommandEncoder encoder, const RenderTarget& target, const DebugUiFrameInfo& frame_info) {
    if (encoder == nullptr) {
        throw EngineError("DebugUi render requires a command encoder");
    }
    if (target.m_view == nullptr || target.m_width == 0 || target.m_height == 0) {
        throw EngineError("DebugUi render requires a valid render target");
    }
    if (m_context == nullptr || !m_backend_initialized) {
        throw EngineError("DebugUi render called before initialization");
    }

    if (frame_info.m_input.m_toggle_visibility) {
        m_status.m_visible = !m_status.m_visible;
    }

    reset_frame_status();
    if (!m_status.m_visible) {
        return;
    }

    ImGui::SetCurrentContext(m_context);

    ImGuiIO& io = ImGui::GetIO();
    const float pixel_ratio = sanitized_pixel_ratio(frame_info.m_device_pixel_ratio);
    io.DisplaySize =
        ImVec2(static_cast<float>(target.m_width) / pixel_ratio, static_cast<float>(target.m_height) / pixel_ratio);
    io.DisplayFramebufferScale = ImVec2(pixel_ratio, pixel_ratio);
    io.DeltaTime = sanitized_delta_seconds(frame_info.m_delta_seconds);
    apply_debug_ui_input(io, frame_info.m_input);

    ImGui_ImplWGPU_NewFrame();
    ImGui::NewFrame();
    if (render_debug_menu_window(m_current_menu_path)) {
        ++m_status.m_menu_tree_rebuild_count;
    }
    ImGui::Render();

    update_draw_status();
    update_buffer_status();

    ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data == nullptr || draw_data->TotalVtxCount == 0 || draw_data->TotalIdxCount == 0) {
        return;
    }

    WGPURenderPassColorAttachment color_attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color_attachment.view = target.m_view;
    color_attachment.loadOp = WGPULoadOp_Load;
    color_attachment.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor pass_desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    pass_desc.label = gpu::cstring_view("OFG debug UI pass");
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color_attachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    if (pass == nullptr) {
        throw EngineError("Failed to begin DebugUi render pass");
    }

    ImGui_ImplWGPU_RenderDrawData(draw_data, pass);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    ++m_status.m_overlay_pass_count;
}

void DebugUi::set_visible(bool visible) noexcept {
    m_status.m_visible = visible;
}

bool DebugUi::visible() const noexcept {
    return m_status.m_visible;
}

DebugUiStatus DebugUi::status() const noexcept {
    return m_status;
}

void DebugUi::reset_frame_status() noexcept {
    m_status.m_wants_capture_mouse = false;
    m_status.m_wants_capture_keyboard = false;
    m_status.m_draw_list_count = 0;
    m_status.m_draw_command_count = 0;
    m_status.m_vertex_count = 0;
    m_status.m_index_count = 0;
    m_status.m_uploaded_vertex_bytes = 0;
    m_status.m_uploaded_index_bytes = 0;
}

void DebugUi::update_draw_status() noexcept {
    DebugMenu& menu = DebugMenu::instance();
    const DebugMenuTree& tree = menu.tree();
    m_status.m_menu_tree_generation = tree.m_generation;

    ImGuiIO& io = ImGui::GetIO();
    m_status.m_wants_capture_mouse = io.WantCaptureMouse;
    m_status.m_wants_capture_keyboard = io.WantCaptureKeyboard;

    const ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data == nullptr) {
        return;
    }

    m_status.m_draw_list_count = static_cast<std::uint32_t>(std::max(draw_data->CmdListsCount, 0));
    m_status.m_draw_command_count = count_draw_commands(*draw_data);
    m_status.m_vertex_count = static_cast<std::uint32_t>(std::max(draw_data->TotalVtxCount, 0));
    m_status.m_index_count = static_cast<std::uint32_t>(std::max(draw_data->TotalIdxCount, 0));
    m_status.m_uploaded_vertex_bytes = static_cast<std::uint64_t>(m_status.m_vertex_count) * sizeof(ImDrawVert);
    m_status.m_uploaded_index_bytes = static_cast<std::uint64_t>(m_status.m_index_count) * sizeof(ImDrawIdx);
}

void DebugUi::update_buffer_status() noexcept {
    const ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data == nullptr) {
        return;
    }

    if (draw_data->TotalVtxCount > static_cast<int>(m_status.m_vertex_buffer_capacity)) {
        m_status.m_vertex_buffer_capacity = capacity_with_slack(draw_data->TotalVtxCount, k_vertex_capacity_slack);
        ++m_status.m_vertex_buffer_resize_count;
    }
    if (draw_data->TotalIdxCount > static_cast<int>(m_status.m_index_buffer_capacity)) {
        m_status.m_index_buffer_capacity = capacity_with_slack(draw_data->TotalIdxCount, k_index_capacity_slack);
        ++m_status.m_index_buffer_resize_count;
    }
    if (!m_font_texture_seen && draw_data->TotalVtxCount > 0) {
        m_font_texture_seen = true;
        ++m_status.m_font_texture_create_count;
    }
}

DebugUiStatus default_debug_ui_status() noexcept {
    return {};
}

void clear_debug_ui_input_transients(DebugUiInput& input) noexcept {
    input.m_wheel_x = 0.0F;
    input.m_wheel_y = 0.0F;
    input.m_toggle_visibility = false;
    input.m_key_pressed_codes.clear();
    input.m_key_released_codes.clear();
    input.m_text_input_utf8.clear();
}

} // namespace ofg
