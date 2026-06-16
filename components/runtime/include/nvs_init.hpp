/// @file nvs_init.hpp
/// @brief Shared idempotent NVS initialization.

#pragma once

#include "esp_err.h"

namespace runtime {

[[nodiscard]] esp_err_t EnsureNvsInitialized() noexcept;

} // namespace runtime
