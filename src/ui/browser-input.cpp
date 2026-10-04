// Browser event adaptation without GLFW or a second renderer backend.
#include "ui/browser-input.h"
#include <emscripten.h>
#include <cstring>
#include <cstdio>

namespace ofg {
namespace {
// Maps physical DOM key codes to ImGui navigation, editing and laboratory keys.
ImGuiKey keyFor(const char* code)
{
    if (std::strlen(code) == 4 && std::strncmp(code, "Key", 3) == 0 && code[3] >= 'A' && code[3] <= 'Z')
    {
        return ImGuiKey(ImGuiKey_A + code[3] - 'A');
    }
    if (std::strlen(code) == 6 && std::strncmp(code, "Digit", 5) == 0 && code[5] >= '0' && code[5] <= '9')
    {
        return ImGuiKey(ImGuiKey_0 + code[5] - '0');
    }
    struct Mapping
    {
        const char* code;
        ImGuiKey key;
    };
    const Mapping keys[]{
        {"Tab", ImGuiKey_Tab},
        {"ArrowLeft", ImGuiKey_LeftArrow},
        {"ArrowRight", ImGuiKey_RightArrow},
        {"ArrowUp", ImGuiKey_UpArrow},
        {"ArrowDown", ImGuiKey_DownArrow},
        {"PageUp", ImGuiKey_PageUp},
        {"PageDown", ImGuiKey_PageDown},
        {"Home", ImGuiKey_Home},
        {"End", ImGuiKey_End},
        {"Insert", ImGuiKey_Insert},
        {"Delete", ImGuiKey_Delete},
        {"Backspace", ImGuiKey_Backspace},
        {"Space", ImGuiKey_Space},
        {"Enter", ImGuiKey_Enter},
        {"Escape", ImGuiKey_Escape},
        {"ShiftLeft", ImGuiKey_LeftShift},
        {"ShiftRight", ImGuiKey_RightShift},
        {"ControlLeft", ImGuiKey_LeftCtrl},
        {"ControlRight", ImGuiKey_RightCtrl},
        {"AltLeft", ImGuiKey_LeftAlt},
        {"AltRight", ImGuiKey_RightAlt},
        {"MetaLeft", ImGuiKey_LeftSuper},
        {"MetaRight", ImGuiKey_RightSuper},
        {"Minus", ImGuiKey_Minus},
        {"Equal", ImGuiKey_Equal},
        {"Period", ImGuiKey_Period},
        {"Comma", ImGuiKey_Comma},
        {"Slash", ImGuiKey_Slash},
        {"Semicolon", ImGuiKey_Semicolon},
        {"Quote", ImGuiKey_Apostrophe},
        {"BracketLeft", ImGuiKey_LeftBracket},
        {"BracketRight", ImGuiKey_RightBracket},
        {"Backslash", ImGuiKey_Backslash}
    };
    for (const auto& mapping : keys)
    {
        if (std::strcmp(code, mapping.code) == 0)
        {
            return mapping.key;
        }
    }
    return ImGuiKey_None;
}
} // namespace

BrowserInput::BrowserInput(Workspace& workspace)
    : m_workspace(workspace)
{
    auto& io = ImGui::GetIO();
    io.BackendPlatformName = "ofg-emscripten";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    emscripten_set_mousedown_callback("#canvas", this, false, mouse);
    emscripten_set_mousemove_callback("#canvas", this, false, mouse);
    emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, this, false, mouse);
    emscripten_set_wheel_callback("#canvas", this, false, wheel);
    emscripten_set_keydown_callback("#canvas", this, false, keyboard);
    emscripten_set_keyup_callback("#canvas", this, false, keyboard);
    emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, blur);
    emscripten_set_pointerlockchange_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, this, false, pointerLock);
}

BrowserInput::~BrowserInput()
{
    release();
    emscripten_set_mousedown_callback("#canvas", nullptr, false, nullptr);
    emscripten_set_mousemove_callback("#canvas", nullptr, false, nullptr);
    emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, false, nullptr);
    emscripten_set_wheel_callback("#canvas", nullptr, false, nullptr);
    emscripten_set_keydown_callback("#canvas", nullptr, false, nullptr);
    emscripten_set_keyup_callback("#canvas", nullptr, false, nullptr);
    emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, nullptr);
    emscripten_set_pointerlockchange_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, false, nullptr);
    ImGui::GetIO().BackendPlatformName = nullptr;
    ImGui::GetIO().BackendFlags &= ~ImGuiBackendFlags_HasMouseCursors;
}

void BrowserInput::release()
{
    const bool wasRequested = m_rightHeld || m_captured;
    m_rightHeld = m_captured = false;
    m_look = {};
    if (wasRequested)
    {
        emscripten_exit_pointerlock();
    }
}

EM_BOOL BrowserInput::mouse(int type, const EmscriptenMouseEvent* event, void* context)
{
    auto& self = *static_cast<BrowserInput*>(context);
    auto& io = ImGui::GetIO();
    if (type == EMSCRIPTEN_EVENT_MOUSEMOVE)
    {
        if (self.m_captured)
        {
            self.m_look.x += event->movementX;
            self.m_look.y += event->movementY;
        }
        else
        {
            io.AddMousePosEvent(float(event->targetX), float(event->targetY));
        }
        return true;
    }
    const bool down = type == EMSCRIPTEN_EVENT_MOUSEDOWN;
    const int button = event->button == 2 ? 1 : event->button == 1 ? 2 : event->button;
    if (button >= 0 && button < 5)
    {
        io.AddMouseButtonEvent(button, down);
    }
    if (down)
    {
        EM_ASM({ Module.canvas.focus(); });
        io.AddFocusEvent(true);
        io.AddMousePosEvent(float(event->targetX), float(event->targetY));
    }
    if (event->button == 2)
    {
        if (down && self.m_workspace.canCaptureAt(float(event->targetX), float(event->targetY)))
        {
            self.m_rightHeld = true;
            const auto result = emscripten_request_pointerlock("#canvas", false);
            if (result != EMSCRIPTEN_RESULT_SUCCESS)
            {
                self.m_rightHeld = false;
                std::fprintf(stderr, "Scene pointer lock request rejected (%d).\n", result);
            }
        }
        else if (!down)
        {
            self.release();
        }
    }
    return true;
}

EM_BOOL BrowserInput::wheel(int, const EmscriptenWheelEvent* event, void*)
{
    const float scale = event->deltaMode == DOM_DELTA_PIXEL ? 0.01f : event->deltaMode == DOM_DELTA_LINE ? 1.f : 10.f;
    ImGui::GetIO().AddMouseWheelEvent(float(-event->deltaX) * scale, float(-event->deltaY) * scale);
    return true;
}

EM_BOOL BrowserInput::keyboard(int type, const EmscriptenKeyboardEvent* event, void* context)
{
    auto& self = *static_cast<BrowserInput*>(context);
    auto& io = ImGui::GetIO();
    const bool down = type == EMSCRIPTEN_EVENT_KEYDOWN;
    io.AddKeyEvent(ImGuiMod_Ctrl, event->ctrlKey);
    io.AddKeyEvent(ImGuiMod_Shift, event->shiftKey);
    io.AddKeyEvent(ImGuiMod_Alt, event->altKey);
    io.AddKeyEvent(ImGuiMod_Super, event->metaKey);
    const auto key = keyFor(event->code);
    if (key != ImGuiKey_None)
    {
        io.AddKeyEvent(key, down);
    }
    if (down && key == ImGuiKey_Escape)
    {
        self.release();
    }
    if (down && !event->ctrlKey && !event->metaKey && !event->altKey && !self.m_captured)
    {
        // One Unicode scalar is at most four UTF-8 bytes; DOM named keys contain multiple ASCII characters.
        const auto length = std::strlen(event->key);
        if (length == 1 || (length <= 4 && (static_cast<unsigned char>(event->key[0]) & 0x80)))
        {
            io.AddInputCharactersUTF8(event->key);
        }
    }
    return key != ImGuiKey_None && !event->metaKey && !(event->ctrlKey && key == ImGuiKey_R);
}

EM_BOOL BrowserInput::blur(int, const EmscriptenFocusEvent*, void* context)
{
    static_cast<BrowserInput*>(context)->release();
    ImGui::GetIO().AddFocusEvent(false);
    return false;
}

EM_BOOL BrowserInput::pointerLock(int, const EmscriptenPointerlockChangeEvent* event, void* context)
{
    auto& self = *static_cast<BrowserInput*>(context);
    self.m_captured = event->isActive && self.m_rightHeld;
    self.m_look = {};
    if (event->isActive && !self.m_rightHeld)
    {
        emscripten_exit_pointerlock();
    }
    if (!event->isActive)
    {
        self.m_rightHeld = false;
    }
    return false;
}

FlyCameraInput BrowserInput::cameraInput(Scene& scene)
{
    if (!m_workspace.sceneTarget())
    {
        release();
    }
    FlyCameraInput input;
    if (m_captured)
    {
        input.movement = {
            float(ImGui::IsKeyDown(ImGuiKey_D)) - float(ImGui::IsKeyDown(ImGuiKey_A)),
            float(ImGui::IsKeyDown(ImGuiKey_E)) - float(ImGui::IsKeyDown(ImGuiKey_Q)),
            float(ImGui::IsKeyDown(ImGuiKey_W)) - float(ImGui::IsKeyDown(ImGuiKey_S))
        };
        input.lookPixels = m_look;
        input.fast = ImGui::GetIO().KeyShift;
    }
    m_look = {};
    if (m_captured || m_workspace.cameraKeyboardAllowed())
    {
        input.reset = ImGui::IsKeyPressed(ImGuiKey_R, false);
        input.closeup = ImGui::IsKeyPressed(ImGuiKey_F, false);
        for (int i = 0; i <= 4; ++i)
        {
            if (ImGui::IsKeyPressed(ImGuiKey(ImGuiKey_0 + i), false))
            {
                scene.lighting.debugView = i;
            }
        }
    }
    const int cursor = int(ImGui::GetMouseCursor());
    EM_ASM({ Module.setUiCursor($0); }, cursor);
    return input;
}
} // namespace ofg
