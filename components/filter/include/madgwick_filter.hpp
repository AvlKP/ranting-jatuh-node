#pragma once

#include <span>
#include <array>
#include "filter_math.hpp"

namespace filter {

/// @brief Madgwick filter for IMU data (AHRS).
class Madgwick {
public:
    /// @brief Constructor
    /// @param beta Filter gain (gradient descent step size)
    explicit Madgwick(float beta = 0.1f) noexcept
        : beta_{beta}, q_{1.0f, 0.0f, 0.0f, 0.0f} {}

    /// @brief 6-DOF Update (Accel + Gyro)
    /// @param accel Acceleration span [x,y,z] in g's or m/s^2
    /// @param gyro Gyroscope span [x,y,z] in rad/s
    /// @param dt Time delta in seconds
    void update(std::span<const float, 3> accel, std::span<const float, 3> gyro, float dt) noexcept {
        float ax = accel[0], ay = accel[1], az = accel[2];
        float gx = gyro[0],  gy = gyro[1],  gz = gyro[2];

        // Rate of change of quaternion from gyroscope
        float qDot1 = 0.5f * (-q_[1] * gx - q_[2] * gy - q_[3] * gz);
        float qDot2 = 0.5f * ( q_[0] * gx + q_[2] * gz - q_[3] * gy);
        float qDot3 = 0.5f * ( q_[0] * gy - q_[1] * gz + q_[3] * gx);
        float qDot4 = 0.5f * ( q_[0] * gz + q_[1] * gy - q_[2] * gx);

        // Compute feedback only if accelerometer measurement valid (avoids NaN in accel normalisation)
        if (!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {
            float recipNorm = math::fast_inv_sqrt(ax * ax + ay * ay + az * az);
            ax *= recipNorm;
            ay *= recipNorm;
            az *= recipNorm;

            // Auxiliary variables to avoid repeated arithmetic
            const float _2q0 = 2.0f * q_[0];
            const float _2q1 = 2.0f * q_[1];
            const float _2q2 = 2.0f * q_[2];
            const float _2q3 = 2.0f * q_[3];
            const float _4q0 = 4.0f * q_[0];
            const float _4q1 = 4.0f * q_[1];
            const float _4q2 = 4.0f * q_[2];
            const float _8q1 = 8.0f * q_[1];
            const float _8q2 = 8.0f * q_[2];
            const float q0q0 = q_[0] * q_[0];
            const float q1q1 = q_[1] * q_[1];
            const float q2q2 = q_[2] * q_[2];
            const float q3q3 = q_[3] * q_[3];

            // Gradient descent algorithm corrective step
            float s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
            float s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * q_[1] - _2q0 * ay - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
            float s2 = 4.0f * q0q0 * q_[2] + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
            float s3 = 4.0f * q1q1 * q_[3] - _2q1 * ax + 4.0f * q2q2 * q_[3] - _2q2 * ay;
            
            recipNorm = math::fast_inv_sqrt(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3); 
            s0 *= recipNorm;
            s1 *= recipNorm;
            s2 *= recipNorm;
            s3 *= recipNorm;

            // Apply feedback step
            qDot1 -= beta_ * s0;
            qDot2 -= beta_ * s1;
            qDot3 -= beta_ * s2;
            qDot4 -= beta_ * s3;
        }

        // Integrate
        q_[0] += qDot1 * dt;
        q_[1] += qDot2 * dt;
        q_[2] += qDot3 * dt;
        q_[3] += qDot4 * dt;

        // Normalise quaternion
        float recipNorm = math::fast_inv_sqrt(q_[0]*q_[0] + q_[1]*q_[1] + q_[2]*q_[2] + q_[3]*q_[3]);
        q_[0] *= recipNorm;
        q_[1] *= recipNorm;
        q_[2] *= recipNorm;
        q_[3] *= recipNorm;
    }

    [[nodiscard]] std::array<float, 4> quaternion() const noexcept { return q_; }

    [[nodiscard]] std::array<float, 3> euler_angles_deg() const noexcept {
        std::array<float, 3> euler{};
        // Roll
        euler[0] = math::rad2deg(std::atan2(2.0f * (q_[0]*q_[1] + q_[2]*q_[3]), 1.0f - 2.0f*(q_[1]*q_[1] + q_[2]*q_[2])));
        // Pitch
        euler[1] = math::rad2deg(std::asin(2.0f * (q_[0]*q_[2] - q_[3]*q_[1])));
        // Yaw
        euler[2] = math::rad2deg(std::atan2(2.0f * (q_[0]*q_[3] + q_[1]*q_[2]), 1.0f - 2.0f*(q_[2]*q_[2] + q_[3]*q_[3])));
        return euler;
    }

private:
    float beta_;
    std::array<float, 4> q_;
};

} // namespace filter