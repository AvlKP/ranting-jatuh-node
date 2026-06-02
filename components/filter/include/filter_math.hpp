#pragma once

#include <cmath>
#include <numbers>

namespace filter {
namespace math {

/// @brief Convert degrees to radians
/// @param deg Angle in degrees
/// @return Angle in radians
[[nodiscard]] constexpr float deg2rad(float deg) noexcept {
    return deg * (std::numbers::pi_v<float> / 180.0f);
}

/// @brief Convert radians to degrees
/// @param rad Angle in radians
/// @return Angle in degrees
[[nodiscard]] constexpr float rad2deg(float rad) noexcept {
    return rad * (180.0f / std::numbers::pi_v<float>);
}

/// @brief Fast inverse square root. 
/// Utilizes the compiler's built-in which maps to hardware instructions 
/// like rsqrt.s on ESP32-S3 when optimized.
/// @param x Input value
/// @return 1.0 / sqrt(x)
[[nodiscard]] inline float fast_inv_sqrt(float x) noexcept {
    // A fallback to the standard highly optimized path if hardware rsqrt is missing.
    // Modern GCC with ESP32-S3 target optimizes 1.0f / std::sqrt(x) very well.
    return 1.0f / std::sqrt(x);
}

} // namespace math
} // namespace filter