#include "orb_strategy.hpp"
#include "context.hpp"
#include "bar.hpp"
#include <cmath>
#include <memory>
#include <string>

namespace backtest {

namespace {

/// Return calendar date "YYYY-MM-DD" from bar timestamp.
std::string barDate(const std::string& timestamp) {
    auto t = timestamp.find('T');
    auto s = timestamp.find(' ');
    if (t != std::string::npos)
        return timestamp.substr(0, t);
    if (s != std::string::npos)
        return timestamp.substr(0, s);
    return timestamp.size() >= 10 ? timestamp.substr(0, 10) : "";
}

/// Parse time from timestamp; support "T09:30", "T09_30", " 09:30:00". Returns true if parsed.
bool barTime(const std::string& timestamp, int& hour, int& minute) {
    hour = 0;
    minute = 0;
    std::string timePart;
    auto t = timestamp.find('T');
    auto s = timestamp.find(' ');
    if (t != std::string::npos && t + 1 < timestamp.size())
        timePart = timestamp.substr(t + 1);
    else if (s != std::string::npos && s + 1 < timestamp.size())
        timePart = timestamp.substr(s + 1);
    else
        return false;
    for (auto& c : timePart) if (c == '_') c = ':';
    auto c1 = timePart.find(':');
    if (c1 == std::string::npos || c1 < 1) return false;
    try {
        hour = std::stoi(timePart.substr(0, c1));
        auto c2 = timePart.find(':', c1 + 1);
        if (c2 != std::string::npos)
            minute = std::stoi(timePart.substr(c1 + 1, c2 - (c1 + 1)));
        else if (c1 + 1 < timePart.size())
            minute = std::stoi(timePart.substr(c1 + 1));
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

class OrbStrategy : public IStrategy {
public:
    explicit OrbStrategy(const OrbParams& params) : p_(params) {}

    void onStart(IContext& /*ctx*/) override {
        current_date_.clear();
        orb_bar_index_ = -1;
        orb_high_ = orb_low_ = 0;
        triggered_this_day_ = false;
        stop_price_ = 0;
    }

    void onBar(const Bar& bar, IContext& ctx) override {
        std::string date = barDate(bar.timestamp);
        if (date.empty()) return;
        double pos = ctx.position();
        double price = bar.close;
        if (price <= 0) return;

        // New day: close any open position at EOD, then reset ORB state
        if (date != current_date_) {
            if (pos != 0) {  // always exit at EOD (position is day-trade only)
                if (pos > 0)
                    ctx.placeOrder(Side::Short, static_cast<double>(static_cast<int>(pos)));
                else
                    ctx.placeOrder(Side::Long, static_cast<double>(static_cast<int>(-pos)));
            }
            current_date_ = date;
            orb_bar_index_ = -1;  // haven't seen 9:30 bar yet this day
            orb_high_ = orb_low_ = 0;
            triggered_this_day_ = false;
            stop_price_ = 0;
        }

        int bar_hour = 0, bar_minute = 0;
        bool has_time = barTime(bar.timestamp, bar_hour, bar_minute);

        // Only treat a bar as the 9:30 (ORB) bar when its time matches session start (or first bar of day if no time in timestamp)
        if (orb_bar_index_ == -1) {
            bool is_session_start = has_time
                ? (bar_hour == p_.session_start_hour && bar_minute == p_.session_start_minute)
                : true;  // date-only timestamp: assume first bar of day is ORB
            if (is_session_start) {
                orb_high_ = bar.high;
                orb_low_ = bar.low;
                orb_bar_index_ = 1;
            }
            return;
        }

        // Bar right after 9:30 = 9:45 bar (trigger bar)
        if (orb_bar_index_ == 1) {
            if (!triggered_this_day_ && pos == 0) {
                if (bar.close > orb_high_) {
                    double units = std::floor(ctx.equity() / price * p_.position_equity_pct);
                    if (units < 1.0) units = 1.0;
                    ctx.placeOrder(Side::Long, units);
                    stop_price_ = orb_low_;
                    triggered_this_day_ = true;
                } else if (bar.close < orb_low_) {
                    double units = std::floor(ctx.equity() / price * p_.position_equity_pct);
                    if (units < 1.0) units = 1.0;
                    ctx.placeOrder(Side::Short, units);
                    stop_price_ = orb_high_;
                    triggered_this_day_ = true;
                }
            }
            orb_bar_index_ = 2;
            return;
        }

        // Bar 2+ of day: check stop loss (long stop at ORB low, short stop at ORB high)
        if (stop_price_ != 0) {
            if (pos > 0 && bar.low <= stop_price_) {
                ctx.placeOrder(Side::Short, static_cast<double>(static_cast<int>(pos)));
                stop_price_ = 0;
            } else if (pos < 0 && bar.high >= stop_price_) {
                ctx.placeOrder(Side::Long, static_cast<double>(static_cast<int>(-pos)));
                stop_price_ = 0;
            }
        }
    }

    void onEnd(IContext& /*ctx*/) override {}

private:
    OrbParams p_;
    std::string current_date_;
    int orb_bar_index_ = -1;
    double orb_high_ = 0;
    double orb_low_ = 0;
    bool triggered_this_day_ = false;
    double stop_price_ = 0;   // 0 = no active stop
};

std::unique_ptr<IStrategy> createOrbStrategy(const OrbParams& params) {
    return std::make_unique<OrbStrategy>(params);
}

} // namespace backtest
