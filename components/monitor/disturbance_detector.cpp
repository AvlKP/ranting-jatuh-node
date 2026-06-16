/// @file disturbance_detector.cpp
/// @brief TKEO energy operator and Schmitt-trigger disturbance detector.
/// @details Implements the per-sample TkeoWindow and DspDisturbanceDetector
/// state machines. These classes have no dependency on Monitor internals.
/// @ingroup monitor

#include "monitor.hpp"

namespace monitor {

bool TkeoWindow::Push(float gmag, float& out_tkeo) noexcept {
    out_tkeo = 0.0f;
    if (count_ < samples_.size()) {
        samples_[count_] = gmag;
        ++count_;
        if (count_ < samples_.size()) {
            return false;
        }
    } else {
        samples_[0U] = samples_[1U];
        samples_[1U] = samples_[2U];
        samples_[2U] = gmag;
    }

    out_tkeo = (samples_[1U] * samples_[1U]) - (samples_[0U] * samples_[2U]);
    return true;
}

void TkeoWindow::Reset() noexcept {
    samples_ = {};
    count_ = 0U;
}

void DspDisturbanceDetector::Reset() noexcept {
    state_ = NodeState::IDLE;
    quiet_count_ = 0U;
}

NodeState DspDisturbanceDetector::Update(float gmag, float tkeo, const MonitorConfig& config) noexcept {
    if (state_ == NodeState::IDLE) {
        if ((tkeo > config.dsp_tkeo_high) || (gmag > config.dsp_gmag_onset_dps)) {
            state_ = NodeState::DISTURBED;
            quiet_count_ = 0U;
        }
        return state_;
    }

    if (quiet_count_ > 0U) {
        if ((tkeo > config.dsp_tkeo_high) || (gmag >= config.dsp_gmag_quiet_dps)) {
            quiet_count_ = 0U;
        } else if ((tkeo < config.dsp_tkeo_low) && (gmag < config.dsp_gmag_quiet_dps)) {
            if (quiet_count_ < config.dsp_quiet_debounce) {
                ++quiet_count_;
            }
            if (quiet_count_ >= config.dsp_quiet_debounce) {
                state_ = NodeState::IDLE;
                quiet_count_ = 0U;
            }
        }
    } else if ((tkeo < config.dsp_tkeo_low) && (gmag < config.dsp_gmag_quiet_dps)) {
        quiet_count_ = 1U;
    } else {
        quiet_count_ = 0U;
    }

    return state_;
}

} // namespace monitor
