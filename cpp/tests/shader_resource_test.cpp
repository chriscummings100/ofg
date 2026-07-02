// Doctest coverage for CPU-side OFG shader resources.
#include "doctest.h"

#include "ofg/core/engine_error.hpp"
#include "ofg/resources/shader.hpp"

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// Verifies shader resources validate source and explicit parameter layout.
TEST_CASE("shader resource validates source and parameter schema") {
    ofg::ShaderParameterLayout layout;
    layout.m_parameters.push_back(
        ofg::ShaderParameter{"base_color_factor", ofg::ShaderParameterType::Vec4, ofg::ShaderParameterScope::Material});

    ofg::Shader shader{ofg::GpuContext{}, "opaque"};
    shader.init_from_wgsl("fn vs_main() {}", layout, std::vector<ofg::PipelineDefinition>{});
    CHECK(shader.parameter("base_color_factor") != nullptr);
    CHECK(shader.parameter("missing") == nullptr);
    CHECK(shader.parameters().size() == 1);
    CHECK(shader.parameters_for_scope(ofg::ShaderParameterScope::Frame).empty());
    CHECK(shader.label() == "opaque");
    CHECK(shader.source() == "fn vs_main() {}");
    CHECK(shader.module() == nullptr);
    CHECK(shader.revision() == 1);

    shader.replace_source("fn fs_main() {}");
    CHECK(shader.revision() == 2);
    try {
        shader.replace_source("");
        FAIL("Expected empty shader replacement source to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("source") != std::string::npos);
    }
}

// Verifies invalid shader schemas fail before renderer code observes them.
TEST_CASE("shader resource rejects duplicate and unnamed parameters") {
    ofg::ShaderParameterLayout duplicate_layout;
    duplicate_layout.m_parameters.push_back(
        ofg::ShaderParameter{"model", ofg::ShaderParameterType::Mat4, ofg::ShaderParameterScope::Draw});
    duplicate_layout.m_parameters.push_back(
        ofg::ShaderParameter{"model", ofg::ShaderParameterType::Mat4, ofg::ShaderParameterScope::Draw});

    ofg::Shader duplicate_shader{ofg::GpuContext{}, "bad"};
    try {
        duplicate_shader.init_from_wgsl("source", duplicate_layout, {});
        FAIL("Expected duplicate shader parameters to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("more than once") != std::string::npos);
    }

    ofg::ShaderParameterLayout empty_name_layout;
    empty_name_layout.m_parameters.push_back(
        ofg::ShaderParameter{"", ofg::ShaderParameterType::Float, ofg::ShaderParameterScope::Frame});
    try {
        ofg::Shader empty_name_shader{ofg::GpuContext{}, "bad"};
        empty_name_shader.init_from_wgsl("source", empty_name_layout, {});
        FAIL("Expected empty shader parameter name to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("must not be empty") != std::string::npos);
    }

    try {
        ofg::Shader shader{ofg::GpuContext{}, ""};
        (void)shader;
        FAIL("Expected empty shader label to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("label") != std::string::npos);
    }

    try {
        ofg::Shader shader{ofg::GpuContext{}, "bad"};
        shader.init_from_wgsl("", {}, {});
        FAIL("Expected empty shader source to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("source") != std::string::npos);
    }
}

// Verifies shader resources are address-stable Object-derived values.
TEST_CASE("shader resource is not movable") {
    CHECK_FALSE(std::is_move_constructible_v<ofg::Shader>);
    CHECK_FALSE(std::is_move_assignable_v<ofg::Shader>);
}

// Verifies readable enum names cover the public shader vocabulary.
TEST_CASE("shader parameter names cover every enum value") {
    CHECK(std::string(ofg::shader_parameter_type_name(ofg::ShaderParameterType::Float)) == "float");
    CHECK(std::string(ofg::shader_parameter_type_name(ofg::ShaderParameterType::Vec2)) == "vec2");
    CHECK(std::string(ofg::shader_parameter_type_name(ofg::ShaderParameterType::Vec3)) == "vec3");
    CHECK(std::string(ofg::shader_parameter_type_name(ofg::ShaderParameterType::Vec4)) == "vec4");
    CHECK(std::string(ofg::shader_parameter_type_name(ofg::ShaderParameterType::Mat4)) == "mat4");
    CHECK(std::string(ofg::shader_parameter_type_name(ofg::ShaderParameterType::Texture)) == "texture");

    CHECK(std::string(ofg::shader_parameter_scope_name(ofg::ShaderParameterScope::Frame)) == "frame");
    CHECK(std::string(ofg::shader_parameter_scope_name(ofg::ShaderParameterScope::Draw)) == "draw");
    CHECK(std::string(ofg::shader_parameter_scope_name(ofg::ShaderParameterScope::Material)) == "material");
}
