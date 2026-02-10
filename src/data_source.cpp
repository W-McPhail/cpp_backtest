#include "data_source.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <set>
#include <map>

namespace fs = std::filesystem;

namespace backtest {

namespace {

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::vector<std::string> split(const std::string& line, char delim) {
    std::vector<std::string> parts;
    std::istringstream iss(line);
    std::string part;
    while (std::getline(iss, part, delim)) {
        parts.push_back(trim(part));
    }
    return parts;
}

void toLower(std::string& s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

int findColumn(const std::vector<std::string>& headers, const std::vector<std::string>& names) {
    for (const auto& name : names) {
        for (std::size_t i = 0; i < headers.size(); ++i) {
            std::string h = headers[i];
            toLower(h);
            if (h == name) return static_cast<int>(i);
        }
    }
    return -1;
}

// Parse timestamp to (year, month, day, hour, minute). Returns false if unparseable.
// Supports: "2025-08-04T00_00_00.000000000Z", "2025-08-04T00:00:00", "2024-01-02", "2024-01-02 12:30:00"
bool parseTimestamp(const std::string& ts, int& year, int& month, int& day, int& hour, int& minute) {
    year = month = day = hour = minute = 0;
    std::string s = ts;
    for (auto& c : s) if (c == '_') c = ':';
    std::string datePart, timePart;
    auto tPos = s.find('T');
    auto spPos = s.find(' ');
    if (tPos != std::string::npos) {
        datePart = s.substr(0, tPos);
        timePart = s.substr(tPos + 1);
    } else if (spPos != std::string::npos) {
        datePart = s.substr(0, spPos);
        timePart = s.substr(spPos + 1);
    } else {
        datePart = s;
    }
    // Date YYYY-MM-DD
    if (datePart.size() < 10) return false;
    try {
        year = std::stoi(datePart.substr(0, 4));
        month = std::stoi(datePart.substr(5, 2));
        day = std::stoi(datePart.substr(8, 2));
    } catch (...) { return false; }
    if (!timePart.empty()) {
        auto colon1 = timePart.find(':');
        if (colon1 != std::string::npos) {
            try {
                hour = std::stoi(timePart.substr(0, colon1));
                auto colon2 = timePart.find(':', colon1 + 1);
                if (colon2 != std::string::npos)
                    minute = std::stoi(timePart.substr(colon1 + 1, colon2 - (colon1 + 1)));
            } catch (...) { /* keep 0,0 */ }
        }
    }
    return true;
}

// Period key for grouping: "YYYY-MM-DDTHH:MM" (15m: MM in {00,15,30,45}; 1h: MM=00)
std::string periodKey(int year, int month, int day, int hour, int minute, int intervalMinutes) {
    int m = (minute / intervalMinutes) * intervalMinutes;
    int h = hour;
    if (intervalMinutes >= 60) {
        m = 0;
        // 1h: already correct
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d", year, month, day, h, m);
    return std::string(buf);
}

} // namespace

DataSource::DataSource(const std::string& filepath) : filepath_(filepath) {}

bool DataSource::load() {
    bars_.clear();
    std::ifstream f(filepath_);
    if (!f.is_open()) return false;

    std::string line;
    if (!std::getline(f, line)) return false;
    std::vector<std::string> headers = split(line, ',');
    for (auto& h : headers) toLower(h);

    int iDate = findColumn(headers, {"timestamp", "date", "datetime", "time"});
    int iOpen = findColumn(headers, {"open", "o"});
    int iHigh = findColumn(headers, {"high", "h"});
    int iLow = findColumn(headers, {"low", "l"});
    int iClose = findColumn(headers, {"close", "c"});

    if (iDate < 0 || iOpen < 0 || iHigh < 0 || iLow < 0 || iClose < 0)
        return false;

    while (std::getline(f, line)) {
        auto bar = parseLine(line, headers);
        if (!bar) continue;
        bars_.push_back(*bar);
    }

    return true;
}

std::optional<Bar> DataSource::parseDatabentoFilename(const std::string& filename) {
    // Filename format: ts, ignore, ignore, ignore, open, high, low, close, volume, symbol
    auto parts = split(filename, ',');
    if (parts.size() < 10) return std::nullopt;
    Bar b;
    b.timestamp = parts[0];
    try {
        b.open = std::stod(parts[4]);
        b.high = std::stod(parts[5]);
        b.low = std::stod(parts[6]);
        b.close = std::stod(parts[7]);
        b.volume = std::stod(parts[8]);
    } catch (...) {
        return std::nullopt;
    }
    return b;
}

bool DataSource::loadFromDatabentoDir(const std::string& dir, const std::string& symbol_filter) {
    bars_.clear();
    std::error_code ec;
    if (!fs::is_directory(dir, ec) || ec) return false;

    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec || !entry.is_regular_file()) continue;
        std::string filename = entry.path().filename().string();
        if (filename.empty()) continue;

        // Filter by symbol (last field). Case-insensitive match.
        if (!symbol_filter.empty()) {
            auto parts = split(filename, ',');
            if (parts.size() < 10) continue;
            std::string sym = parts[9];
            std::string want = symbol_filter;
            toLower(sym);
            toLower(want);
            if (sym != want) continue;
        }

        auto bar = parseDatabentoFilename(filename);
        if (!bar) continue;
        bars_.push_back(*bar);
    }

    std::sort(bars_.begin(), bars_.end(), [](const Bar& a, const Bar& b) {
        return a.timestamp < b.timestamp;
    });
    return true;
}

void DataSource::aggregateBars(const std::string& resolution) {
    std::string r = resolution;
    toLower(r);
    if (r == "1m" || r.empty()) return;
    int intervalMinutes = 0;
    if (r == "15m") intervalMinutes = 15;
    else if (r == "1h" || r == "1hr") intervalMinutes = 60;
    else return;

    std::map<std::string, Bar> keyToBar;
    for (const Bar& b : bars_) {
        int y, mo, d, h, mi;
        if (!parseTimestamp(b.timestamp, y, mo, d, h, mi)) continue;
        std::string key = periodKey(y, mo, d, h, mi, intervalMinutes);
        auto it = keyToBar.find(key);
        if (it == keyToBar.end()) {
            Bar agg;
            agg.timestamp = key;
            agg.open = b.open;
            agg.high = b.high;
            agg.low = b.low;
            agg.close = b.close;
            agg.volume = b.volume;
            keyToBar[key] = agg;
        } else {
            Bar& agg = it->second;
            if (b.high > agg.high) agg.high = b.high;
            if (b.low < agg.low) agg.low = b.low;
            agg.close = b.close;
            agg.volume += b.volume;
        }
    }
    bars_.clear();
    for (const auto& p : keyToBar)
        bars_.push_back(p.second);
    std::sort(bars_.begin(), bars_.end(), [](const Bar& a, const Bar& b) {
        return a.timestamp < b.timestamp;
    });
}

std::vector<std::string> DataSource::listSymbolsInDatabentoDir(const std::string& dir) {
    std::set<std::string> symbols;
    std::error_code ec;
    if (!fs::is_directory(dir, ec) || ec) return {};

    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec || !entry.is_regular_file()) continue;
        std::string filename = entry.path().filename().string();
        if (filename.empty()) continue;
        auto parts = split(filename, ',');
        if (parts.size() < 10) continue;
        std::string sym = parts[9];
        toLower(sym);
        symbols.insert(sym);
    }
    return std::vector<std::string>(symbols.begin(), symbols.end());
}

std::optional<Bar> DataSource::parseLine(const std::string& line,
                                          const std::vector<std::string>& headers) {
    auto parts = split(line, ',');
    if (parts.size() < 5) return std::nullopt;

    int iDate = findColumn(headers, {"timestamp", "date", "datetime", "time"});
    int iOpen = findColumn(headers, {"open", "o"});
    int iHigh = findColumn(headers, {"high", "h"});
    int iLow = findColumn(headers, {"low", "l"});
    int iClose = findColumn(headers, {"close", "c"});
    int iVol = findColumn(headers, {"volume", "vol", "v"});

    Bar b;
    b.timestamp = parts[static_cast<std::size_t>(iDate)];
    try {
        b.open = std::stod(parts[static_cast<std::size_t>(iOpen)]);
        b.high = std::stod(parts[static_cast<std::size_t>(iHigh)]);
        b.low = std::stod(parts[static_cast<std::size_t>(iLow)]);
        b.close = std::stod(parts[static_cast<std::size_t>(iClose)]);
        if (iVol >= 0 && static_cast<std::size_t>(iVol) < parts.size())
            b.volume = std::stod(parts[static_cast<std::size_t>(iVol)]);
    } catch (...) {
        return std::nullopt;
    }
    return b;
}

} // namespace backtest
