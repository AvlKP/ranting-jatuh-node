/// @file monitor_publisher.cpp
/// @brief Event publication to the ESP event loop.
/// @details Implements Monitor::ComputeAndPublish and Monitor::PublishFailure.
/// These functions copy results onto the ESP event loop with a zero timeout and
/// track dropped events. No heap is allocated on the publish path.
/// @ingroup monitor

#include "monitor.hpp"
#include "monitor_internal.hpp"

#include "esp_event.h"
#include "esp_timer.h"
#include "esp_log.h"

namespace monitor {

bool Monitor::ComputeAndPublish(NodeState pub_state, bool is_exit) noexcept {
    if (!fft_initialized_) {
        return false;
    }

    MonitorResult result{};
    if (!ComputeStats(result)) {
        return false;
    }

    result.state = pub_state;

    if (pub_state == NodeState::DISTURBED) {
        static_cast<void>(ComputeSwayAndDamping(result));
        if (is_exit) {
            const EventAnalysisResult event = AnalyzeImuEvent();
            result.natural_freq_hz = event.natural_freq_hz;
            result.natural_freq_roll_hz = event.natural_freq_hz;
            result.natural_freq_pitch_hz = event.natural_freq_hz;
            result.roll_damping_ratio = event.damping_ratio;
            result.pitch_damping_ratio = event.damping_ratio;
            result.damping_confidence = event.damping_confidence;
        } else {
            result.natural_freq_roll_hz = 0.0f;
            result.natural_freq_pitch_hz = 0.0f;
            result.natural_freq_hz = 0.0f;
            result.roll_damping_ratio = 0.0f;
            result.pitch_damping_ratio = 0.0f;
            SetConfidence(result.damping_confidence, "low");
        }
    }

    ESP_LOGD(kTag,
             "roll_mean=%.3f roll_var=%.3f pitch_mean=%.3f pitch_var=%.3f "
             "roll_pp_max=%.3f roll_pp_mean=%.3f pitch_pp_max=%.3f pitch_pp_mean=%.3f "
             "roll_zeta=%.4f pitch_zeta=%.4f freq=%.3fHz freq_roll=%.3fHz freq_pitch=%.3fHz samples=%u ts_us=%llu state=%d",
             result.roll_mean,
             result.roll_variance,
             result.pitch_mean,
             result.pitch_variance,
             result.roll_sway_pp_max,
             result.roll_sway_pp_mean,
             result.pitch_sway_pp_max,
             result.pitch_sway_pp_mean,
             result.roll_damping_ratio,
             result.pitch_damping_ratio,
             result.natural_freq_hz,
             result.natural_freq_roll_hz,
             result.natural_freq_pitch_hz,
             static_cast<unsigned>(result.sample_count),
             static_cast<unsigned long long>(result.timestamp_us),
             static_cast<int>(result.state));

    const esp_err_t post_err = esp_event_post(MONITOR_EVENT_BASE,
                                              MONITOR_EVENT_RESULT,
                                              &result,
                                              sizeof(result),
                                              0);
    if (post_err != ESP_OK) {
        ++dropped_result_events_;
        ESP_LOGW(kTag, "Result event post failed: %s", esp_err_to_name(post_err));
        return false;
    }

    return true;
}

void Monitor::PublishFailure(FailureEvent event) noexcept {
    FailureResult result{};
    result.event = event;
    result.timestamp_us = static_cast<std::uint64_t>(esp_timer_get_time());
    const esp_err_t post_err = esp_event_post(MONITOR_EVENT_BASE,
                                              MONITOR_EVENT_FAILURE,
                                              &result,
                                              sizeof(result),
                                              0);
    if (post_err != ESP_OK) {
        ++dropped_failure_events_;
        ESP_LOGW(kTag, "Failure event post failed: %s", esp_err_to_name(post_err));
    }
}

} // namespace monitor
