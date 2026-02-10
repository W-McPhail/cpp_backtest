#pragma once

#include <cstdint>

namespace backtest {

enum class Side { Long, Short };

enum class OrderType { Market, Limit };

struct Order {
    Side side{Side::Long};
    double quantity{0};
    OrderType type{OrderType::Market};
    double limit_price{0};  // used if type == Limit
};

} // namespace backtest
