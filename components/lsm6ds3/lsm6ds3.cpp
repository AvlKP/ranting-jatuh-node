#include "lsm6ds3.hpp"

namespace sensor {

Lsm6ds3::Lsm6ds3(const Config& config) : config_(config) {
    accel_sensitivity_ = lsm6ds3::accel_range_to_sensitivity(config_.imu_config.accel_range);
    gyro_sensitivity_ = lsm6ds3::gyro_range_to_sensitivity(config_.imu_config.gyro_range);
}

bool Lsm6ds3::init() {
    if (!config_.read_cb || !config_.write_cb) return false;

    uint8_t device_id = 0;
    if (!config_.read_cb(static_cast<uint8_t>(lsm6ds3::Register::WHO_AM_I), &device_id, 1)) return false;
    
    // 0x6A for LSM6DS3TR-C, 0x69 for standard LSM6DS3. 
    if (device_id != lsm6ds3::LSM6DS3_ID && device_id != 0x69) return false; 

    if (!write_register(lsm6ds3::Register::CTRL3_C, 0x01)) return false;
    // Host should wait ~15ms after this in reality for SW Reset

    if (!write_register(lsm6ds3::Register::CTRL3_C, 0x44)) return false;

    // Shift Accel Range into FS_XL bits [3:2]
    uint8_t ctrl1_xl = (static_cast<uint8_t>(config_.imu_config.accel_odr) << 4) | 
                       (static_cast<uint8_t>(config_.imu_config.accel_range) << 2);
    if (!write_register(lsm6ds3::Register::CTRL1_XL, ctrl1_xl)) return false;

    // Shift Gyro Range into FS_G bits [3:2] or trigger the 125DPS bit [1]
    uint8_t gyro_fs_bits = static_cast<uint8_t>(config_.imu_config.gyro_range);
    uint8_t ctrl2_g = (static_cast<uint8_t>(config_.imu_config.gyro_odr) << 4);
    if (config_.imu_config.gyro_range == lsm6ds3::GyroRange::DPS_125) {
        ctrl2_g |= 0x02; 
    } else {
        ctrl2_g |= (gyro_fs_bits << 2); 
    }
    return write_register(lsm6ds3::Register::CTRL2_G, ctrl2_g);
}

// --- 1. Interrupt Routing ---
bool Lsm6ds3::configure_int1(const Int1Config& cfg) {
    uint8_t int1_ctrl = 0; // INT1_CTRL (0x0D)
    if (cfg.drdy_xl) int1_ctrl |= (1 << 0);
    if (cfg.drdy_g) int1_ctrl |= (1 << 1);
    if (cfg.fifo_th) int1_ctrl |= (1 << 3);
    if (cfg.fifo_ovr) int1_ctrl |= (1 << 4);
    if (cfg.fifo_full) int1_ctrl |= (1 << 5);
    if (cfg.step_detector) int1_ctrl |= (1 << 7);
    if (!write_register(lsm6ds3::Register::INT1_CTRL, int1_ctrl)) return false;

    uint8_t md1_cfg = 0; // MD1_CFG (0x5E)
    if (cfg.tilt) md1_cfg |= (1 << 1);
    if (cfg.six_d) md1_cfg |= (1 << 2);
    if (cfg.double_tap) md1_cfg |= (1 << 3);
    if (cfg.free_fall) md1_cfg |= (1 << 4);
    if (cfg.wakeup) md1_cfg |= (1 << 5);
    if (cfg.single_tap) md1_cfg |= (1 << 6);
    return write_register(lsm6ds3::Register::MD1_CFG, md1_cfg);
}

bool Lsm6ds3::configure_int2(const Int2Config& cfg) {
    uint8_t int2_ctrl = 0; // INT2_CTRL (0x0E)
    if (cfg.drdy_xl) int2_ctrl |= (1 << 0);
    if (cfg.drdy_g) int2_ctrl |= (1 << 1);
    if (cfg.drdy_temp) int2_ctrl |= (1 << 2);
    if (cfg.fifo_full) int2_ctrl |= (1 << 3);
    if (cfg.fifo_ovr) int2_ctrl |= (1 << 4);
    if (cfg.fifo_th) int2_ctrl |= (1 << 5);
    if (cfg.step_count_ov) int2_ctrl |= (1 << 6);
    if (cfg.step_delta) int2_ctrl |= (1 << 7);
    if (!write_register(lsm6ds3::Register::INT2_CTRL, int2_ctrl)) return false;

    uint8_t md2_cfg = 0; // MD2_CFG (0x5F)
    if (cfg.tilt) md2_cfg |= (1 << 1);
    if (cfg.six_d) md2_cfg |= (1 << 2);
    if (cfg.double_tap) md2_cfg |= (1 << 3);
    if (cfg.free_fall) md2_cfg |= (1 << 4);
    if (cfg.wakeup) md2_cfg |= (1 << 5);
    if (cfg.single_tap) md2_cfg |= (1 << 6);
    return write_register(lsm6ds3::Register::MD2_CFG, md2_cfg);
}

// --- 2. FIFO Management ---
bool Lsm6ds3::configure_fifo(FifoMode mode, uint16_t watermark_threshold) {
    if (!write_register(lsm6ds3::Register::FIFO_CTRL1, watermark_threshold & 0xFF)) return false;
    if (!write_register(lsm6ds3::Register::FIFO_CTRL2, (watermark_threshold >> 8) & 0x0F)) return false;
    
    // DEC_FIFO_XL = 001 (No dec.), DEC_FIFO_G = 001 (No dec.) => 0x09
    if (!write_register(lsm6ds3::Register::FIFO_CTRL3, 0x09)) return false; 
    
    uint8_t ctrl5 = (0x04 << 3) | static_cast<uint8_t>(mode); // 104Hz FIFO ODR
    return write_register(lsm6ds3::Register::FIFO_CTRL5, ctrl5);
}

uint16_t Lsm6ds3::get_fifo_unread_words() {
    uint8_t data[2] = {0};
    if (!config_.read_cb(static_cast<uint8_t>(lsm6ds3::Register::FIFO_STATUS1), data, 2)) return 0;
    return (data[0] | ((data[1] & 0x0F) << 8));
}

bool Lsm6ds3::read_fifo_dataset(lsm6ds3::Value& out_gyro, lsm6ds3::Value& out_accel) {
    uint8_t data[12];
    if (!config_.read_cb(static_cast<uint8_t>(lsm6ds3::Register::FIFO_DATA_OUT_L), data, sizeof(data))) return false;

    int16_t gx = static_cast<int16_t>((data[1] << 8) | data[0]);
    int16_t gy = static_cast<int16_t>((data[3] << 8) | data[2]);
    int16_t gz = static_cast<int16_t>((data[5] << 8) | data[4]);
    
    int16_t ax = static_cast<int16_t>((data[7] << 8) | data[6]);
    int16_t ay = static_cast<int16_t>((data[9] << 8) | data[8]);
    int16_t az = static_cast<int16_t>((data[11] << 8) | data[10]);

    out_gyro = { gx * gyro_sensitivity_, gy * gyro_sensitivity_, gz * gyro_sensitivity_ };
    out_accel = { ax * accel_sensitivity_, ay * accel_sensitivity_, az * accel_sensitivity_ };
    
    return true;
}

bool Lsm6ds3::read_accel_gyro(lsm6ds3::Value& out_gyro, lsm6ds3::Value& out_accel) {
    uint8_t data[12];
    if (!config_.read_cb(static_cast<uint8_t>(lsm6ds3::Register::OUTX_L_G), data, sizeof(data))) {
        return false;
    }

    int16_t gx = static_cast<int16_t>((data[1] << 8) | data[0]);
    int16_t gy = static_cast<int16_t>((data[3] << 8) | data[2]);
    int16_t gz = static_cast<int16_t>((data[5] << 8) | data[4]);

    int16_t ax = static_cast<int16_t>((data[7] << 8) | data[6]);
    int16_t ay = static_cast<int16_t>((data[9] << 8) | data[8]);
    int16_t az = static_cast<int16_t>((data[11] << 8) | data[10]);

    out_gyro = { gx * gyro_sensitivity_, gy * gyro_sensitivity_, gz * gyro_sensitivity_ };
    out_accel = { ax * accel_sensitivity_, ay * accel_sensitivity_, az * accel_sensitivity_ };

    return true;
}

// --- 3. Motion Detection ---
bool Lsm6ds3::configure_motion_detection(uint8_t tap_ths, uint8_t wakeup_ths, uint8_t freefall_ths,
                                        uint8_t ff_dur) {
    if (!write_register(lsm6ds3::Register::TAP_CFG, 0x8E)) return false; // Enable interrupts and XYZ tap
    if (!write_register(lsm6ds3::Register::TAP_THS_6D, tap_ths & 0x1F)) return false;
    if (!write_register(lsm6ds3::Register::WAKE_UP_THS, wakeup_ths & 0x3F)) return false;

    uint8_t freefall = (freefall_ths & 0x07) | ((ff_dur & 0x1F) << 3);
    if (!write_register(lsm6ds3::Register::FREE_FALL, freefall)) return false;
    return true;
}

// --- 4. Tilt & Embedded Pedometer (AWT) ---
bool Lsm6ds3::enable_pedometer_and_tilt() {
    uint8_t ctrl10_c = 0; 
    if (!config_.read_cb(static_cast<uint8_t>(lsm6ds3::Register::CTRL10_C), &ctrl10_c, 1)) return false;
    ctrl10_c |= (1 << 2) | (1 << 3) | (1 << 4); // FUNC_EN, TILT_EN, PEDO_EN
    return write_register(lsm6ds3::Register::CTRL10_C, ctrl10_c);
}

uint16_t Lsm6ds3::get_step_count() {
    uint8_t data[2] = {0};
    if (!config_.read_cb(static_cast<uint8_t>(lsm6ds3::Register::STEP_COUNTER_L), data, 2)) return 0; 
    return (data[0] | (data[1] << 8));
}

Lsm6ds3::MotionEvents Lsm6ds3::get_motion_events() {
    MotionEvents events;
    uint8_t wake_src = 0, tap_src = 0, func_src = 0;
    
    config_.read_cb(static_cast<uint8_t>(lsm6ds3::Register::WAKE_UP_SRC), &wake_src, 1);
    config_.read_cb(static_cast<uint8_t>(lsm6ds3::Register::TAP_SRC), &tap_src, 1);
    config_.read_cb(static_cast<uint8_t>(lsm6ds3::Register::FUNC_SRC1), &func_src, 1);

    events.single_tap = (tap_src & 0x20) != 0;
    events.double_tap = (tap_src & 0x10) != 0;
    events.wake_up    = (wake_src & 0x08) != 0;
    events.free_fall  = (wake_src & 0x20) != 0;
    events.tilt_detected = (func_src & 0x20) != 0;
    events.step_detected = (func_src & 0x10) != 0;

    return events;
}

bool Lsm6ds3::write_register(lsm6ds3::Register reg, uint8_t value) {
    return write_register(static_cast<uint8_t>(reg), value);
}

bool Lsm6ds3::write_register(uint8_t reg, uint8_t value) {
    return config_.write_cb(reg, &value, 1);
}

} // namespace sensor
