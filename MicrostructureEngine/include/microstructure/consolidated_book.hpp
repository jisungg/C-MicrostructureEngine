#pragma once

#include <map>
#include <optional>
#include <vector>

#include "microstructure/pipeline.hpp"
#include "microstructure/order_book.hpp"

namespace microstructure {

class CrossVenueConsolidatedBook : public BookView {
public:
    CrossVenueConsolidatedBook();

    [[nodiscard]] BookUpdate process(const Event& event);
    [[nodiscard]] const OrderBookStateEngine& venue_book(Venue venue) const;
    [[nodiscard]] std::optional<Price> best_bid() const override;
    [[nodiscard]] std::optional<Price> best_ask() const override;
    [[nodiscard]] Quantity best_bid_volume() const override;
    [[nodiscard]] Quantity best_ask_volume() const override;
    [[nodiscard]] Quantity total_volume(Side side) const override;
    [[nodiscard]] Quantity volume_at(Side side, Price price) const;
    [[nodiscard]] std::vector<BookLevelState> top_levels(Side side, std::size_t levels) const override;
    [[nodiscard]] std::vector<BookLevelState> all_levels(Side side) const override;
    [[nodiscard]] BookSummary summary() const override;
    [[nodiscard]] bool is_crossed() const;
    [[nodiscard]] bool is_locked() const;
    void clear();

private:
    struct SideDepth {
        Quantity bid_volume{0};
        Quantity ask_volume{0};
    };

    EventValidator validator_{};
    EventNormalizer normalizer_{};
    std::map<Venue, OrderBookStateEngine> venue_books_{};
    std::map<Venue, std::map<Price, SideDepth>> venue_contributions_{};
    std::map<Price, SideDepth> consolidated_depth_{};
    std::map<Price, Quantity, std::greater<Price>> consolidated_bids_{};
    std::map<Price, Quantity, std::less<Price>> consolidated_asks_{};
    Quantity total_bid_volume_{0};
    Quantity total_ask_volume_{0};

    void apply_consolidated_delta(Side side, Price price, Quantity delta);
    void rebuild_venue_contribution(Venue venue);
};

class CrossVenueMicrostructurePipeline {
public:
    explicit CrossVenueMicrostructurePipeline(FeatureConfig config = {});

    [[nodiscard]] PipelineResult process(const Event& event);
    void reset();

    [[nodiscard]] const CrossVenueConsolidatedBook& book() const noexcept;
    [[nodiscard]] const IncrementalFeatureEngine& features() const noexcept;
    [[nodiscard]] const ResearchInterface& research() const noexcept;

private:
    CrossVenueConsolidatedBook book_{};
    IncrementalFeatureEngine feature_engine_;
    SignalVectorGenerator signal_generator_{};
    ResearchInterface research_{};
};

} // namespace microstructure
