#pragma once

// HtmlExporter — generates a self-contained HTML replay viewer.
//
// Primary API returns a std::string (deterministic, testable).
// write_html() is a thin wrapper that writes the string to disk.
//
// The HTML file contains:
//   • All frames serialized as embedded JSON (via JsonSerializer)
//   • A vanilla JS renderer (no npm, no CDN, no build step)
//   • Replay controls: First | Prev | Play/Pause | Next | Last | Jump
//   • Book ladder panel (bid/ask levels with volume bars)
//   • Signal panel (spread, microprice, imbalance, OFI, regime, …)
//   • Trade markers

#include <string>
#include <vector>

#include "visualization/frame.hpp"

namespace visualization {

class HtmlExporter {
public:
    // Returns the complete HTML document as a string.
    // Safe to call multiple times; output is deterministic.
    [[nodiscard]] std::string render_html(
        const std::vector<VisualizationFrame>& frames) const;

    // Write HTML to output_path.  Returns false on I/O failure.
    bool write_html(const std::vector<VisualizationFrame>& frames,
                    const std::string& output_path) const;

private:
    // Build the full HTML string given the pre-serialized JSON array.
    [[nodiscard]] static std::string build_html(const std::string& frames_json,
                                                std::size_t        frame_count);
};

} // namespace visualization
