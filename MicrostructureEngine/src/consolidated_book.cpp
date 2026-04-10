#include "microstructure/consolidated_book.hpp"

#include <algorithm>
#include <limits>

#include "microstructure/exceptions.hpp"

namespace microstructure {

CrossVenueConsolidatedBook::CrossVenueConsolidatedBook() {
    venue_books_.emplace(Venue::Nasdaq, OrderBookStateEngine{});
    venue_books_.emplace(Venue::Arca, OrderBookStateEngine{});
    venue_books_.emplace(Venue::Bats, OrderBookStateEngine{});
    venue_books_.emplace(Venue::Iex, OrderBookStateEngine{});
    venue_contributions_.emplace(Venue::Nasdaq, std::map<Price, SideDepth>{});
    venue_contributions_.emplace(Venue::Arca, std::map<Price, SideDepth>{});
    venue_contributions_.emplace(Venue::Bats, std::map<Price, SideDepth>{});
    venue_contributions_.emplace(Venue::Iex, std::map<Price, SideDepth>{});
}

void CrossVenueConsolidatedBook::apply_consolidated_delta(const Side side,
                                                          const Price price,
                                                          const Quantity delta) {
    SideDepth& depth = consolidated_depth_[price];
    if (side == Side::Bid) {
        depth.bid_volume += delta;
        total_bid_volume_ += delta;
        if (depth.bid_volume < 0) {
            throw BookInvariantError("consolidated bid volume cannot be negative");
        }
        if (total_bid_volume_ < 0) {
            throw BookInvariantError("consolidated total bid volume cannot be negative");
        }
        if (depth.bid_volume == 0) {
            consolidated_bids_.erase(price);
        } else {
            consolidated_bids_[price] = depth.bid_volume;
        }
    } else {
        depth.ask_volume += delta;
        total_ask_volume_ += delta;
        if (depth.ask_volume < 0) {
            throw BookInvariantError("consolidated ask volume cannot be negative");
        }
        if (total_ask_volume_ < 0) {
            throw BookInvariantError("consolidated total ask volume cannot be negative");
        }
        if (depth.ask_volume == 0) {
            consolidated_asks_.erase(price);
        } else {
            consolidated_asks_[price] = depth.ask_volume;
        }
    }

    if (depth.bid_volume == 0 && depth.ask_volume == 0) {
        consolidated_depth_.erase(price);
    }
}

void CrossVenueConsolidatedBook::rebuild_venue_contribution(const Venue venue) {
    auto& contribution = venue_contributions_.at(venue);
    for (const auto& [price, depth] : contribution) {
        if (depth.bid_volume != 0) {
            apply_consolidated_delta(Side::Bid, price, -depth.bid_volume);
        }
        if (depth.ask_volume != 0) {
            apply_consolidated_delta(Side::Ask, price, -depth.ask_volume);
        }
    }
    contribution.clear();

    const OrderBookStateEngine& book = venue_books_.at(venue);
    for (const BookLevelState& level : book.all_levels(Side::Bid)) {
        contribution[level.price].bid_volume = level.total_volume;
        apply_consolidated_delta(Side::Bid, level.price, level.total_volume);
    }
    for (const BookLevelState& level : book.all_levels(Side::Ask)) {
        contribution[level.price].ask_volume = level.total_volume;
        apply_consolidated_delta(Side::Ask, level.price, level.total_volume);
    }
}

BookUpdate CrossVenueConsolidatedBook::process(const Event& raw_event) {
    validator_.validate(raw_event);
    const Event event = normalizer_.normalize(raw_event);

    OrderBookStateEngine& book = venue_books_.at(event.venue());
    BookUpdate update = book.process(event);

    if (update.snapshot_restored) {
        rebuild_venue_contribution(event.venue());
        return update;
    }

    auto& contribution = venue_contributions_.at(event.venue());
    for (const PriceLevelDelta& delta : update.deltas) {
        if (delta.side == Side::Bid) {
            contribution[delta.price].bid_volume += delta.delta_volume;
            if (contribution[delta.price].bid_volume < 0) {
                throw BookInvariantError("venue bid contribution cannot be negative");
            }
        } else {
            contribution[delta.price].ask_volume += delta.delta_volume;
            if (contribution[delta.price].ask_volume < 0) {
                throw BookInvariantError("venue ask contribution cannot be negative");
            }
        }
        apply_consolidated_delta(delta.side, delta.price, delta.delta_volume);
    }

    return update;
}

const OrderBookStateEngine& CrossVenueConsolidatedBook::venue_book(const Venue venue) const {
    return venue_books_.at(venue);
}

std::optional<Price> CrossVenueConsolidatedBook::best_bid() const {
    return consolidated_bids_.empty() ? std::nullopt : std::optional<Price>(consolidated_bids_.begin()->first);
}

std::optional<Price> CrossVenueConsolidatedBook::best_ask() const {
    return consolidated_asks_.empty() ? std::nullopt : std::optional<Price>(consolidated_asks_.begin()->first);
}

Quantity CrossVenueConsolidatedBook::best_bid_volume() const {
    return consolidated_bids_.empty() ? 0 : consolidated_bids_.begin()->second;
}

Quantity CrossVenueConsolidatedBook::best_ask_volume() const {
    return consolidated_asks_.empty() ? 0 : consolidated_asks_.begin()->second;
}

Quantity CrossVenueConsolidatedBook::total_volume(const Side side) const {
    return side == Side::Bid ? total_bid_volume_ : total_ask_volume_;
}

bool CrossVenueConsolidatedBook::is_crossed() const {
    return !consolidated_bids_.empty() && !consolidated_asks_.empty() &&
           consolidated_bids_.begin()->first > consolidated_asks_.begin()->first;
}

bool CrossVenueConsolidatedBook::is_locked() const {
    return !consolidated_bids_.empty() && !consolidated_asks_.empty() &&
           consolidated_bids_.begin()->first == consolidated_asks_.begin()->first;
}

Quantity CrossVenueConsolidatedBook::volume_at(const Side side, const Price price) const {
    const auto it = consolidated_depth_.find(price);
    if (it == consolidated_depth_.end()) {
        return 0;
    }
    return side == Side::Bid ? it->second.bid_volume : it->second.ask_volume;
}

std::vector<BookLevelState> CrossVenueConsolidatedBook::top_levels(const Side side,
                                                                   const std::size_t levels) const {
    std::vector<BookLevelState> result;
    if (side == Side::Bid) {
        result.reserve(std::min(levels, consolidated_bids_.size()));
        for (auto it = consolidated_bids_.begin(); it != consolidated_bids_.end() && result.size() < levels; ++it) {
            result.push_back(BookLevelState{Side::Bid, it->first, it->second, 0U, 0.0, 0.0, 0.0});
        }
        return result;
    }
    result.reserve(std::min(levels, consolidated_asks_.size()));
    for (auto it = consolidated_asks_.begin(); it != consolidated_asks_.end() && result.size() < levels; ++it) {
        result.push_back(BookLevelState{Side::Ask, it->first, it->second, 0U, 0.0, 0.0, 0.0});
    }
    return result;
}

std::vector<BookLevelState> CrossVenueConsolidatedBook::all_levels(const Side side) const {
    return top_levels(side, std::numeric_limits<std::size_t>::max());
}

BookSummary CrossVenueConsolidatedBook::summary() const {
    std::size_t total_orders = 0;
    for (const auto& [venue, book] : venue_books_) {
        (void)venue;
        total_orders += book.total_order_count();
    }

    return BookSummary{
        best_bid(),
        best_ask(),
        best_bid_volume(),
        best_ask_volume(),
        total_bid_volume_,
        total_ask_volume_,
        consolidated_bids_.size(),
        consolidated_asks_.size(),
        total_orders};
}

void CrossVenueConsolidatedBook::clear() {
    validator_.reset();
    for (auto& [venue, book] : venue_books_) {
        (void)venue;
        book.clear();
    }
    for (auto& [venue, contribution] : venue_contributions_) {
        (void)venue;
        contribution.clear();
    }
    consolidated_depth_.clear();
    consolidated_bids_.clear();
    consolidated_asks_.clear();
    total_bid_volume_ = 0;
    total_ask_volume_ = 0;
}

CrossVenueMicrostructurePipeline::CrossVenueMicrostructurePipeline(FeatureConfig config)
    : feature_engine_(std::move(config)) {}

PipelineResult CrossVenueMicrostructurePipeline::process(const Event& event) {
    const BookUpdate update = book_.process(event);
    const FeatureSnapshot features = feature_engine_.on_event(event, book_, update);
    const SignalVector signal = signal_generator_.generate(features);

    PipelineResult result;
    result.event_id = event.event_id();
    result.event_type = event.event_type();
    result.venue = event.venue();
    result.book = book_.summary();
    result.deltas = update.deltas;
    result.trade = update.trade;
    result.features = features;
    result.features.signal = signal;
    result.signal = signal;
    return result;
}

void CrossVenueMicrostructurePipeline::reset() {
    book_.clear();
    feature_engine_.reset();
}

const CrossVenueConsolidatedBook& CrossVenueMicrostructurePipeline::book() const noexcept {
    return book_;
}

const IncrementalFeatureEngine& CrossVenueMicrostructurePipeline::features() const noexcept {
    return feature_engine_;
}

const ResearchInterface& CrossVenueMicrostructurePipeline::research() const noexcept {
    return research_;
}

} // namespace microstructure
