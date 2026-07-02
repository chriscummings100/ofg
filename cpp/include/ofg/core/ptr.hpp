// Lifetime-aware non-owning pointer for Object-derived OFG types.
//
// Ptr<T> is intentionally for stored observer references, not ownership and not
// hot loops. It registers one intrusive node in the target Object and becomes
// null when that Object is destroyed.
#pragma once

#include "ofg/core/engine_error.hpp"
#include "ofg/core/object.hpp"

#include <cstddef>
#include <string>
#include <type_traits>
#include <typeinfo>

namespace ofg {

namespace detail {

struct PtrReferenceNode {
    Object* m_object{nullptr};
    PtrReferenceNode* m_previous{nullptr};
    PtrReferenceNode* m_next{nullptr};
};

} // namespace detail

template <typename T> class Ptr {
public:
    Ptr() noexcept = default;
    Ptr(std::nullptr_t) noexcept {}

    // Registers this pointer as an observer of object when object is non-null.
    Ptr(T* object) noexcept {
        reset(object);
    }

    // Registers this pointer as another observer of other's current target.
    Ptr(const Ptr& other) noexcept {
        reset(other.get());
    }

    // Moves the observer relationship from other to this pointer.
    Ptr(Ptr&& other) noexcept {
        reset(other.get());
        other.reset();
    }

    Ptr& operator=(std::nullptr_t) noexcept {
        reset();
        return *this;
    }

    Ptr& operator=(T* object) noexcept {
        reset(object);
        return *this;
    }

    Ptr& operator=(const Ptr& other) noexcept {
        if (this != &other) {
            reset(other.get());
        }
        return *this;
    }

    Ptr& operator=(Ptr&& other) noexcept {
        if (this != &other) {
            reset(other.get());
            other.reset();
        }
        return *this;
    }

    ~Ptr() {
        reset();
    }

    // Clears this observer without affecting the target object's lifetime.
    void reset() noexcept {
        Object::unregister_reference(m_reference);
    }

    // Replaces this observer target without affecting either object's lifetime.
    void reset(T* object) noexcept {
        static_assert(std::is_base_of_v<Object, T>, "Ptr<T> requires T to inherit Object.");
        Object* next_object = object;
        if (m_reference.m_object == next_object) {
            return;
        }
        Object::unregister_reference(m_reference);
        if (next_object != nullptr) {
            next_object->register_reference(m_reference);
        }
    }

    // Returns the observed object, or nullptr after reset or target destruction.
    [[nodiscard]] T* get() const noexcept {
        return static_cast<T*>(m_reference.m_object);
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return get() != nullptr;
    }

    [[nodiscard]] bool operator==(const T* object) const noexcept {
        return get() == object;
    }

    [[nodiscard]] bool operator!=(const T* object) const noexcept {
        return get() != object;
    }

    // Returns the observed object or throws a clear engine error when null.
    [[nodiscard]] T& operator*() const {
        return *require_live();
    }

    // Returns the observed object or throws a clear engine error when null.
    [[nodiscard]] T* operator->() const {
        return require_live();
    }

private:
    [[nodiscard]] T* require_live() const {
        T* object = get();
        if (object == nullptr) {
            throw EngineError(std::string("Ptr<") + typeid(T).name() + "> does not reference a live object.");
        }
        return object;
    }

    detail::PtrReferenceNode m_reference;
};

template <typename T> [[nodiscard]] bool operator==(const T* object, const Ptr<T>& ptr) noexcept {
    return ptr == object;
}

template <typename T> [[nodiscard]] bool operator!=(const T* object, const Ptr<T>& ptr) noexcept {
    return ptr != object;
}

} // namespace ofg
