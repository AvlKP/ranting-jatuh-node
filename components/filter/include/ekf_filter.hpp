#pragma once

#include <span>
#include <array>

namespace filter {

/// @brief Wrapper for the ESP-DSP 13-state EKF IMU filter.
/// Utilizing OOP to manage context lifetime and state extraction safely without exceptions.
class EkfImu {
public:
    /// @brief Configuration structure for the EKF IMU filter
    struct Config {
        float process_noise_accel     = 0.1f;
        float process_noise_gyro      = 0.1f;
        float measure_noise_accel     = 0.01f;
        float measure_noise_gyro      = 0.01f;
        float measure_noise_mag       = 0.01f; // if mag is used
    };

    /// @brief Initialize the EKF context with default configuration
    EkfImu();

    /// @brief Initialize the EKF context with custom configuration
    explicit EkfImu(const Config& cfg);

    /// @brief Clean up the EKF context
    ~EkfImu();

    // Delete copy/move to prevent double-free of underlying C-struct context
    EkfImu(const EkfImu&) = delete;
    EkfImu& operator=(const EkfImu&) = delete;
    EkfImu(EkfImu&&) = delete;
    EkfImu& operator=(EkfImu&&) = delete;

    /// @brief Safely initialize and allocate the underlying EKF structures
    /// @return true if allocation and setup succeeded, false otherwise
    [[nodiscard]] bool init() noexcept;

    /// @brief Check if the filter has been successfully initialized
    /// @return true if ready to use
    [[nodiscard]] bool is_initialized() const noexcept { return ctx_ != nullptr; }

    /// @brief EKF Predict step
    /// @param gyro Gyroscope readings [x, y, z] in rad/s
    /// @param dt Time since last predict in seconds
    void predict(std::span<const float, 3> gyro, float dt) noexcept;

    /// @brief EKF Update step using Accelerometer
    /// @param accel Accelerometer readings [x, y, z] in m/s^2
    void update_accel(std::span<const float, 3> accel) noexcept;

    /// @brief Get current 13-state vector
    /// States usually consist of: Position(3), Velocity(3), Quaternion(4), Gyro Bias(3)
    [[nodiscard]] std::array<float, 13> get_states() const noexcept;

    /// @brief Get attitude quaternion [w, x, y, z]
    [[nodiscard]] std::array<float, 4> get_quaternion() const noexcept;

private:
    Config cfg_;
    void* ctx_{nullptr}; // Opaque pointer to esp-dsp ekf_imu13states class
};

} // namespace filter