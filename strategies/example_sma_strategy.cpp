#include "example_sma_strategy.hpp"
#include "context.hpp"
#include "bar.hpp"
#include <vector>
#include <numeric>
#include <cmath>
#include <memory>

namespace backtest {

/// SMA 9/21: long when fast > slow, short when fast < slow.
/// Exit: close long when fast < slow, close short when fast > slow.
class SmaCrossoverStrategy : public IStrategy {
public:
    SmaCrossoverStrategy(int fast_period = 9, int slow_period = 21, double position_size = 1.0)
        : fast_period_(fast_period)
        , slow_period_(slow_period)
        , position_size_(position_size)
    {}

    void onStart(IContext& /*ctx*/) override {}

    void onBar(const Bar& bar, IContext& ctx) override {
        const auto& bars = ctx.bars();
        // Use only bars up to and including current bar (no look-ahead)
        std::size_t n = ctx.barIndex() + 1;
        if (n < static_cast<std::size_t>(slow_period_)) return;

        double fast_sma = sma(bars, n, fast_period_);
        double slow_sma = sma(bars, n, slow_period_);
        double current_pos = ctx.position();
        double price = bar.close;

        if (price <= 0) return;

        double units = position_size_ * (ctx.equity() / price);
        if (units < 1.0) units = 1.0;

        // 1) Exit conditions: close existing trades when signal flips
        if (current_pos > 0 && fast_sma < slow_sma) {
            // Close long: sell exactly current position
            ctx.placeOrder(Side::Short, static_cast<double>(static_cast<int>(current_pos)));
            return;
        }
        if (current_pos < 0 && fast_sma > slow_sma) {
            // Close short: buy to cover exactly |position|
            ctx.placeOrder(Side::Long, static_cast<double>(static_cast<int>(-current_pos)));
            return;
        }

        // 2) Entry when flat: long when fast > slow, short when fast < slow (crossover preferred but not required for first entry)
        if (current_pos != 0) return;

        if (fast_sma > slow_sma) {
            ctx.placeOrder(Side::Long, std::floor(units));
        } else if (fast_sma < slow_sma) {
            ctx.placeOrder(Side::Short, std::floor(units));
        }
    }

    void onEnd(IContext& /*ctx*/) override {}

private:
    static double sma(const std::vector<Bar>& bars, std::size_t end_index, int period) {
        if (period <= 0 || end_index < static_cast<std::size_t>(period)) return 0;
        double sum = 0;
        for (int i = 0; i < period; ++i) {
            sum += bars[end_index - 1 - i].close;
        }
        return sum / period;
    }

    int fast_period_;
    int slow_period_;
    double position_size_;
};

} // namespace backtest

namespace backtest {

std::unique_ptr<IStrategy> createSmaCrossoverStrategy(int fast, int slow, double size) {
    return std::make_unique<SmaCrossoverStrategy>(fast, slow, size);
}

} // namespace backtest
