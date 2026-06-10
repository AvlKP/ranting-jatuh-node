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
