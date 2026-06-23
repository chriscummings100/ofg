// Doctest coverage for CPU-side OFG material resources.
#include "doctest.h"

#include "ofg/math/vec.hpp"
#include "ofg/resources/material.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/resources/shader.hpp"

#include <optional>
#include <string>
#include <utility>

namespace {

// Builds a material shader with one base color parameter.
ofg::Shader make_material_shader() {
    ofg::ShaderParameterLayout layout;
    layout.m_parameters.push_back(
        ofg::ShaderParameter{"base_color_factor", ofg::ShaderParameterType::Vec4, ofg::ShaderParameterScope::Material});
    std::string error;
    std::optional<ofg::Shader> shader = ofg::Shader::create(ofg::GpuContext{}, "shader", "source", layout, {}, error);
    REQUIRE(shader.has_value());
    return std::move(*shader);
}

} // namespace

// Verifies material creation validates shader material properties.
TEST_CASE("material resource validates properties against shader") {
    ofg::Shader shader = make_material_shader();
    ofg::PropertyBag properties;
    properties.set("base_color_factor", ofg::math::vec4(1.0F, 0.0F, 0.0F, 1.0F));

    std::string error;
    std::optional<ofg::Material> material = ofg::Material::create(ofg::GpuContext{}, "red", shader, properties, error);
    REQUIRE(material.has_value());
    CHECK(&material->shader() == &shader);
    CHECK(material->label() == "red");
    CHECK(material->properties().get("base_color_factor") != nullptr);
    CHECK(material->revision() == 1);
    CHECK(material->bind_group() == nullptr);

    REQUIRE(material->set_property("base_color_factor", ofg::math::vec4(0.0F, 1.0F, 0.0F, 1.0F), error));
    CHECK(material->revision() == 2);
    CHECK(material->set_property("base_color_factor", 1.0F, error) == false);
    CHECK(error.find("expected type") != std::string::npos);
}

// Verifies material creation rejects incomplete property bags.
TEST_CASE("material resource rejects missing required properties") {
    ofg::Shader shader = make_material_shader();
    ofg::PropertyBag properties;
    std::string error;

    CHECK(ofg::Material::create(ofg::GpuContext{}, "bad", shader, properties, error).has_value() == false);
    CHECK(error.find("Missing required") != std::string::npos);

    properties.set("base_color_factor", ofg::math::vec4(1.0F, 1.0F, 1.0F, 1.0F));
    CHECK(ofg::Material::create(ofg::GpuContext{}, "", shader, properties, error).has_value() == false);
    CHECK(error.find("label") != std::string::npos);
}

// Verifies material move assignment and moved-from validation behavior.
TEST_CASE("material resource supports move assignment") {
    ofg::Shader shader = make_material_shader();
    ofg::PropertyBag red_properties;
    red_properties.set("base_color_factor", ofg::math::vec4(1.0F, 0.0F, 0.0F, 1.0F));
    ofg::PropertyBag blue_properties;
    blue_properties.set("base_color_factor", ofg::math::vec4(0.0F, 0.0F, 1.0F, 1.0F));

    std::string error;
    std::optional<ofg::Material> destination =
        ofg::Material::create(ofg::GpuContext{}, "red", shader, red_properties, error);
    std::optional<ofg::Material> source =
        ofg::Material::create(ofg::GpuContext{}, "blue", shader, blue_properties, error);
    REQUIRE(destination.has_value());
    REQUIRE(source.has_value());

    *destination = std::move(*source);
    CHECK(destination->label() == "blue");
    CHECK(&destination->shader() == &shader);
    CHECK(source->set_property("base_color_factor", ofg::math::vec4(1.0F, 1.0F, 1.0F, 1.0F), error) == false);
    CHECK(error.find("shader reference") != std::string::npos);
}
