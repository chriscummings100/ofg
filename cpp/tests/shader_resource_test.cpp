// Doctest coverage for CPU-side OFG shader resources.
#include "doctest.h"

#include "ofg/resources/shader.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

// Verifies shader resources validate source and explicit parameter layout.
TEST_CASE("shader resource validates source and parameter schema") {
    std::string error;
    ofg::ShaderParameterLayout layout;
    layout.m_parameters.push_back(
        ofg::ShaderParameter{"base_color_factor", ofg::ShaderParameterType::Vec4, ofg::ShaderParameterScope::Material});

    std::optional<ofg::Shader> shader = ofg::Shader::create(
        ofg::GpuContext{}, "opaque", "fn vs_main() {}", layout, std::vector<ofg::PipelineDefinition>{}, error);
    REQUIRE(shader.has_value());
    CHECK(shader->parameter("base_color_factor") != nullptr);
    CHECK(shader->parameter("missing") == nullptr);
    CHECK(shader->parameters().size() == 1);
    CHECK(shader->parameters_for_scope(ofg::ShaderParameterScope::Frame).empty());
    CHECK(shader->label() == "opaque");
    CHECK(shader->source() == "fn vs_main() {}");
    CHECK(shader->module() == nullptr);
    CHECK(shader->revision() == 1);

    REQUIRE(shader->replace_source("fn fs_main() {}", error));
    CHECK(shader->revision() == 2);
    CHECK(shader->replace_source("", error) == false);
    CHECK(error.find("source") != std::string::npos);
}

// Verifies invalid shader schemas fail before renderer code observes them.
TEST_CASE("shader resource rejects duplicate and unnamed parameters") {
    std::string error;
    ofg::ShaderParameterLayout duplicate_layout;
    duplicate_layout.m_parameters.push_back(
        ofg::ShaderParameter{"model", ofg::ShaderParameterType::Mat4, ofg::ShaderParameterScope::Draw});
    duplicate_layout.m_parameters.push_back(
        ofg::ShaderParameter{"model", ofg::ShaderParameterType::Mat4, ofg::ShaderParameterScope::Draw});

    CHECK(ofg::Shader::create(ofg::GpuContext{}, "bad", "source", duplicate_layout, {}, error).has_value() == false);
    CHECK(error.find("more than once") != std::string::npos);

    ofg::ShaderParameterLayout empty_name_layout;
    empty_name_layout.m_parameters.push_back(
        ofg::ShaderParameter{"", ofg::ShaderParameterType::Float, ofg::ShaderParameterScope::Frame});
    CHECK(ofg::Shader::create(ofg::GpuContext{}, "bad", "source", empty_name_layout, {}, error).has_value() == false);
    CHECK(error.find("must not be empty") != std::string::npos);

    CHECK(ofg::Shader::create(ofg::GpuContext{}, "", "source", {}, {}, error).has_value() == false);
    CHECK(error.find("label") != std::string::npos);
    CHECK(ofg::Shader::create(ofg::GpuContext{}, "bad", "", {}, {}, error).has_value() == false);
    CHECK(error.find("source") != std::string::npos);
}

// Verifies shader move assignment keeps ownership single and readable.
TEST_CASE("shader resource supports move assignment") {
    std::string error;
    std::optional<ofg::Shader> destination =
        ofg::Shader::create(ofg::GpuContext{}, "destination", "source a", {}, {}, error);
    std::optional<ofg::Shader> source = ofg::Shader::create(ofg::GpuContext{}, "source", "source b", {}, {}, error);
    REQUIRE(destination.has_value());
    REQUIRE(source.has_value());

    *destination = std::move(*source);
    CHECK(destination->label() == "source");
    CHECK(destination->source() == "source b");
    CHECK(destination->module() == nullptr);
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
