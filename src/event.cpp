#include "microstructure/event.hpp"

#include <algorithm>
#include <unordered_set>

#include "microstructure/exceptions.hpp"

namespace microstructure {

namespace {

bool is_valid_event_type(const EventType event_type) {
    switch (event_type) {
    case EventType::Add:
    case EventType::Cancel:
    case EventType::Modify:
    case EventType::Trade:
    case EventType::Snapshot:
        return true;
    }
    return false;
}

bool is_valid_side(const Side side) {
    switch (side) {
    case Side::Bid:
    case Side::Ask:
        return true;
    }
    return false;
}

bool is_valid_venue(const Venue venue) {
    switch (venue) {
    case Venue::Nasdaq:
    case Venue::Arca:
    case Venue::Bats:
    case Venue::Iex:
        return true;
    }
    return false;
}

bool snapshot_order_less(const SnapshotOrder& lhs, const SnapshotOrder& rhs) {
    if (lhs.side != rhs.side) {
        return lhs.side == Side::Bid;
    }
    if (lhs.side == Side::Bid && lhs.price != rhs.price) {
        return lhs.price > rhs.price;
    }
    if (lhs.side == Side::Ask && lhs.price != rhs.price) {
        return lhs.price < rhs.price;
    }
    // Same side, same price: use queue_priority when provided (lower = earlier),
    // otherwise fall back to order_id for deterministic ordering.
    if (lhs.queue_priority != 0 || rhs.queue_priority != 0) {
        return lhs.queue_priority < rhs.queue_priority;
    }
    return lhs.order_id < rhs.order_id;
}

} // namespace

Event::Event(const EventId event_id,
             const EventType event_type,
             const OrderId order_id,
             const Price price,
             const Quantity size,
             const Side side,
             const Timestamp exchange_timestamp,
             const Timestamp gateway_timestamp,
             const Timestamp processing_timestamp,
             const Venue venue,
             std::shared_ptr<const SnapshotPayload> snapshot)
    : event_id_(event_id),
      event_type_(event_type),
      order_id_(order_id),
      price_(price),
      size_(size),
      side_(side),
      exchange_timestamp_(exchange_timestamp),
      gateway_timestamp_(gateway_timestamp),
      processing_timestamp_(processing_timestamp),
      venue_(venue),
      snapshot_(std::move(snapshot)) {}

EventId Event::event_id() const noexcept { return event_id_; }
EventType Event::event_type() const noexcept { return event_type_; }
OrderId Event::order_id() const noexcept { return order_id_; }
Price Event::price() const noexcept { return price_; }
Quantity Event::size() const noexcept { return size_; }
Side Event::side() const noexcept { return side_; }
Timestamp Event::exchange_timestamp() const noexcept { return exchange_timestamp_; }
Timestamp Event::gateway_timestamp() const noexcept { return gateway_timestamp_; }
Timestamp Event::processing_timestamp() const noexcept { return processing_timestamp_; }
Venue Event::venue() const noexcept { return venue_; }
bool Event::has_snapshot() const noexcept { return static_cast<bool>(snapshot_); }
const std::shared_ptr<const SnapshotPayload>& Event::snapshot() const noexcept { return snapshot_; }

LatencyMetrics Event::latency() const noexcept {
    return LatencyMetrics{
        gateway_timestamp_ - exchange_timestamp_,
        processing_timestamp_ - gateway_timestamp_,
        processing_timestamp_ - exchange_timestamp_};
}

void EventValidator::validate(const Event& event) {
    if (event.event_id() == 0) {
        throw ValidationError("event id must be positive");
    }
    if (!is_valid_event_type(event.event_type())) {
        throw ValidationError("invalid event type");
    }
    if (!is_valid_side(event.side())) {
        throw ValidationError("invalid side");
    }
    if (!is_valid_venue(event.venue())) {
        throw ValidationError("invalid venue");
    }
    if (event.price() <= 0) {
        throw ValidationError("price must be positive");
    }
    if (event.size() <= 0) {
        throw ValidationError("size must be positive");
    }
    if (event.gateway_timestamp() < event.exchange_timestamp()) {
        throw ValidationError("gateway timestamp cannot precede exchange timestamp");
    }
    if (event.processing_timestamp() < event.gateway_timestamp()) {
        throw ValidationError("processing timestamp cannot precede gateway timestamp");
    }
    if (has_last_exchange_ && event.exchange_timestamp() < last_exchange_timestamp_) {
        throw ValidationError("exchange timestamps must be monotonically non-decreasing");
    }
    if (has_last_gateway_ && event.gateway_timestamp() < last_gateway_timestamp_) {
        throw ValidationError("gateway timestamps must be monotonically non-decreasing");
    }
    if (has_last_processing_ && event.processing_timestamp() < last_processing_timestamp_) {
        throw ValidationError("processing timestamps must be monotonically non-decreasing");
    }
    if (event.event_type() == EventType::Snapshot) {
        if (!event.has_snapshot()) {
            throw ValidationError("snapshot event requires snapshot payload");
        }
        std::unordered_set<OrderId> seen_snapshot_orders;
        for (const SnapshotOrder& order : event.snapshot()->orders) {
            if (order.order_id == 0) {
                throw ValidationError("snapshot order id must be positive");
            }
            if (!is_valid_side(order.side)) {
                throw ValidationError("snapshot order side must be valid");
            }
            if (!is_valid_venue(order.venue)) {
                throw ValidationError("snapshot order venue must be valid");
            }
            if (order.price <= 0 || order.size <= 0) {
                throw ValidationError("snapshot orders must have positive price and size");
            }
            if (!seen_snapshot_orders.insert(order.order_id).second) {
                throw ValidationError("snapshot contains duplicate order ids");
            }
        }
    } else if (event.event_type() != EventType::Trade && event.order_id() == 0) {
        throw ValidationError("order id must be positive for non-trade events");
    }
    if (seen_event_ids_.contains(event.event_id())) {
        throw ValidationError("duplicate event id detected");
    }

    has_last_exchange_ = true;
    has_last_gateway_ = true;
    has_last_processing_ = true;
    last_exchange_timestamp_ = event.exchange_timestamp();
    last_gateway_timestamp_ = event.gateway_timestamp();
    last_processing_timestamp_ = event.processing_timestamp();
    seen_event_ids_.insert(event.event_id());
}

void EventValidator::reset() noexcept {
    has_last_exchange_ = false;
    has_last_gateway_ = false;
    has_last_processing_ = false;
    last_exchange_timestamp_ = 0;
    last_gateway_timestamp_ = 0;
    last_processing_timestamp_ = 0;
    seen_event_ids_.clear();
}

Event EventNormalizer::normalize(const Event& event) const {
    if (event.event_type() != EventType::Snapshot || !event.has_snapshot()) {
        return event;
    }

    auto normalized_payload = std::make_shared<SnapshotPayload>();
    normalized_payload->orders = event.snapshot()->orders;
    std::stable_sort(normalized_payload->orders.begin(),
                     normalized_payload->orders.end(),
                     snapshot_order_less);

    return Event(event.event_id(),
                 event.event_type(),
                 event.order_id(),
                 event.price(),
                 event.size(),
                 event.side(),
                 event.exchange_timestamp(),
                 event.gateway_timestamp(),
                 event.processing_timestamp(),
                 event.venue(),
                 normalized_payload);
}

} // namespace microstructure
