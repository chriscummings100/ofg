// Creation and validation of procedural shader source descriptions.
#include "resources/shader.h"
#include "core/engine-error.h"
#include <utility>

namespace ofg {
Shader::Shader(std::string name, std::string source, std::string vertexEntry, std::string fragmentEntry)
    : m_name(std::move(name))
    , m_source(std::move(source))
    , m_vertexEntry(std::move(vertexEntry))
    , m_fragmentEntry(std::move(fragmentEntry))
{
}

std::shared_ptr<Shader> Shader::create(
    std::string name,
    std::string source,
    std::string vertexEntry,
    std::string fragmentEntry
)
{
    if (name.empty() || source.empty() || vertexEntry.empty() || fragmentEntry.empty())
    {
        throw EngineError("Shader requires nonempty name, source and entry points.");
    }
    return std::shared_ptr<Shader>(
        new Shader(std::move(name), std::move(source), std::move(vertexEntry), std::move(fragmentEntry))
    );
}
} // namespace ofg
