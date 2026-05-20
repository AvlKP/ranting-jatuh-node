#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "monitor.hpp"

namespace logger {

constexpr std::size_t kCsvLineMax = 256U;

struct CsvLine {
    std::array<char, kCsvLineMax> buffer{};
    std::uint16_t length{0U};
};

struct TimeInfo {
    bool valid{false};
    std::int64_t unix_time{0};
    std::uint64_t timestamp_us{0};
    std::array<char, 9U> date_yyyymmdd{};
};

[[nodiscard]] const char* FailureEventName(monitor::FailureEvent event) noexcept;
[[nodiscard]] bool BuildTimeInfo(TimeInfo& out_time) noexcept;
[[nodiscard]] bool FormatParameterCsv(const monitor::MonitorResult& result,
                                      const TimeInfo& time_info,
                                      CsvLine& line) noexcept;
[[nodiscard]] bool FormatFailureCsv(const monitor::FailureResult& result,
                                    const TimeInfo& time_info,
                                    CsvLine& line) noexcept;

namespace storage {
void SetMountPoint(const char* mount_point) noexcept;
[[nodiscard]] bool AppendParameter(const TimeInfo& time_info, const CsvLine& line) noexcept;
[[nodiscard]] bool AppendFailure(const TimeInfo& time_info, const CsvLine& line) noexcept;
} // namespace storage

namespace mqtt {
[[nodiscard]] bool Init() noexcept;
[[nodiscard]] bool PublishParameters(const CsvLine* lines, std::size_t count) noexcept;
[[nodiscard]] bool PublishFailure(const CsvLine& line) noexcept;
[[nodiscard]] bool PublishRaw(const char* topic, const char* payload, const char* content_type) noexcept;
} // namespace mqtt

} // namespace logger
