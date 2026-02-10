/**
 * Minimal test suite for backtester (no external test framework).
 * Run: build/test_runner.exe (or add test target to CMake/build.bat).
 */
#include "simulator.hpp"
#include "bar.hpp"
#include "data_source.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>

#define ASSERT_EQ(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (_a != _b) { \
        std::cerr << "FAIL: " << #a << " == " << #b << " => " << _a << " != " << _b << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        std::exit(1); \
    } \
} while(0)

#define ASSERT_NEAR(a, b, tol) do { \
    auto _a = (double)(a); auto _b = (double)(b); \
    if (std::abs(_a - _b) > (tol)) { \
        std::cerr << "FAIL: " << #a << " ~= " << #b << " => " << _a << " vs " << _b << " (tol " << (tol) << ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
        std::exit(1); \
    } \
} while(0)

namespace {

using namespace backtest;

//--- Simulator: one long trade, fill at next bar open, then close
void run_simulator_long_trade() {
    Simulator sim(10000.0, 0.0);
    Bar bar1; bar1.timestamp = "2024-01-01T10:00"; bar1.open = 100; bar1.high = 101; bar1.low = 99; bar1.close = 100.5;
    Bar bar2; bar2.timestamp = "2024-01-01T10:15"; bar2.open = 102; bar2.high = 103; bar2.low = 101; bar2.close = 102;

    ASSERT_EQ(sim.position(), 0);
    sim.placeOrder(Side::Long, 10);
    sim.processOrders(bar1);  // fill at bar1.open = 100
    ASSERT_EQ(sim.position(), 10);
    ASSERT_NEAR(sim.cash(), 10000 - 100*10, 1e-6);  // paid 1000
    sim.updateEquity(bar1);
    sim.placeOrder(Side::Short, 10);  // close
    sim.processOrders(bar2);  // fill at bar2.open = 102
    ASSERT_EQ(sim.position(), 0);
    ASSERT_NEAR(sim.cash(), 10000 - 1000 + 20, 1e-6);  // cash after entry 9k, +20 PnL on close
    ASSERT_EQ(sim.trades().size(), 1u);
    ASSERT_NEAR(sim.trades()[0].pnl, 20.0, 1e-6);
}

//--- Simulator: commission applied
void run_simulator_commission() {
    Simulator sim(10000.0, 1.0);  // 1 per trade
    Bar bar1; bar1.open = 100; bar1.close = 100;
    Bar bar2; bar2.open = 105; bar2.close = 105;
    sim.placeOrder(Side::Long, 5);
    sim.processOrders(bar1);
    sim.placeOrder(Side::Short, 5);
    sim.processOrders(bar2);
    ASSERT_EQ(sim.trades().size(), 1u);
    ASSERT_NEAR(sim.trades()[0].pnl, (105 - 100) * 5 - 1.0, 1e-6);  // 25 - 1 commission (charged on close in trade record)
}

//--- Simulator: slippage applied (long fills higher, short fills lower)
void run_simulator_slippage() {
    const double slip = 0.01;  // 1%
    Simulator sim(10000.0, 0.0, slip);
    Bar bar1; bar1.timestamp = "2024-01-01T10:00"; bar1.open = 100; bar1.close = 100;
    Bar bar2; bar2.timestamp = "2024-01-01T10:15"; bar2.open = 102; bar2.close = 102;
    sim.placeOrder(Side::Long, 10);
    sim.processOrders(bar1);  // fill at 100 * 1.01 = 101
    sim.placeOrder(Side::Short, 10);
    sim.processOrders(bar2);  // fill at 102 * 0.99 = 100.98
    ASSERT_EQ(sim.trades().size(), 1u);
    ASSERT_NEAR(sim.trades()[0].entry_price, 101.0, 1e-6);
    ASSERT_NEAR(sim.trades()[0].exit_price, 100.98, 1e-6);
    ASSERT_NEAR(sim.trades()[0].pnl, (100.98 - 101.0) * 10, 1e-6);  // -0.20
}

//--- DataSource: load from CSV string (temp file)
void run_data_source_csv_load() {
    std::string csv = "timestamp,open,high,low,close\n"
                      "2024-01-01T09:30,100,101,99,100.5\n"
                      "2024-01-01T09:45,100.5,102,100,101\n";
    std::string path = "test_sample_ohlc.csv";
    {
        std::ofstream f(path);
        f << csv;
    }
    DataSource ds(path);
    bool ok = ds.load();
    ASSERT_EQ(ok, true);
    ASSERT_EQ(ds.size(), 2u);
    ASSERT_NEAR(ds.at(0).open, 100, 1e-6);
    ASSERT_NEAR(ds.at(1).close, 101, 1e-6);
    std::remove(path.c_str());
}

//--- DataSource: aggregate 1m to 15m (4 bars -> 1)
void run_data_source_aggregate_15m() {
    std::string csv = "timestamp,open,high,low,close,volume\n"
                      "2024-01-01T09:30,100,101,99,100.5,100\n"
                      "2024-01-01T09:31,100.5,102,100,101,200\n"
                      "2024-01-01T09:32,101,103,100.5,102,150\n"
                      "2024-01-01T09:33,102,102.5,101,101.5,50\n";
    std::string path = "test_agg_ohlc.csv";
    {
        std::ofstream f(path);
        f << csv;
    }
    DataSource ds(path);
    ASSERT_EQ(ds.load(), true);
    ASSERT_EQ(ds.size(), 4u);
    ds.aggregateBars("15m");
    ASSERT_EQ(ds.size(), 1u);
    ASSERT_NEAR(ds.at(0).open, 100, 1e-6);
    ASSERT_NEAR(ds.at(0).high, 103, 1e-6);
    ASSERT_NEAR(ds.at(0).low, 99, 1e-6);
    ASSERT_NEAR(ds.at(0).close, 101.5, 1e-6);
    ASSERT_NEAR(ds.at(0).volume, 500, 1e-6);
    std::remove(path.c_str());
}

void run_all_tests() {
    std::cerr << "  simulator_long_trade ... "; run_simulator_long_trade(); std::cerr << "ok\n";
    std::cerr << "  simulator_commission ... "; run_simulator_commission(); std::cerr << "ok\n";
    std::cerr << "  simulator_slippage ... "; run_simulator_slippage(); std::cerr << "ok\n";
    std::cerr << "  data_source_csv_load ... "; run_data_source_csv_load(); std::cerr << "ok\n";
    std::cerr << "  data_source_aggregate_15m ... "; run_data_source_aggregate_15m(); std::cerr << "ok\n";
}

} // namespace

int main() {
    std::cerr << "Running tests...\n";
    run_all_tests();
    std::cerr << "All tests passed.\n";
    return 0;
}
