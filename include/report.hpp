#pragma once

#include "simulator.hpp"
#include "data_source.hpp"
#include <string>
#include <ostream>
#include <iostream>

namespace backtest {

/// Backtest metrics for reporting.
struct BacktestMetrics {
    double total_return_pct{0};   // (final_equity - initial) / initial * 100
    double max_drawdown_pct{0};   // max peak-to-trough decline %
    double sharpe_ratio{0};       // annualized Sharpe (0 if < 2 bars)
    int num_trades{0};            // closed round-trip trades only
    int winning_trades{0};
    double win_rate_pct{0};
    double avg_trade_pnl{0};
    double initial_equity{0};
    double final_equity{0};
    double open_position{0};       // shares at end (+ long, - short); 0 = flat
    double unrealized_pnl{0};     // mark-to-market P&L on open position
};

class Report {
public:
    /// strategy_name and strategy_params are included in report output (e.g. "sma_crossover", "fast=10 slow=30").
    Report(const Simulator& sim, const DataSource& data, double initial_cash,
           const std::string& strategy_name = "",
           const std::string& strategy_params = "");

private:
    void printReportHeader(std::ostream& out) const;

public:

    /// Compute all metrics from simulator and equity curve.
    BacktestMetrics computeMetrics();

    /// Print summary to console.
    void printSummary(std::ostream& out = std::cout) const;

    /// Write trade log CSV to file. Returns false and logs to stderr on failure.
    bool writeTradeLog(const std::string& filepath) const;

    /// Write equity curve CSV to file. Returns false and logs to stderr on failure.
    bool writeEquityCurve(const std::string& filepath) const;

    /// Write full report to a text file. Returns false and logs to stderr on failure.
    bool writeReport(const std::string& filepath) const;

    /// Write bars + trades as JSON for the chart viewer (session.json). Returns false on failure.
    bool writeSessionJson(const std::string& filepath,
                          const std::string& symbol_or_label = "") const;

    void setMetrics(const BacktestMetrics& m) { metrics_ = m; }
    const BacktestMetrics& metrics() const { return metrics_; }

    void setStoppedReason(const std::string& reason) { stopped_reason_ = reason; }

private:
    const Simulator& sim_;
    const DataSource& data_;
    double initial_cash_;
    std::string strategy_name_;
    std::string strategy_params_;
    std::string stopped_reason_;
    BacktestMetrics metrics_;
};

} // namespace backtest
