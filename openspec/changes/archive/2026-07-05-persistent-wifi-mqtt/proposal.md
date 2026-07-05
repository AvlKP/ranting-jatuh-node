## Why

Demo deployments need a stable, always-on WiFi+MQTT link so the dashboard can stream live data without per-publish reconnect latency. The existing persistent strategy (gated on `CONFIG_DASHBOARD_ENABLE`) hot-loops `esp_wifi_connect()` on every disconnect event with no backoff, and the MQTT client is built with `disable_auto_reconnect = true`, so neither layer self-heals in a bounded way. Field builds that want persistent behavior without the HTTP dashboard currently have no way to select it, and every reconnect occurrence (WiFi disconnect event, WiFi `EnsureConnected` failure, MQTT disconnect event, MQTT connect timeout, boot) is not uniformly bounded.

## What Changes

- Decouple persistent WiFi/MQTT selection from `CONFIG_DASHBOARD_ENABLE` via a new `CONFIG_LOGGER_NETWORK_MODE` Kconfig choice (`on-demand` | `persistent`). Dashboard no longer implicitly forces persistent; it implies it instead.
- Persistent mode SHALL NOT enable ESP-IDF Power Management (`CONFIG_ESP_PM_ENABLE`) and SHALL leave the WiFi interface power-save at the no-PS/default setting so demo latency stays predictable. No DFS, no light-sleep, CPU stays at the configured 160 MHz.
- Persistent WiFi reconnect SHALL be bounded: exponential backoff with jitter, capped maximum retry interval, and no unbounded `esp_wifi_connect()` calls inside the disconnect event handler.
- Persistent MQTT SHALL auto-reconnect (`disable_auto_reconnect = false`) with a bounded reconnect interval configured on the client; the network task SHALL NOT tear down or stop the MQTT client per publish cycle in persistent mode.
- Every reconnect occurrence SHALL be enumerated and bounded with a single shared backoff policy: (1) boot initial connect, (2) WiFi `WIFI_EVENT_STA_DISCONNECTED` mid-session, (3) WiFi `EnsureConnected()` failure, (4) MQTT `MQTT_EVENT_DISCONNECTED` mid-session, (5) MQTT connect timeout in the task loop.
- On-demand mode behavior is unchanged.
- **BREAKING**: `CONFIG_DASHBOARD_ENABLE=y` no longer directly selects `network_persistent.cpp`; it is selected via `CONFIG_LOGGER_NETWORK_MODE=persistent`. Existing sdkconfig files must set the new choice.

## Capabilities

### New Capabilities

_None._

### Modified Capabilities

- `network-strategy`: persistent strategy selection is decoupled from the dashboard config via `CONFIG_LOGGER_NETWORK_MODE`; persistent reconnect SHALL be bounded with exponential backoff + jitter (no hot-loop in the disconnect event handler); persistent mode SHALL explicitly keep WiFi power-save off and SHALL NOT rely on `CONFIG_ESP_PM_ENABLE`.
- `network-task`: MQTT client SHALL auto-reconnect in persistent mode with bounded backoff; the publish cycle SHALL reuse the persistent WiFi/MQTT link without per-cycle teardown; reconnect attempts at every occurrence (boot, WiFi disconnect, WiFi connect failure, MQTT disconnect, MQTT connect timeout) SHALL be enumerated and bounded.

## Impact

- **Build/config**: `components/logger/CMakeLists.txt` strategy selection switches from `CONFIG_DASHBOARD_ENABLE` to `CONFIG_LOGGER_NETWORK_MODE`; new Kconfig choice added in `Kconfig.defaults` / `sdkconfig.defaults`; all existing `sdkconfig*` files need the new key set.
- **Code**: `components/logger/network_persistent.cpp` gains backoff state, bounded reconnect in the disconnect handler, and an explicit no-PS call; `components/logger/network_task.cpp` gains per-mode MQTT reconnect policy and skips WiFi/MQTT teardown in persistent mode. `components/dashboard/` no longer implicitly drives the network strategy.
- **Power**: persistent adds ~15-25 mA WiFi idle vs ~0.4 mA for on-demand (radio-only delta). Without PM the CPU never light-sleeps; the monitor 52 Hz + AE spectral ADC baseline dominates total draw. Full budget captured in `design.md`.
- **Tests**: `components/monitor/test/test_outbox_network_runtime.cpp` and `test_network_wifi_reason.cpp` exercise on-demand paths and remain valid; new tests cover persistent reconnect bounds and MQTT auto-reconnect policy.
- **Runtime safety**: no heap use added (backoff state is static); no exceptions; no new tasks; event handler stays allocation-free.
