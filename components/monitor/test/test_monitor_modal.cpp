#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>

#include "unity.h"

#define private public
#include "monitor.hpp"
#undef private

namespace {

using monitor::Monitor;
using monitor::MonitorConfig;

sensor::Lsm6ds3::Config DummyImuConfig() {
    sensor::Lsm6ds3::Config cfg{};
    cfg.read_cb = [](std::uint8_t, std::uint8_t*, std::size_t) { return false; };
    cfg.write_cb = [](std::uint8_t, const std::uint8_t*, std::size_t) { return false; };
    return cfg;
}

Monitor MakeMonitor(const MonitorConfig& config) {
    return Monitor{DummyImuConfig(), config};
}

void LoadSamples(Monitor& monitor, const float* samples, std::size_t count) {
    monitor.write_index_ = count % monitor::kStorageSamples;
    monitor.sample_count_ = count;
    for (std::size_t i = 0U; i < count; ++i) {
        monitor.roll_history_[i] = samples[i];
    }
}

} // namespace

TEST_CASE("raw extrema detection applies spacing filter", "[monitor][modal]") {
    MonitorConfig config{};
    config.peak_min_spacing = 3U;
    Monitor monitor = MakeMonitor(config);
    const float samples[] = {0.0f, 1.0f, 0.0f, 0.8f, 0.0f, -1.0f, 0.0f};
    LoadSamples(monitor, samples, 7U);

    Monitor::ExtremaList raw{};
    TEST_ASSERT_TRUE(monitor.DetectRawExtrema(monitor.roll_history_, 0U, 7U, raw));
    TEST_ASSERT_EQUAL_UINT(2U, raw.count);
    TEST_ASSERT_EQUAL_UINT(1U, raw.points[0U].logical_index);
    TEST_ASSERT_EQUAL_UINT(5U, raw.points[1U].logical_index);
}

TEST_CASE("lobe collapse keeps strongest same-kind extrema", "[monitor][modal]") {
    MonitorConfig config{};
    config.centerline_lobe_reversal_deg = 0.5f;
    Monitor monitor = MakeMonitor(config);

    Monitor::ExtremaList raw{};
    raw.count = 5U;
    raw.points[0U] = {1U, 1.0f, Monitor::ExtremaKind::Peak};
    raw.points[1U] = {2U, 1.3f, Monitor::ExtremaKind::Peak};
    raw.points[2U] = {3U, 1.1f, Monitor::ExtremaKind::Trough};
    raw.points[3U] = {4U, -0.9f, Monitor::ExtremaKind::Trough};
    raw.points[4U] = {5U, 0.8f, Monitor::ExtremaKind::Peak};

    Monitor::ExtremaList collapsed{};
    TEST_ASSERT_TRUE(monitor.CollapseExtremaLobes(raw, collapsed));
    TEST_ASSERT_EQUAL_UINT(3U, collapsed.count);
    TEST_ASSERT_EQUAL_UINT(2U, collapsed.points[0U].logical_index);
    TEST_ASSERT_EQUAL_UINT(4U, collapsed.points[1U].logical_index);
    TEST_ASSERT_EQUAL_UINT(5U, collapsed.points[2U].logical_index);
}

TEST_CASE("centerline pairs and residual endpoint hold", "[monitor][modal]") {
    MonitorConfig config{};
    config.centerline_min_amplitude_deg = 0.05f;
    Monitor monitor = MakeMonitor(config);
    const float samples[] = {2.0f, 3.0f, 2.0f, 1.0f, 2.0f, 3.0f, 2.0f};
    LoadSamples(monitor, samples, 7U);

    Monitor::ExtremaList extrema{};
    extrema.count = 3U;
    extrema.points[0U] = {1U, 3.0f, Monitor::ExtremaKind::Peak};
    extrema.points[1U] = {3U, 1.0f, Monitor::ExtremaKind::Trough};
    extrema.points[2U] = {5U, 3.0f, Monitor::ExtremaKind::Peak};

    Monitor::CenterlinePairList pairs{};
    TEST_ASSERT_TRUE(monitor.BuildCenterlinePairs(extrema, pairs));
    TEST_ASSERT_EQUAL_UINT(2U, pairs.count);
    TEST_ASSERT_EQUAL_UINT(2U, pairs.pairs[0U].center_logical_index);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, pairs.pairs[0U].center_value);
    TEST_ASSERT_TRUE(monitor.SubtractCenterline(monitor.roll_history_, 0U, 7U, pairs));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, monitor.residual_scratch_[0U]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, monitor.residual_scratch_[1U]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, monitor.residual_scratch_[6U]);
}

TEST_CASE("bounded FFT bin selection handles empty and clamped bands", "[monitor][modal]") {
    MonitorConfig config{};
    config.modal_freq_min_hz = 40.0f;
    config.modal_freq_max_hz = 50.0f;
    Monitor monitor = MakeMonitor(config);
    Monitor::FftBinRange empty = monitor.SelectFftBinRange(512U, 26.0f);
    TEST_ASSERT_FALSE(empty.valid);

    config.modal_freq_min_hz = 0.5f;
    config.modal_freq_max_hz = 25.0f;
    Monitor monitor_clamped = MakeMonitor(config);
    Monitor::FftBinRange range = monitor_clamped.SelectFftBinRange(512U, 26.0f);
    TEST_ASSERT_TRUE(range.valid);
    TEST_ASSERT_TRUE(range.min_bin >= 1U);
    TEST_ASSERT_TRUE(range.max_bin <= 255U);
}

TEST_CASE("pair envelope damping needs sufficient decreasing amplitudes", "[monitor][modal]") {
    MonitorConfig config{};
    Monitor monitor = MakeMonitor(config);
    Monitor::CenterlinePairList pairs{};
    pairs.count = 5U;
    pairs.pairs[0U] = {0U, 0.0f, 0.8f, 0.0f};
    pairs.pairs[1U] = {1U, 0.0f, 1.0f, 1.0f};
    pairs.pairs[2U] = {2U, 0.0f, 0.7f, 2.0f};
    pairs.pairs[3U] = {3U, 0.0f, 0.5f, 3.0f};
    pairs.pairs[4U] = {4U, 0.0f, 0.25f, 4.0f};

    Monitor::PeakList envelope{};
    TEST_ASSERT_TRUE(monitor.SelectPairEnvelope(pairs, envelope));
    TEST_ASSERT_EQUAL_UINT(4U, envelope.count);
    TEST_ASSERT_TRUE(monitor.ComputeDampingRegression(envelope, 1.0f) > 0.0f);

    envelope.count = 3U;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, monitor.ComputeDampingRegression(envelope, 1.0f));
}

TEST_CASE("drifting baseline modal analysis finds nonzero frequency", "[monitor][modal]") {
    MonitorConfig config{};
    config.centerline_min_amplitude_deg = 0.05f;
    config.centerline_lobe_reversal_deg = 0.10f;
    config.modal_freq_min_hz = 0.5f;
    config.modal_freq_max_hz = 25.0f;
    Monitor monitor = MakeMonitor(config);

    constexpr std::size_t kCount = 156U;
    for (std::size_t i = 0U; i < kCount; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(CONFIG_MONITOR_IMU_RATE_HZ);
        const float baseline = 1.5f - (0.004f * static_cast<float>(i));
        const float amplitude = 0.8f - (0.002f * static_cast<float>(i));
        monitor.roll_history_[i] = baseline + amplitude * std::sin(2.0f * 3.14159265358979323846f * 2.0f * t);
    }
    monitor.write_index_ = kCount;
    monitor.sample_count_ = kCount;

    Monitor::ModalAxisResult result = monitor.AnalyzeModalAxis(monitor.roll_history_);
    TEST_ASSERT_TRUE(result.centerline_pairs.count >= 2U);
    TEST_ASSERT_TRUE(result.natural_freq_hz >= 0.5f);
    TEST_ASSERT_TRUE(result.natural_freq_hz <= 25.0f);
}
