#include "visualization/json_serializer.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "visualization/export_schema.hpp"
#include "microstructure/types.hpp"

namespace visualization {

// ── Helpers ──────────────────────────────────────────────────────────────

namespace {

std::string fmt_int64(std::int64_t v) {
    return std::to_string(v);
}

std::string fmt_uint64(std::uint64_t v) {
    return std::to_string(v);
}

std::string fmt_size(std::size_t v) {
    return std::to_string(v);
}

std::string fmt_bool(bool v) {
    return v ? "true" : "false";
}

} // namespace

std::string JsonSerializer::fmt_double(double v) {
    // JSON (RFC 8259) forbids Infinity and NaN.  Emit null for non-finite
    // values so the output remains valid JSON and valid JavaScript.
    if (!std::isfinite(v)) return "null";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    return std::string{buf};
}

std::string JsonSerializer::escape_string(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (const char raw_c : s) {
        const unsigned char c = static_cast<unsigned char>(raw_c);
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '/':  out += "\\/";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '<':  out += "\\u003c"; break;
        case '>':  out += "\\u003e"; break;
        default:
            if (c < 0x20u) {
                char esc[8];
                std::snprintf(esc, sizeof(esc), "\\u%04x", static_cast<unsigned>(c));
                out += esc;
            } else {
                out += static_cast<char>(c);
            }
            break;
        }
    }
    return out;
}

std::string JsonSerializer::serialize_depth_level(const DepthLevel& lvl) {
    std::string o;
    o.reserve(128);
    o += '{';
    o += "\"price\":" + fmt_int64(lvl.price) + ',';
    o += "\"volume\":" + fmt_int64(lvl.volume) + ',';
    o += "\"queue_depth\":" + fmt_size(lvl.queue_depth) + ',';
    o += "\"order_flow\":" + fmt_double(lvl.order_flow) + ',';
    o += "\"cancel_rate\":" + fmt_double(lvl.cancel_rate) + ',';
    o += "\"fill_rate\":" + fmt_double(lvl.fill_rate);
    o += '}';
    return o;
}

std::string JsonSerializer::serialize_trade(const microstructure::TradeExecution& t) {
    std::string o;
    o.reserve(200);
    o += '{';
    o += "\"price\":" + fmt_int64(t.price) + ',';
    o += "\"size\":" + fmt_int64(t.size) + ',';
    o += "\"resting_side\":\"" + escape_string(microstructure::to_string(t.resting_side)) + "\",";
    o += "\"aggressor\":\"" + escape_string(microstructure::to_string(t.aggressor)) + "\",";
    o += "\"visible_depth_before\":" + fmt_int64(t.visible_depth_before) + ',';
    o += "\"remaining_at_price_after\":" + fmt_int64(t.remaining_at_price_after);
    o += '}';
    return o;
}

// ── Public API ────────────────────────────────────────────────────────────

std::string JsonSerializer::serialize_frame(const VisualizationFrame& f) const {
    std::string o;
    o.reserve(2048);
    o += '{';

    // Schema version
    o += "\"schema_version\":" + std::to_string(kSchemaVersion) + ',';

    // Replay position
    o += "\"frame_index\":" + fmt_size(f.frame_index) + ',';
    o += "\"event_id\":" + fmt_uint64(f.event_id) + ',';
    o += "\"event_type\":\"" + escape_string(microstructure::to_string(f.event_type)) + "\",";
    o += "\"venue\":\"" + escape_string(microstructure::to_string(f.venue)) + "\",";
    o += "\"exchange_timestamp\":" + fmt_int64(f.exchange_timestamp) + ',';

    // Top of book — optional fields
    if (f.best_bid.has_value()) {
        o += "\"best_bid\":" + fmt_int64(*f.best_bid) + ',';
    } else {
        o += "\"best_bid\":null,";
    }
    if (f.best_ask.has_value()) {
        o += "\"best_ask\":" + fmt_int64(*f.best_ask) + ',';
    } else {
        o += "\"best_ask\":null,";
    }
    o += "\"best_bid_volume\":" + fmt_int64(f.best_bid_volume) + ',';
    o += "\"best_ask_volume\":" + fmt_int64(f.best_ask_volume) + ',';

    // Derived
    o += "\"spread\":" + fmt_double(f.spread) + ',';
    o += "\"mid\":" + fmt_double(f.mid) + ',';
    o += "\"microprice\":" + fmt_double(f.microprice) + ',';

    // Analytics
    o += "\"imbalance\":" + fmt_double(f.imbalance) + ',';
    o += "\"ofi\":" + fmt_double(f.ofi) + ',';
    o += "\"depth_ratio\":" + fmt_double(f.depth_ratio) + ',';
    o += "\"cancel_rate\":" + fmt_double(f.cancel_rate) + ',';
    o += "\"queue_half_life\":" + fmt_double(f.queue_half_life) + ',';
    o += "\"liquidity_slope\":" + fmt_double(f.liquidity_slope) + ',';
    o += "\"regime\":\"" + escape_string(microstructure::to_string(f.regime)) + "\",";

    // Trade marker
    o += "\"is_trade\":" + fmt_bool(f.is_trade) + ',';
    if (f.trade.has_value()) {
        o += "\"trade\":" + serialize_trade(*f.trade) + ',';
    } else {
        o += "\"trade\":null,";
    }
    o += "\"last_trade_aggressor\":\"" +
         escape_string(microstructure::to_string(f.last_trade_aggressor)) + "\",";

    // Latency
    o += "\"network_latency\":" + fmt_int64(f.network_latency) + ',';
    o += "\"gateway_latency\":" + fmt_int64(f.gateway_latency) + ',';
    o += "\"processing_latency\":" + fmt_int64(f.processing_latency) + ',';

    // Bid levels
    o += "\"bid_levels\":[";
    for (std::size_t i = 0; i < f.bid_levels.size(); ++i) {
        if (i > 0) o += ',';
        o += serialize_depth_level(f.bid_levels[i]);
    }
    o += "],";

    // Ask levels
    o += "\"ask_levels\":[";
    for (std::size_t i = 0; i < f.ask_levels.size(); ++i) {
        if (i > 0) o += ',';
        o += serialize_depth_level(f.ask_levels[i]);
    }
    o += ']';

    o += '}';
    return o;
}

std::string JsonSerializer::serialize_frames(
    const std::vector<VisualizationFrame>& frames) const
{
    std::string o;
    o.reserve(frames.size() * 2048);
    o += '[';
    for (std::size_t i = 0; i < frames.size(); ++i) {
        if (i > 0) o += ',';
        o += serialize_frame(frames[i]);
    }
    o += ']';
    return o;
}

} // namespace visualization
