#pragma once

#include "bar.hpp"
#include "order.hpp"
#include <vector>
#include <functional>

namespace backtest {

class IContext {
public:
    virtual ~IContext() = default;

    /// Place a market order (executed at next bar open in the simulator).
    virtual void placeOrder(Side side, double quantity) = 0;

    /// Current position: positive = long, negative = short, 0 = flat.
    virtual double position() const = 0;

    /// Current equity (cash + position value at last close).
    virtual double equity() const = 0;

    /// Cash available (excluding position value).
    virtual double cash() const = 0;

    /// Last bar's close (e.g. for position value).
    virtual double lastClose() const = 0;

    /// Number of bars processed so far (0-based).
    virtual std::size_t barIndex() const = 0;

    /// History of bars up to and including current bar (no look-ahead).
    virtual const std::vector<Bar>& bars() const = 0;
};

} // namespace backtest
