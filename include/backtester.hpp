#pragma once

#include "bar.hpp"
#include "strategy.hpp"
#include "context.hpp"
#include "data_source.hpp"
#include "simulator.hpp"
#include <memory>
#include <string>

namespace backtest {

/// Concrete context implementation passed to the strategy.
class BacktestContext : public IContext {
public:
    explicit BacktestContext(Simulator& sim, const std::vector<Bar>& bars);

    void placeOrder(Side side, double quantity) override;
    double position() const override;
    double equity() const override;
    double cash() const override;
    double lastClose() const override;
    std::size_t barIndex() const override;
    const std::vector<Bar>& bars() const override;

    void setBarIndex(std::size_t i) { bar_index_ = i; }

private:
    Simulator& sim_;
    const std::vector<Bar>& bars_;
    std::size_t bar_index_{0};
};

/// Orchestrates the backtest: feed bars to strategy, run simulator, collect results.
class Backtester {
public:
    /// If databento_dir non-empty, load from that folder (filename = bar data); else load from data_path CSV.
    /// symbol_filter: when using databento, load only this symbol (e.g. "NQU5"); empty = all.
    /// bar_resolution: "1m" (default), "15m", or "1h" — aggregate 1m bars to that timeframe before backtest.
    Backtester(std::unique_ptr<IStrategy> strategy,
              const std::string& data_path,
              double initial_cash = 100000.0,
              double commission = 0.0,
              const std::string& databento_dir = "",
              const std::string& symbol_filter = "",
              const std::string& bar_resolution = "1m");

    /// Run the backtest. Returns false if data failed to load.
    /// If equity <= 0 or max drawdown >= 100%, stops early and sets stoppedEarly() / stopReason().
    bool run();

    const Simulator& simulator() const { return *sim_; }
    Simulator& simulator() { return *sim_; }
    const std::vector<Bar>& bars() const { return data_.bars(); }
    const DataSource& data() const { return data_; }

    bool stoppedEarly() const { return stopped_early_; }
    const std::string& stopReason() const { return stop_reason_; }

private:
    std::unique_ptr<IStrategy> strategy_;
    DataSource data_;
    double initial_cash_;
    std::string databento_dir_;
    std::string symbol_filter_;
    std::string bar_resolution_;
    std::unique_ptr<Simulator> sim_;
    std::unique_ptr<BacktestContext> ctx_;
    bool stopped_early_{false};
    std::string stop_reason_;
};

} // namespace backtest
