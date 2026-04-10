#pragma once

// VisualizationFrame — core data contract for the visualization layer.
//
// Every field is derived exclusively from MicrostructureEngine state; no
// analytics are computed or approximated here. This struct is the only
// type passed between visualization subsystems — FrameExtractor produces
// it, everything else consumes it.

#include <cstddef>
#include <optional>
#include <vector>

#include "microstructure/types.hpp"

namespace visualization {

// A single resting price level extracted from the engine's book ladder.
//
// Fields:
//   price        — raw engine Price (int64, tick units)
//   volume       — total resting quantity at this price
//   queue_depth  — number of individual orders at this price
//   order_flow   — net signed order flow from engine BookLevelState
//   cancel_rate  — fraction of add volume that was canceled at this level
//   fill_rate    — fraction of add volume that was filled at this level
//
// Ordering guarantee: bid_levels[0] is the best (highest) bid;
//                     ask_levels[0] is the best (lowest) ask.
struct DepthLevel {
    microstructure::Price    price{0};
    microstructure::Quantity volume{0};
    std::size_t              queue_depth{0};
    double                   order_flow{0.0};
    double                   cancel_rate{0.0};
    double                   fill_rate{0.0};
};

// VisualizationFrame captures the full visible state of the engine after
// processing one event during replay.
//
// Determinism: given the same event stream and FeatureConfig, frames at
// the same frame_index are always identical across runs.
//
// Optional fields: best_bid, best_ask, and trade are absent (nullopt/false)
// when the book is one-sided, empty, or the event was not a trade.
//
// Units:
//   Price      — integer tick units (as used throughout the engine)
//   Quantity   — integer share/contract units
//   Timestamp  — nanoseconds (int64)
//   spread/mid — double in tick units (best_ask − best_bid)
//   microprice — double tick units (engine-computed volume-weighted mid)
//   All rates  — dimensionless [0, 1] fractions
//   latency    — nanoseconds (int64 offsets)
struct VisualizationFrame {
    // ── Replay position ──────────────────────────────────────────────────
    // frame_index: 0-based position in the replay sequence (deterministic).
    // event_id:    engine EventId from PipelineResult (unique per stream).
    std::size_t                   frame_index{0};
    microstructure::EventId       event_id{0};
    microstructure::EventType     event_type{microstructure::EventType::Add};
    microstructure::Venue         venue{microstructure::Venue::Nasdaq};
    microstructure::Timestamp     exchange_timestamp{0};

    // ── Top of book ──────────────────────────────────────────────────────
    // Absent (nullopt) when the respective side has no resting orders.
    std::optional<microstructure::Price>    best_bid{};
    std::optional<microstructure::Price>    best_ask{};
    microstructure::Quantity                best_bid_volume{0};
    microstructure::Quantity                best_ask_volume{0};

    // ── Derived top-of-book metrics ───────────────────────────────────────
    // spread: best_ask − best_bid in ticks; 0.0 when either side is absent.
    // mid:    (best_bid + best_ask) / 2.0; 0.0 when either side is absent.
    // microprice: volume-weighted mid from engine FeatureSnapshot.
    double spread{0.0};
    double mid{0.0};
    double microprice{0.0};

    // ── Depth ladder ─────────────────────────────────────────────────────
    // Up to FeatureConfig::depth_levels entries per side.
    // bid_levels[0] = best bid (highest price).
    // ask_levels[0] = best ask (lowest price).
    std::vector<DepthLevel> bid_levels{};
    std::vector<DepthLevel> ask_levels{};

    // ── Engine-derived analytics ──────────────────────────────────────────
    // All sourced directly from FeatureSnapshot / SignalVector.
    double                        imbalance{0.0};
    double                        ofi{0.0};
    double                        depth_ratio{0.0};
    double                        cancel_rate{0.0};
    double                        queue_half_life{0.0};
    double                        liquidity_slope{0.0};
    microstructure::LiquidityRegime regime{microstructure::LiquidityRegime::Illiquid};

    // ── Trade marker ─────────────────────────────────────────────────────
    // is_trade: true iff this frame's event was a Trade execution.
    // trade:    execution detail; present iff is_trade == true.
    bool                                          is_trade{false};
    std::optional<microstructure::TradeExecution> trade{};
    microstructure::TradeAggressor                last_trade_aggressor{
        microstructure::TradeAggressor::Unknown};

    // ── Latency metrics ───────────────────────────────────────────────────
    // Nanosecond offsets sourced from FeatureSnapshot::latency.
    microstructure::Timestamp network_latency{0};
    microstructure::Timestamp gateway_latency{0};
    microstructure::Timestamp processing_latency{0};
};

} // namespace visualization
