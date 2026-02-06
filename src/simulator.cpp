#include "simulator.hpp"
#include <cmath>
#include <algorithm>

namespace backtest {

namespace {
    constexpr double POSITION_ZERO_EPS = 1e-9;
}

Simulator::Simulator(double initial_cash, double commission_per_trade)
    : initial_cash_(initial_cash)
    , commission_(commission_per_trade)
    , cash_(initial_cash)
    , position_(0)
    , avg_entry_(0)
    , equity_(initial_cash)
    , last_close_(0)
{
}

void Simulator::placeOrder(Side side, double quantity) {
    if (quantity <= 0) return;
    pending_order_.side = side;
    pending_order_.quantity = quantity;
    pending_order_.type = OrderType::Market;
    has_pending_ = true;
}

void Simulator::processOrders(const Bar& bar) {
    if (!has_pending_) return;

    double fill_price = bar.open;  // fill at bar open (no look-ahead)
    double qty = pending_order_.quantity;
    Side side = pending_order_.side;

    // Close or reduce opposite position first
    if ((side == Side::Long && position_ < 0) || (side == Side::Short && position_ > 0)) {
        double close_qty = std::min(qty, std::abs(position_));
        double pnl = (side == Side::Long)
            ? (avg_entry_ - fill_price) * close_qty   // was short, buy to cover
            : (fill_price - avg_entry_) * close_qty;  // was long, sell to close
        cash_ += pnl - commission_;
        equity_ = cash_ + position_ * fill_price;  // will update position_ below

        Trade t;
        t.entry_time = last_bar_time_;
        t.exit_time = bar.timestamp;
        t.side = position_ > 0 ? Side::Long : Side::Short;
        t.quantity = close_qty;
        t.entry_price = avg_entry_;
        t.exit_price = fill_price;
        t.pnl = pnl - commission_;
        t.pnl_pct = (t.entry_price != 0) ? (t.pnl / (t.entry_price * close_qty)) * 100.0 : 0;
        trades_.push_back(t);

        if (position_ > 0) {
            position_ -= close_qty;
            if (std::abs(position_) < POSITION_ZERO_EPS) position_ = 0;
        } else {
            position_ += close_qty;
            if (std::abs(position_) < POSITION_ZERO_EPS) position_ = 0;
        }
        qty -= close_qty;
        if (qty <= 0) {
            has_pending_ = false;
            last_bar_time_ = bar.timestamp;
            return;
        }
        if (std::abs(position_) < POSITION_ZERO_EPS) avg_entry_ = 0;
    }

    // Open or add to position
    if (qty > 0) {
        double cost = fill_price * qty;
        if (side == Side::Short) cost = -cost;
        cash_ -= cost - commission_;

        if (position_ == 0) {
            avg_entry_ = fill_price;
            position_ = (side == Side::Long) ? qty : -qty;
        } else {
            double total_qty = std::abs(position_) + qty;
            avg_entry_ = (avg_entry_ * std::abs(position_) + fill_price * qty) / total_qty;
            position_ += (side == Side::Long) ? qty : -qty;
        }
    }

    has_pending_ = false;
    last_bar_time_ = bar.timestamp;
}

void Simulator::updateEquity(const Bar& bar) {
    last_close_ = bar.close;
    equity_ = cash_ + position_ * bar.close;
    equity_curve_.push_back(equity_);
    last_bar_time_ = bar.timestamp;
}

} // namespace backtest
