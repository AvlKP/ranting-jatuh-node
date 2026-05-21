#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include "monitor.hpp"
#include "logger.hpp"

namespace dashboard {

class Dashboard {
public:
    struct Config {
        std::uint16_t port{80};
        bool enabled{true};
    };

    explicit Dashboard(monitor::Monitor& monitor, logger::Logger& logger) noexcept;
    ~Dashboard() noexcept;

    [[nodiscard]] esp_err_t Start() noexcept;
    [[nodiscard]] esp_err_t Start(const Config& config) noexcept;
    void Stop() noexcept;

private:
    static esp_err_t IndexHandler(httpd_req_t* req) noexcept;
    static esp_err_t StatusHandler(httpd_req_t* req) noexcept;
    static esp_err_t ConfigHandler(httpd_req_t* req) noexcept;

    monitor::Monitor& monitor_;
    logger::Logger& logger_;
    httpd_handle_t server_{nullptr};
    Config config_{};
};

} // namespace dashboard
