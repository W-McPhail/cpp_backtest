#pragma once

#include <string>
#include <cstdint>

namespace backtest {

/// Single OHLC (Open, High, Low, Close) bar.
struct Bar {
    std::string timestamp;  // e.g. "2024-01-02" or ISO datetime
    double open{0};
    double high{0};
    double low{0};
    double close{0};
    double volume{0};       // optional

    double typical_price() const { return (high + low + close) / 3.0; }
};

} // namespace backtest
