#pragma once

#include <cstdint>

namespace sensor::lsm6ds3 {

/// @brief Interface type for the LSM6DS3
enum class Interface : uint8_t {
    I2C = 0,
    SPI = 1,
};

/// @brief Device identification constant for LSM6DS3TR-C
constexpr uint8_t LSM6DS3_ID = 0x6A;

/// @brief Comprehensive core registers for LSM6DS3 mapped from ST's lsm6ds3_reg.h
enum class Register : uint8_t {
    FUNC_CFG_ACCESS      = 0x01,
    SENSOR_SYNC_TIME_FR  = 0x04,
    SENSOR_SYNC_RES_RATIO= 0x05,
    FIFO_CTRL1           = 0x06,
    FIFO_CTRL2           = 0x07,
    FIFO_CTRL3           = 0x08,
    FIFO_CTRL4           = 0x09,
    FIFO_CTRL5           = 0x0A,
    ORIENT_CFG_G         = 0x0B,
    INT1_CTRL            = 0x0D,
    INT2_CTRL            = 0x0E,
    WHO_AM_I             = 0x0F,
    CTRL1_XL             = 0x10,
    CTRL2_G              = 0x11,
    CTRL3_C              = 0x12,
    CTRL4_C              = 0x13,
    CTRL5_C              = 0x14,
    CTRL6_C              = 0x15,
    CTRL7_G              = 0x16,
    CTRL8_XL             = 0x17,
    CTRL9_XL             = 0x18,
    CTRL10_C             = 0x19,
    MASTER_CONFIG        = 0x1A,
    WAKE_UP_SRC          = 0x1B,
    TAP_SRC              = 0x1C,
    D6D_SRC              = 0x1D,
    STATUS_REG           = 0x1E,
    OUT_TEMP_L           = 0x20,
    OUT_TEMP_H           = 0x21,
    OUTX_L_G             = 0x22,
    OUTX_H_G             = 0x23,
    OUTY_L_G             = 0x24,
    OUTY_H_G             = 0x25,
    OUTZ_L_G             = 0x26,
    OUTZ_H_G             = 0x27,
    OUTX_L_XL            = 0x28,
    OUTX_H_XL            = 0x29,
    OUTY_L_XL            = 0x2A,
    OUTY_H_XL            = 0x2B,
    OUTZ_L_XL            = 0x2C,
    OUTZ_H_XL            = 0x2D,
    SENSORHUB1_REG       = 0x2E,
    SENSORHUB2_REG       = 0x2F,
    SENSORHUB3_REG       = 0x30,
    SENSORHUB4_REG       = 0x31,
    SENSORHUB5_REG       = 0x32,
    SENSORHUB6_REG       = 0x33,
    SENSORHUB7_REG       = 0x34,
    SENSORHUB8_REG       = 0x35,
    SENSORHUB9_REG       = 0x36,
    SENSORHUB10_REG      = 0x37,
    SENSORHUB11_REG      = 0x38,
    SENSORHUB12_REG      = 0x39,
    FIFO_STATUS1         = 0x3A,
    FIFO_STATUS2         = 0x3B,
    FIFO_STATUS3         = 0x3C,
    FIFO_STATUS4         = 0x3D,
    FIFO_DATA_OUT_L      = 0x3E,
    FIFO_DATA_OUT_H      = 0x3F,
    TIMESTAMP0_REG       = 0x40,
    TIMESTAMP1_REG       = 0x41,
    TIMESTAMP2_REG       = 0x42,
    STEP_TIMESTAMP_L     = 0x49,
    STEP_TIMESTAMP_H     = 0x4A,
    STEP_COUNTER_L       = 0x4B,
    STEP_COUNTER_H       = 0x4C,
    SENSORHUB13_REG      = 0x4D,
    SENSORHUB14_REG      = 0x4E,
    SENSORHUB15_REG      = 0x4F,
    SENSORHUB16_REG      = 0x50,
    SENSORHUB17_REG      = 0x51,
    SENSORHUB18_REG      = 0x52,
    FUNC_SRC1            = 0x53,
    FUNC_SRC2            = 0x54,
    WRIST_TILT_IA        = 0x55,
    TAP_CFG              = 0x58,
    TAP_THS_6D           = 0x59,
    INT_DUR2             = 0x5A,
    WAKE_UP_THS          = 0x5B,
    WAKE_UP_DUR          = 0x5C,
    FREE_FALL            = 0x5D,
    MD1_CFG              = 0x5E,
    MD2_CFG              = 0x5F,
    MASTER_CMD_CODE      = 0x60,
    SENS_SYNC_SPI_ERROR_CODE = 0x61,
    OUT_MAG_RAW_X_L      = 0x66,
    OUT_MAG_RAW_X_H      = 0x67,
    OUT_MAG_RAW_Y_L      = 0x68,
    OUT_MAG_RAW_Y_H      = 0x69,
    OUT_MAG_RAW_Z_L      = 0x6A,
    OUT_MAG_RAW_Z_H      = 0x6B,
    INT_OIS              = 0x6F,
    CTRL1_OIS            = 0x70,
    CTRL2_OIS            = 0x71,
    CTRL3_OIS            = 0x72,
    X_OFS_USR            = 0x73,
    Y_OFS_USR            = 0x74,
    Z_OFS_USR            = 0x75
};

/// @brief Accelerometer Full Scale range (Aligned with ST lsm6ds3_reg.h)
enum class AccelRange : uint8_t {
    RANGE_2G  = 0x00, // 0b00
    RANGE_16G = 0x01, // 0b01
    RANGE_4G  = 0x02, // 0b10
    RANGE_8G  = 0x03  // 0b11
};

/// @brief Gyroscope Full Scale range (Aligned with ST lsm6ds3_reg.h)
enum class GyroRange : uint8_t {
    DPS_250  = 0x00, // FS_G = 0b00
    DPS_500  = 0x01, // FS_G = 0b01
    DPS_1000 = 0x02, // FS_G = 0b10
    DPS_2000 = 0x03, // FS_G = 0b11
    DPS_125  = 0x04  // Special flag for 125 dps (Bit 1 in CTRL2_G)
};

/// @brief Output Data Rate for Accelerometer
enum class AccelOdr : uint8_t {
    POWER_DOWN = 0b0000,
    ODR_12_5Hz = 0b0001,
    ODR_26Hz   = 0b0010,
    ODR_52Hz   = 0b0011,
    ODR_104Hz  = 0b0100,
    ODR_208Hz  = 0b0101,
    ODR_416Hz  = 0b0110,
    ODR_833Hz  = 0b0111,
    ODR_1_66kHz= 0b1000,
    ODR_3_33kHz= 0b1001,
    ODR_6_66kHz= 0b1010
};

/// @brief Output Data Rate for Gyroscope
enum class GyroOdr : uint8_t {
    POWER_DOWN = 0b0000,
    ODR_12_5Hz = 0b0001,
    ODR_26Hz   = 0b0010,
    ODR_52Hz   = 0b0011,
    ODR_104Hz  = 0b0100,
    ODR_208Hz  = 0b0101,
    ODR_416Hz  = 0b0110,
    ODR_833Hz  = 0b0111,
    ODR_1_66kHz= 0b1000
};

/// @brief 3-Axis Data Structures
struct RawValue {
    int16_t x{0};
    int16_t y{0};
    int16_t z{0};
};

struct Value {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
};

/// @brief Configuration structure passed to the driver
struct Config {
    AccelRange accel_range{AccelRange::RANGE_2G};
    AccelOdr accel_odr{AccelOdr::ODR_104Hz};
    GyroRange gyro_range{GyroRange::DPS_250};
    GyroOdr gyro_odr{GyroOdr::ODR_104Hz};
};

// --- Compile-Time Sensitivities (g/LSB and dps/LSB) ---
// Based on ST LSM6DS3TR-C Datasheet Mechanical Characteristics
static constexpr float accel_range_to_sensitivity(const AccelRange& range) {
    switch (range) {
        case AccelRange::RANGE_2G:  return 0.061f / 1000.0f;
        case AccelRange::RANGE_4G:  return 0.122f / 1000.0f;
        case AccelRange::RANGE_8G:  return 0.244f / 1000.0f;
        case AccelRange::RANGE_16G: return 0.488f / 1000.0f;
        default:                    return 0.061f / 1000.0f;
    }
}

static constexpr float gyro_range_to_sensitivity(const GyroRange& range) {
    switch (range) {
        case GyroRange::DPS_125:  return 4.375f / 1000.0f;
        case GyroRange::DPS_250:  return 8.750f / 1000.0f;
        case GyroRange::DPS_500:  return 17.50f / 1000.0f;
        case GyroRange::DPS_1000: return 35.00f / 1000.0f;
        case GyroRange::DPS_2000: return 70.00f / 1000.0f;
        default:                  return 8.750f / 1000.0f;
    }
}

} // namespace sensor::lsm6ds3