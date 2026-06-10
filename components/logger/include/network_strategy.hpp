/// @file network_strategy.hpp
/// @brief Compile-time WiFi connection strategy selection for logger.
/// @details Provides a shared interface for two network strategies:
///   - On-demand: full WiFi connect/disconnect per publish cycle (field deployment)
///   - Persistent: WiFi stays up with auto-reconnect (dashboard mode)
/// The correct implementation is selected at link time via CMakeLists conditional.
/// No #if guards needed in calling code.
/// @ingroup logger

#pragma once

namespace logger::network {

/// @brief Initialize network stack (NVS, netif, event loop, WiFi init, event handlers).
/// @return true if initialization succeeded.
[[nodiscard]] bool Init() noexcept;

/// @brief Ensure WiFi is connected and ready.
/// On-demand: full connect cycle (start, connect, wait for IP).
/// Persistent: check event group bit for connected state.
/// @return true if WiFi is connected and has IP.
[[nodiscard]] bool EnsureConnected() noexcept;

/// @brief Release WiFi connection after publish.
/// On-demand: disconnect and stop WiFi to save power.
/// Persistent: no-op (WiFi stays connected).
void ReleaseConnection() noexcept;

/// @brief Check if WiFi is currently connected.
/// @return true if WiFi has IP and is connected.
[[nodiscard]] bool IsConnected() noexcept;

} // namespace logger::network
