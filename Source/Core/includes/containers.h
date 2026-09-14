#pragma once

#include <algorithm>
#include <concepts>
#include <ranges>

// Generic container helpers.
//
// The standard library splits "does this hold X?" across three unrelated spellings: the associative containers got a
// "contains()" member (C++20), the sequence containers never did (you're expected to write "std::find(...) != end()"),
// and the range algorithm that unifies them ("std::ranges::contains()") only landed in C++23 and still doesn't pick the
// container's own faster lookup. We can't add our own overloads to "std" (that's UB, and MSVC is free to break it),
// so this is our own single entry point instead.
namespace Containers
{
   // Containers that have their own (much faster than linear) lookup: std::(unordered_)set/map, std::string, ...
   template <typename T, typename V>
   concept HasContainsMember = requires(const T& container, const V& value) { { container.contains(value) } -> std::convertible_to<bool>; };

   // Uses the container's own hashing/tree lookup. Note that for maps this checks the *keys*, matching "std::map::contains()",
   // and that for strings it's a *substring* search when passing a string, matching "std::string::contains()".
   template <typename T, typename V> requires HasContainsMember<T, V>
   [[nodiscard]] constexpr bool Contains(const T& container, const V& value)
   {
      return container.contains(value);
   }

   // Everything else (std::vector/array/list/deque/span, C arrays, views, ...): linear scan, needs "operator==" on the elements
   template <typename T, typename V> requires (!HasContainsMember<T, V> && std::ranges::input_range<const T&>)
   [[nodiscard]] constexpr bool Contains(const T& container, const V& value)
   {
      // "std::ranges::find()" is too strict here, it demands a symmetric "std::equality_comparable_with" (so both
      // "a == b" and "b == a", plus a common reference type), which rules out the many types that only define one
      // direction of "operator==" (e.g. ReShade's resource handles vs raw uint64_t ones). "std::find()" only needs "*it == value".
      const auto end = std::ranges::end(container);
      return std::find(std::ranges::begin(container), end, value) != end;
   }
}

// Exposed globally, this is meant to be as convenient to reach for as the members it replaces (the qualified
// "Containers::Contains()" spelling keeps working if a call site ever needs to disambiguate it)
using Containers::Contains;
