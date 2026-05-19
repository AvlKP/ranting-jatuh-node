#include "ekf_filter.hpp"
#include "esp_log.h"
#include <new> // Required for std::nothrow

// Include the actual ESP-DSP EKF C++ header
#include "ekf_imu13states.h" 

namespace filter {

static const char* TAG = "Filter_EkfImu";

EkfImu::EkfImu() : EkfImu(Config{}) {}

// Constructor is now trivial and cannot fail
EkfImu::EkfImu(const Config& cfg) : cfg_(cfg), ctx_(nullptr) {}

EkfImu::~EkfImu() {
    if (ctx_ != nullptr) {
        delete static_cast<ekf_imu13states*>(ctx_);
        ctx_ = nullptr;
    }
}

bool EkfImu::init() noexcept {
    if (ctx_ != nullptr) {
        return true; // Already initialized
    }

    // Safely allocate without throwing exceptions
    auto* ekf = new (std::nothrow) ekf_imu13states();
    
    if (ekf == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate EKF 13-state object");
        return false;
    }

    ekf->Init();

    // The process noise matrix Q is an inherited dspm::Mat from the base 'ekf' class.
    // We inject our configuration directly to the diagonals.
    for (int i = 0; i < 3; i++) {
        ekf->Q(i, i) = cfg_.process_noise_accel;     
        ekf->Q(i + 4, i + 4) = cfg_.process_noise_gyro; 
    }
    
    ctx_ = ekf;
    ESP_LOGI(TAG, "EKF IMU 13-States Filter initialized.");
    return true;
}

void EkfImu::predict(std::span<const float, 3> gyro, float dt) noexcept {
    if (is_initialized()) {
        auto* ekf = static_cast<ekf_imu13states*>(ctx_);
        float u[3] = {gyro[0], gyro[1], gyro[2]};
        ekf->Process(u, dt);
    }
}

void EkfImu::update_accel(std::span<const float, 3> accel) noexcept {
    if (is_initialized()) {
        auto* ekf = static_cast<ekf_imu13states*>(ctx_);
        
        // R (measurement noise) is passed directly to the ESP-DSP update function
        // Indices 0-2: Accel covariance
        // Indices 3-5: Mag covariance
        float R[6] = {
            cfg_.measure_noise_accel, cfg_.measure_noise_accel, cfg_.measure_noise_accel,
            cfg_.measure_noise_mag,   cfg_.measure_noise_mag,   cfg_.measure_noise_mag
        };
        
        float z_accel[3] = {accel[0], accel[1], accel[2]};
        float z_mag_dummy[3] = {0.0f, 0.0f, 0.0f}; // Ideally add magnetometer data to signature
        
        ekf->UpdateRefMeasurement(z_accel, z_mag_dummy, R);
    }
}

std::array<float, 13> EkfImu::get_states() const noexcept {
    std::array<float, 13> states{};
    if (is_initialized()) {
        auto* ekf = static_cast<ekf_imu13states*>(ctx_);
        // ESP-DSP stores state in a dspm::Mat named X
        for(int i = 0; i < 13; ++i) {
            states[i] = ekf->X(i, 0);
        }
    }
    return states;
}

std::array<float, 4> EkfImu::get_quaternion() const noexcept {
    std::array<float, 4> quat{1.0f, 0.0f, 0.0f, 0.0f};
    if (is_initialized()) {
        auto* ekf = static_cast<ekf_imu13states*>(ctx_);
        // According to ekf_imu13states.h, states 0..3 are the quaternion
        quat[0] = ekf->X(0, 0);
        quat[1] = ekf->X(1, 0);
        quat[2] = ekf->X(2, 0);
        quat[3] = ekf->X(3, 0);
    }
    return quat;
}

} // namespace filter