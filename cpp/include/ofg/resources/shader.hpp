// Mutable WGSL shader resource for the OFG renderer.
//
// Shaders store WGSL source plus explicit parameter layout data, and they
// eagerly prepare a WebGPU shader module when created with a ready GpuContext.
#pragma once

#include "ofg/game/gpu_context.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <webgpu/webgpu.h>

namespace ofg {

enum class ShaderParameterScope {
    Frame,
    Draw,
    Material,
};

enum class ShaderParameterType {
    Float,
    Vec2,
    Vec3,
    Vec4,
    Mat4,
    Texture,
};

struct ShaderParameter {
    std::string m_name;
    ShaderParameterType m_type{ShaderParameterType::Float};
    ShaderParameterScope m_scope{ShaderParameterScope::Material};
    std::uint32_t m_uniform_offset{0};
    bool m_required{true};
};

struct ShaderParameterLayout {
    std::vector<ShaderParameter> m_parameters;
};

struct PipelineDefinition {
    std::string m_label;
    std::string m_vertex_entry{"vs_main"};
    std::string m_fragment_entry{"fs_main"};
};

class Shader {
public:
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;
    ~Shader();

    // Creates a shader resource and validates its declared layout.
    [[nodiscard]] static std::optional<Shader> create(GpuContext gpu,
        std::string label,
        std::string wgsl_source,
        ShaderParameterLayout parameter_layout,
        std::vector<PipelineDefinition> pipelines,
        std::string& error);

    // Replaces WGSL source and increments the shader revision.
    bool replace_source(std::string wgsl_source, std::string& error);
    // Finds a declared shader parameter by name.
    [[nodiscard]] const ShaderParameter* parameter(std::string_view name) const noexcept;
    // Returns all declared parameters.
    [[nodiscard]] std::span<const ShaderParameter> parameters() const noexcept;
    // Returns parameters for one binding scope.
    [[nodiscard]] std::vector<ShaderParameter> parameters_for_scope(ShaderParameterScope scope) const;
    // Returns the shader module, null for CPU-only resources.
    [[nodiscard]] WGPUShaderModule module() const noexcept;
    // Returns the current source revision.
    [[nodiscard]] std::uint64_t revision() const noexcept;
    // Returns the shader label.
    [[nodiscard]] const std::string& label() const noexcept;
    // Returns the WGSL source.
    [[nodiscard]] const std::string& source() const noexcept;

private:
    // Stores validated shader CPU data; use create() for validation.
    Shader(GpuContext gpu,
        std::string label,
        std::string wgsl_source,
        ShaderParameterLayout parameter_layout,
        std::vector<PipelineDefinition> pipelines);

    // Creates or recreates the WebGPU shader module from WGSL source.
    [[nodiscard]] bool prepare_gpu_state(std::string& error);
    // Releases the owned WebGPU shader module.
    void release_gpu_state() noexcept;

    GpuContext m_gpu;
    std::string m_label;
    std::string m_wgsl_source;
    ShaderParameterLayout m_parameter_layout;
    std::vector<PipelineDefinition> m_pipelines;
    WGPUShaderModule m_module{nullptr};
    std::uint64_t m_revision{1};
};

// Converts a shader parameter type into a readable diagnostic label.
[[nodiscard]] const char* shader_parameter_type_name(ShaderParameterType type) noexcept;

// Converts a shader parameter scope into a readable diagnostic label.
[[nodiscard]] const char* shader_parameter_scope_name(ShaderParameterScope scope) noexcept;

} // namespace ofg
