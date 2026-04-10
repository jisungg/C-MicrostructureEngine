#pragma once

// JsonSerializer — hand-rolled deterministic JSON serialization for
// VisualizationFrame.  No external library required.
//
// Field order, escaping rules, and schema version are locked by tests.
// See export_schema.hpp for the full field contract.

#include <string>
#include <string_view>
#include <vector>

#include "visualization/frame.hpp"

namespace visualization {

class JsonSerializer {
public:
    // Serialize a single frame to a self-contained JSON object string.
    // Includes "schema_version" as the first field.
    [[nodiscard]] std::string serialize_frame(const VisualizationFrame& frame) const;

    // Serialize all frames to a JSON array string.
    // Each element is produced by serialize_frame().
    [[nodiscard]] std::string serialize_frames(
        const std::vector<VisualizationFrame>& frames) const;

private:
    // Escape a UTF-8 string for embedding in a JSON string literal.
    // Handles: \", \\, \/, \n, \r, \t, \b, \f, control chars (\uXXXX),
    // and HTML-unsafe chars < > (as \u003c, \u003e).
    [[nodiscard]] static std::string escape_string(std::string_view s);

    // Serialize one DepthLevel object.
    [[nodiscard]] static std::string serialize_depth_level(const DepthLevel& lvl);

    // Serialize TradeExecution object (used inside "trade" field).
    [[nodiscard]] static std::string serialize_trade(
        const microstructure::TradeExecution& t);

    // Format a double with %.10g (10 significant digits, compact notation).
    [[nodiscard]] static std::string fmt_double(double v);
};

} // namespace visualization
