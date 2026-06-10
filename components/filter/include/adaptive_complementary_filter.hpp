/// @file adaptive_complementary_filter.hpp
/// @brief Self-tuning complementary filter for IMU orientation estimation.
/// @details Fuses gyroscope integration and accelerometer gravity reference
/// with adaptive blending. Trusts accelerometer tilt more when total acceleration
/// is near 1 g (static); trusts gyroscope integration more during dynamic motion.
/// The self-tuning weight is:
///   weight = 1 / (1 + K * |accel_mag - 1.0|)
///   alpha = 1 - (1 - alpha_base) * weight
///   theta = alpha * gyro_integration + (1 - alpha) * accel_tilt
/// @ingroup filter

#pragma once

#include <span>
#include <cmath>
#include "filter_math.hpp"

namespace filter {

/// @brief Adaptive complementary filter for roll/pitch estimation.
/// Near 1 g: trusts accelerometer tilt more.
/// During strong dynamic acceleration: gyroscope integration dominates.
class AdaptiveComplementary {
public:
    /// @param alpha_base Base gyro/accel blending ratio (0.98 = 98% gyro).
    /// @param k_gain Gain controlling how aggressively alpha shifts toward gyro during motion.
    explicit AdaptiveComplementary(float alpha_base = 0.98f, float k_gain = 50.0f) noexcept
        : alpha_base_{alpha_base}, k_gain_{k_gain}, pitch_{0.0f}, roll_{0.0f} {}

    /// @brief Update filter with new IMU sample.
    /// @param accel Calibrated accelerometer vector [ax, ay, az] in g.
    /// @param gyro Calibrated gyroscope vector [gx, gy, gz] in dps.
    /// @param dt Time delta since last update [s].
    void update(std::span<const float, 3> accel, std::span<const float, 3> gyro, float dt) noexcept {
        const float gx_rad = math::deg2rad(gyro[0]);
        const float gy_rad = math::deg2rad(gyro[1]);

        const float ax = accel[0];
        const float ay = accel[1];
        const float az = accel[2];

        const float accel_mag = std::sqrt(ax * ax + ay * ay + az * az);
        const float accel_err = std::abs(accel_mag - 1.0f);
        const float weight = 1.0f / (1.0f + k_gain_ * accel_err);
        const float alpha = 1.0f - (1.0f - alpha_base_) * weight;

        const float accel_pitch = std::atan2(accel[0], accel[2]);
        const float accel_roll  = std::atan2(-accel[1], accel[2]);

        const float gyro_pitch = pitch_ + (gy_rad * dt);
        const float gyro_roll  = roll_  + (gx_rad * dt);

        pitch_ = (alpha * gyro_pitch) + ((1.0f - alpha) * accel_pitch);
        roll_  = (alpha * gyro_roll)  + ((1.0f - alpha) * accel_roll);
    }

    [[nodiscard]] float pitch() const noexcept { return math::rad2deg(pitch_); }
    [[nodiscard]] float roll() const noexcept { return math::rad2deg(roll_); }

private:
    float alpha_base_;
    float k_gain_;
    float pitch_;
    float roll_;
};

} // namespace filter
