/// @file lsm6ds3.hpp
/// @brief LSM6DS3TR 6-axis IMU driver via I2C.
/// @details Provides register-level read/write, FIFO management, interrupt routing,
/// motion detection (free-fall, tap, tilt, wake-up), pedometer, and temperature
/// sensor access. Decouples I2C transport (callbacks) from register logic for
/// testability. Operates at 400 kHz via ESP-IDF I2C master bus-device API.
/// @ingroup lsm6ds3

#pragma once

#include <functional>
#include "lsm6ds3_detail.hpp"

namespace sensor {

/// @brief Standalone driver for the LSM6DS3TR 6-axis inertial measurement unit.
class Lsm6ds3 {
public:
    /// @brief Callback: read from a sensor register via I2C.
    using ReadRegCb = std::function<bool(uint8_t reg, uint8_t* data, size_t len)>;
    /// @brief Callback: write to a sensor register via I2C.
    using WriteRegCb = std::function<bool(uint8_t reg, const uint8_t* data, size_t len)>;

    /// @brief Driver configuration binding sensor settings to I2C transport.
    struct Config {
        lsm6ds3::Config imu_config; ///< ODR, full-scale, filter settings.
        ReadRegCb read_cb;          ///< I2C read function.
        WriteRegCb write_cb;        ///< I2C write function.
    };

    /// @brief Interrupt 1 pin routing configuration.
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
        bool drdy_xl{false};        ///< Data-ready for accelerometer.
        bool drdy_g{false};         ///< Data-ready for gyroscope.
        bool drdy_temp{false};      ///< Data-ready for temperature sensor.
        bool fifo_full{false};      ///< FIFO full.
        bool fifo_ovr{false};       ///< FIFO overrun.
        bool fifo_th{false};        ///< FIFO watermark threshold.
        bool step_count_ov{false};  ///< Step counter overflow.
        bool step_delta{false};     ///< Step delta event.
        
        bool tilt{false};           ///< Tilt event.
        bool wakeup{false};         ///< Wake-up event.
        bool free_fall{false};      ///< Free-fall event.
        bool single_tap{false};     ///< Single-tap event.
        bool double_tap{false};     ///< Double-tap event.
        bool six_d{false};          ///< 6D orientation event.
    };

    /// @brief FIFO operation modes.
    enum class FifoMode : uint8_t {
        Bypass = 0b000,              ///< FIFO disabled, direct register access.
        Fifo = 0b001,                ///< Collect data until FIFO full, then stop.
        ContinuousToFifo = 0b011,    ///< Continuous mode, switch to FIFO on trigger.
        BypassToContinuous = 0b100,  ///< Bypass mode, switch to continuous on trigger.
        Continuous = 0b110           ///< Continuous FIFO (oldest data overwritten).
    };

    /// @brief Holds the status of hardware-detected motion events.
    struct MotionEvents {
        bool single_tap{false};     ///< Single-tap detected.
        bool double_tap{false};     ///< Double-tap detected.
        bool wake_up{false};        ///< Wake-up event triggered.
        bool free_fall{false};      ///< Free-fall condition detected.
        bool step_detected{false};  ///< Step detected via pedometer.
        bool tilt_detected{false};  ///< Tilt detected via embedded AWT engine.
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

    /// @brief Reads the on-die temperature sensor
    /// @param out_temp_c Temperature in degrees Celsius
    /// @return true on success, false on I2C failure
    bool read_temp(float& out_temp_c);

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
