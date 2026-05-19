#pragma once

#include <array>
#include <span>

namespace filter {

/// @brief Linear Kalman Filter utilizing C++ Templates for static loop unrolling
/// @tparam N Number of states
template <size_t N>
class Kalman {
public:
    Kalman() noexcept {
        X_.fill(0.0f);
        P_.fill({});
        Q_.fill({});
        R_.fill({});
        K_.fill({});
        
        // Initialize covariance matrices as Identity
        for (size_t i = 0; i < N; i++) {
            P_[i][i] = 1.0f;
            Q_[i][i] = 0.1f;
            R_[i][i] = 0.1f;
        }
    }

    /// @brief Set the process noise covariance matrix Q
    void set_q(const std::array<std::array<float, N>, N>& q) noexcept { Q_ = q; }

    /// @brief Set the measurement noise covariance matrix R
    void set_r(const std::array<std::array<float, N>, N>& r) noexcept { R_ = r; }

    /// @brief Predict step (X = X + U*dt, P = P + Q)
    /// @param U Control input array
    /// @param dt Time delta
    void predict(std::span<const float, N> U, float dt) noexcept {
        // Compile-time unrolling friendly bounds
        for (size_t i = 0; i < N; ++i) {
            X_[i] += U[i] * dt;
        }
        for (size_t i = 0; i < N; ++i) {
            for (size_t j = 0; j < N; ++j) {
                P_[i][j] += Q_[i][j];
            }
        }
    }

    /// @brief Update step with measurement Z
    /// @param Z Measurement array
    void update(std::span<const float, N> Z) noexcept {
        std::array<float, N> Y{}; 
        
        for (size_t i = 0; i < N; ++i) {
            Y[i] = Z[i] - X_[i];
        }

        for (size_t i = 0; i < N; ++i) {
            for (size_t j = 0; j < N; ++j) {
                K_[i][j] = P_[i][j] / (P_[i][j] + R_[i][j]);
            }
        }

        for (size_t i = 0; i < N; ++i) {
            X_[i] += K_[i][i] * Y[i];
        }

        for (size_t i = 0; i < N; ++i) {
            for (size_t j = 0; j < N; ++j) {
                P_[i][j] = (1.0f - K_[i][i]) * P_[i][j];
            }
        }
    }

    [[nodiscard]] std::array<float, N> state() const noexcept { return X_; }

private:
    std::array<float, N> X_; // State vector
    std::array<std::array<float, N>, N> P_; // Error covariance
    std::array<std::array<float, N>, N> Q_; // Process noise
    std::array<std::array<float, N>, N> R_; // Measurement noise
    std::array<std::array<float, N>, N> K_; // Kalman Gain
};

} // namespace filter