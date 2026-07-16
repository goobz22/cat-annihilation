// test_container_dynamic_array.cpp
// ---------------------------------------------------------------------------
// Regression-locks the SFINAE-disabled count-only ctor / resize() overloads
// on Engine::DynamicArray.
//
// WHY this suite exists:
//   The previous DynamicArray signatures were
//       DynamicArray(size_type count, const T& value = T(), ...);
//       resize(size_type count, const T& value = T());
//   Both used a default-constructed T() as the default argument, which
//   forced T to be default-constructible JUST to declare the function, even
//   when a caller supplied a real value or wanted move-only semantics. The
//   2026-05-16 fix split each signature into two SFINAE-gated overloads:
//   one available when T is copy-constructible (count + value form) and one
//   available when T is default-constructible (count-only form).
//
//   The test exercises:
//     (a) a non-default-constructible T can still be sized via count+value
//         (compile + run, ctor and resize),
//     (b) a default-constructible T can use the count-only form (compile +
//         run, ctor and resize),
//     (c) move-only T (std::unique_ptr) compiles when only the count-only
//         form is used — proving the copy-constructible-only form is
//         correctly SFINAE'd OUT of the overload set for move-only types
//         (a residual instantiation would still attempt to use the value
//         arg's default and fail).
// ---------------------------------------------------------------------------

#include "catch.hpp"
#include "engine/containers/DynamicArray.hpp"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace {

// NoDefault has an explicit-only ctor and no default ctor. The pre-fix
// DynamicArray declared a `T()` default argument on every count-taking
// signature, which made `DynamicArray<NoDefault>` fail to even compile.
struct NoDefault {
    int payload;
    explicit NoDefault(int v) : payload(v) {}
    NoDefault(const NoDefault&) = default;
    NoDefault& operator=(const NoDefault&) = default;
};

static_assert(!std::is_default_constructible_v<NoDefault>,
              "test depends on NoDefault being non-default-constructible");
static_assert(std::is_copy_constructible_v<NoDefault>,
              "test depends on NoDefault being copy-constructible");

} // namespace

TEST_CASE("DynamicArray count+value ctor works for non-default-constructible T",
          "[dynamic_array][sfinae]") {
    // Pre-fix: this constructor call failed because the default argument
    // `const T& value = T()` instantiated NoDefault::NoDefault() — which
    // doesn't exist. Post-fix: the count+value overload is the only one
    // selected (the count-only overload is SFINAE'd out by the
    // default-constructibility check), so this compiles AND runs.
    Engine::DynamicArray<NoDefault> arr(5, NoDefault(42));
    REQUIRE(arr.size() == 5);
    for (size_t i = 0; i < arr.size(); ++i) {
        REQUIRE(arr[i].payload == 42);
    }
}

TEST_CASE("DynamicArray::resize(count, value) works for non-default-constructible T",
          "[dynamic_array][sfinae]") {
    Engine::DynamicArray<NoDefault> arr;
    arr.push_back(NoDefault(7));
    REQUIRE(arr.size() == 1);

    // Pre-fix: the bare `resize(n, value)` form's signature was
    // `resize(size_type, const T& value = T())`, which again forced
    // T() instantiation even when a real value was supplied. Post-fix
    // the count+value overload has no default arg and is unconditionally
    // available for copy-constructible T.
    arr.resize(4, NoDefault(99));
    REQUIRE(arr.size() == 4);
    REQUIRE(arr[0].payload == 7);
    REQUIRE(arr[1].payload == 99);
    REQUIRE(arr[2].payload == 99);
    REQUIRE(arr[3].payload == 99);

    // Shrinking via resize(count) — but NoDefault is not default-
    // constructible, so the count-only overload is SFINAE-disabled.
    // We still need to shrink, so route through resize(count, value)
    // with a value that won't actually be used (shrink doesn't construct
    // anything past size()).
    arr.resize(2, NoDefault(0));
    REQUIRE(arr.size() == 2);
    REQUIRE(arr[0].payload == 7);
    REQUIRE(arr[1].payload == 99);
}

TEST_CASE("DynamicArray count-only ctor works for default-constructible T",
          "[dynamic_array][sfinae]") {
    // std::string is default-constructible AND copy-constructible, so both
    // overloads are available — count-only picks the default-construct
    // path explicitly.
    Engine::DynamicArray<std::string> arr(3);
    REQUIRE(arr.size() == 3);
    REQUIRE(arr[0].empty());
    REQUIRE(arr[1].empty());
    REQUIRE(arr[2].empty());

    arr.resize(5);
    REQUIRE(arr.size() == 5);
    REQUIRE(arr[3].empty());
    REQUIRE(arr[4].empty());

    arr.resize(2);
    REQUIRE(arr.size() == 2);
}

TEST_CASE("DynamicArray supports move-only T when only count-only form is used",
          "[dynamic_array][sfinae][move_only]") {
    // unique_ptr is default-constructible (null) but NOT copy-constructible.
    // The pre-fix code refused to compile because the default-arg `T()` ran
    // even when push_back / emplace_back used moves — and the value-taking
    // signatures' `const T&` ate the move-only T. Post-fix the
    // copy-constructible-only overloads are SFINAE'd out cleanly, leaving
    // the default-constructible-only overloads as the only callable forms.
    Engine::DynamicArray<std::unique_ptr<int>> arr;
    arr.push_back(std::make_unique<int>(10));
    arr.push_back(std::make_unique<int>(20));
    REQUIRE(arr.size() == 2);
    REQUIRE(*arr[0] == 10);
    REQUIRE(*arr[1] == 20);

    // Default-construct three null unique_ptrs at the tail via the
    // count-only resize form. The copy-form would not compile here because
    // unique_ptr has no const& copy ctor — so the existence of THIS line
    // is itself the regression-lock that the SFINAE split worked.
    arr.resize(5);
    REQUIRE(arr.size() == 5);
    REQUIRE(*arr[0] == 10);
    REQUIRE(*arr[1] == 20);
    REQUIRE(arr[2] == nullptr);
    REQUIRE(arr[3] == nullptr);
    REQUIRE(arr[4] == nullptr);
}
