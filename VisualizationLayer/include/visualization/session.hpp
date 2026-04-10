#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "microstructure/types.hpp"

namespace visualization {

enum class SourceMode { Demo, Synthetic, Realistic, Csv };

struct VisualizationSession {
    std::string               source_path;            // CSV path or "" for generated
    SourceMode                source_mode{SourceMode::Demo};
    std::size_t               frame_count{0};
    microstructure::Timestamp first_ts{0};
    microstructure::Timestamp last_ts{0};
    std::size_t               current_frame{0};
    std::string               active_filter;          // "ALL", "TRADE", "ADD", ...
    std::vector<std::size_t>  bookmarks;

    // Serialise to compact JSON string.
    std::string to_json() const;

    // Deserialise from JSON produced by to_json().
    // Throws std::runtime_error on malformed input.
    static VisualizationSession from_json(const std::string& json);

    // Write session to file.  Returns false on I/O error.
    bool save(const std::string& path) const;

    // Load session from file.  Throws std::runtime_error on parse or I/O error.
    static VisualizationSession load(const std::string& path);

    // Return true if event_type should be shown under active_filter.
    // Empty or "ALL" active_filter always returns true.
    bool matches(const std::string& event_type) const;
};

} // namespace visualization
