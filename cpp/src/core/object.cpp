// Intrusive observer-list implementation for Object and Ptr<T>.
#include "ofg/core/object.hpp"

#include "ofg/core/ptr.hpp"

namespace ofg {

Object::~Object() noexcept {
    detail::PtrReferenceNode* node = m_first_reference;
    while (node != nullptr) {
        detail::PtrReferenceNode* next = node->m_next;
        node->m_object = nullptr;
        node->m_previous = nullptr;
        node->m_next = nullptr;
        node = next;
    }
    m_first_reference = nullptr;
}

void Object::register_reference(detail::PtrReferenceNode& node) noexcept {
    node.m_object = this;
    node.m_previous = nullptr;
    node.m_next = m_first_reference;
    if (m_first_reference != nullptr) {
        m_first_reference->m_previous = &node;
    }
    m_first_reference = &node;
}

void Object::unregister_reference(detail::PtrReferenceNode& node) noexcept {
    Object* object = node.m_object;
    if (object == nullptr) {
        return;
    }

    if (node.m_previous != nullptr) {
        node.m_previous->m_next = node.m_next;
    } else {
        object->m_first_reference = node.m_next;
    }
    if (node.m_next != nullptr) {
        node.m_next->m_previous = node.m_previous;
    }

    node.m_object = nullptr;
    node.m_previous = nullptr;
    node.m_next = nullptr;
}

} // namespace ofg
