#pragma once

// export_schema.hpp — JSON serialization contract for VisualizationFrame.
//
// Increment kSchemaVersion whenever field names, types, or ordering change.
// Tests lock this version; a bump forces explicit test updates.

#include <string_view>

namespace visualization {

// ── Schema version ────────────────────────────────────────────────────────
inline constexpr int             kSchemaVersion    = 1;
inline constexpr std::string_view kSchemaVersionKey = "schema_version";

// ── Required fields (always present, never null) ─────────────────────────
// schema_version       int
// frame_index          uint  (0-based)
// event_id             uint
// event_type           string  ("ADD"|"CANCEL"|"MODIFY"|"TRADE"|"SNAPSHOT")
// venue                string  ("NASDAQ"|"ARCA"|"BATS"|"IEX")
// exchange_timestamp   int64   nanoseconds
// best_bid_volume      int64
// best_ask_volume      int64
// spread               double  tick units
// mid                  double  tick units
// microprice           double  tick units
// imbalance            double  [-1, 1]  (positive = bid-heavy)
// ofi                  double
// depth_ratio          double  | null   (null when one side is empty)
// cancel_rate          double  [0, 1]
// queue_half_life      double
// liquidity_slope      double
// regime               string  ("tight"|"normal"|"stressed"|"illiquid")
// is_trade             bool
// last_trade_aggressor string  ("UNKNOWN"|"BUY_AGGRESSOR"|"SELL_AGGRESSOR")
// network_latency      int64   ns
// gateway_latency      int64   ns
// processing_latency   int64   ns
// bid_levels           array<DepthLevel>
// ask_levels           array<DepthLevel>

// ── Optional fields (null in JSON when absent) ────────────────────────────
// best_bid             int64 | null
// best_ask             int64 | null
// trade                object | null  (present iff is_trade == true)

// ── DepthLevel object fields ──────────────────────────────────────────────
// price        int64
// volume       int64
// queue_depth  uint
// order_flow   double
// cancel_rate  double
// fill_rate    double

// ── TradeExecution object fields (within "trade") ─────────────────────────
// price                    int64
// size                     int64
// resting_side             string  ("BID"|"ASK")
// aggressor                string
// visible_depth_before     int64
// remaining_at_price_after int64

// ── Ordering guarantees ───────────────────────────────────────────────────
// Fields emitted in serialization order (see json_serializer.cpp); the
// logical required/optional grouping is conceptual, not a wire-order promise.
// bid_levels[0] = best bid (highest price).
// ask_levels[0] = best ask (lowest price).
// Frame array ordered by frame_index ascending.

// ── String escaping ───────────────────────────────────────────────────────
// JSON strings are escaped per RFC 8259: \", \\, \/, \n, \r, \t, \b, \f,
// plus \uXXXX for control characters. < and > are escaped as \u003c \u003e
// to make the output safe for embedding inside <script> tags in HTML.

} // namespace visualization
