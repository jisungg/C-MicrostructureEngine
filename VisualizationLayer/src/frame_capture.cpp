#include "visualization/frame_capture.hpp"

#include "microstructure/pipeline.hpp"

namespace visualization {

FrameCapture::FrameCapture(std::size_t depth_levels)
    : extractor_{depth_levels} {}

std::vector<VisualizationFrame> FrameCapture::capture(
    const std::vector<microstructure::Event>& events,
    const microstructure::FeatureConfig&      config)
{
    microstructure::MicrostructurePipeline pipeline{config};

    std::vector<VisualizationFrame> frames;
    frames.reserve(events.size());

    for (std::size_t i = 0; i < events.size(); ++i) {
        const microstructure::Event& event = events[i];
        const microstructure::PipelineResult result = pipeline.process(event);
        frames.push_back(extractor_.extract(i, event, result, pipeline.book()));
    }

    return frames;
}

} // namespace visualization
