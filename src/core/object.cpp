// Intrusive observer-list implementation for Object and Ptr<T>.
#include "core/object.h"

#include "core/ptr.h"

namespace ofg {

Object::~Object() noexcept
{
    detail::PtrReferenceNode* node = m_firstReference;
    while (node != nullptr)
    {
        detail::PtrReferenceNode* next = node->next;
        node->object = nullptr;
        node->previous = nullptr;
        node->next = nullptr;
        node = next;
    }
    m_firstReference = nullptr;
}

void Object::registerReference(detail::PtrReferenceNode& node) noexcept
{
    node.object = this;
    node.previous = nullptr;
    node.next = m_firstReference;
    if (m_firstReference != nullptr)
    {
        m_firstReference->previous = &node;
    }
    m_firstReference = &node;
}

void Object::unregisterReference(detail::PtrReferenceNode& node) noexcept
{
    Object* object = node.object;
    if (object == nullptr)
    {
        return;
    }

    if (node.previous != nullptr)
    {
        node.previous->next = node.next;
    }
    else
    {
        object->m_firstReference = node.next;
    }
    if (node.next != nullptr)
    {
        node.next->previous = node.previous;
    }

    node.object = nullptr;
    node.previous = nullptr;
    node.next = nullptr;
}

} // namespace ofg
