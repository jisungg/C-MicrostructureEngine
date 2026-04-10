#include "microstructure/pipeline.hpp"

namespace microstructure {

SignalVector SignalVectorGenerator::generate(const FeatureSnapshot& snapshot) const {
    return SignalVector{
        snapshot.imbalance,
        snapshot.microprice,
        snapshot.spread,
        snapshot.ofi,
        snapshot.depth_ratio,
        snapshot.queue.cancel_rate,
        snapshot.queue.queue_half_life,
        snapshot.liquidity_slope};
}

BookGraph ResearchInterface::export_graph(const BookView& book) const {
    return exporter_.export_graph(book);
}

std::vector<double> ResearchInterface::export_embedding(const BookView& book,
                                                        const std::size_t depth_levels) const {
    return exporter_.book_embedding(book, depth_levels);
}

std::vector<LiquidityDensityPoint> ResearchInterface::export_liquidity_density(const BookView& book,
                                                                               const Side side,
                                                                               const std::size_t depth_levels,
                                                                               const double bandwidth) const {
    return exporter_.kernel_density(book, side, depth_levels, bandwidth);
}

std::vector<HeatmapBucket> ResearchInterface::export_liquidity_heatmap(const BookView& book,
                                                                       const std::size_t depth_levels) const {
    return exporter_.liquidity_heatmap(book, depth_levels);
}

MicrostructurePipeline::MicrostructurePipeline(FeatureConfig config)
    : feature_engine_(std::move(config)) {}

PipelineResult MicrostructurePipeline::process(const Event& event) {
    validator_.validate(event);
    const Event normalized = normalizer_.normalize(event);
    const BookUpdate update = book_.process(normalized);
    const FeatureSnapshot features = feature_engine_.on_event(normalized, book_, update);
    const SignalVector signal = signal_generator_.generate(features);

    PipelineResult result;
    result.event_id = normalized.event_id();
    result.event_type = normalized.event_type();
    result.venue = normalized.venue();
    result.book = book_.summary();
    result.deltas = update.deltas;
    result.trade = update.trade;
    result.features = features;
    result.features.signal = signal;
    result.signal = signal;
    return result;
}

void MicrostructurePipeline::reset() {
    validator_.reset();
    book_.clear();
    feature_engine_.reset();
}

const OrderBookStateEngine& MicrostructurePipeline::book() const noexcept {
    return book_;
}

const IncrementalFeatureEngine& MicrostructurePipeline::features() const noexcept {
    return feature_engine_;
}

const ResearchInterface& MicrostructurePipeline::research() const noexcept {
    return research_;
}

} // namespace microstructure
