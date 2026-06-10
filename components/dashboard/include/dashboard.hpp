/// @file dashboard.hpp
/// @brief HTTP debug dashboard for real-time monitoring.
/// @details Serves a web UI via ESP HTTP server with endpoints for live status
/// (WiFi, MQTT, heap, state), IMU tilt history, FFT data, SD file listing,
/// MQTT logs, and node configuration. Used during development and field trials
/// for observability without external debugger.
/// @ingroup dashboard

#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include "monitor.hpp"
#include "logger.hpp"
#include <mutex>

namespace dashboard {

/// @brief HTTP debug dashboard with live monitoring endpoints.
class Dashboard {
public:
    /// @brief Dashboard configuration.
    struct Config {
        std::uint16_t port{80};   ///< HTTP server port.
        bool enabled{true};       ///< Enable/disable dashboard at runtime.
    };

    /// @param monitor Reference to the active Monitor instance.
    /// @param logger Reference to the active Logger instance.
    explicit Dashboard(monitor::Monitor& monitor, logger::Logger& logger) noexcept;
    ~Dashboard() noexcept;

    [[nodiscard]] esp_err_t Start() noexcept;
    [[nodiscard]] esp_err_t Start(const Config& config) noexcept;
    void Stop() noexcept;

private:
    static esp_err_t IndexHandler(httpd_req_t* req) noexcept;
    static esp_err_t StatusHandler(httpd_req_t* req) noexcept;
    static esp_err_t ConfigHandler(httpd_req_t* req) noexcept;
    static esp_err_t DownloadHandler(httpd_req_t* req) noexcept;
    static void EventHandler(void* handler_args,
                             esp_event_base_t base,
                             std::int32_t id,
                             void* event_data) noexcept;

    monitor::Monitor& monitor_;
    logger::Logger& logger_;
    httpd_handle_t server_{nullptr};
    Config config_{};
    monitor::MonitorResult latest_result_{};
    mutable std::mutex mutex_;
};

} // namespace dashboard
