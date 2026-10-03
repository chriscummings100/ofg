// Immutable Slang source description; the graphics layer owns preparation of its private GPU program.
#pragma once

#include "resources/resource.h"
#include <memory>

namespace ofg {
struct ShaderGpuData;
class Graphics;

class Shader : public Resource
{
public:
    // Creates an uncached source description; compilation occurs on first draw.
    static std::shared_ptr<Shader> create(
        std::string name,
        std::string source,
        std::string vertexEntry = "vertexMain",
        std::string fragmentEntry = "fragmentMain"
    );
    // Returns the diagnostic source name.
    const std::string& name() const noexcept { return m_name; }
    // Returns immutable Slang source.
    const std::string& source() const noexcept { return m_source; }
    // Returns the vertex entry-point name.
    const std::string& vertexEntry() const noexcept { return m_vertexEntry; }
    // Returns the fragment entry-point name.
    const std::string& fragmentEntry() const noexcept { return m_fragmentEntry; }

private:
    friend class Graphics;
    // Stores an already validated source description.
    Shader(std::string name, std::string source, std::string vertexEntry, std::string fragmentEntry);
    std::string m_name, m_source, m_vertexEntry, m_fragmentEntry;
    std::shared_ptr<ShaderGpuData> m_gpu;
};
} // namespace ofg
