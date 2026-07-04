// Raw browser/debug input snapshot for the renderer-owned Dear ImGui overlay.
//
// Gameplay control input remains a separate, compact movement/camera snapshot. This type preserves the browser-style
// data Dear ImGui expects: canvas CSS-pixel mouse position, button state, wheel deltas, DOM KeyboardEvent.code values,
// text input, focus, pointer-lock state, and per-frame visibility-toggle edges.
#pragma once

#include <array>
#include <string>
#include <vector>

namespace ofg {

struct DebugUiInput {
    static constexpr std::size_t k_mouse_button_count = 5;

    bool m_has_focus{true};
    bool m_pointer_locked{false};
    bool m_mouse_position_valid{false};
    float m_mouse_x{0.0F};
    float m_mouse_y{0.0F};
    std::array<bool, k_mouse_button_count> m_mouse_down{};
    float m_wheel_x{0.0F};
    float m_wheel_y{0.0F};
    bool m_toggle_visibility{false};
    std::vector<std::string> m_key_down_codes;
    std::vector<std::string> m_key_pressed_codes;
    std::vector<std::string> m_key_released_codes;
    std::string m_text_input_utf8;
};

// Clears one-frame values after the renderer consumes a snapshot.
void clear_debug_ui_input_transients(DebugUiInput& input) noexcept;

} // namespace ofg
