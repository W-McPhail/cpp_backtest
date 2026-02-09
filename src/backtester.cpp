#include "backtester.hpp"
#include "context.hpp"
#include "simulator.hpp"
#include "strategy.hpp"

namespace backtest {

BacktestContext::BacktestContext(Simulator& sim, const std::vector<Bar>& bars)
    : sim_(sim), bars_(bars) {}

void BacktestContext::placeOrder(Side side, double quantity) {
    sim_.placeOrder(side, quantity);
}

double BacktestContext::position() const { return sim_.position(); }
double BacktestContext::equity() const { return sim_.equity(); }
double BacktestContext::cash() const { return sim_.cash(); }
double BacktestContext::lastClose() const { return sim_.lastClose(); }
std::size_t BacktestContext::barIndex() const { return bar_index_; }
const std::vector<Bar>& BacktestContext::bars() const { return bars_; }

Backtester::Backtester(std::unique_ptr<IStrategy> strategy,
                       const std::string& data_path,
                       double initial_cash,
                       double commission,
                       const std::string& databento_dir,
                       const std::string& symbol_filter,
                       const std::string& bar_resolution,
                       double slippage)
    : strategy_(std::move(strategy))
    , data_(data_path)
    , initial_cash_(initial_cash)
    , databento_dir_(databento_dir)
    , symbol_filter_(symbol_filter)
    , bar_resolution_(bar_resolution.empty() ? "1m" : bar_resolution)
    , sim_(std::make_unique<Simulator>(initial_cash, commission, slippage))
{
}

bool Backtester::run() {
    bool ok = !databento_dir_.empty()
        ? data_.loadFromDatabentoDir(databento_dir_, symbol_filter_)
        : data_.load();
    if (!ok || data_.empty()) return false;

    data_.aggregateBars(bar_resolution_);

    ctx_ = std::make_unique<BacktestContext>(*sim_, data_.bars());
    strategy_->onStart(*ctx_);

    double peak_equity = initial_cash_;
    for (std::size_t i = 0; i < data_.size(); ++i) {
        const Bar& bar = data_.at(i);
        ctx_->setBarIndex(i);

        // 1. Process orders from previous bar (fill at this bar's open)
        sim_->processOrders(bar);

        // Equity after fill uses bar open (we just filled at open). Don't use sim_->equity() here
        // because it's only updated in updateEquity(bar), so it would be stale.
        double eq_after_fill = sim_->cash() + sim_->position() * bar.open;
        if (eq_after_fill <= 0) {
            stopped_early_ = true;
            stop_reason_ = "no more equity";
            sim_->updateEquity(bar);  // record final equity at bar close for report
            break;
        }

        // 2. Strategy sees current bar and can place orders (filled next bar)
        strategy_->onBar(bar, *ctx_);

        // 3. Update equity at this bar's close (used for curve and next bar's checks)
        sim_->updateEquity(bar);

        double eq = sim_->equity();
        if (eq > peak_equity) peak_equity = eq;
        double drawdown_pct = (peak_equity > 0) ? ((peak_equity - eq) / peak_equity * 100.0) : 100.0;

        if (eq <= 0) {
            stopped_early_ = true;
            stop_reason_ = "no more equity";
            break;
        }
        if (drawdown_pct >= 100.0) {
            stopped_early_ = true;
            stop_reason_ = "max drawdown 100%";
            break;
        }
    }

    strategy_->onEnd(*ctx_);
    return true;
}

} // namespace backtest
