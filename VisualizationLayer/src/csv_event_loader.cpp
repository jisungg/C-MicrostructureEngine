#include "visualization/event_loader.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "microstructure/event.hpp"
#include "microstructure/types.hpp"

namespace visualization {

CsvEventLoader::CsvEventLoader(std::string path)
    : path_{std::move(path)}
{}

std::string CsvEventLoader::source_description() const {
    return "CSV:" + path_;
}

// ── Internal helpers ──────────────────────────────────────────────────────────

namespace {

// Strip leading and trailing ASCII whitespace.
std::string_view trim(std::string_view sv) noexcept {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
        sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))
        sv.remove_suffix(1);
    return sv;
}

// Split a single CSV line on commas.  Quoted fields are not supported
// (field values in the microstructure format never contain commas).
std::vector<std::string_view> split_csv(std::string_view line) {
    std::vector<std::string_view> out;
    out.reserve(10);
    std::size_t start = 0;
    for (std::size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == ',') {
            out.push_back(trim(line.substr(start, i - start)));
            start = i + 1;
        }
    }
    return out;
}

// Parse a decimal integer using std::from_chars (no locale, no allocation).
template <typename T>
T parse_int(std::string_view s) {
    T val{};
    const char* first = s.data();
    const char* last  = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(first, last, val);
    if (ec != std::errc{} || ptr != last)
        throw std::runtime_error("invalid integer: '" + std::string{s} + "'");
    return val;
}

microstructure::EventType parse_event_type(std::string_view s) {
    if (s == "ADD")      return microstructure::EventType::Add;
    if (s == "CANCEL")   return microstructure::EventType::Cancel;
    if (s == "MODIFY")   return microstructure::EventType::Modify;
    if (s == "TRADE")    return microstructure::EventType::Trade;
    if (s == "SNAPSHOT") return microstructure::EventType::Snapshot;
    throw std::runtime_error("unknown event_type: '" + std::string{s} + "'");
}

microstructure::Side parse_side(std::string_view s) {
    if (s == "BID") return microstructure::Side::Bid;
    if (s == "ASK") return microstructure::Side::Ask;
    throw std::runtime_error("unknown side: '" + std::string{s} + "'");
}

microstructure::Venue parse_venue(std::string_view s) {
    if (s == "NASDAQ") return microstructure::Venue::Nasdaq;
    if (s == "ARCA")   return microstructure::Venue::Arca;
    if (s == "BATS")   return microstructure::Venue::Bats;
    if (s == "IEX")    return microstructure::Venue::Iex;
    throw std::runtime_error("unknown venue: '" + std::string{s} + "'");
}

// Case-insensitive ASCII string comparison (for header row detection).
bool iequal(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

// Return true if this looks like the header row (first field is "event_id").
bool is_header(const std::vector<std::string_view>& fields) noexcept {
    return !fields.empty() && iequal(fields[0], "event_id");
}

} // anonymous namespace

// ── CsvEventLoader::load ──────────────────────────────────────────────────────

std::vector<microstructure::Event> CsvEventLoader::load() const {
    std::ifstream file{path_};
    if (!file.is_open())
        throw std::runtime_error("CsvEventLoader: cannot open '" + path_ + "'");

    std::vector<microstructure::Event> events;
    std::string  line;
    std::size_t  line_num    = 0;
    bool         header_seen = false;

    while (std::getline(file, line)) {
        ++line_num;
        const std::string_view sv = trim(std::string_view{line});

        // Skip blank lines and comment lines.
        if (sv.empty() || sv.front() == '#') continue;

        const auto fields = split_csv(sv);

        // Skip (or consume) the header row.
        if (!header_seen && is_header(fields)) {
            header_seen = true;
            continue;
        }
        // If no header has appeared, treat first data line as data
        // (the header is optional in our format).
        header_seen = true;

        if (fields.size() < 10) {
            throw std::runtime_error(
                "CsvEventLoader: line " + std::to_string(line_num) +
                ": expected 10 fields, got " + std::to_string(fields.size()));
        }

        try {
            const auto eid  = parse_int<microstructure::EventId>(fields[0]);
            const auto etype = parse_event_type(fields[1]);
            const auto oid  = parse_int<microstructure::OrderId>(fields[2]);
            const auto px   = parse_int<microstructure::Price>(fields[3]);
            const auto sz   = parse_int<microstructure::Quantity>(fields[4]);
            const auto sd   = parse_side(fields[5]);
            const auto ets  = parse_int<microstructure::Timestamp>(fields[6]);
            const auto rts  = parse_int<microstructure::Timestamp>(fields[7]);
            const auto pts  = parse_int<microstructure::Timestamp>(fields[8]);
            const auto ven  = parse_venue(fields[9]);

            // Enforce monotonically non-decreasing exchange timestamps.
            if (!events.empty() && ets < events.back().exchange_timestamp()) {
                throw std::runtime_error(
                    "exchange_timestamp not monotonically non-decreasing (got " +
                    std::to_string(ets) + " after " +
                    std::to_string(events.back().exchange_timestamp()) + ")");
            }

            events.emplace_back(eid, etype, oid, px, sz, sd, ets, rts, pts, ven);

        } catch (const std::exception& ex) {
            throw std::runtime_error(
                "CsvEventLoader: line " + std::to_string(line_num) +
                ": " + ex.what());
        }
    }

    return events;
}

} // namespace visualization
