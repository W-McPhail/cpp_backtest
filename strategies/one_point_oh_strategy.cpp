#include "one_point_oh_strategy.hpp"
#include "context.hpp"
#include "bar.hpp"
#include <cmath>
#include <memory>
#include <vector>
#include <algorithm>
#include <limits>

namespace backtest {

namespace {

/// Linear regression on points (0, y[0]), (1, y[1]), ..., (n-1, y[n-1]).
/// Returns slope and intercept; value at x is intercept + slope * x.
void fitLine(const double* y, int n, double& slope, double& intercept) {
    if (n < 2) {
        slope = 0;
        intercept = n == 1 ? y[0] : 0;
        return;
    }
    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;
    for (int i = 0; i < n; ++i) {
        sum_x += i;
        sum_y += y[i];
        sum_xy += i * y[i];
        sum_xx += i * i;
    }
    double denom = n * sum_xx - sum_x * sum_x;
    if (std::fabs(denom) < 1e-20) {
        slope = 0;
        intercept = sum_y / n;
        return;
    }
    slope = (n * sum_xy - sum_x * sum_y) / denom;
    intercept = (sum_y - slope * sum_x) / n;
}

} // namespace

class OnePointOhStrategy : public IStrategy {
public:
    explicit OnePointOhStrategy(const OnePointOhParams& params) : p_(params) {}

    void onStart(IContext& /*ctx*/) override {
        in_position_ = false;
        entry_price_ = 0;
        stop_price_ = 0;
        target_price_ = 0;
        position_qty_ = 0;
        is_long_ = true;
    }

    void onBar(const Bar& bar, IContext& ctx) override {
        const std::vector<Bar>& bars = ctx.bars();
        const std::size_t i = ctx.barIndex();
        const int lookback = p_.lookback;
        const int stop_lookback = p_.stop_lookback;

        if (bar.close <= 0) return;

        // --- Exit logic: check stop and target (stop first, conservative) ---
        double pos = ctx.position();
        if (in_position_ && pos != 0) {
            int qty = static_cast<int>(std::abs(pos));
            if (qty <= 0) { in_position_ = false; return; }
            if (is_long_) {
                if (bar.low <= stop_price_) {
                    ctx.placeOrder(Side::Short, static_cast<double>(qty));
                    in_position_ = false;
                    return;
                }
                if (bar.high >= target_price_) {
                    ctx.placeOrder(Side::Short, static_cast<double>(qty));
                    in_position_ = false;
                    return;
                }
            } else {
                if (bar.high >= stop_price_) {
                    ctx.placeOrder(Side::Long, static_cast<double>(qty));
                    in_position_ = false;
                    return;
                }
                if (bar.low <= target_price_) {
                    ctx.placeOrder(Side::Long, static_cast<double>(qty));
                    in_position_ = false;
                    return;
                }
            }
        }

        // --- Entry: need enough history ---
        if (in_position_) return;
        if (i < static_cast<std::size_t>(lookback)) return;
        if (i < 1u) return;

        const Bar& prev_bar = bars[i - 1];
        double prev_close = prev_bar.close;
        double curr_close = bar.close;

        // Fit line to last lookback highs (x = 0..lookback-1, y = high)
        std::vector<double> highs(static_cast<std::size_t>(lookback));
        std::vector<double> lows(static_cast<std::size_t>(lookback));
        for (int k = 0; k < lookback; ++k) {
            std::size_t idx = i - lookback + 1 + static_cast<std::size_t>(k);
            highs[static_cast<std::size_t>(k)] = bars[idx].high;
            lows[static_cast<std::size_t>(k)] = bars[idx].low;
        }

        double slope_high = 0, intercept_high = 0;
        double slope_low = 0, intercept_low = 0;
        fitLine(highs.data(), lookback, slope_high, intercept_high);
        fitLine(lows.data(), lookback, slope_low, intercept_low);

        // Line value at previous bar (index lookback-2) and current bar (lookback-1)
        const int prev_x = lookback - 2;
        const int curr_x = lookback - 1;
        double line_high_prev = intercept_high + slope_high * prev_x;
        double line_high_curr = intercept_high + slope_high * curr_x;
        double line_low_prev = intercept_low + slope_low * prev_x;
        double line_low_curr = intercept_low + slope_low * curr_x;

        // Long: descending line on highs (slope < 0), close crosses above
        if (slope_high < 0 && prev_close <= line_high_prev && curr_close > line_high_curr) {
            // Stop = nearest local low (min of lows over last stop_lookback bars before current)
            std::size_t start = (i >= static_cast<std::size_t>(stop_lookback))
                ? i - static_cast<std::size_t>(stop_lookback) : 0;
            double stop = std::numeric_limits<double>::max();
            for (std::size_t k = start; k < i; ++k)
                if (bars[k].low < stop) stop = bars[k].low;
            if (stop >= curr_close) return;  // stop must be below entry
            double entry = curr_close;
            double risk = entry - stop;
            double target = entry + p_.risk_reward_ratio * risk;

            double eq = ctx.equity();
            if (eq <= 0 || risk <= 0) return;
            int qty = static_cast<int>((eq * p_.position_fraction) / entry);
            if (qty < 1) qty = 1;

            ctx.placeOrder(Side::Long, static_cast<double>(qty));
            in_position_ = true;
            entry_price_ = entry;
            stop_price_ = stop;
            target_price_ = target;
            position_qty_ = qty;
            is_long_ = true;
            return;
        }

        // Short: ascending line on lows (slope > 0), close crosses below
        if (slope_low > 0 && prev_close >= line_low_prev && curr_close < line_low_curr) {
            std::size_t start = (i >= static_cast<std::size_t>(stop_lookback))
                ? i - static_cast<std::size_t>(stop_lookback) : 0;
            double stop = -std::numeric_limits<double>::max();
            for (std::size_t k = start; k < i; ++k)
                if (bars[k].high > stop) stop = bars[k].high;
            if (stop <= curr_close) return;  // stop must be above entry
            double entry = curr_close;
            double risk = stop - entry;
            double target = entry - p_.risk_reward_ratio * risk;

            double eq = ctx.equity();
            if (eq <= 0 || risk <= 0) return;
            int qty = static_cast<int>((eq * p_.position_fraction) / entry);
            if (qty < 1) qty = 1;

            ctx.placeOrder(Side::Short, static_cast<double>(qty));
            in_position_ = true;
            entry_price_ = entry;
            stop_price_ = stop;
            target_price_ = target;
            position_qty_ = qty;
            is_long_ = false;
        }
    }

private:
    OnePointOhParams p_;
    bool in_position_{false};
    double entry_price_{0};
    double stop_price_{0};
    double target_price_{0};
    int position_qty_{0};
    bool is_long_{true};
};

std::unique_ptr<IStrategy> createOnePointOhStrategy(const OnePointOhParams& params) {
    return std::make_unique<OnePointOhStrategy>(params);
}

} // namespace backtest
