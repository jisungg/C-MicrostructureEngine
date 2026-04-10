#include "visualization/frame_extractor.hpp"

#include <algorithm>

#include "microstructure/types.hpp"

namespace visualization {

FrameExtractor::FrameExtractor(std::size_t depth_levels)
    : depth_levels_{depth_levels} {}

VisualizationFrame FrameExtractor::extract(
    std::size_t                                frame_index,
    const microstructure::Event&               event,
    const microstructure::PipelineResult&      result,
    const microstructure::OrderBookStateEngine& book) const
{
    VisualizationFrame frame;

    // ── Replay position ───────────────────────────────────────────────────
    frame.frame_index       = frame_index;
    frame.event_id          = result.event_id;
    frame.event_type        = result.event_type;
    frame.venue             = result.venue;
    frame.exchange_timestamp = event.exchange_timestamp();

    // ── Top of book ───────────────────────────────────────────────────────
    frame.best_bid        = result.book.best_bid;
    frame.best_ask        = result.book.best_ask;
    frame.best_bid_volume = result.book.best_bid_volume;
    frame.best_ask_volume = result.book.best_ask_volume;

    // ── Derived metrics ───────────────────────────────────────────────────
    if (result.book.best_bid.has_value() && result.book.best_ask.has_value()) {
        const double bid = static_cast<double>(*result.book.best_bid);
        const double ask = static_cast<double>(*result.book.best_ask);
        frame.spread = ask - bid;
        frame.mid    = (bid + ask) * 0.5;
    }
    frame.microprice = result.features.microprice;

    // ── Analytics ─────────────────────────────────────────────────────────
    frame.imbalance       = result.features.imbalance;
    frame.ofi             = result.features.ofi;
    frame.depth_ratio     = result.features.depth_ratio;
    frame.cancel_rate     = result.features.queue.cancel_rate;
    frame.queue_half_life = result.features.queue.queue_half_life;
    frame.liquidity_slope = result.features.liquidity_slope;
    frame.regime          = result.features.regime;

    // ── Trade marker ──────────────────────────────────────────────────────
    frame.is_trade            = result.trade.has_value();
    frame.trade               = result.trade;
    frame.last_trade_aggressor = result.features.last_trade_aggressor;

    // ── Latency ───────────────────────────────────────────────────────────
    frame.network_latency    = result.features.latency.network_latency;
    frame.gateway_latency    = result.features.latency.gateway_latency;
    frame.processing_latency = result.features.latency.processing_latency;

    // ── Depth ladder ──────────────────────────────────────────────────────
    // Pull live depth from the book (not from BookSummary which only has top).
    const auto bid_levels = book.top_levels(microstructure::Side::Bid, depth_levels_);
    const auto ask_levels = book.top_levels(microstructure::Side::Ask, depth_levels_);

    frame.bid_levels.reserve(bid_levels.size());
    for (const auto& lvl : bid_levels) {
        DepthLevel dl;
        dl.price       = lvl.price;
        dl.volume      = lvl.total_volume;
        dl.queue_depth = lvl.queue_depth;
        dl.order_flow  = lvl.order_flow;
        dl.cancel_rate = lvl.cancel_rate;
        dl.fill_rate   = lvl.fill_rate;
        frame.bid_levels.push_back(dl);
    }

    frame.ask_levels.reserve(ask_levels.size());
    for (const auto& lvl : ask_levels) {
        DepthLevel dl;
        dl.price       = lvl.price;
        dl.volume      = lvl.total_volume;
        dl.queue_depth = lvl.queue_depth;
        dl.order_flow  = lvl.order_flow;
        dl.cancel_rate = lvl.cancel_rate;
        dl.fill_rate   = lvl.fill_rate;
        frame.ask_levels.push_back(dl);
    }

    return frame;
}

} // namespace visualization
