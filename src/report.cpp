#include "report.hpp"
#include <fstream>
#include <iomanip>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <iostream>

namespace backtest {

Report::Report(const Simulator& sim, const DataSource& data, double initial_cash,
               const std::string& strategy_name, const std::string& strategy_params)
    : sim_(sim), data_(data), initial_cash_(initial_cash)
    , strategy_name_(strategy_name), strategy_params_(strategy_params) {}

void Report::printReportHeader(std::ostream& out) const {
    if (!strategy_name_.empty()) {
        out << "Strategy: " << strategy_name_;
        if (!strategy_params_.empty()) out << " (" << strategy_params_ << ")";
        out << "\n";
    }
}

BacktestMetrics Report::computeMetrics() {
    BacktestMetrics m;
    m.initial_equity = initial_cash_;
    m.final_equity = sim_.equity();
    m.total_return_pct = (initial_cash_ != 0)
        ? ((m.final_equity - initial_cash_) / initial_cash_) * 100.0
        : 0;

    const auto& curve = sim_.equityCurve();
    if (curve.empty()) return m;

    double peak = curve[0];
    double max_dd = 0;
    for (double eq : curve) {
        if (eq > peak) peak = eq;
        double dd = (peak != 0) ? (peak - eq) / peak * 100.0 : 0;
        if (dd > max_dd) max_dd = dd;
    }
    m.max_drawdown_pct = max_dd;

    // Sharpe: mean and std of period returns, annualized (trading days per year)
    constexpr double TRADING_DAYS_PER_YEAR = 252.0;
    if (curve.size() >= 2) {
        std::vector<double> returns;
        returns.reserve(curve.size() - 1);
        for (std::size_t i = 1; i < curve.size(); ++i) {
            if (curve[i - 1] != 0)
                returns.push_back((curve[i] - curve[i - 1]) / curve[i - 1]);
            else
                returns.push_back(0);
        }
        double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / static_cast<double>(returns.size());
        double sq_sum = 0;
        for (double r : returns) sq_sum += (r - mean) * (r - mean);
        double stddev = (returns.size() > 1) ? std::sqrt(sq_sum / (returns.size() - 1)) : 0;
        m.sharpe_ratio = (stddev != 0) ? (mean / stddev) * std::sqrt(TRADING_DAYS_PER_YEAR) : 0;
    }

    const auto& trades = sim_.trades();
    m.num_trades = static_cast<int>(trades.size());
    int wins = 0;
    double total_pnl = 0;
    for (const auto& t : trades) {
        if (t.pnl > 0) ++wins;
        total_pnl += t.pnl;
    }
    m.winning_trades = wins;
    m.win_rate_pct = (m.num_trades > 0) ? (100.0 * wins / m.num_trades) : 0;
    m.avg_trade_pnl = (m.num_trades > 0) ? (total_pnl / m.num_trades) : 0;

    // Open position at end (only closed trades counted in num_trades)
    double pos = sim_.position();
    double avg_entry = sim_.avgEntryPrice();
    double last_close = sim_.lastClose();
    m.open_position = pos;
    if (std::abs(pos) >= 1e-9 && last_close > 0)
        m.unrealized_pnl = pos * (last_close - avg_entry);

    return m;
}

void Report::printSummary(std::ostream& out) const {
    out << "\n========== Backtest Report ==========\n";
    if (!stopped_reason_.empty())
        out << "*** Backtest stopped: " << stopped_reason_ << " ***\n\n";
    printReportHeader(out);
    out << std::fixed << std::setprecision(2);
    out << "Bars loaded:   " << data_.size() << "\n";
    out << "Initial equity:  " << metrics_.initial_equity << "\n";
    out << "Final equity:   " << metrics_.final_equity << "\n";
    out << "Total return:   " << metrics_.total_return_pct << "%\n";
    out << "Max drawdown:   " << std::min(metrics_.max_drawdown_pct, 100.0) << "%\n";
    out << "Sharpe ratio:   " << std::setprecision(3) << metrics_.sharpe_ratio << "\n";
    out << std::setprecision(2);
    out << "Closed trades:  " << metrics_.num_trades << "\n";
    out << "Winning trades: " << metrics_.winning_trades << "\n";
    out << "Win rate:       " << metrics_.win_rate_pct << "%\n";
    out << "Avg trade P&L:  " << metrics_.avg_trade_pnl << "\n";
    if (std::abs(metrics_.open_position) >= 1e-9) {
        out << "Open position:   " << metrics_.open_position
            << (metrics_.open_position > 0 ? " (long)" : " (short)") << "\n";
        out << "Unrealized P&L:  " << metrics_.unrealized_pnl << "\n";
    }
    out << "======================================\n\n";
}

namespace {
    void writeCsvQuoted(std::ostream& out, const std::string& s) {
        out << '"';
        for (char c : s) {
            if (c == '"') out << "\"\"";
            else out << c;
        }
        out << '"';
    }

    void writeJsonString(std::ostream& out, const std::string& s) {
        out << '"';
        for (char c : s) {
            if (c == '"') out << "\\\"";
            else if (c == '\\') out << "\\\\";
            else if (c == '\n') out << "\\n";
            else if (c == '\r') out << "\\r";
            else out << c;
        }
        out << '"';
    }
}

bool Report::writeTradeLog(const std::string& filepath) const {
    std::ofstream f(filepath);
    if (!f) {
        std::cerr << "Failed to open for writing: " << filepath << "\n";
        return false;
    }
    f << static_cast<char>(0xEF) << static_cast<char>(0xBB) << static_cast<char>(0xBF);
    f << "entry_time,exit_time,side,quantity,entry_price,exit_price,pnl,pnl_pct\n";
    f << std::fixed << std::setprecision(2);
    for (const auto& t : sim_.trades()) {
        writeCsvQuoted(f, t.entry_time);
        f << ',';
        writeCsvQuoted(f, t.exit_time);
        f << ',' << (t.side == Side::Long ? "long" : "short") << ','
          << t.quantity << ',' << t.entry_price << ',' << t.exit_price << ','
          << t.pnl << ',' << t.pnl_pct << "\n";
    }
    if (!f) {
        std::cerr << "Failed to write trade log: " << filepath << "\n";
        return false;
    }
    return true;
}

bool Report::writeEquityCurve(const std::string& filepath) const {
    std::ofstream f(filepath);
    if (!f) {
        std::cerr << "Failed to open for writing: " << filepath << "\n";
        return false;
    }
    f << static_cast<char>(0xEF) << static_cast<char>(0xBB) << static_cast<char>(0xBF);
    f << "bar_index,timestamp,equity\n";
    f << std::fixed << std::setprecision(2);
    const auto& curve = sim_.equityCurve();
    const std::size_t n = std::min(curve.size(), data_.size());
    for (std::size_t i = 0; i < n; ++i) {
        f << i << ',';
        writeCsvQuoted(f, data_.at(i).timestamp);
        f << ',' << curve[i] << "\n";
    }
    for (std::size_t i = n; i < curve.size(); ++i)
        f << i << ",\"\"," << curve[i] << "\n";
    if (!f) {
        std::cerr << "Failed to write equity curve: " << filepath << "\n";
        return false;
    }
    return true;
}

bool Report::writeReport(const std::string& filepath) const {
    std::ofstream f(filepath);
    if (!f) {
        std::cerr << "Failed to open for writing: " << filepath << "\n";
        return false;
    }
    f << "Backtest Report\n";
    f << "================\n\n";
    if (!stopped_reason_.empty())
        f << "*** Backtest stopped: " << stopped_reason_ << " ***\n\n";
    if (!strategy_name_.empty()) {
        f << "Strategy: " << strategy_name_;
        if (!strategy_params_.empty()) f << " (" << strategy_params_ << ")";
        f << "\n\n";
    }
    f << std::fixed << std::setprecision(2);
    f << "Initial equity:  " << metrics_.initial_equity << "\n";
    f << "Final equity:   " << metrics_.final_equity << "\n";
    f << "Total return:   " << metrics_.total_return_pct << "%\n";
    f << "Max drawdown:   " << std::min(metrics_.max_drawdown_pct, 100.0) << "%\n";
    f << std::setprecision(3);
    f << "Sharpe ratio:   " << metrics_.sharpe_ratio << "\n";
    f << std::setprecision(2);
    f << "Closed trades:  " << metrics_.num_trades << "\n";
    f << "Winning trades: " << metrics_.winning_trades << "\n";
    f << "Win rate:       " << metrics_.win_rate_pct << "%\n";
    f << "Avg trade P&L:  " << metrics_.avg_trade_pnl << "\n";
    if (std::abs(metrics_.open_position) >= 1e-9) {
        f << "Open position:   " << metrics_.open_position
          << (metrics_.open_position > 0 ? " (long)" : " (short)") << "\n";
        f << "Unrealized P&L:  " << metrics_.unrealized_pnl << "\n";
    }
    return f ? true : (std::cerr << "Failed to write report: " << filepath << "\n", false);
}

bool Report::writeSessionJson(const std::string& filepath, const std::string& symbol_or_label) const {
    std::ofstream f(filepath);
    if (!f) {
        std::cerr << "Failed to open for writing: " << filepath << "\n";
        return false;
    }
    f << std::fixed << std::setprecision(4);
    f << "{\n  \"symbol\": ";
    writeJsonString(f, symbol_or_label.empty() ? "backtest" : symbol_or_label);
    f << ",\n  \"strategy\": ";
    writeJsonString(f, strategy_name_);
    f << ",\n  \"params\": ";
    writeJsonString(f, strategy_params_);
    f << ",\n  \"bars\": [\n";
    const auto& bars = data_.bars();
    for (std::size_t i = 0; i < bars.size(); ++i) {
        const auto& b = bars[i];
        f << "    {\"t\":";
        writeJsonString(f, b.timestamp);
        f << ",\"o\":" << b.open << ",\"h\":" << b.high << ",\"l\":" << b.low << ",\"c\":" << b.close;
        if (b.volume != 0) f << ",\"v\":" << b.volume;
        f << "}";
        if (i + 1 < bars.size()) f << ",";
        f << "\n";
    }
    f << "  ],\n  \"trades\": [\n";
    const auto& trades = sim_.trades();
    for (std::size_t i = 0; i < trades.size(); ++i) {
        const auto& t = trades[i];
        f << "    {\"entry_time\":";
        writeJsonString(f, t.entry_time);
        f << ",\"exit_time\":";
        writeJsonString(f, t.exit_time);
        f << ",\"side\":\"" << (t.side == Side::Long ? "long" : "short") << "\""
          << ",\"entry_price\":" << t.entry_price << ",\"exit_price\":" << t.exit_price
          << ",\"quantity\":" << t.quantity << ",\"pnl\":" << t.pnl << "}";
        if (i + 1 < trades.size()) f << ",";
        f << "\n";
    }
    f << "  ]\n}\n";
    if (!f) {
        std::cerr << "Failed to write session JSON: " << filepath << "\n";
        return false;
    }
    return true;
}

} // namespace backtest
