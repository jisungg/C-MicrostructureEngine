#include "visualization/terminal_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <sstream>

#include "microstructure/types.hpp"

namespace visualization {

TerminalRenderer::TerminalRenderer(TerminalConfig config)
    : config_{config} {}

// ── Helpers ──────────────────────────────────────────────────────────────

std::string TerminalRenderer::fmt4(double v) {
    if (!std::isfinite(v)) return " n/a";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    return std::string{buf};
}

std::string TerminalRenderer::make_bar(std::size_t len) const {
    const std::size_t capped = std::min(len, config_.bar_width);
    return std::string(capped, '#');
}

microstructure::Quantity TerminalRenderer::max_volume(const VisualizationFrame& frame) {
    microstructure::Quantity mx = 1;
    for (const auto& lvl : frame.bid_levels) mx = std::max(mx, lvl.volume);
    for (const auto& lvl : frame.ask_levels) mx = std::max(mx, lvl.volume);
    return mx;
}

// ── Render sections ───────────────────────────────────────────────────────

std::string TerminalRenderer::render_trade(const VisualizationFrame& frame) {
    if (!frame.is_trade || !frame.trade.has_value()) return {};
    const auto& t = *frame.trade;
    std::ostringstream ss;
    ss << "[TRADE @ " << t.price
       << " x" << t.size
       << " | " << microstructure::to_string(t.aggressor)
       << " | resting=" << microstructure::to_string(t.resting_side)
       << "]\n";
    return ss.str();
}

std::string TerminalRenderer::render_ladder(const VisualizationFrame& frame) const {
    const microstructure::Quantity mv = max_volume(frame);
    std::ostringstream ss;

    // Ask side — show reversed so best ask is at bottom (nearest mid)
    const std::size_t ask_display =
        std::min(config_.depth_levels, frame.ask_levels.size());

    ss << "  ASK\n";
    for (std::size_t i = ask_display; i > 0; --i) {
        const DepthLevel& lvl = frame.ask_levels[i - 1];
        const std::size_t bar_len =
            static_cast<std::size_t>(
                static_cast<double>(lvl.volume) /
                static_cast<double>(mv) *
                static_cast<double>(config_.bar_width));
        char pricebuf[24];
        char volbuf[16];
        std::snprintf(pricebuf, sizeof(pricebuf), "%8lld", static_cast<long long>(lvl.price));
        std::snprintf(volbuf, sizeof(volbuf), "%6lld", static_cast<long long>(lvl.volume));
        ss << "  " << pricebuf << " |" << volbuf << " | " << make_bar(bar_len) << "\n";
    }

    // Spread row
    if (frame.best_bid.has_value() && frame.best_ask.has_value()) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "  ── spread:%.1f mid:%.2f uP:%.4f ──\n",
            frame.spread, frame.mid, frame.microprice);
        ss << buf;
    } else {
        ss << "  ── (one-sided or empty book) ──\n";
    }

    // Bid side — best bid at top
    const std::size_t bid_display =
        std::min(config_.depth_levels, frame.bid_levels.size());

    for (std::size_t i = 0; i < bid_display; ++i) {
        const DepthLevel& lvl = frame.bid_levels[i];
        const std::size_t bar_len =
            static_cast<std::size_t>(
                static_cast<double>(lvl.volume) /
                static_cast<double>(mv) *
                static_cast<double>(config_.bar_width));
        char pricebuf[24];
        char volbuf[16];
        std::snprintf(pricebuf, sizeof(pricebuf), "%8lld", static_cast<long long>(lvl.price));
        std::snprintf(volbuf, sizeof(volbuf), "%6lld", static_cast<long long>(lvl.volume));
        ss << "  " << pricebuf << " |" << volbuf << " | " << make_bar(bar_len) << "\n";
    }
    ss << "  BID\n";

    return ss.str();
}

std::string TerminalRenderer::render_signals(const VisualizationFrame& frame) const {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "  imbalance=%s  OFI=%s  depth_ratio=%s  regime=%s\n",
        fmt4(frame.imbalance).c_str(),
        fmt4(frame.ofi).c_str(),
        fmt4(frame.depth_ratio).c_str(),
        microstructure::to_string(frame.regime).c_str());
    return std::string{buf};
}

// ── Public API ────────────────────────────────────────────────────────────

std::string TerminalRenderer::render_frame(const VisualizationFrame& frame) const {
    std::ostringstream ss;

    // Header
    ss << "Frame " << frame.frame_index
       << " | Event " << frame.event_id
       << " | " << microstructure::to_string(frame.event_type)
       << " | " << microstructure::to_string(frame.venue)
       << " | ts=" << frame.exchange_timestamp << "\n";

    ss << std::string(54, '-') << "\n";

    if (config_.show_trade) {
        ss << render_trade(frame);
    }

    ss << render_ladder(frame);

    if (config_.show_signals) {
        ss << std::string(54, '-') << "\n";
        ss << render_signals(frame);
    }

    ss << std::string(54, '=') << "\n";

    return ss.str();
}

void TerminalRenderer::print_frame(const VisualizationFrame& frame) const {
    std::cout << render_frame(frame);
}

} // namespace visualization
