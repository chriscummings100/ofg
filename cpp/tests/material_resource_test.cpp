// Doctest coverage for CPU-side OFG material resources.
#include "doctest.h"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/resources/shader.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace {

// Builds a material shader with one base color parameter.
std::unique_ptr<ofg::Shader> make_material_shader() {
    ofg::ShaderParameterLayout layout;
    layout.m_parameters.push_back(
        ofg::ShaderParameter{"base_color_factor", ofg::ShaderParameterType::Vec4, ofg::ShaderParameterScope::Material});
    auto shader = std::make_unique<ofg::Shader>(ofg::GpuContext{}, "shader");
    shader->init_from_wgsl("source", layout, {});
    return shader;
}

} // namespace

// Verifies material creation validates shader material properties.
TEST_CASE("material resource validates properties against shader") {
    std::unique_ptr<ofg::Shader> shader = make_material_shader();
    ofg::PropertyBag properties;
    properties.set("base_color_factor", ofg::math::vec4(1.0F, 0.0F, 0.0F, 1.0F));

    ofg::Material material{ofg::GpuContext{}, "red"};
    material.init(*shader, properties);
    CHECK(&material.shader() == shader.get());
    CHECK(material.label() == "red");
    CHECK(material.properties().get("base_color_factor") != nullptr);
    CHECK(material.revision() == 1);
    CHECK(material.bind_group() == nullptr);

    material.set_property("base_color_factor", ofg::math::vec4(0.0F, 1.0F, 0.0F, 1.0F));
    CHECK(material.revision() == 2);
    CHECK(material.uniform_buffer() == nullptr);
    try {
        material.set_property("base_color_factor", 1.0F);
        FAIL("Expected invalid material property type to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("expected type") != std::string::npos);
    }
}

// Verifies material creation rejects incomplete property bags.
TEST_CASE("material resource rejects missing required properties") {
    std::unique_ptr<ofg::Shader> shader = make_material_shader();
    ofg::PropertyBag properties;

    try {
        ofg::Material material{ofg::GpuContext{}, "bad"};
        material.init(*shader, properties);
        FAIL("Expected missing required material property to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("Missing required") != std::string::npos);
    }

    properties.set("base_color_factor", ofg::math::vec4(1.0F, 1.0F, 1.0F, 1.0F));
    try {
        ofg::Material material{ofg::GpuContext{}, ""};
        material.init(*shader, properties);
        FAIL("Expected empty material label to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("label") != std::string::npos);
    }
}

// Verifies property mutation before initialization reports the missing shader binding.
TEST_CASE("material resource rejects property mutation before initialization") {
    ofg::Material material{ofg::GpuContext{}, "uninitialized"};

    CHECK_THROWS_WITH_AS(material.set_property("base_color_factor", ofg::math::vec4(1.0F, 1.0F, 1.0F, 1.0F)),
        doctest::Contains("shader reference"),
        ofg::EngineError);
    CHECK(material.uniform_buffer() == nullptr);
    CHECK(material.revision() == 0);
}

// Verifies incomplete GPU contexts are rejected before mutating material state.
TEST_CASE("material resource rejects mutation with incomplete gpu context") {
    std::unique_ptr<ofg::Shader> shader = make_material_shader();
    ofg::PropertyBag properties;
    properties.set("base_color_factor", ofg::math::vec4(1.0F, 1.0F, 1.0F, 1.0F));

    ofg::GpuContext incomplete_gpu;
    incomplete_gpu.m_device = reinterpret_cast<WGPUDevice>(static_cast<std::uintptr_t>(1));
    ofg::Material material{incomplete_gpu, "partial gpu"};

    CHECK_THROWS_WITH_AS(
        material.init(*shader, properties), doctest::Contains("WebGPU device and queue"), ofg::EngineError);
    CHECK(material.revision() == 0);
    CHECK_THROWS_WITH_AS(material.set_property("base_color_factor", ofg::math::vec4(0.0F, 1.0F, 0.0F, 1.0F)),
        doctest::Contains("WebGPU device and queue"),
        ofg::EngineError);
    CHECK(material.uniform_buffer() == nullptr);
    CHECK(material.revision() == 0);
}

// Verifies material resources are address-stable Object-derived values.
TEST_CASE("material resource is not movable") {
    CHECK_FALSE(std::is_move_constructible_v<ofg::Material>);
    CHECK_FALSE(std::is_move_assignable_v<ofg::Material>);
}
