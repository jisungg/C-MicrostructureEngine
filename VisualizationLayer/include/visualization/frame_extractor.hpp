#pragma once

// FrameExtractor — the ONLY visualization component that directly
// interfaces with MicrostructureEngine types.
//
// All other visualization components receive VisualizationFrame only.

#include <cstddef>

#include "visualization/frame.hpp"
#include "microstructure/event.hpp"
#include "microstructure/order_book.hpp"
#include "microstructure/pipeline.hpp"

namespace visualization {

// Converts one (Event, PipelineResult, OrderBookStateEngine) triple into
// a VisualizationFrame.  Stateless — safe to call concurrently with
// independent inputs.
class FrameExtractor {
public:
    // depth_levels: max book levels extracted per side.
    // Capped to the number of levels actually present in the book.
    explicit FrameExtractor(std::size_t depth_levels = 10);

    // Produce a VisualizationFrame from engine state after one event.
    //
    // frame_index — 0-based position in the caller's replay sequence.
    // event       — the event that was just processed (provides timestamp).
    // result      — PipelineResult returned by MicrostructurePipeline::process().
    // book        — live book state after process() (provides depth ladder).
    [[nodiscard]] VisualizationFrame extract(
        std::size_t                                frame_index,
        const microstructure::Event&               event,
        const microstructure::PipelineResult&      result,
        const microstructure::OrderBookStateEngine& book) const;

private:
    std::size_t depth_levels_;
};

} // namespace visualization
