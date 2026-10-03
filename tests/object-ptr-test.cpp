// Doctest coverage for Object and Ptr lifetime-aware non-owning references.
//
// These tests pin the Milestone 0 safety contract: stored observers become null
// when their Object target is destroyed, and accidental dereference reports a
// clear EngineError instead of following stale memory.
#include "doctest.h"

#include "core/engine-error.h"
#include "core/object.h"
#include "core/ptr.h"

#include <cstdint>
#include <memory>
#include <string>


namespace {

class TestObject : public ofg::Object
{
public:
    // Creates a small referenceable value for lifetime assertions.
    explicit TestObject(int value) noexcept
        : m_value(value)
    {
    }

    int m_value{0};
};

} // namespace

// Verifies Ptr observes live objects and throws when no live object is present.
TEST_CASE("Ptr reports null access clearly")
{
    ofg::Ptr<TestObject> empty;
    CHECK(empty.get() == nullptr);
    CHECK_FALSE(static_cast<bool>(empty));
    CHECK_THROWS_WITH_AS(
        [&]()
        {
            (void)empty->m_value;
        }(),
        doctest::Contains("Ptr<"),
        ofg::EngineError
    );

    TestObject object{7};
    ofg::Ptr<TestObject> pointer{&object};
    REQUIRE(pointer.get() == &object);
    CHECK(pointer->m_value == 7);
    CHECK((*pointer).m_value == 7);

    pointer = nullptr;
    CHECK(pointer.get() == nullptr);
    CHECK_THROWS_WITH_AS(
        [&]()
        {
            (void)*pointer;
        }(),
        doctest::Contains("live object"),
        ofg::EngineError
    );
}

// Removing a middle observer and resetting a target must leave other nodes valid.
TEST_CASE("Ptr reset destruction and self assignment preserve remaining observers")
{
    auto object = std::make_unique<TestObject>(9);
    ofg::Ptr<TestObject> first{object.get()};
    {
        ofg::Ptr<TestObject> middle{object.get()};
        ofg::Ptr<TestObject> last{object.get()};
        middle.reset();
        first.reset(object.get());
        auto* self = &first;
        first = *self;
        first = std::move(*self);
        CHECK(first.get() == object.get());
        CHECK(last.get() == object.get());
        CHECK(object.get() == first);
        CHECK_FALSE(object.get() != first);
    }
    CHECK(first->m_value == 9);
    object.reset();
    CHECK(first == nullptr);
}

// Verifies Object destruction nulls every registered Ptr copy and move target.
TEST_CASE("Object destruction invalidates registered Ptr values")
{
    ofg::Ptr<TestObject> first;
    ofg::Ptr<TestObject> second;
    ofg::Ptr<TestObject> moved;

    {
        auto object = std::make_unique<TestObject>(11);
        first = object.get();
        second = first;
        moved = ofg::Ptr<TestObject>{object.get()};

        CHECK(first.get() == object.get());
        CHECK(second.get() == object.get());
        CHECK(moved.get() == object.get());
        object.reset();
    }

    CHECK(first.get() == nullptr);
    CHECK(second.get() == nullptr);
    CHECK(moved.get() == nullptr);
    CHECK_THROWS_WITH_AS(
        [&]()
        {
            (void)first->m_value;
        }(),
        doctest::Contains("live object"),
        ofg::EngineError
    );
}

// Verifies Ptr copy and move assignment maintain one valid intrusive list entry.
TEST_CASE("Ptr copy and move assignment preserve observer registration")
{
    TestObject firstObject{3};
    TestObject secondObject{4};

    ofg::Ptr<TestObject> first{&firstObject};
    ofg::Ptr<TestObject> copy = first;
    ofg::Ptr<TestObject> moved = std::move(copy);
    CHECK(first.get() == &firstObject);
    CHECK(copy.get() == nullptr);
    CHECK(moved.get() == &firstObject);

    first = &secondObject;
    CHECK(first.get() == &secondObject);
    CHECK(moved.get() == &firstObject);

    moved = std::move(first);
    CHECK(first.get() == nullptr);
    CHECK(moved.get() == &secondObject);
}
