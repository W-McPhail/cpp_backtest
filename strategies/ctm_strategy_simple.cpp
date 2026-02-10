#include "ctm_strategy_simple.hpp"
#include "context.hpp"
#include "bar.hpp"
#include <vector>
#include <cmath>
#include <algorithm>
#include <memory>

namespace backtest {

namespace {

double sma(const std::vector<Bar>& bars, std::size_t end_index, int period) {
    if (period <= 0 || end_index < static_cast<std::size_t>(period)) return 0;
    double sum = 0;
    for (int i = 0; i < period; ++i)
        sum += bars[end_index - 1 - i].close;
    return sum / period;
}

// Kalman smoothing: price -> smoothed price. State: kalman_price, kalman_velo.
inline double kalman_smooth(double price, double prev_kalman_price, double prev_kalman_velo,
                            double kalman_gain, double& out_kalman_price, double& out_kalman_velo) {
    double distance = price - prev_kalman_price;
    double smooth = prev_kalman_price + distance * std::sqrt(kalman_gain / 10000.0 * 2.0);
    out_kalman_velo = prev_kalman_velo + (kalman_gain / 10000.0) * distance;
    out_kalman_price = smooth + out_kalman_velo;
    return out_kalman_price;
}

// Loft trend: 1 = UP, -1 = DOWN. Returns (trend_direction, loft_moved).
inline void loft_trend(double price, int prev_trend, double prev_loft_level, double prev_dist_pct,
                       double dist_init, double dist_min, double dist_decrement,
                       int& out_trend, double& out_loft_level, double& out_dist_pct, bool& out_loft_moved) {
    const int UP = 1, DOWN = -1;
    out_loft_moved = false;
    out_dist_pct = prev_dist_pct;
    out_loft_level = prev_loft_level;
    out_trend = prev_trend;

    if (prev_trend == UP) {
        out_loft_level = price * (1.0 - out_dist_pct / 100.0);
        if (prev_trend == UP) {  // prev was UP
            if (out_loft_level <= prev_loft_level) {
                out_loft_level = prev_loft_level;
            } else {
                out_dist_pct = std::max(out_dist_pct - dist_decrement, dist_min);
                out_loft_moved = true;
            }
        }
        if (price < out_loft_level) {
            out_trend = DOWN;
            out_dist_pct = dist_init;
            out_loft_level = price * (1.0 + out_dist_pct / 100.0);
        }
    } else {
        out_loft_level = price * (1.0 + out_dist_pct / 100.0);
        if (prev_trend == DOWN) {
            if (out_loft_level >= prev_loft_level) {
                out_loft_level = prev_loft_level;
            } else {
                out_dist_pct = std::max(out_dist_pct - dist_decrement, dist_min);
                out_loft_moved = true;
            }
        }
        if (price > out_loft_level) {
            out_trend = UP;
            out_dist_pct = dist_init;
            out_loft_level = price * (1.0 - out_dist_pct / 100.0);
        }
    }
}

} // namespace

class CtmStrategy : public IStrategy {
public:
    explicit CtmStrategy(const CtmParams& params) : p_(params) {}

    void onStart(IContext& /*ctx*/) override {
        prev_distance_long_ = 0;
        prev_distance_short_ = 0;
        has_prev_ = false;
        kalman_initialized_long_ = false;
        kalman_initialized_short_ = false;
        loft_trend_long_ = 1;
        loft_trend_short_ = -1;
        loft_level_long_ = 0;
        loft_level_short_ = 0;
        loft_dist_pct_long_ = 0;
        loft_dist_pct_short_ = 0;
    }

    void onBar(const Bar& bar, IContext& ctx) override {
        const auto& bars = ctx.bars();
        std::size_t n = ctx.barIndex() + 1;
        double price = bar.close;
        if (price <= 0) return;

        // Kalman smoothing (run from first bar when filter enabled)
        double kalman_long = price, kalman_short = price;
        if (p_.use_kalman_trend_long) {
            if (!kalman_initialized_long_) {
                kalman_price_long_ = price;
                kalman_velo_long_ = 0;
                kalman_initialized_long_ = true;
                kalman_long = price;
            } else {
                kalman_long = kalman_smooth(price, kalman_price_long_, kalman_velo_long_,
                    p_.kalman_gain_long, kalman_price_long_, kalman_velo_long_);
            }
        }
        if (p_.use_kalman_trend_short) {
            if (!kalman_initialized_short_) {
                kalman_price_short_ = price;
                kalman_velo_short_ = 0;
                kalman_initialized_short_ = true;
                kalman_short = price;
            } else {
                kalman_short = kalman_smooth(price, kalman_price_short_, kalman_velo_short_,
                    p_.kalman_gain_short, kalman_price_short_, kalman_velo_short_);
            }
        }

        // Loft trend on Kalman price (when filter enabled)
        bool loft_moved_long = false, loft_moved_short = false;
        if (p_.use_kalman_trend_long) {
            if (loft_dist_pct_long_ == 0) loft_dist_pct_long_ = p_.distance_pct_init_long;
            double new_level_long;
            loft_trend(kalman_long, loft_trend_long_, loft_level_long_, loft_dist_pct_long_,
                p_.distance_pct_init_long, p_.distance_pct_min_long, p_.distance_pct_decrement,
                loft_trend_long_, new_level_long, loft_dist_pct_long_, loft_moved_long);
            loft_level_long_ = new_level_long;
        }
        if (p_.use_kalman_trend_short) {
            if (loft_dist_pct_short_ == 0) loft_dist_pct_short_ = p_.distance_pct_init_short;
            double new_level_short;
            loft_trend(kalman_short, loft_trend_short_, loft_level_short_, loft_dist_pct_short_,
                p_.distance_pct_init_short, p_.distance_pct_min_short, p_.distance_pct_decrement,
                loft_trend_short_, new_level_short, loft_dist_pct_short_, loft_moved_short);
            loft_level_short_ = new_level_short;
        }

        // Need enough bars for slowest SMA
        int max_period = std::max({ p_.long_fast, p_.long_medium, p_.long_slow,
                                    p_.short_fast, p_.short_medium, p_.short_slow });
        if (n < static_cast<std::size_t>(max_period)) {
            has_prev_ = false;
            return;
        }

        // Long: distance_long = min(price - sma_fast, price - sma_med, price - sma_slow)
        double lf = sma(bars, n, p_.long_fast);
        double lm = sma(bars, n, p_.long_medium);
        double ls = sma(bars, n, p_.long_slow);
        double distance_long = std::min({ price - lf, price - lm, price - ls });

        // Short: distance_short = max(price - sma_fast, price - sma_med, price - sma_slow)
        double sf = sma(bars, n, p_.short_fast);
        double sm = sma(bars, n, p_.short_medium);
        double ss = sma(bars, n, p_.short_slow);
        double distance_short = std::max({ price - sf, price - sm, price - ss });

        double pos = ctx.position();

        // 1) Exits (close long / close short on cross)
        bool close_long = p_.long_trades && has_prev_ && prev_distance_long_ >= 0 && distance_long < 0;
        bool close_short = p_.short_trades && has_prev_ && prev_distance_short_ <= 0 && distance_short > 0;

        if (close_long && pos > 0) {
            ctx.placeOrder(Side::Short, static_cast<double>(static_cast<int>(pos)));
            prev_distance_long_ = distance_long;
            prev_distance_short_ = distance_short;
            has_prev_ = true;
            return;
        }
        if (close_short && pos < 0) {
            ctx.placeOrder(Side::Long, static_cast<double>(static_cast<int>(-pos)));
            prev_distance_long_ = distance_long;
            prev_distance_short_ = distance_short;
            has_prev_ = true;
            return;
        }

        // 2) Entries (only when flat). When Kalman trend filter is on, require loft_moved && trend == UP/DOWN.
        bool enter_long = false;
        if (p_.long_trades && pos <= 0) {
            if (p_.long_enter_on_cross_only)
                enter_long = has_prev_ && prev_distance_long_ <= 0 && distance_long > 0;
            else
                enter_long = distance_long > 0;
            if (p_.use_kalman_trend_long)
                enter_long = enter_long && loft_moved_long && (loft_trend_long_ == 1);
        }
        bool enter_short = false;
        if (p_.short_trades && pos >= 0) {
            if (p_.short_enter_on_cross_only)
                enter_short = has_prev_ && prev_distance_short_ >= 0 && distance_short < 0;
            else
                enter_short = distance_short < 0;
            if (p_.use_kalman_trend_short)
                enter_short = enter_short && loft_moved_short && (loft_trend_short_ == -1);
        }

        if (enter_long && pos == 0) {
            double units = std::floor(ctx.equity() / price * p_.position_equity_pct_long);
            if (units < 1.0) units = 1.0;
            ctx.placeOrder(Side::Long, units);
        } else if (enter_short && pos == 0) {
            double units = std::floor(ctx.equity() / price * p_.position_equity_pct_short);
            if (units < 1.0) units = 1.0;
            ctx.placeOrder(Side::Short, units);
        }

        prev_distance_long_ = distance_long;
        prev_distance_short_ = distance_short;
        has_prev_ = true;
    }

    void onEnd(IContext& /*ctx*/) override {}

private:
    CtmParams p_;
    double prev_distance_long_ = 0;
    double prev_distance_short_ = 0;
    bool has_prev_ = false;

    // Kalman smoothing state
    bool kalman_initialized_long_ = false;
    bool kalman_initialized_short_ = false;
    double kalman_price_long_ = 0;
    double kalman_velo_long_ = 0;
    double kalman_price_short_ = 0;
    double kalman_velo_short_ = 0;

    // Loft trend state (1 = UP, -1 = DOWN)
    int loft_trend_long_ = 1;
    int loft_trend_short_ = -1;
    double loft_level_long_ = 0;
    double loft_level_short_ = 0;
    double loft_dist_pct_long_ = 0;
    double loft_dist_pct_short_ = 0;
};

std::unique_ptr<IStrategy> createCtmStrategy(const CtmParams& params) {
    return std::make_unique<CtmStrategy>(params);
}

} // namespace backtest
