// Lifetime-aware non-owning pointer for Object-derived OFG types.
//
// Ptr<T> is intentionally for stored observer references, not ownership and not
// hot loops. It registers one intrusive node in the target Object and becomes
// null when that Object is destroyed. Registration and access are single-threaded.
#pragma once

#include "core/engine-error.h"
#include "core/object.h"

#include <cstddef>
#include <string>
#include <type_traits>
#include <typeinfo>

namespace ofg {

namespace detail {

struct PtrReferenceNode
{
    Object* object{nullptr};
    PtrReferenceNode* previous{nullptr};
    PtrReferenceNode* next{nullptr};
};

} // namespace detail

template<typename T>
class Ptr
{
public:
    // Creates an empty observer.
    Ptr() noexcept = default;
    // Creates an explicitly empty observer.
    Ptr(std::nullptr_t) noexcept {}

    // Registers this pointer as an observer of object when object is non-null.
    Ptr(T* object) noexcept { reset(object); }

    // Registers this pointer as another observer of other's current target.
    Ptr(const Ptr& other) noexcept { reset(other.get()); }

    // Moves the observer relationship from other to this pointer.
    Ptr(Ptr&& other) noexcept
    {
        reset(other.get());
        other.reset();
    }

    // Clears this observer without deleting its target.
    Ptr& operator=(std::nullptr_t) noexcept
    {
        reset();
        return *this;
    }

    // Observes a new target without taking ownership.
    Ptr& operator=(T* object) noexcept
    {
        reset(object);
        return *this;
    }

    // Copies another observer's current target.
    Ptr& operator=(const Ptr& other) noexcept
    {
        if (this != &other)
        {
            reset(other.get());
        }
        return *this;
    }

    // Transfers an observer relationship and empties the source.
    Ptr& operator=(Ptr&& other) noexcept
    {
        if (this != &other)
        {
            reset(other.get());
            other.reset();
        }
        return *this;
    }

    // Unregisters this observer without affecting the target lifetime.
    ~Ptr() { reset(); }

    // Clears this observer without affecting the target object's lifetime.
    void reset() noexcept { Object::unregisterReference(m_reference); }

    // Replaces this observer target without affecting either object's lifetime.
    void reset(T* object) noexcept
    {
        static_assert(std::is_base_of_v<Object, T>, "Ptr<T> requires T to inherit Object.");
        Object* nextObject = object;
        if (m_reference.object == nextObject)
        {
            return;
        }
        Object::unregisterReference(m_reference);
        if (nextObject != nullptr)
        {
            nextObject->registerReference(m_reference);
        }
    }

    // Returns the observed object, or nullptr after reset or target destruction.
    [[nodiscard]] T* get() const noexcept { return static_cast<T*>(m_reference.object); }

    // Reports whether this observer currently has a live target.
    [[nodiscard]] explicit operator bool() const noexcept { return get() != nullptr; }

    // Compares the observed address with a raw pointer.
    [[nodiscard]] bool operator==(const T* object) const noexcept { return get() == object; }

    // Reports whether the observed address differs from a raw pointer.
    [[nodiscard]] bool operator!=(const T* object) const noexcept { return get() != object; }

    // Returns the observed object or throws a clear engine error when null.
    [[nodiscard]] T& operator*() const { return *requireLive(); }

    // Returns the observed object or throws a clear engine error when null.
    [[nodiscard]] T* operator->() const { return requireLive(); }

private:
    // Rejects null dereferences with a diagnostic naming the observed type.
    [[nodiscard]] T* requireLive() const
    {
        T* object = get();
        if (object == nullptr)
        {
            throw EngineError(std::string("Ptr<") + typeid(T).name() + "> does not reference a live object.");
        }
        return object;
    }

    detail::PtrReferenceNode m_reference;
};

// Allows a raw pointer on the left of an observer equality comparison.
template<typename T>
[[nodiscard]] bool operator==(const T* object, const Ptr<T>& ptr) noexcept
{
    return ptr == object;
}

// Allows a raw pointer on the left of an observer inequality comparison.
template<typename T>
[[nodiscard]] bool operator!=(const T* object, const Ptr<T>& ptr) noexcept
{
    return ptr != object;
}

} // namespace ofg
