#pragma once

#include "bar.hpp"
#include "order.hpp"
#include <vector>
#include <string>
#include <cstddef>

namespace backtest {

/// Single filled trade for reporting.
struct Trade {
    std::string entry_time;
    std::string exit_time;
    Side side{Side::Long};
    double quantity{0};
    double entry_price{0};
    double exit_price{0};
    double pnl{0};
    double pnl_pct{0};
};

/// Simulates order execution and tracks positions, cash, and equity.
/// Orders placed during bar N are filled at bar N+1 open (avoids look-ahead).
/// At most one order can be pending at a time: placing a new order overwrites any previous pending order for the next bar.
/// Slippage: fraction of fill price (e.g. 0.001 = 0.1%). Longs fill at open*(1+slippage), shorts at open*(1-slippage).
class Simulator {
public:
    Simulator(double initial_cash = 100000.0, double commission_per_trade = 0.0, double slippage_fraction = 0.0);

    /// Process pending order: fill at current bar's open (with slippage applied).
    void processOrders(const Bar& bar);

    /// Add order to be filled on next bar. Overwrites any existing pending order.
    void placeOrder(Side side, double quantity);

    /// Update equity snapshot using current bar's close for position value.
    void updateEquity(const Bar& bar);

    double position() const { return position_; }
    double cash() const { return cash_; }
    double equity() const { return equity_; }
    double lastClose() const { return last_close_; }
    double avgEntryPrice() const { return avg_entry_; }
    const std::vector<Trade>& trades() const { return trades_; }
    const std::vector<double>& equityCurve() const { return equity_curve_; }

    void setLastClose(double c) { last_close_ = c; }

private:
    double initial_cash_;
    double commission_;
    double slippage_;  // fraction, e.g. 0.001 = 0.1%
    double cash_;
    double position_;       // signed: + long, - short
    double avg_entry_;      // average entry price for P&L
    double equity_;
    double last_close_;

    Order pending_order_;   // single pending (can extend to queue)
    bool has_pending_{false};

    std::vector<Trade> trades_;
    std::vector<double> equity_curve_;
    std::string last_bar_time_;
};

} // namespace backtest
