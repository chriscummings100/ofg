// Doctest coverage for CPU-side OFG material resources.
#include "doctest.h"

#include "ofg/core/engine_error.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/resources/shader.hpp"

#include <string>
#include <utility>

namespace {

// Builds a material shader with one base color parameter.
ofg::Shader make_material_shader() {
    ofg::ShaderParameterLayout layout;
    layout.m_parameters.push_back(
        ofg::ShaderParameter{"base_color_factor", ofg::ShaderParameterType::Vec4, ofg::ShaderParameterScope::Material});
    ofg::Shader shader{ofg::GpuContext{}, "shader"};
    shader.init_from_wgsl("source", layout, {});
    return shader;
}

} // namespace

// Verifies material creation validates shader material properties.
TEST_CASE("material resource validates properties against shader") {
    ofg::Shader shader = make_material_shader();
    ofg::PropertyBag properties;
    properties.set("base_color_factor", ofg::math::vec4(1.0F, 0.0F, 0.0F, 1.0F));

    ofg::Material material{ofg::GpuContext{}, "red"};
    material.init(shader, properties);
    CHECK(&material.shader() == &shader);
    CHECK(material.label() == "red");
    CHECK(material.properties().get("base_color_factor") != nullptr);
    CHECK(material.revision() == 1);
    CHECK(material.bind_group() == nullptr);

    material.set_property("base_color_factor", ofg::math::vec4(0.0F, 1.0F, 0.0F, 1.0F));
    CHECK(material.revision() == 2);
    try {
        material.set_property("base_color_factor", 1.0F);
        FAIL("Expected invalid material property type to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("expected type") != std::string::npos);
    }
}

// Verifies material creation rejects incomplete property bags.
TEST_CASE("material resource rejects missing required properties") {
    ofg::Shader shader = make_material_shader();
    ofg::PropertyBag properties;

    try {
        ofg::Material material{ofg::GpuContext{}, "bad"};
        material.init(shader, properties);
        FAIL("Expected missing required material property to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("Missing required") != std::string::npos);
    }

    properties.set("base_color_factor", ofg::math::vec4(1.0F, 1.0F, 1.0F, 1.0F));
    try {
        ofg::Material material{ofg::GpuContext{}, ""};
        material.init(shader, properties);
        FAIL("Expected empty material label to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("label") != std::string::npos);
    }
}

// Verifies material move assignment and moved-from validation behavior.
TEST_CASE("material resource supports move assignment") {
    ofg::Shader shader = make_material_shader();
    ofg::PropertyBag red_properties;
    red_properties.set("base_color_factor", ofg::math::vec4(1.0F, 0.0F, 0.0F, 1.0F));
    ofg::PropertyBag blue_properties;
    blue_properties.set("base_color_factor", ofg::math::vec4(0.0F, 0.0F, 1.0F, 1.0F));

    ofg::Material destination{ofg::GpuContext{}, "red"};
    destination.init(shader, red_properties);
    ofg::Material source{ofg::GpuContext{}, "blue"};
    source.init(shader, blue_properties);

    destination = std::move(source);
    CHECK(destination.label() == "blue");
    CHECK(&destination.shader() == &shader);
    try {
        source.set_property("base_color_factor", ofg::math::vec4(1.0F, 1.0F, 1.0F, 1.0F));
        FAIL("Expected moved-from material mutation to throw.");
    } catch (const ofg::EngineError& error) {
        CHECK(std::string(error.what()).find("shader reference") != std::string::npos);
    }
}
