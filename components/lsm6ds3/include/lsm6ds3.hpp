#pragma once

#include <functional>
#include "lsm6ds3_detail.hpp"

namespace sensor {

/// @brief Standalone driver class for the LSM6DS3 6-axis IMU
class Lsm6ds3 {
public:
    using ReadRegCb = std::function<bool(uint8_t reg, uint8_t* data, size_t len)>;
    using WriteRegCb = std::function<bool(uint8_t reg, const uint8_t* data, size_t len)>;

    struct Config {
        lsm6ds3::Config imu_config;
        ReadRegCb read_cb;
        WriteRegCb write_cb;
    };

    struct Int1Config {
        bool drdy_xl{false};
        bool drdy_g{false};
        bool fifo_th{false};
        bool fifo_ovr{false};
        bool fifo_full{false};
        bool step_detector{false};
        
        bool tilt{false};
        bool wakeup{false};
        bool free_fall{false};
        bool single_tap{false};
        bool double_tap{false};
        bool six_d{false};
    };

    struct Int2Config {
        bool drdy_xl{false};
        bool drdy_g{false};
        bool drdy_temp{false};
        bool fifo_full{false};
        bool fifo_ovr{false};
        bool fifo_th{false};
        bool step_count_ov{false};
        bool step_delta{false};
        
        bool tilt{false};
        bool wakeup{false};
        bool free_fall{false};
        bool single_tap{false};
        bool double_tap{false};
        bool six_d{false};
    };

    enum class FifoMode : uint8_t {
        Bypass = 0b000,
        Fifo = 0b001,
        ContinuousToFifo = 0b011,
        BypassToContinuous = 0b100,
        Continuous = 0b110
    };

    /// @brief Holds the status of hardware-detected motion events
    struct MotionEvents {
        bool single_tap{false};
        bool double_tap{false};
        bool wake_up{false};
        bool free_fall{false};
        bool step_detected{false};
        bool tilt_detected{false};
    };

    explicit Lsm6ds3(const Config& config);

    /// @brief Initializes the sensor hardware
    bool init();

    // --- 1. Interrupt Routing ---
    bool configure_int1(const Int1Config& cfg);
    bool configure_int2(const Int2Config& cfg);

    // --- 2. FIFO Management ---
    bool configure_fifo(FifoMode mode, uint16_t watermark_threshold);

    uint16_t get_fifo_unread_words();

    /// @brief Burst reads an interleaved Gyro + Accel dataset from the FIFO
    /// @note Requires `configure_fifo` to be set with `0x09` in `CTRL3` (No decimation for both)
    bool read_fifo_dataset(lsm6ds3::Value& out_gyro, lsm6ds3::Value& out_accel);

    /// @brief Reads the latest Gyro + Accel samples from output registers
    bool read_accel_gyro(lsm6ds3::Value& out_gyro, lsm6ds3::Value& out_accel);

    // --- 3. Motion Detection ---
    bool configure_motion_detection(uint8_t tap_ths = 0x09, uint8_t wakeup_ths = 0x02,
                                    uint8_t freefall_ths = 0x03, uint8_t ff_dur = 5);

    // --- 4. Tilt & Embedded Pedometer (AWT) ---
    bool enable_pedometer_and_tilt();

    uint16_t get_step_count();

    /// @brief Checks source registers to determine which motion event triggered
    MotionEvents get_motion_events();

private:
    bool write_register(lsm6ds3::Register reg, uint8_t value);
    bool write_register(uint8_t reg, uint8_t value);

    Config config_;
    float accel_sensitivity_{0.0f};
    float gyro_sensitivity_{0.0f};
};

} // namespace sensor
