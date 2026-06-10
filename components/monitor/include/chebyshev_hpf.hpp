#pragma once

#include <cmath>
#include <cstddef>

namespace monitor {

/// @brief 2nd-order Chebyshev Type 1 high-pass filter for per-axis accelerometer
/// disturbance detection (Direct Form II biquad).
///
/// @deprecated Superseded by TKEO-based DspDisturbanceDetector. Legacy artifact
/// from early HPF-based disturbance detection architecture. Retained for reference;
/// not used in any production code path.
class ChebyshevHpf {
public:
    void update(float ax, float ay, float az) noexcept {
        {
            const float w0 = ax - a1_ * w1_x_ - a2_ * w2_x_;
            hpf_x_ = b0_ * w0 + b1_ * w1_x_ + b2_ * w2_x_;
            w2_x_ = w1_x_;
            w1_x_ = w0;
        }
        {
            const float w0 = ay - a1_ * w1_y_ - a2_ * w2_y_;
            hpf_y_ = b0_ * w0 + b1_ * w1_y_ + b2_ * w2_y_;
            w2_y_ = w1_y_;
            w1_y_ = w0;
        }
        {
            const float w0 = az - a1_ * w1_z_ - a2_ * w2_z_;
            hpf_z_ = b0_ * w0 + b1_ * w1_z_ + b2_ * w2_z_;
            w2_z_ = w1_z_;
            w1_z_ = w0;
        }
    }

    [[nodiscard]] float magnitude() const noexcept {
        return std::sqrt(hpf_x_ * hpf_x_ + hpf_y_ * hpf_y_ + hpf_z_ * hpf_z_);
    }

private:
    static constexpr float b0_{0.88054028f};
    static constexpr float b1_{-1.76108057f};
    static constexpr float b2_{0.88054028f};
    static constexpr float a1_{-1.97570321f};
    static constexpr float a2_{0.97622659f};

    float w1_x_{0.0f};
    float w2_x_{0.0f};
    float w1_y_{0.0f};
    float w2_y_{0.0f};
    float w1_z_{0.0f};
    float w2_z_{0.0f};

    float hpf_x_{0.0f};
    float hpf_y_{0.0f};
    float hpf_z_{0.0f};
};

} // namespace monitor
