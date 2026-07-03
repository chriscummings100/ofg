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
        .function("blob_loads_json", &ofg::BrowserGame::blob_loads_json)
        .function("mark_blob_loading", &ofg::BrowserGame::mark_blob_loading)
        .function("complete_blob_load", &ofg::BrowserGame::complete_blob_load)
        .function("fail_blob_load", &ofg::BrowserGame::fail_blob_load)
        .function("debug_status_json", &ofg::BrowserGame::debug_status_json)
        .function("dispose", &ofg::BrowserGame::dispose);
}
