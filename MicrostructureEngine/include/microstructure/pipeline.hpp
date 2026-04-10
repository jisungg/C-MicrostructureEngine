#pragma once

#include <cstddef>

#include "microstructure/features.hpp"
#include "microstructure/ml.hpp"
#include "microstructure/order_book.hpp"

namespace microstructure {

struct PipelineResult {
    EventId event_id{0};
    EventType event_type{EventType::Add};
    Venue venue{Venue::Nasdaq};
    BookSummary book{};
    std::vector<PriceLevelDelta> deltas{};
    std::optional<TradeExecution> trade{};
    FeatureSnapshot features{};
    SignalVector signal{};
};

class SignalVectorGenerator {
public:
    [[nodiscard]] SignalVector generate(const FeatureSnapshot& snapshot) const;
};

class ResearchInterface {
public:
    [[nodiscard]] BookGraph export_graph(const BookView& book) const;
    [[nodiscard]] std::vector<double> export_embedding(const BookView& book,
                                                       std::size_t depth_levels = 10) const;
    [[nodiscard]] std::vector<LiquidityDensityPoint> export_liquidity_density(const BookView& book,
                                                                              Side side,
                                                                              std::size_t depth_levels = 10,
                                                                              double bandwidth = 1.0) const;
    [[nodiscard]] std::vector<HeatmapBucket> export_liquidity_heatmap(const BookView& book,
                                                                      std::size_t depth_levels = 10) const;

private:
    ResearchMlInterface exporter_{};
};

class MicrostructurePipeline {
public:
    explicit MicrostructurePipeline(FeatureConfig config = {});

    [[nodiscard]] PipelineResult process(const Event& event);
    void reset();

    [[nodiscard]] const OrderBookStateEngine& book() const noexcept;
    [[nodiscard]] const IncrementalFeatureEngine& features() const noexcept;
    [[nodiscard]] const ResearchInterface& research() const noexcept;

private:
    EventValidator validator_{};
    EventNormalizer normalizer_{};
    OrderBookStateEngine book_{};
    IncrementalFeatureEngine feature_engine_;
    SignalVectorGenerator signal_generator_{};
    ResearchInterface research_{};
};

} // namespace microstructure
