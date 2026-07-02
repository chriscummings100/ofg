// Embind registration for the narrow TypeScript-to-C++ browser runtime facade.
#include "ofg/web/browser_game.hpp"

#include <emscripten/bind.h>
#include <memory>

// Registers the narrow lifecycle facade consumed by wasmRuntime.ts.
EMSCRIPTEN_BINDINGS(ofg_cpp_module) {
    emscripten::class_<ofg::BrowserGame>("BrowserGame")
        .smart_ptr<std::shared_ptr<ofg::BrowserGame>>("BrowserGame")
        .class_function("create", &ofg::BrowserGame::create)
        .function("resize", &ofg::BrowserGame::resize)
        .function("frame", &ofg::BrowserGame::frame)
        .function("set_control_input", &ofg::BrowserGame::set_control_input)
        .function("load_player_model", &ofg::BrowserGame::load_player_model)
        .function("report_player_model_load_error", &ofg::BrowserGame::report_player_model_load_error)
        .function("debug_status_json", &ofg::BrowserGame::debug_status_json)
        .function("dispose", &ofg::BrowserGame::dispose);
}
