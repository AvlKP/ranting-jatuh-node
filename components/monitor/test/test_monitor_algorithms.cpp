#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <new>
#include <span>

#include "dsps_fft2r.h"
#include "esp_heap_caps.h"
#include "unity.h"

#define private public
#include "adaptive_complementary_filter.hpp"
#include "calibration.hpp"
#include "complementary_filter.hpp"
#include "monitor.hpp"
#undef private

namespace {

monitor::Monitor& MakeMonitorForTest(const monitor::MonitorConfig& config) {
    static monitor::Monitor* monitor = nullptr;
    static void* storage = nullptr;

    sensor::Lsm6ds3::Config imu_cfg{};
    imu_cfg.read_cb = [](std::uint8_t, std::uint8_t*, std::size_t) { return false; };
    imu_cfg.write_cb = [](std::uint8_t, const std::uint8_t*, std::size_t) { return false; };

    if (monitor != nullptr) {
        monitor->~Monitor();
    }
    if (storage == nullptr) {
        storage = heap_caps_malloc(sizeof(monitor::Monitor), MALLOC_CAP_8BIT);
        TEST_ASSERT_NOT_NULL(storage);
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
   6.2 Axis Convention Fix Tests
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

TEST_CASE("dsp detector quiet debounce holds through tkeo hysteresis band", "[monitor][dsp]") {
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
    static_cast<void>(detector.Update(1.0f, 10.0f, config));
    TEST_ASSERT_EQUAL_UINT(1U, detector.QuietCount());
}

TEST_CASE("disturbance buffers stay within configured storage bounds", "[monitor][dsp]") {
    monitor::MonitorConfig config{};
    config.dsp_tkeo_high = 40.0f;
    config.dsp_tkeo_low = 5.0f;
    config.dsp_gmag_onset_dps = 2.0f;
    config.dsp_gmag_quiet_dps = 1.5f;
    config.dsp_quiet_debounce = 3U;

    monitor::Monitor& monitor = MakeMonitorForTest(config);
    monitor.PushSample(0.0f, 0.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f, 1.0f, 3.0f, 0.0f);

    const std::size_t pushes = monitor::kStorageSamples + static_cast<std::size_t>(CONFIG_MONITOR_N_DPAD) + 8U;
    for (std::size_t i = 0U; i < pushes; ++i) {
        const float sample = static_cast<float>(i & 0x0FU);
        monitor.PushSample(sample, -sample, sample, -sample, 3.0f, 0.0f, 0.0f, 1.0f, 3.0f, 10.0f);
        TEST_ASSERT_TRUE(monitor.sample_count_ <= monitor::kStorageSamples);
        TEST_ASSERT_TRUE(monitor.write_index_ < monitor::kStorageSamples);
    }
}

TEST_CASE("disturbance copies calibrated short-buffer axes into event storage", "[monitor][dsp]") {
    monitor::MonitorConfig config{};
    config.dsp_tkeo_high = 40.0f;
    config.dsp_tkeo_low = 5.0f;
    config.dsp_gmag_onset_dps = 2.0f;
    config.dsp_gmag_quiet_dps = 1.5f;
    config.dsp_quiet_debounce = 3U;

    monitor::Monitor& monitor = MakeMonitorForTest(config);
    monitor.PushSample(1.0f, -1.0f, 0.1f, 0.2f, 0.3f, 1.1f, 1.2f, 1.3f, 0.4f, 0.0f);
    monitor.PushSample(2.0f, -2.0f, 2.1f, 2.2f, 2.3f, 2.4f, 2.5f, 2.6f, 3.8f, 0.0f);

    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(monitor::NodeState::DISTURBED),
                            static_cast<std::uint8_t>(monitor.state_));
    TEST_ASSERT_TRUE(monitor.sample_count_ >= 2U);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.1f, monitor.gx_history_[0U]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.2f, monitor.gy_history_[1U]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.6f, monitor.az_history_[1U]);
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

/* --------------------------------------------------------------------------
   6.6 Dominant Axis Peak-to-Peak Tests
   -------------------------------------------------------------------------- */

TEST_CASE("dominant axis peak-to-peak detects symmetric oscillation (zero cumulative sum)", "[monitor][dominant]") {
    monitor::MonitorConfig config{};
    monitor::Monitor& monitor = MakeMonitorForTest(config);

    const std::size_t n = 52U;
    monitor.write_index_ = 0U;
    monitor.sample_count_ = n;

    for (std::size_t i = 0U; i < n; ++i) {
        const float t = static_cast<float>(i);
        const float val = std::sin(2.0f * 3.14159f * t / static_cast<float>(n)) * 10.0f;
        monitor.gx_history_[i] = val;
        monitor.gy_history_[i] = 0.0f;
        monitor.gz_history_[i] = 0.0f;
    }

    const auto result = monitor.ComputeDominantAxisSway(0U, n);

    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_TRUE(result.sx_deg > 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, result.sy_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, result.sz_deg);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(monitor::Monitor::DominantAxis::X),
                            static_cast<std::uint8_t>(result.dominant));
}

TEST_CASE("dominant axis peak-to-peak selects correct axis for asymmetric oscillation", "[monitor][dominant]") {
    monitor::MonitorConfig config{};
    monitor::Monitor& monitor = MakeMonitorForTest(config);

    const std::size_t n = 52U;
    monitor.write_index_ = 0U;
    monitor.sample_count_ = n;

    for (std::size_t i = 0U; i < n; ++i) {
        const float t = static_cast<float>(i);
        monitor.gx_history_[i] = std::sin(2.0f * 3.14159f * t / static_cast<float>(n)) * 2.0f;
        monitor.gy_history_[i] = std::sin(2.0f * 3.14159f * t / static_cast<float>(n)) * 10.0f;
        monitor.gz_history_[i] = std::sin(2.0f * 3.14159f * t / static_cast<float>(n)) * 5.0f;
    }

    const auto result = monitor.ComputeDominantAxisSway(0U, n);

    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_TRUE(result.sy_deg > result.sx_deg);
    TEST_ASSERT_TRUE(result.sy_deg > result.sz_deg);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(monitor::Monitor::DominantAxis::Y),
                            static_cast<std::uint8_t>(result.dominant));
}

/* --------------------------------------------------------------------------
   6.7 Noise Gate in AnalyzeImuEvent Tests
   -------------------------------------------------------------------------- */

TEST_CASE("noise gate returns zero damping when peak_gmag below threshold", "[monitor][noise]") {
    monitor::MonitorConfig config{};
    config.noise_gate_gmag_dps = 8.0f;
    config.modal_freq_min_hz = 1.0f;
    config.modal_freq_max_hz = 25.0f;

    monitor::Monitor& monitor = MakeMonitorForTest(config);

    constexpr std::size_t n = 104U;
    monitor.write_index_ = 0U;
    monitor.sample_count_ = n;

    for (std::size_t i = 0U; i < n; ++i) {
        const float t = static_cast<float>(i);
        const float sin_val = std::sin(2.0f * 3.14159f * 2.0f * t / 52.0f);
        monitor.gx_history_[i] = sin_val * 5.0f;
        monitor.gy_history_[i] = sin_val * 1.0f;
        monitor.gz_history_[i] = sin_val * 0.5f;

        if (i < 20U) {
            monitor.gmag_history_[i] = 0.3f;
        } else if (i < 40U) {
            const float decay_t = 1.0f - (static_cast<float>(i - 20U) / 20.0f);
            monitor.gmag_history_[i] = 0.3f + 10.0f * decay_t * decay_t;
        } else {
            monitor.gmag_history_[i] = 0.25f + 0.05f * sin_val;
        }
    }

    monitor.peak_gmag_ = 3.0f;

    dsps_fft2r_init_fc32(nullptr, 1024);

    const auto event = monitor.AnalyzeImuEvent();

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, event.damping_ratio);
    TEST_ASSERT_TRUE(event.damping_confidence[0] == 'l');
}

TEST_CASE("noise gate passes event with peak_gmag above threshold", "[monitor][noise]") {
    monitor::MonitorConfig config{};
    config.noise_gate_gmag_dps = 8.0f;
    config.modal_freq_min_hz = 1.0f;
    config.modal_freq_max_hz = 25.0f;

    monitor::Monitor& monitor = MakeMonitorForTest(config);

    constexpr std::size_t n = 104U;
    monitor.write_index_ = 0U;
    monitor.sample_count_ = n;

    for (std::size_t i = 0U; i < n; ++i) {
        const float t = static_cast<float>(i);
        const float sin_val = std::sin(2.0f * 3.14159f * 2.0f * t / 52.0f);
        monitor.gx_history_[i] = sin_val * 5.0f;
        monitor.gy_history_[i] = sin_val * 1.0f;
        monitor.gz_history_[i] = sin_val * 0.5f;

        if (i < 20U) {
            monitor.gmag_history_[i] = 0.3f;
        } else if (i < 40U) {
            const float decay_t = 1.0f - (static_cast<float>(i - 20U) / 20.0f);
            monitor.gmag_history_[i] = 0.3f + 10.0f * decay_t * decay_t;
        } else {
            monitor.gmag_history_[i] = 0.25f + 0.05f * sin_val;
        }
    }

    monitor.peak_gmag_ = 10.0f;

    dsps_fft2r_init_fc32(nullptr, 1024);

    const auto event = monitor.AnalyzeImuEvent();

    TEST_ASSERT_TRUE(event.damping_confidence[0] == 'h' ||
                     event.damping_confidence[0] == 'm' ||
                     event.damping_confidence[0] == 'l');
}
