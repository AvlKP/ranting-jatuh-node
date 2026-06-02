#pragma once

#include <span>
#include <cmath>
#include "filter_math.hpp"

namespace filter {

/// @brief Complementary filter for estimating pitch and roll from 6-DOF IMU
class Complementary {
public:
    /// @brief Constructor
    /// @param alpha Filter coefficient (0 < alpha < 1). Higher trusts gyro more.
    explicit Complementary(float alpha = 0.98f) noexcept
        : alpha_{alpha}, pitch_{0.0f}, roll_{0.0f} {}

    /// @brief Update filter with new IMU readings
    /// @param accel std::span of 3 Accel readings (m/s^2 or g)
    /// @param gyro std::span of 3 Gyro readings (degrees/s)
    /// @param dt Time delta since last update in seconds
    void update(std::span<const float, 3> accel, std::span<const float, 3> gyro, float dt) noexcept {
        // Convert gyro to rad/s
        const float gx_rad = math::deg2rad(gyro[0]);
        const float gy_rad = math::deg2rad(gyro[1]);

        // Compute pitch and roll from accelerometer
        const float accel_pitch = std::atan2(accel[1], std::sqrt(accel[0] * accel[0] + accel[2] * accel[2]));
        const float accel_roll  = std::atan2(-accel[0], accel[2]);

        // Integrate gyroscope
        const float gyro_pitch = pitch_ + (gy_rad * dt);
        const float gyro_roll  = roll_  + (gx_rad * dt);

        // Complementary fusion
        pitch_ = (alpha_ * gyro_pitch) + ((1.0f - alpha_) * accel_pitch);
        roll_  = (alpha_ * gyro_roll)  + ((1.0f - alpha_) * accel_roll);
    }

    [[nodiscard]] float pitch() const noexcept { return math::rad2deg(pitch_); }
    [[nodiscard]] float roll() const noexcept { return math::rad2deg(roll_); }

private:
    float alpha_;
    float pitch_; // internally maintained in radians
    float roll_;  // internally maintained in radians
};

} // namespace filter