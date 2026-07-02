// Shared shader layout for the opaque PBR renderer path.
#include "ofg/render/opaque_pbr_shader.hpp"

namespace ofg {

// Returns the parameter layout expected by the opaque metallic-roughness WGSL shader.
ShaderParameterLayout opaque_pbr_shader_layout() {
    return ShaderParameterLayout{{
        ShaderParameter{"view_projection", ShaderParameterType::Mat4, ShaderParameterScope::Frame, 0, true},
        ShaderParameter{"main_light_direction", ShaderParameterType::Vec4, ShaderParameterScope::Frame, 64, true},
        ShaderParameter{"main_light_color", ShaderParameterType::Vec4, ShaderParameterScope::Frame, 80, true},
        ShaderParameter{"ambient_light_color", ShaderParameterType::Vec4, ShaderParameterScope::Frame, 96, true},
        ShaderParameter{"camera_position", ShaderParameterType::Vec4, ShaderParameterScope::Frame, 112, true},
        ShaderParameter{"model", ShaderParameterType::Mat4, ShaderParameterScope::Draw, 0, false},
        ShaderParameter{"normal_model", ShaderParameterType::Mat4, ShaderParameterScope::Draw, 64, false},
        ShaderParameter{"base_color_factor", ShaderParameterType::Vec4, ShaderParameterScope::Material, 0, true},
        ShaderParameter{"pbr_factors", ShaderParameterType::Vec4, ShaderParameterScope::Material, 16, true},
        ShaderParameter{"base_color_texture", ShaderParameterType::Texture, ShaderParameterScope::Material, 0, true},
        ShaderParameter{
            "metallic_roughness_texture", ShaderParameterType::Texture, ShaderParameterScope::Material, 0, true},
        ShaderParameter{"normal_texture", ShaderParameterType::Texture, ShaderParameterScope::Material, 0, true},
    }};
}

} // namespace ofg
