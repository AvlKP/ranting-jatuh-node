#include <cstring>

#include "unity.h"

#include "logger_internal.hpp"

namespace {

monitor::MonitorResult MakeResult() {
    monitor::MonitorResult result{};
    result.roll_mean = 1.234f;
    result.pitch_mean = -2.345f;
    result.roll_variance = 0.123f;
    result.pitch_variance = 0.456f;
    result.roll_sway_pp_max = 3.210f;
    result.roll_sway_pp_mean = 1.110f;
    result.pitch_sway_pp_max = 4.320f;
    result.pitch_sway_pp_mean = 2.220f;
    result.roll_damping_ratio = 0.0123f;
    result.pitch_damping_ratio = 0.0456f;
    result.natural_freq_hz = 2.500f;
    result.natural_freq_roll_hz = 2.500f;
    result.natural_freq_pitch_hz = 2.500f;
    result.state = monitor::NodeState::DISTURBED;
    result.sample_count = 3120U;
    result.timestamp_us = 123456789ULL;
    return result;
}

logger::TimeInfo MakeTimeInfo() {
    logger::TimeInfo time{};
    time.valid = true;
    time.unix_time = 1700000000;
    time.timestamp_us = 123456789ULL;
    std::strncpy(time.date_yyyymmdd.data(), "20231114", time.date_yyyymmdd.size());
    return time;
}

TEST_CASE("logger parameter CSV formatter fits fixed line buffer", "[logger][format]") {
    logger::CsvLine line{};
    const bool ok = logger::FormatParameterCsv(MakeResult(), MakeTimeInfo(), line);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_GREATER_THAN_UINT16(0U, line.length);
    TEST_ASSERT_LESS_THAN_UINT16(logger::kCsvLineMax, line.length);
    TEST_ASSERT_EQUAL('\n', line.buffer[line.length - 1U]);
    TEST_ASSERT_NOT_NULL(std::strstr(line.buffer.data(), "DISTURBED"));
    TEST_ASSERT_NULL(std::strstr(line.buffer.data(), "event_type"));
}

TEST_CASE("logger parameter JSON formatter fits fixed line buffer", "[logger][format]") {
    logger::CsvLine line{};
    const bool ok = logger::FormatParameterJson(MakeResult(), MakeTimeInfo(), line);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_GREATER_THAN_UINT16(0U, line.length);
    TEST_ASSERT_LESS_THAN_UINT16(logger::kCsvLineMax, line.length);
    TEST_ASSERT_EQUAL('\n', line.buffer[line.length - 1U]);
    TEST_ASSERT_NOT_NULL(std::strstr(line.buffer.data(), "\"state\":\"DISTURBED\""));
    TEST_ASSERT_NOT_NULL(std::strstr(line.buffer.data(), "\"natural_freq_hz\":2.500"));
    TEST_ASSERT_NOT_NULL(std::strstr(line.buffer.data(), "\"natural_freq_roll_hz\":2.500"));
    TEST_ASSERT_NOT_NULL(std::strstr(line.buffer.data(), "\"natural_freq_pitch_hz\":2.500"));
    TEST_ASSERT_NULL(std::strstr(line.buffer.data(), "event_type"));
}

TEST_CASE("logger failure CSV formatter fits fixed line buffer", "[logger][format]") {
    monitor::FailureResult failure{};
    failure.event = monitor::FailureEvent::AcousticEmission;
    failure.timestamp_us = 2222ULL;

    logger::TimeInfo time = MakeTimeInfo();
    time.timestamp_us = failure.timestamp_us;

    logger::CsvLine line{};
    const bool ok = logger::FormatFailureCsv(failure, time, line);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_GREATER_THAN_UINT16(0U, line.length);
    TEST_ASSERT_LESS_THAN_UINT16(logger::kCsvLineMax, line.length);
    TEST_ASSERT_EQUAL('\n', line.buffer[line.length - 1U]);
    TEST_ASSERT_NOT_NULL(std::strstr(line.buffer.data(), "acoustic_emission"));
}

} // namespace
