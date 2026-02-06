#include "backtester.hpp"
#include "report.hpp"
#include "example_sma_strategy.hpp"
#include "ctm_strategy_simple.hpp"
#include "orb_strategy.hpp"
#include "data_source.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <iomanip>
#include <vector>
#include <memory>
#include <stdexcept>
#include <algorithm>

namespace fs = std::filesystem;

namespace {

// Default strategy parameters (overridable via CLI)
constexpr int DEFAULT_SMA_FAST = 9;
constexpr int DEFAULT_SMA_SLOW = 21;
constexpr int CTM_SHORT_SLOW_LOOKBACK = 333;
constexpr std::size_t MIN_BARS_CTM = 333u;
constexpr std::size_t MIN_BARS_ORB = 10u;
constexpr std::size_t MIN_BARS_SMA = 21u;

//-----------------------------------------------------------------------------
// Config: all CLI and run options in one place
//-----------------------------------------------------------------------------
struct Config {
    std::string data_path = "data/sample_ohlc.csv";
    std::string strategy_name = "sma_crossover";
    std::string databento_dir;
    std::string symbol_filter;
    std::string reports_dir = "reports";
    double initial_cash = 100000.0;
    double commission = 0.0;
    std::string bar_resolution = "1m";

    // Strategy params (shared / repurposed by strategy)
    int sma_fast = DEFAULT_SMA_FAST;
    int sma_slow = DEFAULT_SMA_SLOW;
    double sma_size = 1.0;
    bool ctm_kalman_long = false;
    bool ctm_kalman_short = false;
    int orb_session_hour = 9;
    int orb_session_minute = 30;
};

// Safe parse: on failure set error_msg and return false.
bool parseDouble(const char* s, double& out, std::string& error_msg, const char* flag) {
    try {
        std::size_t pos = 0;
        out = std::stod(s, &pos);
        if (s[pos] != '\0') throw std::invalid_argument("");
        return true;
    } catch (...) {
        error_msg = std::string("Invalid value for ") + flag + ": \"" + s + "\" (expected number)";
        return false;
    }
}
bool parseInt(const char* s, int& out, std::string& error_msg, const char* flag) {
    try {
        std::size_t pos = 0;
        out = std::stoi(s, &pos);
        if (s[pos] != '\0') throw std::invalid_argument("");
        return true;
    } catch (...) {
        error_msg = std::string("Invalid value for ") + flag + ": \"" + s + "\" (expected integer)";
        return false;
    }
}

/// Returns false and sets error_msg on parse error.
bool parseArgs(int argc, char* argv[], Config& cfg, std::string& error_msg) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() { return i + 1 < argc ? argv[++i] : nullptr; };

        if (arg == "--data") { if (next()) cfg.data_path = argv[i]; }
        else if (arg == "--strategy") { if (next()) cfg.strategy_name = argv[i]; }
        else if (arg == "--reports-dir") { if (next()) cfg.reports_dir = argv[i]; }
        else if (arg == "--cash") { if (!next() || !parseDouble(argv[i], cfg.initial_cash, error_msg, "--cash")) return false; }
        else if (arg == "--commission") { if (!next() || !parseDouble(argv[i], cfg.commission, error_msg, "--commission")) return false; }
        else if (arg == "--fast") { if (!next() || !parseInt(argv[i], cfg.sma_fast, error_msg, "--fast")) return false; }
        else if (arg == "--slow") { if (!next() || !parseInt(argv[i], cfg.sma_slow, error_msg, "--slow")) return false; }
        else if (arg == "--size") { if (!next() || !parseDouble(argv[i], cfg.sma_size, error_msg, "--size")) return false; }
        else if (arg == "--databento-dir") { if (next()) cfg.databento_dir = argv[i]; }
        else if (arg == "--symbol") { if (next()) cfg.symbol_filter = argv[i]; }
        else if (arg == "--bar") { if (next()) cfg.bar_resolution = argv[i]; }
        else if (arg == "-15m" || arg == "--15m") { cfg.bar_resolution = "15m"; }
        else if (arg == "-1h" || arg == "-1hr" || arg == "--1h" || arg == "--1hr") { cfg.bar_resolution = "1h"; }
        else if (arg == "--ctm-kalman-long") { cfg.ctm_kalman_long = true; }
        else if (arg == "--ctm-kalman-short") { cfg.ctm_kalman_short = true; }
        else if (arg == "--ctm-kalman") { cfg.ctm_kalman_long = cfg.ctm_kalman_short = true; }
        else if (arg == "--orb-session-hour") { if (!next() || !parseInt(argv[i], cfg.orb_session_hour, error_msg, "--orb-session-hour")) return false; }
        else if (arg == "--orb-session-minute") { if (!next() || !parseInt(argv[i], cfg.orb_session_minute, error_msg, "--orb-session-minute")) return false; }
    }
    return true;
}

/// Returns false and sets error_msg if config is invalid.
bool validateConfig(const Config& cfg, std::string& error_msg) {
    if (cfg.initial_cash < 0) { error_msg = "initial cash (--cash) must be >= 0"; return false; }
    if (cfg.commission < 0) { error_msg = "commission (--commission) must be >= 0"; return false; }
    if (cfg.sma_fast < 1) { error_msg = "--fast must be >= 1"; return false; }
    if (cfg.sma_slow < 1) { error_msg = "--slow must be >= 1"; return false; }
    if (cfg.sma_size < 0 || cfg.sma_size > 10) { error_msg = "--size must be between 0 and 10 (fraction of equity)"; return false; }
    if (cfg.orb_session_hour < 0 || cfg.orb_session_hour > 23) { error_msg = "--orb-session-hour must be 0-23"; return false; }
    if (cfg.orb_session_minute < 0 || cfg.orb_session_minute > 59) { error_msg = "--orb-session-minute must be 0-59"; return false; }
    return true;
}

//-----------------------------------------------------------------------------
// Strategy factory: one place to create strategy + params string
//-----------------------------------------------------------------------------
std::pair<std::unique_ptr<backtest::IStrategy>, std::string> createStrategy(const Config& cfg) {
    using namespace backtest;
    std::unique_ptr<IStrategy> strat;
    std::string params;

    if (cfg.strategy_name == "sma_crossover") {
        strat = createSmaCrossoverStrategy(cfg.sma_fast, cfg.sma_slow, cfg.sma_size);
        params = "fast=" + std::to_string(cfg.sma_fast) + " slow=" + std::to_string(cfg.sma_slow) + " size=" + std::to_string(cfg.sma_size);
    } else if (cfg.strategy_name == "ctm") {
        CtmParams ctm;
        ctm.long_fast = ctm.long_medium = cfg.sma_fast;
        ctm.long_slow = cfg.sma_slow;
        ctm.short_fast = ctm.short_medium = cfg.sma_fast;
        ctm.short_slow = CTM_SHORT_SLOW_LOOKBACK;
        ctm.use_kalman_trend_long = cfg.ctm_kalman_long;
        ctm.use_kalman_trend_short = cfg.ctm_kalman_short;
        strat = createCtmStrategy(ctm);
        params = "long=" + std::to_string(ctm.long_fast) + "/" + std::to_string(ctm.long_slow)
            + " short=" + std::to_string(ctm.short_fast) + "/" + std::to_string(ctm.short_slow);
        if (cfg.ctm_kalman_long || cfg.ctm_kalman_short) params += " kalman=on";
    } else if (cfg.strategy_name == "orb") {
        OrbParams orb;
        // 15% of equity per day; --size 0.1..0.99 overrides (e.g. --size 0.2 for 20%)
        orb.position_equity_pct = (cfg.sma_size >= 0.01 && cfg.sma_size < 1.0) ? cfg.sma_size : 0.15;
        orb.session_start_hour = cfg.orb_session_hour;
        orb.session_start_minute = cfg.orb_session_minute;
        strat = createOrbStrategy(orb);
        params = "session=" + std::to_string(orb.session_start_hour) + ":" + std::to_string(orb.session_start_minute) + " " + std::to_string(static_cast<int>(orb.position_equity_pct * 100)) + "% equity EOD exit";
    }

    return { std::move(strat), params };
}

std::size_t minBarsForStrategy(const std::string& name) {
    if (name == "ctm") return MIN_BARS_CTM;
    if (name == "orb") return MIN_BARS_ORB;
    return MIN_BARS_SMA;
}

//-----------------------------------------------------------------------------
// Single-symbol backtest: run, report, write files
//-----------------------------------------------------------------------------
int runSingle(const Config& cfg,
              std::unique_ptr<backtest::IStrategy> strategy,
              const std::string& strategy_params) {
    using namespace backtest;
    std::string data_path = cfg.databento_dir.empty() ? cfg.data_path : "";
    Backtester bt(std::move(strategy), data_path, cfg.initial_cash, cfg.commission,
                  cfg.databento_dir, cfg.symbol_filter, cfg.bar_resolution);

    if (!bt.run()) {
        if (!cfg.databento_dir.empty())
            std::cerr << "Failed to run backtest (check --databento-dir and --symbol: " << cfg.databento_dir << ")\n";
        else
            std::cerr << "Failed to run backtest (check data file: " << cfg.data_path << ")\n";
        return 1;
    }

    Report report(bt.simulator(), bt.data(), cfg.initial_cash, cfg.strategy_name, strategy_params);
    report.setMetrics(report.computeMetrics());
    if (bt.stoppedEarly())
        report.setStoppedReason(bt.stopReason());
    report.printSummary(std::cout);

    fs::create_directories(cfg.reports_dir);
    report.writeTradeLog((fs::path(cfg.reports_dir) / "trades.csv").string());
    report.writeEquityCurve((fs::path(cfg.reports_dir) / "equity_curve.csv").string());
    report.writeReport((fs::path(cfg.reports_dir) / "report.txt").string());
    std::cout << "Reports written to " << cfg.reports_dir << "/\n";
    return 0;
}

//-----------------------------------------------------------------------------
// All-symbols backtest: run per symbol, print table, write summary
//-----------------------------------------------------------------------------
int runAllSymbols(const Config& cfg, const std::string& strategy_params) {
    using namespace backtest;
    std::vector<std::string> symbols = DataSource::listSymbolsInDatabentoDir(cfg.databento_dir);
    if (symbols.empty()) {
        std::cerr << "No symbols found in " << cfg.databento_dir << "\n";
        return 1;
    }

    struct SymbolResult {
        std::string symbol;
        BacktestMetrics metrics;
        std::string stop_reason;
    };
    std::vector<SymbolResult> results;
    const std::size_t min_bars = minBarsForStrategy(cfg.strategy_name);

    for (const std::string& sym : symbols) {
        auto [sym_strategy, params] = createStrategy(cfg);
        Backtester bt(std::move(sym_strategy), "", cfg.initial_cash, cfg.commission,
                      cfg.databento_dir, sym, cfg.bar_resolution);

        if (!bt.run() || bt.data().empty()) {
            std::cerr << "Skipped " << sym << ": no bars or load failed\n";
            continue;
        }
        if (bt.data().size() < min_bars) {
            std::cerr << "Skipped " << sym << ": only " << bt.data().size() << " bars (need " << min_bars << ")\n";
            continue;
        }

        Report r(bt.simulator(), bt.data(), cfg.initial_cash, cfg.strategy_name, strategy_params);
        r.setMetrics(r.computeMetrics());
        results.push_back({ sym, r.metrics(), bt.stoppedEarly() ? bt.stopReason() : "" });
    }

    if (results.empty()) {
        std::cerr << "All symbols skipped (no bars or load failed).\n";
        return 1;
    }

    // Console table
    std::cout << "\n========== Backtest (all symbols) ==========\n";
    std::cout << "Strategy: " << cfg.strategy_name << " (" << strategy_params << ")\n\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << std::setw(10) << "Symbol" << std::setw(12) << "Return %" << std::setw(10) << "MaxDD %"
              << std::setw(8) << "Trades" << std::setw(14) << "Final equity" << std::setw(22) << "Stopped\n";
    std::cout << std::string(76, '-') << "\n";

    double total_pnl = 0;
    int total_trades = 0;
    double total_initial = cfg.initial_cash * results.size();
    for (const auto& p : results) {
        const auto& m = p.metrics;
        double dd_display = std::min(m.max_drawdown_pct, 100.0);
        std::cout << std::setw(10) << p.symbol << std::setw(12) << m.total_return_pct << std::setw(10) << dd_display
                  << std::setw(8) << m.num_trades << std::setw(14) << m.final_equity << std::setw(22) << (p.stop_reason.empty() ? "-" : p.stop_reason) << "\n";
        total_pnl += (m.final_equity - cfg.initial_cash);
        total_trades += m.num_trades;
    }
    std::cout << std::string(76, '-') << "\n";
    std::cout << std::setw(10) << "Combined" << std::setw(12) << (total_pnl / total_initial * 100.0)
              << std::setw(10) << "" << std::setw(8) << total_trades
              << std::setw(14) << (total_initial + total_pnl) << "\n";
    std::cout << "  (Combined: " << static_cast<int>(results.size()) << " accounts, " << std::fixed << std::setprecision(0) << total_initial << " initial total -> " << (total_initial + total_pnl) << " final total)\n";
    std::cout << "============================================\n\n";

    // Summary file
    fs::create_directories(cfg.reports_dir);
    std::ofstream f(fs::path(cfg.reports_dir) / "all_symbols_summary.txt");
    if (f) {
        f << "Backtest all symbols\nStrategy: " << cfg.strategy_name << " (" << strategy_params << ")\n\n";
        f << std::fixed << std::setprecision(2);
        f << std::setw(10) << "Symbol" << std::setw(12) << "Return %" << std::setw(10) << "MaxDD %"
          << std::setw(8) << "Trades" << std::setw(14) << "Final equity" << std::setw(22) << "Stopped\n";
        f << std::string(76, '-') << "\n";
        for (const auto& p : results) {
            const auto& m = p.metrics;
            f << std::setw(10) << p.symbol << std::setw(12) << m.total_return_pct << std::setw(10) << std::min(m.max_drawdown_pct, 100.0)
              << std::setw(8) << m.num_trades << std::setw(14) << m.final_equity << std::setw(22) << (p.stop_reason.empty() ? "-" : p.stop_reason) << "\n";
        }
        f << std::string(76, '-') << "\n";
        f << std::setw(10) << "Combined" << std::setw(12) << (total_pnl / total_initial * 100.0)
          << std::setw(10) << "" << std::setw(8) << total_trades
          << std::setw(14) << (total_initial + total_pnl) << "\n";
        f << "  (Combined: " << results.size() << " accounts, " << std::fixed << std::setprecision(0) << total_initial << " initial -> " << (total_initial + total_pnl) << " final)\n";
        std::cout << "Summary written to " << (cfg.reports_dir + "/all_symbols_summary.txt") << "\n";
    }
    return 0;
}

} // namespace

//-----------------------------------------------------------------------------
// main
//-----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    Config cfg;
    std::string error_msg;
    if (!parseArgs(argc, argv, cfg, error_msg)) {
        std::cerr << error_msg << "\n";
        return 1;
    }
    if (!validateConfig(cfg, error_msg)) {
        std::cerr << error_msg << "\n";
        return 1;
    }

    // Resolve default data path when running from build/
    if (!(fs::exists(cfg.data_path) && fs::is_regular_file(cfg.data_path)) &&
         fs::exists("../data/sample_ohlc.csv") && fs::is_regular_file("../data/sample_ohlc.csv"))
        cfg.data_path = "../data/sample_ohlc.csv";

    auto [strategy, strategy_params] = createStrategy(cfg);
    if (!strategy) {
        std::cerr << "Unknown strategy: " << cfg.strategy_name << "\n";
        std::cerr << "Available: sma_crossover, ctm, orb\n";
        return 1;
    }

    if (!cfg.databento_dir.empty() && cfg.symbol_filter.empty())
        return runAllSymbols(cfg, strategy_params);

    return runSingle(cfg, std::move(strategy), strategy_params);
}
