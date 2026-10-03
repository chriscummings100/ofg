// Application-thread resource transitions and loading failure publication.
#include "resources/resource.h"
#include "core/engine-error.h"

#include <exception>
#include <utility>

namespace ofg {

Resource::Resource()
    : m_state(ResourceState::Loaded)
{
}

bool Resource::loadStep()
{
    throw EngineError("This resource does not support keyed loading: " + key());
}

Resource::Resource(std::string key)
    : m_key(std::move(key))
{
}

void Resource::advanceLoading()
{
    if (isFinished())
    {
        return;
    }

    try
    {
        if (loadStep())
        {
            m_state = ResourceState::Loaded;
        }
    } catch (const std::exception& exception)
    {
        m_error = exception.what();
        m_state = ResourceState::Failed;
    } catch (...)
    {
        m_error = "Resource loading failed with an unknown exception.";
        m_state = ResourceState::Failed;
    }
}

} // namespace ofg
