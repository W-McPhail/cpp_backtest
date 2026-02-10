#pragma once

#include "strategy.hpp"
#include <memory>

namespace backtest {

/// Factory: create SMA crossover strategy (fast=10, slow=30 by default).
std::unique_ptr<IStrategy> createSmaCrossoverStrategy(int fast = 10, int slow = 30, double size = 1.0);

} // namespace backtest
