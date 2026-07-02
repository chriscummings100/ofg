// Mutable WGSL shader resource for the OFG renderer.
//
// Shaders store WGSL source plus explicit parameter layout data, and they
// eagerly prepare a WebGPU shader module when created with a ready GpuContext.
#pragma once

#include "ofg/core/object.hpp"
#include "ofg/game/gpu_context.hpp"

#include <cstdint>
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

class Shader : public Object {
public:
    // Allocates a labeled shader resource using the creating Resources context.
    Shader(GpuContext gpu, std::string label);
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) = delete;
    Shader& operator=(Shader&& other) = delete;
    ~Shader() override;

    // Initializes this shader from WGSL source and explicit parameter layout.
    void init_from_wgsl(
        std::string wgsl_source, ShaderParameterLayout parameter_layout, std::vector<PipelineDefinition> pipelines);

    // Replaces WGSL source and increments the shader revision.
    void replace_source(std::string wgsl_source);
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
    // Creates or recreates the WebGPU shader module from WGSL source.
    void prepare_gpu_state();
    // Releases the owned WebGPU shader module.
    void release_gpu_state() noexcept;

    GpuContext m_gpu;
    std::string m_label;
    std::string m_wgsl_source;
    ShaderParameterLayout m_parameter_layout;
    std::vector<PipelineDefinition> m_pipelines;
    WGPUShaderModule m_module{nullptr};
    std::uint64_t m_revision{0};
};

// Converts a shader parameter type into a readable diagnostic label.
[[nodiscard]] const char* shader_parameter_type_name(ShaderParameterType type) noexcept;

// Converts a shader parameter scope into a readable diagnostic label.
[[nodiscard]] const char* shader_parameter_scope_name(ShaderParameterScope scope) noexcept;

} // namespace ofg
