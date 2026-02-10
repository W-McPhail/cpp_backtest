#pragma once

#include "strategy.hpp"
#include <memory>

namespace backtest {

/// CTM 2.0 (Conquer The Markets): SMA distance strategy.
/// Long: distance_long = min(price - SMA_fast, price - SMA_med, price - SMA_slow). Enter when > 0 (or on cross above 0), exit when crosses below 0.
/// Short: distance_short = max(price - SMA_fast, price - SMA_med, price - SMA_slow). Enter when < 0 (or on cross below 0), exit when crosses above 0.
/// Optional Kalman trend filter: when enabled, enter long only when loft_moved && trend==UP (short: loft_moved && trend==DOWN).
struct CtmParams {
    bool long_trades = true;
    bool short_trades = true;
    int long_fast = 22;
    int long_medium = 22;
    int long_slow = 70;
    int short_fast = 22;
    int short_medium = 22;
    int short_slow = 333;
    bool long_enter_on_cross_only = false;
    bool short_enter_on_cross_only = false;
    double position_equity_pct_long = 1.0;
    double position_equity_pct_short = 1.0;

    // Kalman trend filter (loft trend on Kalman-smoothed price)
    bool use_kalman_trend_long = false;
    bool use_kalman_trend_short = false;
    double kalman_gain_long = 2400.0;
    double kalman_gain_short = 2400.0;
    double distance_pct_init_long = 0.7;
    double distance_pct_min_long = 1.2;
    double distance_pct_init_short = 0.7;
    double distance_pct_min_short = 1.2;
    double distance_pct_decrement = 0.001;
};

std::unique_ptr<IStrategy> createCtmStrategy(const CtmParams& params = CtmParams{});

} // namespace backtest
