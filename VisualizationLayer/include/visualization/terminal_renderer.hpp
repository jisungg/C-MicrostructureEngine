#pragma once

// TerminalRenderer — renders a VisualizationFrame as structured ASCII text.
//
// Primary API returns std::string (deterministic, testable).
// print_frame() is a thin wrapper that writes to stdout.
//
// Output format:
//   ── frame header (index, event, venue, timestamp) ──
//   ask ladder (reversed, best ask nearest mid)
//   ── spread | mid | microprice ──
//   bid ladder (best bid nearest mid)
//   signal summary line

#include <string>

#include "visualization/frame.hpp"

namespace visualization {

struct TerminalConfig {
    std::size_t bar_width{20};      // max width of ASCII volume bar
    std::size_t depth_levels{5};    // book levels displayed per side
    bool        show_signals{true};
    bool        show_trade{true};
};

class TerminalRenderer {
public:
    explicit TerminalRenderer(TerminalConfig config = {});

    // Render the frame to a std::string. Deterministic.
    [[nodiscard]] std::string render_frame(const VisualizationFrame& frame) const;

    // Write render_frame() result to stdout.
    void print_frame(const VisualizationFrame& frame) const;

private:
    TerminalConfig config_;

    [[nodiscard]] std::string render_ladder(const VisualizationFrame& frame) const;
    [[nodiscard]] std::string render_signals(const VisualizationFrame& frame) const;
    [[nodiscard]] static std::string render_trade(const VisualizationFrame& frame);

    // Format a double with 4 decimal places.
    [[nodiscard]] static std::string fmt4(double v);

    // Build an ASCII bar of `len` '#' chars, capped at bar_width_.
    [[nodiscard]] std::string make_bar(std::size_t len) const;

    // Compute max volume across bid+ask levels for bar scaling.
    [[nodiscard]] static microstructure::Quantity max_volume(
        const VisualizationFrame& frame);
};

} // namespace visualization
