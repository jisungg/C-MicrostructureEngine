#pragma once

#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "microstructure/book_view.hpp"
#include "microstructure/event.hpp"
#include "microstructure/types.hpp"

namespace microstructure {

struct BookUpdate {
    std::vector<PriceLevelDelta> deltas{};
    std::optional<TradeExecution> trade{};
    Quantity added_volume{0};
    Quantity canceled_volume{0};
    Quantity filled_volume{0};
    Quantity repriced_removed_volume{0};
    Quantity repriced_added_volume{0};
    bool snapshot_restored{false};
};

class OrderBookStateEngine : public BookView {
public:
    OrderBookStateEngine() = default;
    OrderBookStateEngine(const OrderBookStateEngine&) = delete;
    OrderBookStateEngine& operator=(const OrderBookStateEngine&) = delete;
    OrderBookStateEngine(OrderBookStateEngine&&) noexcept = default;
    OrderBookStateEngine& operator=(OrderBookStateEngine&&) noexcept = default;

    [[nodiscard]] BookUpdate process(const Event& event);
    [[nodiscard]] BookUpdate add_order(const Event& event);
    [[nodiscard]] BookUpdate cancel_order(const Event& event);
    [[nodiscard]] BookUpdate modify_order(const Event& event);
    [[nodiscard]] BookUpdate execute_trade(const Event& event);
    [[nodiscard]] BookUpdate restore_snapshot(const Event& event);

    [[nodiscard]] std::optional<Price> best_bid() const override;
    [[nodiscard]] std::optional<Price> best_ask() const override;
    [[nodiscard]] Quantity best_bid_volume() const override;
    [[nodiscard]] Quantity best_ask_volume() const override;
    [[nodiscard]] Quantity total_volume(Side side) const override;
    [[nodiscard]] Quantity volume_at(Side side, Price price) const;
    [[nodiscard]] std::size_t queue_depth_at(Side side, Price price) const;
    [[nodiscard]] std::size_t total_order_count() const;
    [[nodiscard]] std::vector<BookLevelState> top_levels(Side side, std::size_t levels) const override;
    [[nodiscard]] std::vector<BookLevelState> all_levels(Side side) const override;
    [[nodiscard]] MarketImpactEstimate estimate_market_impact(TradeAggressor aggressor, Quantity quantity) const;
    [[nodiscard]] BookSummary summary() const override;

    void validate_full_invariants() const;
    void clear();
    void swap(OrderBookStateEngine& other) noexcept;

private:
    struct OrderEntry {
        OrderId order_id{0};
        Price price{0};
        Quantity size{0};
        Side side{Side::Bid};
        Venue venue{Venue::Nasdaq};
        std::uint64_t priority{0};
    };

    struct PriceLevel {
        Price price{0};
        Side side{Side::Bid};
        Quantity total_volume{0};
        std::list<OrderEntry> queue{};
        Quantity add_volume{0};
        Quantity cancel_volume{0};
        Quantity fill_volume{0};
        double net_order_flow{0.0};
    };

    using BidLevels = std::map<Price, PriceLevel, std::greater<Price>>;
    using AskLevels = std::map<Price, PriceLevel, std::less<Price>>;

    struct OrderHandle {
        Side side{Side::Bid};
        Price price{0};
        std::list<OrderEntry>::iterator iterator{};
    };

    struct AggregatedDepth {
        Quantity bid_volume{0};
        Quantity ask_volume{0};
    };

    BidLevels bid_levels_{};
    AskLevels ask_levels_{};
    std::unordered_map<OrderId, OrderHandle> orders_{};
    std::map<Price, AggregatedDepth> aggregated_depth_{};
    Quantity total_bid_volume_{0};
    Quantity total_ask_volume_{0};
    std::uint64_t next_priority_{1};

    [[nodiscard]] PriceLevel* mutable_level(Side side, Price price);
    [[nodiscard]] const PriceLevel* level(Side side, Price price) const;
    [[nodiscard]] BookLevelState to_level_state(const PriceLevel& level) const;
    void validate_book_event(const Event& event, EventType expected_type) const;
    void ensure_not_crossed(Side side, Price price) const;
    void validate_top_of_book() const;
    void append_delta(BookUpdate& update, Side side, Price price, Quantity delta, Venue venue);
    void apply_aggregate_delta(Side side, Price price, Quantity delta);
    void erase_level_if_empty(Side side, Price price);
    void insert_order(OrderId order_id, Price price, Quantity size, Side side, Venue venue, bool count_as_flow);
};

} // namespace microstructure
