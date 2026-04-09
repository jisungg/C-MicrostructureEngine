#include "microstructure/order_book.hpp"

#include <limits>
#include <utility>

#include "microstructure/exceptions.hpp"

namespace microstructure {

namespace {

double safe_rate(const double numerator, const double denominator) {
    if (denominator <= 0.0) {
        return 0.0;
    }
    return numerator / denominator;
}

} // namespace

OrderBookStateEngine::PriceLevel* OrderBookStateEngine::mutable_level(const Side side, const Price price) {
    if (side == Side::Bid) {
        auto it = bid_levels_.find(price);
        return it == bid_levels_.end() ? nullptr : &it->second;
    }
    auto it = ask_levels_.find(price);
    return it == ask_levels_.end() ? nullptr : &it->second;
}

const OrderBookStateEngine::PriceLevel* OrderBookStateEngine::level(const Side side, const Price price) const {
    if (side == Side::Bid) {
        auto it = bid_levels_.find(price);
        return it == bid_levels_.end() ? nullptr : &it->second;
    }
    auto it = ask_levels_.find(price);
    return it == ask_levels_.end() ? nullptr : &it->second;
}

BookLevelState OrderBookStateEngine::to_level_state(const PriceLevel& level) const {
    const Quantity activity = level.add_volume + level.cancel_volume + level.fill_volume;
    return BookLevelState{
        level.side,
        level.price,
        level.total_volume,
        level.queue.size(),
        safe_rate(level.net_order_flow, static_cast<double>(activity)),
        safe_rate(static_cast<double>(level.cancel_volume), static_cast<double>(activity)),
        safe_rate(static_cast<double>(level.fill_volume), static_cast<double>(activity))};
}

void OrderBookStateEngine::validate_book_event(const Event& event, const EventType expected_type) const {
    if (event.event_type() != expected_type) {
        throw ValidationError("unexpected event type for book operation");
    }
    if (event.price() <= 0) {
        throw ValidationError("price must be positive");
    }
    if (event.size() <= 0) {
        throw ValidationError("size must be positive");
    }
    switch (event.side()) {
    case Side::Bid:
    case Side::Ask:
        break;
    default:
        throw ValidationError("invalid side");
    }
}

void OrderBookStateEngine::ensure_not_crossed(const Side side, const Price price) const {
    if (side == Side::Bid) {
        if (!ask_levels_.empty() && price >= ask_levels_.begin()->first) {
            throw CrossedBookError("bid price would cross the best ask");
        }
        return;
    }
    if (!bid_levels_.empty() && price <= bid_levels_.begin()->first) {
        throw CrossedBookError("ask price would cross the best bid");
    }
}

void OrderBookStateEngine::validate_top_of_book() const {
    if (!bid_levels_.empty() && !ask_levels_.empty() && bid_levels_.begin()->first >= ask_levels_.begin()->first) {
        throw BookInvariantError("best bid must be strictly less than best ask");
    }
}

void OrderBookStateEngine::append_delta(BookUpdate& update,
                                        const Side side,
                                        const Price price,
                                        const Quantity delta,
                                        const Venue venue) {
    if (delta == 0) {
        return;
    }
    for (PriceLevelDelta& existing : update.deltas) {
        if (existing.side == side && existing.price == price && existing.venue == venue) {
            existing.delta_volume += delta;
            return;
        }
    }
    update.deltas.push_back(PriceLevelDelta{side, price, delta, venue});
}

void OrderBookStateEngine::apply_aggregate_delta(const Side side, const Price price, const Quantity delta) {
    AggregatedDepth& depth = aggregated_depth_[price];
    if (side == Side::Bid) {
        depth.bid_volume += delta;
        total_bid_volume_ += delta;
        if (depth.bid_volume < 0 || total_bid_volume_ < 0) {
            throw BookInvariantError("negative bid volume is not allowed");
        }
    } else {
        depth.ask_volume += delta;
        total_ask_volume_ += delta;
        if (depth.ask_volume < 0 || total_ask_volume_ < 0) {
            throw BookInvariantError("negative ask volume is not allowed");
        }
    }
    if (depth.bid_volume == 0 && depth.ask_volume == 0) {
        aggregated_depth_.erase(price);
    }
}

void OrderBookStateEngine::erase_level_if_empty(const Side side, const Price price) {
    if (side == Side::Bid) {
        auto it = bid_levels_.find(price);
        if (it != bid_levels_.end() && it->second.total_volume == 0) {
            bid_levels_.erase(it);
        }
        return;
    }
    auto it = ask_levels_.find(price);
    if (it != ask_levels_.end() && it->second.total_volume == 0) {
        ask_levels_.erase(it);
    }
}

void OrderBookStateEngine::insert_order(const OrderId order_id,
                                        const Price price,
                                        const Quantity size,
                                        const Side side,
                                        const Venue venue,
                                        const bool count_as_flow) {
    if (orders_.contains(order_id)) {
        throw DuplicateOrderError("order id already exists");
    }

    ensure_not_crossed(side, price);

    if (side == Side::Bid) {
        auto [level_it, inserted] = bid_levels_.try_emplace(price, PriceLevel{price, side});
        (void)inserted;
        PriceLevel& level_ref = level_it->second;
        level_ref.queue.push_back(OrderEntry{order_id, price, size, side, venue, next_priority_++});
        auto queue_it = std::prev(level_ref.queue.end());
        level_ref.total_volume += size;
        if (count_as_flow) {
            level_ref.add_volume += size;
            level_ref.net_order_flow += static_cast<double>(size);
        }
        orders_[order_id] = OrderHandle{side, price, queue_it};
    } else {
        auto [level_it, inserted] = ask_levels_.try_emplace(price, PriceLevel{price, side});
        (void)inserted;
        PriceLevel& level_ref = level_it->second;
        level_ref.queue.push_back(OrderEntry{order_id, price, size, side, venue, next_priority_++});
        auto queue_it = std::prev(level_ref.queue.end());
        level_ref.total_volume += size;
        if (count_as_flow) {
            level_ref.add_volume += size;
            level_ref.net_order_flow -= static_cast<double>(size);
        }
        orders_[order_id] = OrderHandle{side, price, queue_it};
    }

    apply_aggregate_delta(side, price, size);
    validate_top_of_book();
}

BookUpdate OrderBookStateEngine::process(const Event& event) {
    switch (event.event_type()) {
    case EventType::Add:
        return add_order(event);
    case EventType::Cancel:
        return cancel_order(event);
    case EventType::Modify:
        return modify_order(event);
    case EventType::Trade:
        return execute_trade(event);
    case EventType::Snapshot:
        return restore_snapshot(event);
    }
    throw ValidationError("unsupported event type");
}

BookUpdate OrderBookStateEngine::add_order(const Event& event) {
    validate_book_event(event, EventType::Add);
    BookUpdate update;
    insert_order(event.order_id(), event.price(), event.size(), event.side(), event.venue(), true);
    append_delta(update, event.side(), event.price(), event.size(), event.venue());
    update.added_volume = event.size();
    validate_top_of_book();
    return update;
}

BookUpdate OrderBookStateEngine::cancel_order(const Event& event) {
    validate_book_event(event, EventType::Cancel);
    const auto order_it = orders_.find(event.order_id());
    if (order_it == orders_.end()) {
        throw OrderNotFoundError("cannot cancel missing order");
    }

    OrderHandle& handle = order_it->second;
    if (handle.side != event.side()) {
        throw ValidationError("cancel side does not match resting order");
    }
    if (handle.price != event.price()) {
        throw ValidationError("cancel price does not match resting order");
    }

    PriceLevel* level_ptr = mutable_level(handle.side, handle.price);
    if (level_ptr == nullptr) {
        throw BookInvariantError("missing level for resting order");
    }

    OrderEntry& order = *handle.iterator;
    if (event.size() > order.size) {
        throw ValidationError("cancel size exceeds resting order size");
    }

    const Quantity cancel_size = event.size();
    order.size -= cancel_size;
    level_ptr->total_volume -= cancel_size;
    level_ptr->cancel_volume += cancel_size;
    if (handle.side == Side::Bid) {
        level_ptr->net_order_flow -= static_cast<double>(cancel_size);
    } else {
        level_ptr->net_order_flow += static_cast<double>(cancel_size);
    }
    apply_aggregate_delta(handle.side, handle.price, -cancel_size);

    BookUpdate update;
    append_delta(update, handle.side, handle.price, -cancel_size, event.venue());
    update.canceled_volume = cancel_size;

    const Side saved_side = handle.side;
    const Price saved_price = handle.price;

    if (order.size == 0) {
        level_ptr->queue.erase(handle.iterator);
        orders_.erase(order_it);
    }

    erase_level_if_empty(saved_side, saved_price);
    validate_top_of_book();
    return update;
}

BookUpdate OrderBookStateEngine::modify_order(const Event& event) {
    validate_book_event(event, EventType::Modify);
    const auto order_it = orders_.find(event.order_id());
    if (order_it == orders_.end()) {
        throw OrderNotFoundError("cannot modify missing order");
    }

    OrderHandle handle = order_it->second;
    if (handle.side != event.side()) {
        throw ValidationError("modify side does not match resting order");
    }

    PriceLevel* current_level = mutable_level(handle.side, handle.price);
    if (current_level == nullptr) {
        throw BookInvariantError("missing level for modify");
    }

    OrderEntry order_copy = *handle.iterator;
    const Quantity old_size = order_copy.size;
    const Price old_price = order_copy.price;

    BookUpdate update;

    if (event.price() == old_price) {
        const Quantity delta = event.size() - old_size;
        if (delta < 0) {
            const Quantity reduction = -delta;
            handle.iterator->size = event.size();
            current_level->total_volume -= reduction;
            current_level->cancel_volume += reduction;
            if (handle.side == Side::Bid) {
                current_level->net_order_flow -= static_cast<double>(reduction);
            } else {
                current_level->net_order_flow += static_cast<double>(reduction);
            }
            apply_aggregate_delta(handle.side, old_price, -reduction);
            append_delta(update, handle.side, old_price, -reduction, event.venue());
            update.canceled_volume = reduction;
        } else if (delta > 0) {
            handle.iterator->size = event.size();
            current_level->total_volume += delta;
            current_level->add_volume += delta;
            if (handle.side == Side::Bid) {
                current_level->net_order_flow += static_cast<double>(delta);
            } else {
                current_level->net_order_flow -= static_cast<double>(delta);
            }
            current_level->queue.splice(current_level->queue.end(), current_level->queue, handle.iterator);
            apply_aggregate_delta(handle.side, old_price, delta);
            append_delta(update, handle.side, old_price, delta, event.venue());
            order_it->second.iterator = std::prev(current_level->queue.end());
            update.added_volume = delta;
        }
        validate_top_of_book();
        return update;
    }

    if (event.side() == Side::Bid) {
        if (!ask_levels_.empty() && event.price() >= ask_levels_.begin()->first) {
            throw CrossedBookError("modified bid price would cross the book");
        }
    } else if (!bid_levels_.empty() && event.price() <= bid_levels_.begin()->first) {
        throw CrossedBookError("modified ask price would cross the book");
    }

    current_level->total_volume -= old_size;
    current_level->cancel_volume += old_size;
    if (handle.side == Side::Bid) {
        current_level->net_order_flow -= static_cast<double>(old_size);
    } else {
        current_level->net_order_flow += static_cast<double>(old_size);
    }
    apply_aggregate_delta(handle.side, old_price, -old_size);
    append_delta(update, handle.side, old_price, -old_size, event.venue());
    update.repriced_removed_volume = old_size;
    current_level->queue.erase(handle.iterator);
    orders_.erase(order_it);
    erase_level_if_empty(handle.side, old_price);

    insert_order(event.order_id(), event.price(), event.size(), event.side(), event.venue(), true);
    append_delta(update, event.side(), event.price(), event.size(), event.venue());
    update.repriced_added_volume = event.size();

    validate_top_of_book();
    return update;
}

BookUpdate OrderBookStateEngine::execute_trade(const Event& event) {
    validate_book_event(event, EventType::Trade);
    BookUpdate update;
    const Side resting_side = event.side();
    PriceLevel* level_ptr = mutable_level(resting_side, event.price());
    if (level_ptr == nullptr) {
        throw OrderNotFoundError("trade price level does not exist");
    }
    if (event.size() > level_ptr->total_volume) {
        throw ValidationError("trade size exceeds visible depth at the price level");
    }

    const Quantity visible_before = level_ptr->total_volume;
    Quantity remaining = event.size();

    if (event.order_id() != 0) {
        const auto order_it = orders_.find(event.order_id());
        if (order_it == orders_.end()) {
            throw OrderNotFoundError("trade references missing order");
        }
        if (order_it->second.side != resting_side || order_it->second.price != event.price()) {
            throw ValidationError("trade order does not match price level");
        }
        if (level_ptr->queue.empty() || level_ptr->queue.front().order_id != event.order_id()) {
            throw BookInvariantError("trade violates price-time priority");
        }
    }

    while (remaining > 0) {
        if (level_ptr->queue.empty()) {
            throw BookInvariantError("trade exhausted queue before size was fully executed");
        }

        OrderEntry& front = level_ptr->queue.front();
        const Quantity traded = std::min(front.size, remaining);
        front.size -= traded;
        remaining -= traded;
        level_ptr->total_volume -= traded;
        level_ptr->fill_volume += traded;
        if (resting_side == Side::Bid) {
            level_ptr->net_order_flow -= static_cast<double>(traded);
        } else {
            level_ptr->net_order_flow += static_cast<double>(traded);
        }
        apply_aggregate_delta(resting_side, event.price(), -traded);
        append_delta(update, resting_side, event.price(), -traded, event.venue());

        if (front.size == 0) {
            orders_.erase(front.order_id);
            level_ptr->queue.pop_front();
        }
    }

    const Quantity remaining_after = level_ptr->total_volume;
    erase_level_if_empty(resting_side, event.price());
    validate_top_of_book();

    update.trade = TradeExecution{
        event.price(),
        event.size(),
        resting_side,
        resting_side == Side::Ask ? TradeAggressor::BuyAggressor : TradeAggressor::SellAggressor,
        visible_before,
        remaining_after};
    update.filled_volume = event.size();

    return update;
}

BookUpdate OrderBookStateEngine::restore_snapshot(const Event& event) {
    validate_book_event(event, EventType::Snapshot);
    if (!event.has_snapshot()) {
        throw ValidationError("snapshot payload missing");
    }

    OrderBookStateEngine restored;
    for (const SnapshotOrder& order : event.snapshot()->orders) {
        restored.insert_order(order.order_id, order.price, order.size, order.side, order.venue, false);
    }

    for (auto& [price, level] : restored.bid_levels_) {
        (void)price;
        level.add_volume = 0;
        level.cancel_volume = 0;
        level.fill_volume = 0;
        level.net_order_flow = 0.0;
    }
    for (auto& [price, level] : restored.ask_levels_) {
        (void)price;
        level.add_volume = 0;
        level.cancel_volume = 0;
        level.fill_volume = 0;
        level.net_order_flow = 0.0;
    }

    swap(restored);
    validate_full_invariants();
    return BookUpdate{{}, std::nullopt, 0, 0, 0, 0, 0, true};
}

std::optional<Price> OrderBookStateEngine::best_bid() const {
    return bid_levels_.empty() ? std::nullopt : std::optional<Price>(bid_levels_.begin()->first);
}

std::optional<Price> OrderBookStateEngine::best_ask() const {
    return ask_levels_.empty() ? std::nullopt : std::optional<Price>(ask_levels_.begin()->first);
}

Quantity OrderBookStateEngine::best_bid_volume() const {
    return bid_levels_.empty() ? 0 : bid_levels_.begin()->second.total_volume;
}

Quantity OrderBookStateEngine::best_ask_volume() const {
    return ask_levels_.empty() ? 0 : ask_levels_.begin()->second.total_volume;
}

Quantity OrderBookStateEngine::total_volume(const Side side) const {
    return side == Side::Bid ? total_bid_volume_ : total_ask_volume_;
}

Quantity OrderBookStateEngine::volume_at(const Side side, const Price price) const {
    const PriceLevel* level_ptr = level(side, price);
    return level_ptr == nullptr ? 0 : level_ptr->total_volume;
}

std::size_t OrderBookStateEngine::queue_depth_at(const Side side, const Price price) const {
    const PriceLevel* level_ptr = level(side, price);
    return level_ptr == nullptr ? 0U : level_ptr->queue.size();
}

std::size_t OrderBookStateEngine::total_order_count() const {
    return orders_.size();
}

std::vector<BookLevelState> OrderBookStateEngine::top_levels(const Side side, const std::size_t levels) const {
    std::vector<BookLevelState> result;
    if (side == Side::Bid) {
        result.reserve(std::min(levels, bid_levels_.size()));
        for (auto it = bid_levels_.begin(); it != bid_levels_.end() && result.size() < levels; ++it) {
            result.push_back(to_level_state(it->second));
        }
        return result;
    }
    result.reserve(std::min(levels, ask_levels_.size()));
    for (auto it = ask_levels_.begin(); it != ask_levels_.end() && result.size() < levels; ++it) {
        result.push_back(to_level_state(it->second));
    }
    return result;
}

std::vector<BookLevelState> OrderBookStateEngine::all_levels(const Side side) const {
    std::vector<BookLevelState> result;
    if (side == Side::Bid) {
        result.reserve(bid_levels_.size());
        for (const auto& [price, level_ref] : bid_levels_) {
            (void)price;
            result.push_back(to_level_state(level_ref));
        }
        return result;
    }
    result.reserve(ask_levels_.size());
    for (const auto& [price, level_ref] : ask_levels_) {
        (void)price;
        result.push_back(to_level_state(level_ref));
    }
    return result;
}

MarketImpactEstimate OrderBookStateEngine::estimate_market_impact(const TradeAggressor aggressor,
                                                                  const Quantity quantity) const {
    MarketImpactEstimate estimate;
    estimate.requested_quantity = quantity;
    if (quantity <= 0) {
        return estimate;
    }
    if (aggressor == TradeAggressor::Unknown) {
        throw ValidationError("market impact requires a known aggressor side");
    }

    Quantity remaining = quantity;
    if (aggressor == TradeAggressor::BuyAggressor) {
        for (const auto& [price, level] : ask_levels_) {
            const Quantity take = std::min(level.total_volume, remaining);
            estimate.filled_quantity += take;
            estimate.total_notional += static_cast<double>(take) * static_cast<double>(price);
            estimate.terminal_price = price;
            remaining -= take;
            if (remaining == 0) {
                break;
            }
        }
    } else if (aggressor == TradeAggressor::SellAggressor) {
        for (const auto& [price, level] : bid_levels_) {
            const Quantity take = std::min(level.total_volume, remaining);
            estimate.filled_quantity += take;
            estimate.total_notional += static_cast<double>(take) * static_cast<double>(price);
            estimate.terminal_price = price;
            remaining -= take;
            if (remaining == 0) {
                break;
            }
        }
    }

    estimate.fully_filled = estimate.filled_quantity == quantity;
    if (estimate.filled_quantity > 0) {
        estimate.average_price = estimate.total_notional / static_cast<double>(estimate.filled_quantity);
    }
    return estimate;
}

BookSummary OrderBookStateEngine::summary() const {
    return BookSummary{
        best_bid(),
        best_ask(),
        best_bid_volume(),
        best_ask_volume(),
        total_bid_volume_,
        total_ask_volume_,
        bid_levels_.size(),
        ask_levels_.size(),
        orders_.size()};
}

void OrderBookStateEngine::validate_full_invariants() const {
    validate_top_of_book();

    Quantity recalculated_bid_volume = 0;
    Quantity recalculated_ask_volume = 0;
    std::size_t recalculated_orders = 0;

    for (const auto& [price, level_ref] : bid_levels_) {
        if (price <= 0 || level_ref.total_volume < 0) {
            throw BookInvariantError("invalid bid level state");
        }
        if (!aggregated_depth_.contains(price)) {
            throw BookInvariantError("missing aggregated depth entry for bid level");
        }
        if (level_ref.side != Side::Bid) {
            throw BookInvariantError("bid side mismatch");
        }
        Quantity level_volume = 0;
        for (const OrderEntry& order : level_ref.queue) {
            if (order.side != Side::Bid || order.price != price || order.size <= 0) {
                throw BookInvariantError("invalid bid queue entry");
            }
            if (!orders_.contains(order.order_id)) {
                throw BookInvariantError("orphaned bid order");
            }
            level_volume += order.size;
            ++recalculated_orders;
        }
        if (level_volume != level_ref.total_volume) {
            throw BookInvariantError("bid level aggregation mismatch");
        }
        recalculated_bid_volume += level_volume;
    }

    for (const auto& [price, level_ref] : ask_levels_) {
        if (price <= 0 || level_ref.total_volume < 0) {
            throw BookInvariantError("invalid ask level state");
        }
        if (!aggregated_depth_.contains(price)) {
            throw BookInvariantError("missing aggregated depth entry for ask level");
        }
        if (level_ref.side != Side::Ask) {
            throw BookInvariantError("ask side mismatch");
        }
        Quantity level_volume = 0;
        for (const OrderEntry& order : level_ref.queue) {
            if (order.side != Side::Ask || order.price != price || order.size <= 0) {
                throw BookInvariantError("invalid ask queue entry");
            }
            if (!orders_.contains(order.order_id)) {
                throw BookInvariantError("orphaned ask order");
            }
            level_volume += order.size;
            ++recalculated_orders;
        }
        if (level_volume != level_ref.total_volume) {
            throw BookInvariantError("ask level aggregation mismatch");
        }
        recalculated_ask_volume += level_volume;
    }

    if (recalculated_bid_volume != total_bid_volume_ || recalculated_ask_volume != total_ask_volume_) {
        throw BookInvariantError("book totals do not match aggregated depth");
    }
    if (recalculated_orders != orders_.size()) {
        throw BookInvariantError("order index size mismatch");
    }
    for (const auto& [order_id, handle] : orders_) {
        (void)order_id;
        const PriceLevel* level_ptr = level(handle.side, handle.price);
        if (level_ptr == nullptr || handle.iterator == level_ptr->queue.end()) {
            throw BookInvariantError("dangling order handle");
        }
    }
    for (const auto& [price, depth] : aggregated_depth_) {
        const Quantity bid_volume = volume_at(Side::Bid, price);
        const Quantity ask_volume = volume_at(Side::Ask, price);
        if (depth.bid_volume != bid_volume || depth.ask_volume != ask_volume) {
            throw BookInvariantError("aggregated depth map mismatch");
        }
    }
}

void OrderBookStateEngine::clear() {
    bid_levels_.clear();
    ask_levels_.clear();
    orders_.clear();
    aggregated_depth_.clear();
    total_bid_volume_ = 0;
    total_ask_volume_ = 0;
    next_priority_ = 1;
}

void OrderBookStateEngine::swap(OrderBookStateEngine& other) noexcept {
    using std::swap;
    swap(bid_levels_, other.bid_levels_);
    swap(ask_levels_, other.ask_levels_);
    swap(orders_, other.orders_);
    swap(aggregated_depth_, other.aggregated_depth_);
    swap(total_bid_volume_, other.total_bid_volume_);
    swap(total_ask_volume_, other.total_ask_volume_);
    swap(next_priority_, other.next_priority_);
}

} // namespace microstructure
