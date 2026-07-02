// Base class for referenceable OFG runtime objects.
//
// Object owns the intrusive reference list used by Ptr<T>. Referenceable scene,
// component, and resource types derive from Object so non-owning persistent
// references can be nulled deterministically when the target is destroyed.
#pragma once

namespace ofg {

namespace detail {
struct PtrReferenceNode;
} // namespace detail

class Object {
public:
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&&) = delete;
    Object& operator=(Object&&) = delete;
    virtual ~Object() noexcept;

protected:
    // Creates an object with no registered observers.
    Object() noexcept = default;

private:
    template <typename T> friend class Ptr;

    // Adds a Ptr-owned reference node to this object's intrusive list.
    void register_reference(detail::PtrReferenceNode& node) noexcept;
    // Removes a Ptr-owned reference node from its current target object.
    static void unregister_reference(detail::PtrReferenceNode& node) noexcept;

    detail::PtrReferenceNode* m_first_reference{nullptr};
};

} // namespace ofg
