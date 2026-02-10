#pragma once

#include "bar.hpp"
#include "order.hpp"

namespace backtest {

class IContext;  // forward declaration

/// Interface your trading algorithm must implement.
/// The engine calls onBar() for each bar in chronological order (no look-ahead).
class IStrategy {
public:
    virtual ~IStrategy() = default;

    /// Called once per bar. Use ctx to place orders and read state.
    /// You only see the current bar and past data.
    virtual void onBar(const Bar& bar, IContext& ctx) = 0;

    /// Optional: called when backtest starts (e.g. to init indicators).
    virtual void onStart(IContext& /*ctx*/) {}

    /// Optional: called when backtest ends.
    virtual void onEnd(IContext& /*ctx*/) {}
};

} // namespace backtest
