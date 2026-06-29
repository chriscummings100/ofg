// Mutable material resource for OFG renderer shader parameters.
//
// Materials reference a shader owned elsewhere and keep a validated property
// bag. GPU-ready materials own their uniform buffer and bind group state.
#pragma once

#include "ofg/game/gpu_context.hpp"
#include "ofg/resources/property_bag.hpp"
#include "ofg/resources/shader.hpp"

#include <cstdint>
#include <string>

#include <webgpu/webgpu.h>

namespace ofg {

class Material {
public:
    // Allocates a labeled material resource using the creating Resources context.
    Material(GpuContext gpu, std::string label);
    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;
    Material(Material&& other) noexcept;
    Material& operator=(Material&& other) noexcept;
    ~Material();

    // Initializes this material and validates its properties against material scope.
    void init(Shader& shader, PropertyBag properties);

    // Replaces one property and refreshes validation state.
    void set_property(std::string name, PropertyValue value);
    // Returns the referenced shader.
    [[nodiscard]] const Shader& shader() const noexcept;
    // Returns the material label.
    [[nodiscard]] const std::string& label() const noexcept;
    // Returns the material properties.
    [[nodiscard]] const PropertyBag& properties() const noexcept;
    // Returns the WebGPU bind group layout, null for CPU-only resources.
    [[nodiscard]] WGPUBindGroupLayout bind_group_layout() const noexcept;
    // Returns the material uniform buffer, null when no uniform data exists.
    [[nodiscard]] WGPUBuffer uniform_buffer() const noexcept;
    // Returns the WebGPU bind group, null for CPU-only resources.
    [[nodiscard]] WGPUBindGroup bind_group() const noexcept;
    // Returns the material revision.
    [[nodiscard]] std::uint64_t revision() const noexcept;

private:
    // Creates GPU material uniform and bind-group state.
    void prepare_gpu_state();
    // Releases all owned WebGPU material handles.
    void release_gpu_state() noexcept;

    GpuContext m_gpu;
    std::string m_label;
    Shader* m_shader{nullptr};
    PropertyBag m_properties;
    WGPUBindGroupLayout m_bind_group_layout{nullptr};
    WGPUBuffer m_uniform_buffer{nullptr};
    WGPUBindGroup m_bind_group{nullptr};
    std::uint64_t m_revision{0};
};

} // namespace ofg
