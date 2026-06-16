/// @file monitor_internal.hpp
/// @brief Shared internal constants and helpers for the monitor component.
/// @details This header is private to the monitor component. It contains small
/// helpers and constants that are reused across multiple implementation units.
/// @ingroup monitor

#pragma once

#include <cstddef>
#include <cstdint>

namespace monitor {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.2831853071795864769f;

[[nodiscard]] inline bool IsPowerOfTwo(std::size_t value) noexcept {
    return (value > 0U) && ((value & (value - 1U)) == 0U);
}

inline constexpr const char* kTag = "MONITOR";

} // namespace monitor
