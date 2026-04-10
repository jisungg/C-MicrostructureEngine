#pragma once

// FrameCapture — drives replay through a MicrostructurePipeline event by
// event and accumulates deterministic VisualizationFrames.
//
// Separation of concerns:
//   FrameCapture  — replay stepping, pipeline ownership, frame accumulation
//   FrameExtractor — engine state → VisualizationFrame conversion

#include <vector>

#include "visualization/frame.hpp"
#include "visualization/frame_extractor.hpp"
#include "microstructure/event.hpp"
#include "microstructure/features.hpp"

namespace visualization {

// Replays an event sequence through a fresh MicrostructurePipeline and
// returns the captured frame sequence ordered by replay position
// (frame_index 0 … N-1, one frame per event).
//
// Throws whatever MicrostructurePipeline::process() throws
// (e.g. microstructure::ValidationError) if the event stream is invalid.
class FrameCapture {
public:
    // depth_levels: forwarded to FrameExtractor (levels per side).
    explicit FrameCapture(std::size_t depth_levels = 10);

    // Replay events and capture one VisualizationFrame per event.
    // A fresh pipeline is created for each call — no state carried between
    // capture() calls.
    [[nodiscard]] std::vector<VisualizationFrame> capture(
        const std::vector<microstructure::Event>& events,
        const microstructure::FeatureConfig&      config = {});

private:
    FrameExtractor extractor_;
};

} // namespace visualization
