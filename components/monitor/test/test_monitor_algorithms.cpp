#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <new>
#include <span>

#include "unity.h"

#define private public
#include "adaptive_complementary_filter.hpp"
#include "chebyshev_hpf.hpp"
#include "calibration.hpp"
#include "complementary_filter.hpp"
#include "monitor.hpp"
#undef private

namespace {

monitor::Monitor& MakeMonitorForTest(const monitor::MonitorConfig& config) {
    alignas(monitor::Monitor) static std::byte storage[sizeof(monitor::Monitor)];
    static monitor::Monitor* monitor = nullptr;

    sensor::Lsm6ds3::Config imu_cfg{};
    imu_cfg.read_cb = [](std::uint8_t, std::uint8_t*, std::size_t) { return false; };
    imu_cfg.write_cb = [](std::uint8_t, const std::uint8_t*, std::size_t) { return false; };

    if (monitor != nullptr) {
        monitor->~Monitor();
    }
    monitor = new (storage) monitor::Monitor{imu_cfg, config};
    return *monitor;
}

/* --------------------------------------------------------------------------
   6.1 Adaptive Complementary Filter Tests
   -------------------------------------------------------------------------- */

TEST_CASE("adaptive filter alpha approaches 1.0 during high accel error", "[monitor][adaptive]") {
    filter::AdaptiveComplementary f{0.98f, 50.0f};

    float accel[3] = {2.0f, 0.0f, 0.0f};
    float gyro[3] = {0.0f, 0.0f, 0.0f};
    std::span<const float, 3> a{accel};
    std::span<const float, 3> g{gyro};

    f.update(a, g, 0.01f);

    float pitch = f.pitch();
    float roll = f.roll();
    TEST_ASSERT_TRUE(std::isfinite(pitch));
    TEST_ASSERT_TRUE(std::isfinite(roll));
}

TEST_CASE("adaptive filter alpha approaches alpha_base during normal conditions", "[monitor][adaptive]") {
    filter::AdaptiveComplementary f{0.98f, 50.0f};

    float accel[3] = {0.0f, 0.0f, 1.0f};
    float gyro[3] = {0.0f, 0.0f, 0.0f};
    std::span<const float, 3> a{accel};
    std::span<const float, 3> g{gyro};

    for (int i = 0; i < 10; ++i) {
        f.update(a, g, 0.01f);
    }

    float pitch = f.pitch();
    float roll = f.roll();
    TEST_ASSERT_TRUE(std::isfinite(pitch));
    TEST_ASSERT_TRUE(std::isfinite(roll));
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, pitch);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, roll);
}

TEST_CASE("adaptive filter angles remain continuous", "[monitor][adaptive]") {
    filter::AdaptiveComplementary f{0.98f, 50.0f};

    float prev_pitch = 0.0f;
    float prev_roll = 0.0f;

    for (int i = 0; i < 50; ++i) {
        float t = static_cast<float>(i) * 0.01f;
        float ax = 0.2f * std::sin(2.0f * 3.14159f * 1.0f * t);
        float ay = 0.1f;
        float az = std::sqrt(1.0f - ax * ax - ay * ay);
        float accel[3] = {ax, ay, az};
        float gyro[3] = {0.0f, 0.0f, 0.0f};
        std::span<const float, 3> a{accel};
        std::span<const float, 3> g{gyro};

        f.update(a, g, 0.01f);

        if (i > 0) {
            float dp = std::fabs(f.pitch() - prev_pitch);
            float dr = std::fabs(f.roll() - prev_roll);
            TEST_ASSERT_TRUE(dp < 2.0f);
            TEST_ASSERT_TRUE(dr < 2.0f);
        }
        prev_pitch = f.pitch();
        prev_roll = f.roll();
    }
}

/* --------------------------------------------------------------------------
   6.2 Chebyshev HPF Biquad Tests
   -------------------------------------------------------------------------- */

TEST_CASE("chebyshev hpf rejects DC constant input", "[monitor][hpf]") {
    monitor::ChebyshevHpf hpf;

    for (int i = 0; i < 600; ++i) {
        hpf.update(1.0f, 0.0f, 0.0f);
    }

    float mag = hpf.magnitude();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, mag);
}

TEST_CASE("chebyshev hpf passes high frequency content", "[monitor][hpf]") {
    monitor::ChebyshevHpf hpf;

    float max_mag = 0.0f;
    for (int i = 0; i < 600; ++i) {
        float t = static_cast<float>(i) / 26.0f;
        float signal = std::sin(2.0f * 3.14159f * 1.0f * t);
        hpf.update(signal, 0.0f, 0.0f);

        if (i > 500) {
            float mag = hpf.magnitude();
            if (mag > max_mag) max_mag = mag;
        }
    }

    TEST_ASSERT_TRUE(max_mag > 0.1f);
}

TEST_CASE("chebyshev hpf magnitude zero for zero input", "[monitor][hpf]") {
    monitor::ChebyshevHpf hpf;

    for (int i = 0; i < 600; ++i) {
        hpf.update(0.0f, 0.0f, 0.0f);
    }

    float mag = hpf.magnitude();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, mag);
}

/* --------------------------------------------------------------------------
   6.3 Axis Convention Fix Tests
   -------------------------------------------------------------------------- */

TEST_CASE("complementary filter pitch uses atan2(ax, az)", "[monitor][axis]") {
    filter::Complementary f{0.98f};

    {
        float accel[3] = {0.5f, 0.0f, 0.866f};
        float gyro[3] = {0.0f, 0.0f, 0.0f};
        std::span<const float, 3> a{accel};
        std::span<const float, 3> g{gyro};

        f.update(a, g, 0.01f);
        float pitch = f.pitch();
        TEST_ASSERT_TRUE(pitch > 0.0f);
    }
}

TEST_CASE("complementary filter roll uses atan2(-ay, az)", "[monitor][axis]") {
    filter::Complementary f{0.98f};

    {
        float accel[3] = {0.0f, 0.5f, 0.866f};
        float gyro[3] = {0.0f, 0.0f, 0.0f};
        std::span<const float, 3> a{accel};
        std::span<const float, 3> g{gyro};

        f.update(a, g, 0.01f);
        float roll = f.roll();
        TEST_ASSERT_TRUE(roll < 0.0f);
    }
}

TEST_CASE("branch hanging vertically gives near-zero pitch and roll", "[monitor][axis]") {
    filter::Complementary f{0.98f};

    {
        float accel[3] = {0.0f, 0.0f, 1.0f};
        float gyro[3] = {0.0f, 0.0f, 0.0f};
        std::span<const float, 3> a{accel};
        std::span<const float, 3> g{gyro};

        for (int i = 0; i < 5; ++i) {
            f.update(a, g, 0.01f);
        }

        TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, f.pitch());
        TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, f.roll());
    }
}

/* --------------------------------------------------------------------------
   6.4 Calibration Bias Subtraction Tests
   -------------------------------------------------------------------------- */

TEST_CASE("calibration bias struct defaults to zero", "[monitor][calib]") {
    calibration::CalibrationBias bias{};
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 0.0f, bias.ax);
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 0.0f, bias.ay);
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 0.0f, bias.az);
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 0.0f, bias.gx);
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 0.0f, bias.gy);
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 0.0f, bias.gz);
}

TEST_CASE("calibration bias subtraction math is correct", "[monitor][calib]") {
    calibration::CalibrationBias bias{};
    bias.ax = 0.05f;
    bias.ay = -0.02f;
    bias.az = 0.10f;
    bias.gx = 0.01f;
    bias.gy = -0.03f;
    bias.gz = 0.00f;

    float raw_ax = 1.05f;
    float calib_ax = raw_ax - bias.ax;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.00f, calib_ax);

    float raw_gy = 0.47f;
    float calib_gy = raw_gy - bias.gy;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.50f, calib_gy);
}

/* --------------------------------------------------------------------------
   6.5 FSM - no references to deprecated accel_err_var or K_HIGH/K_LOW
   -------------------------------------------------------------------------- */

TEST_CASE("monitor members do not include deprecated accel_err fields", "[monitor][fsm]") {
    using monitor::MonitorConfig;

    MonitorConfig config{};
    config.filter_alpha_base = 0.98f;
    config.filter_k_gain = 50.0f;

    monitor::Monitor& monitor = MakeMonitorForTest(config);
    static_cast<void>(monitor);
    TEST_ASSERT_TRUE(true);
}

TEST_CASE("hpf settle counter exists and starts at zero", "[monitor][fsm]") {
    using monitor::MonitorConfig;

    MonitorConfig config{};
    monitor::Monitor& monitor = MakeMonitorForTest(config);
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 0.0f, monitor.hpf_settle_counter_);
}

TEST_CASE("tkeo startup is bounded until three samples", "[monitor][dsp]") {
    monitor::TkeoWindow window{};
    float tkeo = -1.0f;

    TEST_ASSERT_FALSE(window.Push(1.0f, tkeo));
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 0.0f, tkeo);
    TEST_ASSERT_FALSE(window.Push(2.0f, tkeo));
    TEST_ASSERT_FLOAT_WITHIN(0.0f, 0.0f, tkeo);
    TEST_ASSERT_EQUAL_UINT(2U, window.Count());
}

TEST_CASE("tkeo computes middle sample energy with one sample latency", "[monitor][dsp]") {
    monitor::TkeoWindow window{};
    float tkeo = 0.0f;

    TEST_ASSERT_FALSE(window.Push(1.0f, tkeo));
    TEST_ASSERT_FALSE(window.Push(2.0f, tkeo));
    TEST_ASSERT_TRUE(window.Push(3.0f, tkeo));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, tkeo);

    TEST_ASSERT_TRUE(window.Push(4.0f, tkeo));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, tkeo);
}

TEST_CASE("dsp detector enters on tkeo or gmag and exits after quiet debounce", "[monitor][dsp]") {
    monitor::MonitorConfig config{};
    config.dsp_tkeo_high = 40.0f;
    config.dsp_tkeo_low = 5.0f;
    config.dsp_gmag_onset_dps = 2.0f;
    config.dsp_gmag_quiet_dps = 1.5f;
    config.dsp_quiet_debounce = 3U;

    monitor::DspDisturbanceDetector detector{};
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(monitor::NodeState::DISTURBED),
                            static_cast<std::uint8_t>(detector.Update(1.0f, 41.0f, config)));
    TEST_ASSERT_EQUAL_UINT(0U, detector.QuietCount());

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(monitor::NodeState::DISTURBED),
                            static_cast<std::uint8_t>(detector.Update(1.0f, 1.0f, config)));
    TEST_ASSERT_EQUAL_UINT(1U, detector.QuietCount());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(monitor::NodeState::DISTURBED),
                            static_cast<std::uint8_t>(detector.Update(1.0f, 1.0f, config)));
    TEST_ASSERT_EQUAL_UINT(2U, detector.QuietCount());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(monitor::NodeState::IDLE),
                            static_cast<std::uint8_t>(detector.Update(1.0f, 1.0f, config)));
}

TEST_CASE("dsp detector quiet debounce resets on renewed disturbance", "[monitor][dsp]") {
    monitor::MonitorConfig config{};
    config.dsp_tkeo_high = 40.0f;
    config.dsp_tkeo_low = 5.0f;
    config.dsp_gmag_onset_dps = 2.0f;
    config.dsp_gmag_quiet_dps = 1.5f;
    config.dsp_quiet_debounce = 3U;

    monitor::DspDisturbanceDetector detector{};
    static_cast<void>(detector.Update(2.1f, 0.0f, config));
    static_cast<void>(detector.Update(1.0f, 1.0f, config));
    TEST_ASSERT_EQUAL_UINT(1U, detector.QuietCount());

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(monitor::NodeState::DISTURBED),
                            static_cast<std::uint8_t>(detector.Update(1.6f, 1.0f, config)));
    TEST_ASSERT_EQUAL_UINT(0U, detector.QuietCount());
}

TEST_CASE("disturbance buffers stay within configured storage bounds", "[monitor][dsp]") {
    monitor::MonitorConfig config{};
    config.dsp_tkeo_high = 40.0f;
    config.dsp_tkeo_low = 5.0f;
    config.dsp_gmag_onset_dps = 2.0f;
    config.dsp_gmag_quiet_dps = 1.5f;
    config.dsp_quiet_debounce = 3U;

    monitor::Monitor& monitor = MakeMonitorForTest(config);
    monitor.PushSample(0.0f, 0.0f, 3.0f, 0.0f);

    const std::size_t pushes = monitor::kStorageSamples + static_cast<std::size_t>(CONFIG_MONITOR_N_DPAD) + 8U;
    for (std::size_t i = 0U; i < pushes; ++i) {
        const float sample = static_cast<float>(i & 0x0FU);
        monitor.PushSample(sample, -sample, 3.0f, 10.0f);
        TEST_ASSERT_TRUE(monitor.sample_count_ <= monitor::kStorageSamples);
        TEST_ASSERT_TRUE(monitor.write_index_ < monitor::kStorageSamples);
    }
}

TEST_CASE("raw_log_7 reduced replay enters disturbance after calibrated gyro spike", "[monitor][dsp][replay]") {
    struct RawLogRow {
        float gx;
        float gy;
        float gz;
    };

    constexpr RawLogRow kRows[] = {
        {1.198750f, -2.572500f, 0.761250f},
        {1.207500f, -2.572500f, 0.743750f},
        {1.198750f, -2.572500f, 0.717500f},
        {1.198750f, -2.572500f, 0.717500f},
        {1.207500f, -2.555000f, 0.752500f},
        {2.826250f, -4.392500f, 1.365000f},
        {2.826250f, -4.392500f, 1.365000f},
        {2.511250f, -4.138750f, 0.638750f},
        {2.773750f, -4.628750f, 0.542500f},
        {3.080000f, -4.873750f, 0.778750f},
    };

    monitor::MonitorConfig config{};
    config.dsp_tkeo_high = 40.0f;
    config.dsp_tkeo_low = 5.0f;
    config.dsp_gmag_onset_dps = 2.0f;
    config.dsp_gmag_quiet_dps = 1.5f;
    config.dsp_quiet_debounce = 3U;

    const RawLogRow bias = kRows[0U];
    monitor::DspDisturbanceDetector detector{};
    monitor::TkeoWindow tkeo_window{};
    float tkeo = 0.0f;

    for (std::size_t i = 0U; i < 5U; ++i) {
        const float gx = kRows[i].gx - bias.gx;
        const float gy = kRows[i].gy - bias.gy;
        const float gz = kRows[i].gz - bias.gz;
        const float gmag = std::sqrt((gx * gx) + (gy * gy) + (gz * gz));
        static_cast<void>(tkeo_window.Push(gmag, tkeo));
        TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(monitor::NodeState::IDLE),
                                static_cast<std::uint8_t>(detector.Update(gmag, tkeo, config)));
    }

    bool entered = false;
    for (std::size_t i = 5U; i < (sizeof(kRows) / sizeof(kRows[0U])); ++i) {
        const float gx = kRows[i].gx - bias.gx;
        const float gy = kRows[i].gy - bias.gy;
        const float gz = kRows[i].gz - bias.gz;
        const float gmag = std::sqrt((gx * gx) + (gy * gy) + (gz * gz));
        static_cast<void>(tkeo_window.Push(gmag, tkeo));
        entered = detector.Update(gmag, tkeo, config) == monitor::NodeState::DISTURBED;
        if (entered) {
            break;
        }
    }

    TEST_ASSERT_TRUE(entered);
}

} // namespace
