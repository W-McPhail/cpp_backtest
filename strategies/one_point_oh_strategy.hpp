#pragma once

#include "strategy.hpp"
#include <memory>

namespace backtest {

/// OnePointOh: lines of best fit on highs/lows; enter when close crosses the line.
/// Long when close breaks above a *descending* line (fitted to highs).
/// Short when close breaks below an *ascending* line (fitted to lows).
/// Exit at 3:1 risk-reward; stop at nearest local low (long) or high (short).
struct OnePointOhParams {
    int lookback = 20;           // bars for linear regression (highs and lows)
    int stop_lookback = 20;     // bars to find nearest local high/low for stop
    double position_fraction = 0.15;  // fraction of equity per trade
    double risk_reward_ratio = 3.0;  // take profit = entry + R:R * (entry - stop) for long
};

std::unique_ptr<IStrategy> createOnePointOhStrategy(const OnePointOhParams& params = OnePointOhParams{});

} // namespace backtest
