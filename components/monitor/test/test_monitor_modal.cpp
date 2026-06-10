#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <new>

#include "dsps_fft2r.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
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

Monitor& MakeMonitor(const MonitorConfig& config) {
    static Monitor* monitor = nullptr;
    static void* storage = nullptr;
    static bool fft_initialized = false;
    if (!fft_initialized) {
        TEST_ASSERT_EQUAL(ESP_OK, dsps_fft2r_init_fc32(nullptr, static_cast<int>(monitor::kFftWindowSamples)));
        fft_initialized = true;
    }
    if (monitor != nullptr) {
        monitor->~Monitor();
    }
    if (storage == nullptr) {
        storage = heap_caps_malloc(sizeof(Monitor), MALLOC_CAP_8BIT);
        TEST_ASSERT_NOT_NULL(storage);
    }
    monitor = new (storage) Monitor{DummyImuConfig(), config};
    monitor->fft_initialized_ = true;
    return *monitor;
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
    Monitor& monitor = MakeMonitor(config);
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
    Monitor& monitor = MakeMonitor(config);

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
    Monitor& monitor = MakeMonitor(config);
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
    Monitor& monitor = MakeMonitor(config);
    Monitor::FftBinRange empty = monitor.SelectFftBinRange(512U, 26.0f);
    TEST_ASSERT_FALSE(empty.valid);

    config.modal_freq_min_hz = 0.5f;
    config.modal_freq_max_hz = 25.0f;
    Monitor& monitor_clamped = MakeMonitor(config);
    Monitor::FftBinRange range = monitor_clamped.SelectFftBinRange(512U, 26.0f);
    TEST_ASSERT_TRUE(range.valid);
    TEST_ASSERT_TRUE(range.min_bin >= 1U);
    TEST_ASSERT_TRUE(range.max_bin <= 255U);
}

TEST_CASE("pair envelope damping needs sufficient decreasing amplitudes", "[monitor][modal]") {
    MonitorConfig config{};
    Monitor& monitor = MakeMonitor(config);
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
    Monitor& monitor = MakeMonitor(config);

    constexpr std::size_t kCount = 156U;
    for (std::size_t i = 0U; i < kCount; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(CONFIG_MONITOR_IMU_RATE_HZ);
        const float baseline = 1.5f - (0.004f * static_cast<float>(i));
        const float amplitude = 0.8f - (0.002f * static_cast<float>(i));
        monitor.roll_history_[i] = baseline + amplitude * std::sin(2.0f * 3.14159265358979323846f * 2.0f * t);
    }
    monitor.write_index_ = kCount;
    monitor.sample_count_ = kCount;

    Monitor::ModalAxisResult result{};
    monitor.AnalyzeModalAxis(monitor.roll_history_, result);
    TEST_ASSERT_TRUE(result.centerline_pairs.count >= 2U);
    TEST_ASSERT_TRUE(result.natural_freq_hz >= 0.5f);
    TEST_ASSERT_TRUE(result.natural_freq_hz <= 25.0f);
}

TEST_CASE("gmag FFT finds known frequency inside modal band", "[monitor][modal][dsp]") {
    MonitorConfig config{};
    config.modal_freq_min_hz = 0.5f;
    config.modal_freq_max_hz = 12.0f;
    Monitor& monitor = MakeMonitor(config);

    constexpr float kSignalHz = 2.0f;
    constexpr std::size_t kCount = 512U;
    for (std::size_t i = 0U; i < kCount; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(CONFIG_MONITOR_IMU_RATE_HZ);
        monitor.gmag_history_[i] = 4.0f + std::sin(2.0f * 3.14159265358979323846f * kSignalHz * t);
    }
    monitor.write_index_ = kCount;
    monitor.sample_count_ = kCount;

    const float freq = monitor.ComputeGmagNaturalFrequency();
    TEST_ASSERT_FLOAT_WITHIN(0.15f, kSignalHz, freq);
}

TEST_CASE("tkeo decay onset finds burst and flags usable decay", "[monitor][event]") {
    MonitorConfig config{};
    Monitor& monitor = MakeMonitor(config);

    constexpr std::size_t kCount = 96U;
    for (std::size_t i = 0U; i < kCount; ++i) {
        monitor.gmag_history_[i] = 0.2f;
    }
    monitor.gmag_history_[20U] = 10.0f;
    for (std::size_t i = 21U; i < kCount; ++i) {
        monitor.gmag_history_[i] = std::max(0.4f, 9.0f - 0.12f * static_cast<float>(i - 21U));
    }
    monitor.write_index_ = kCount;
    monitor.sample_count_ = kCount;

    const Monitor::DecayOnsetResult onset = monitor.FindDecayOnsetTkeo();
    TEST_ASSERT_TRUE(onset.quality != Monitor::DecayQuality::None);
    TEST_ASSERT_TRUE(onset.onset >= 15U);
    TEST_ASSERT_TRUE(onset.onset <= 30U);
}

TEST_CASE("dominant axis selects largest integrated signed gyro", "[monitor][event]") {
    MonitorConfig config{};
    Monitor& monitor = MakeMonitor(config);

    constexpr std::size_t kCount = 64U;
    for (std::size_t i = 0U; i < kCount; ++i) {
        monitor.gx_history_[i] = 0.2f;
        monitor.gy_history_[i] = -3.0f;
        monitor.gz_history_[i] = 1.0f;
    }
    monitor.write_index_ = kCount;
    monitor.sample_count_ = kCount;

    const Monitor::SwayAxisResult sway = monitor.ComputeDominantAxisSway(0U, kCount);
    TEST_ASSERT_TRUE(sway.valid);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(Monitor::DominantAxis::Y),
                            static_cast<std::uint8_t>(sway.dominant));
}

TEST_CASE("signed dominant-axis FFT finds configured modal frequency", "[monitor][event]") {
    MonitorConfig config{};
    config.modal_freq_min_hz = 0.5f;
    config.modal_freq_max_hz = 12.0f;
    Monitor& monitor = MakeMonitor(config);

    constexpr float kSignalHz = 3.0f;
    constexpr std::size_t kCount = 512U;
    for (std::size_t i = 0U; i < kCount; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(CONFIG_MONITOR_IMU_RATE_HZ);
        monitor.gx_history_[i] = 0.1f * std::sin(2.0f * 3.14159265358979323846f * kSignalHz * t);
        monitor.gy_history_[i] = 3.0f * std::sin(2.0f * 3.14159265358979323846f * kSignalHz * t);
        monitor.gz_history_[i] = 0.2f;
    }
    monitor.write_index_ = kCount;
    monitor.sample_count_ = kCount;

    const float freq = monitor.ComputeSignedAxisNaturalFrequency(Monitor::DominantAxis::Y, 0U, kCount);
    TEST_ASSERT_FLOAT_WITHIN(0.15f, kSignalHz, freq);
}

TEST_CASE("peak-hold envelope damping reports bounded confidence", "[monitor][event]") {
    MonitorConfig config{};
    Monitor& monitor = MakeMonitor(config);

    constexpr std::size_t kCount = 160U;
    for (std::size_t i = 0U; i < kCount; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(CONFIG_MONITOR_IMU_RATE_HZ);
        monitor.gmag_history_[i] = 1.5f + 8.0f * std::exp(-0.8f * t);
    }
    monitor.write_index_ = kCount;
    monitor.sample_count_ = kCount;

    const Monitor::DampingFitResult fit =
        monitor.ComputePeakHoldDamping(0U, kCount, 2.0f, Monitor::DecayQuality::Reliable);
    TEST_ASSERT_TRUE(fit.damping_ratio >= 0.0f);
    TEST_ASSERT_TRUE(std::strstr(fit.confidence.data(), "low") != nullptr ||
                     std::strstr(fit.confidence.data(), "medium") != nullptr ||
                     std::strstr(fit.confidence.data(), "high") != nullptr);
}
