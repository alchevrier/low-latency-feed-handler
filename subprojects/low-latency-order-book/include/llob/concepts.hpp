#pragma once
#include <cstddef>
#include <type_traits>

namespace llob
{

template <typename T>
concept FitsCacheLine = sizeof(T) <= 64 && alignof(T) == 64;

template <typename T>
concept IsNotVirtual = !std::is_polymorphic_v<T>;

template <std::size_t N>
concept IsPowerOfTwo = N > 0 && (N & (N - 1)) == 0;

template <typename T>
concept IsTriviallyCopyable = std::is_trivially_copyable_v<T>;

template <typename T>
concept SlotType = FitsCacheLine<T> && IsNotVirtual<T> && IsTriviallyCopyable<T>;

} // namespace llob
