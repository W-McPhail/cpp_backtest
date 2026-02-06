#pragma once

#include "bar.hpp"
#include <vector>
#include <string>
#include <optional>

namespace backtest {

/// Loads OHLC bars from a CSV file or from Databento glbx folder (filename = data).
/// CSV: expected columns timestamp/date, open, high, low, close [, volume].
/// Databento: each file is 0 bytes; filename is comma-separated: ts, ignore, ignore, ignore, o, h, l, c, v, symbol.
class DataSource {
public:
    explicit DataSource(const std::string& filepath);

    /// Load bars from CSV file. Returns false on parse error.
    bool load();

    /// Load bars from Databento glbx... folder. Each filename = one bar (ts, 3 ignored, o, h, l, c, v, symbol).
    /// Skips empty/invalid filenames. Optional symbol_filter (e.g. "NQU5") to load only that symbol.
    /// Bars are sorted by timestamp.
    bool loadFromDatabentoDir(const std::string& dir, const std::string& symbol_filter = "");

    /// Discover unique symbols in a Databento dir (parses filenames, symbol at index 9). Returns sorted list; empty if dir missing or no valid filenames.
    static std::vector<std::string> listSymbolsInDatabentoDir(const std::string& dir);

    const std::vector<Bar>& bars() const { return bars_; }
    std::size_t size() const { return bars_.size(); }
    bool empty() const { return bars_.empty(); }

    /// Get bar at index (0-based). No bounds check in release.
    const Bar& at(std::size_t i) const { return bars_.at(i); }

    /// Aggregate 1m bars into 15m or 1h bars. Resolution: "15m", "1h" (or "1hr"); "1m" = no-op.
    /// OHLCV: open=first, high=max, low=min, close=last, volume=sum. Bars must be sorted by timestamp.
    void aggregateBars(const std::string& resolution);

private:
    std::string filepath_;
    std::vector<Bar> bars_;

    std::optional<Bar> parseLine(const std::string& line,
                                 const std::vector<std::string>& headers);
    std::optional<Bar> parseDatabentoFilename(const std::string& filename);
};

} // namespace backtest
