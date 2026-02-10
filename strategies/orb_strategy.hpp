#pragma once

#include "strategy.hpp"
#include <memory>
#include <string>

namespace backtest {

/// ORB (Opening Range Breakout): 9:30 bar = opening range (high/low); 9:45 bar = trigger.
/// Position size = position_equity_pct of equity per day (default 15%). Closes at end of day (exit_at_eod).
/// Only bars with time == session_start_hour:session_start_minute are treated as the 9:30 bar.
struct OrbParams {
    double position_equity_pct = 0.15;  // 15% of equity per day
    bool exit_at_eod = true;            // close position at end of day
    int session_start_hour = 9;    // 9:30 ET = 9
    int session_start_minute = 30; // 9:30 ET = 30 (use 14,30 if timestamps are UTC)
};

std::unique_ptr<IStrategy> createOrbStrategy(const OrbParams& params = OrbParams{});

} // namespace backtest
