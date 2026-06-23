// Doctest coverage for OFG shader property bags.
#include "doctest.h"

#include "ofg/math/mat.hpp"
#include "ofg/math/transform.hpp"
#include "ofg/math/vec.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/resources/shader.hpp"

#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace {

// Builds a shader with material and draw parameters for property tests.
ofg::Shader make_property_shader() {
    ofg::ShaderParameterLayout layout;
    layout.m_parameters.push_back(
        ofg::ShaderParameter{"base_color_factor", ofg::ShaderParameterType::Vec4, ofg::ShaderParameterScope::Material});
    layout.m_parameters.push_back(
        ofg::ShaderParameter{"model", ofg::ShaderParameterType::Mat4, ofg::ShaderParameterScope::Draw});
    std::string error;
    std::optional<ofg::Shader> shader =
        ofg::Shader::create(ofg::GpuContext{}, "property shader", "source", layout, {}, error);
    REQUIRE(shader.has_value());
    return std::move(*shader);
}

// Builds a shader with every uniform-compatible property type.
ofg::Shader make_uniform_shader() {
    ofg::ShaderParameterLayout layout;
    layout.m_parameters.push_back(
        ofg::ShaderParameter{"roughness", ofg::ShaderParameterType::Float, ofg::ShaderParameterScope::Draw});
    layout.m_parameters.push_back(ofg::ShaderParameter{
        "optional_roughness", ofg::ShaderParameterType::Float, ofg::ShaderParameterScope::Draw, 0, false});
    layout.m_parameters.push_back(
        ofg::ShaderParameter{"uv_scale", ofg::ShaderParameterType::Vec2, ofg::ShaderParameterScope::Draw});
    layout.m_parameters.push_back(
        ofg::ShaderParameter{"normal", ofg::ShaderParameterType::Vec3, ofg::ShaderParameterScope::Draw});
    layout.m_parameters.push_back(
        ofg::ShaderParameter{"tint", ofg::ShaderParameterType::Vec4, ofg::ShaderParameterScope::Draw});
    layout.m_parameters.push_back(
        ofg::ShaderParameter{"model", ofg::ShaderParameterType::Mat4, ofg::ShaderParameterScope::Draw});
    layout.m_parameters.push_back(ofg::ShaderParameter{
        "optional_texture", ofg::ShaderParameterType::Texture, ofg::ShaderParameterScope::Draw, 0, false});
    std::string error;
    std::optional<ofg::Shader> shader =
        ofg::Shader::create(ofg::GpuContext{}, "uniform shader", "source", layout, {}, error);
    REQUIRE(shader.has_value());
    return std::move(*shader);
}

// Reads one float from packed property bytes.
float read_packed_float(const std::vector<std::byte>& packed, std::size_t float_index) {
    float value = 0.0F;
    std::memcpy(&value, packed.data() + sizeof(float) * float_index, sizeof(float));
    return value;
}

} // namespace

// Verifies property bags validate declared shader scopes.
TEST_CASE("property bag validates shader parameter scopes") {
    ofg::Shader shader = make_property_shader();
    ofg::PropertyBag material_properties;
    material_properties.set("base_color_factor", ofg::math::vec4(1.0F, 0.5F, 0.25F, 1.0F));

    std::string error;
    CHECK(material_properties.validate_for_scope(shader, ofg::ShaderParameterScope::Material, error));
    CHECK(error.empty());

    material_properties.set("unknown", 1.0F);
    CHECK(material_properties.validate_for_scope(shader, ofg::ShaderParameterScope::Material, error) == false);
    CHECK(error.find("not declared") != std::string::npos);

    ofg::PropertyBag wrong_type;
    wrong_type.set("base_color_factor", 1.0F);
    CHECK(wrong_type.validate_for_scope(shader, ofg::ShaderParameterScope::Material, error) == false);
    CHECK(error.find("expected type") != std::string::npos);
}

// Verifies uniform packing follows declared layout order.
TEST_CASE("property bag packs uniform values") {
    ofg::Shader shader = make_property_shader();
    ofg::PropertyBag draw_properties;
    draw_properties.set("model", ofg::math::mat4_translation(ofg::math::vec3(2.0F, 3.0F, 4.0F)));

    std::string error;
    const std::optional<std::vector<std::byte>> packed =
        draw_properties.pack_uniforms_for_scope(shader, ofg::ShaderParameterScope::Draw, error);
    REQUIRE(packed.has_value());
    CHECK(packed->size() == sizeof(float) * 16);
    const auto* floats = reinterpret_cast<const float*>(packed->data());
    CHECK(floats[12] == doctest::Approx(2.0F));
    CHECK(floats[13] == doctest::Approx(3.0F));
    CHECK(floats[14] == doctest::Approx(4.0F));
}

// Verifies all uniform-compatible value types can be packed in layout order.
TEST_CASE("property bag packs all scalar vector and matrix uniform types") {
    ofg::Shader shader = make_uniform_shader();
    ofg::PropertyBag draw_properties;
    draw_properties.set("roughness", 0.5F);
    draw_properties.set("uv_scale", ofg::math::vec2(2.0F, 3.0F));
    draw_properties.set("normal", ofg::math::vec3(0.0F, 1.0F, 0.0F));
    draw_properties.set("tint", ofg::math::vec4(1.0F, 0.25F, 0.5F, 1.0F));
    draw_properties.set("model", ofg::math::mat4_identity());

    std::string error;
    const std::optional<std::vector<std::byte>> packed =
        draw_properties.pack_uniforms_for_scope(shader, ofg::ShaderParameterScope::Draw, error);
    REQUIRE(packed.has_value());
    CHECK(packed->size() == sizeof(float) * 26);
    CHECK(read_packed_float(*packed, 0) == doctest::Approx(0.5F));
    CHECK(read_packed_float(*packed, 1) == doctest::Approx(2.0F));
    CHECK(read_packed_float(*packed, 3) == doctest::Approx(0.0F));
    CHECK(read_packed_float(*packed, 6) == doctest::Approx(1.0F));
    CHECK(read_packed_float(*packed, 10) == doctest::Approx(1.0F));
}

// Verifies declared uniform offsets create deterministic padding.
TEST_CASE("property bag honors explicit uniform offsets") {
    ofg::ShaderParameterLayout layout;
    layout.m_parameters.push_back(
        ofg::ShaderParameter{"roughness", ofg::ShaderParameterType::Float, ofg::ShaderParameterScope::Draw});
    layout.m_parameters.push_back(
        ofg::ShaderParameter{"uv_scale", ofg::ShaderParameterType::Vec2, ofg::ShaderParameterScope::Draw, 16});
    std::string error;
    std::optional<ofg::Shader> shader =
        ofg::Shader::create(ofg::GpuContext{}, "offset shader", "source", layout, {}, error);
    REQUIRE(shader.has_value());

    ofg::PropertyBag draw_properties;
    draw_properties.set("roughness", 0.5F);
    draw_properties.set("uv_scale", ofg::math::vec2(2.0F, 3.0F));

    const std::optional<std::vector<std::byte>> packed =
        draw_properties.pack_uniforms_for_scope(*shader, ofg::ShaderParameterScope::Draw, error);
    REQUIRE(packed.has_value());
    CHECK(packed->size() == 24);
    CHECK(read_packed_float(*packed, 0) == doctest::Approx(0.5F));
    CHECK(read_packed_float(*packed, 1) == doctest::Approx(0.0F));
    CHECK(read_packed_float(*packed, 4) == doctest::Approx(2.0F));
    CHECK(read_packed_float(*packed, 5) == doctest::Approx(3.0F));
}

// Verifies overlapping declared offsets are rejected instead of corrupting bytes.
TEST_CASE("property bag rejects overlapping uniform offsets") {
    ofg::ShaderParameterLayout layout;
    layout.m_parameters.push_back(
        ofg::ShaderParameter{"roughness", ofg::ShaderParameterType::Float, ofg::ShaderParameterScope::Draw});
    layout.m_parameters.push_back(
        ofg::ShaderParameter{"metalness", ofg::ShaderParameterType::Float, ofg::ShaderParameterScope::Draw, 2});
    std::string error;
    std::optional<ofg::Shader> shader =
        ofg::Shader::create(ofg::GpuContext{}, "overlap shader", "source", layout, {}, error);
    REQUIRE(shader.has_value());

    ofg::PropertyBag draw_properties;
    draw_properties.set("roughness", 0.5F);
    draw_properties.set("metalness", 0.25F);

    CHECK(
        draw_properties.pack_uniforms_for_scope(*shader, ofg::ShaderParameterScope::Draw, error).has_value() == false);
    CHECK(error.find("overlaps") != std::string::npos);
}

// Verifies direct property helpers cover type and size edge cases.
TEST_CASE("property bag helper functions describe shader value types") {
    ofg::PropertyBag properties;
    properties.set("roughness", 0.5F);
    CHECK(properties.size() == 1);
    CHECK(properties.get("missing") == nullptr);

    CHECK(ofg::property_value_matches_type(0.5F, ofg::ShaderParameterType::Float));
    CHECK(ofg::property_value_matches_type(ofg::math::vec2(1.0F, 2.0F), ofg::ShaderParameterType::Vec2));
    CHECK(ofg::property_value_matches_type(ofg::math::vec3(1.0F, 2.0F, 3.0F), ofg::ShaderParameterType::Vec3));
    CHECK(ofg::property_value_matches_type(ofg::math::vec4(1.0F, 2.0F, 3.0F, 4.0F), ofg::ShaderParameterType::Vec4));
    CHECK(ofg::property_value_matches_type(ofg::math::mat4_identity(), ofg::ShaderParameterType::Mat4));
    CHECK(ofg::property_value_matches_type(static_cast<ofg::Texture*>(nullptr), ofg::ShaderParameterType::Texture) ==
          false);

    CHECK(*ofg::shader_parameter_uniform_size(ofg::ShaderParameterType::Float) == sizeof(float));
    CHECK(*ofg::shader_parameter_uniform_size(ofg::ShaderParameterType::Vec2) == sizeof(float) * 2);
    CHECK(*ofg::shader_parameter_uniform_size(ofg::ShaderParameterType::Vec3) == sizeof(float) * 3);
    CHECK(*ofg::shader_parameter_uniform_size(ofg::ShaderParameterType::Vec4) == sizeof(float) * 4);
    CHECK(*ofg::shader_parameter_uniform_size(ofg::ShaderParameterType::Mat4) == sizeof(float) * 16);
    CHECK(ofg::shader_parameter_uniform_size(ofg::ShaderParameterType::Texture).has_value() == false);
}

// Verifies invalid bags fail before packing bytes.
TEST_CASE("property bag refuses to pack invalid scoped values") {
    ofg::Shader shader = make_property_shader();
    ofg::PropertyBag draw_properties;
    draw_properties.set("model", ofg::math::mat4_identity());
    draw_properties.set("unknown", 1.0F);

    std::string error;
    CHECK(draw_properties.pack_uniforms_for_scope(shader, ofg::ShaderParameterScope::Draw, error).has_value() == false);
    CHECK(error.find("not declared") != std::string::npos);
}
