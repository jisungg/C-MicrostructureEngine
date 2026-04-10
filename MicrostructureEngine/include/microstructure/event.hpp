#pragma once

#include <memory>
#include <unordered_set>
#include <vector>

#include "microstructure/types.hpp"

namespace microstructure {

struct SnapshotPayload {
    std::vector<SnapshotOrder> orders{};
};

class Event final {
public:
    Event(EventId event_id,
          EventType event_type,
          OrderId order_id,
          Price price,
          Quantity size,
          Side side,
          Timestamp exchange_timestamp,
          Timestamp gateway_timestamp,
          Timestamp processing_timestamp,
          Venue venue = Venue::Nasdaq,
          std::shared_ptr<const SnapshotPayload> snapshot = nullptr);

    [[nodiscard]] EventId event_id() const noexcept;
    [[nodiscard]] EventType event_type() const noexcept;
    [[nodiscard]] OrderId order_id() const noexcept;
    [[nodiscard]] Price price() const noexcept;
    [[nodiscard]] Quantity size() const noexcept;
    [[nodiscard]] Side side() const noexcept;
    [[nodiscard]] Timestamp exchange_timestamp() const noexcept;
    [[nodiscard]] Timestamp gateway_timestamp() const noexcept;
    [[nodiscard]] Timestamp processing_timestamp() const noexcept;
    [[nodiscard]] Venue venue() const noexcept;
    [[nodiscard]] bool has_snapshot() const noexcept;
    [[nodiscard]] const std::shared_ptr<const SnapshotPayload>& snapshot() const noexcept;
    [[nodiscard]] LatencyMetrics latency() const noexcept;

private:
    EventId event_id_;
    EventType event_type_;
    OrderId order_id_;
    Price price_;
    Quantity size_;
    Side side_;
    Timestamp exchange_timestamp_;
    Timestamp gateway_timestamp_;
    Timestamp processing_timestamp_;
    Venue venue_;
    std::shared_ptr<const SnapshotPayload> snapshot_;
};

class EventValidator {
public:
    void validate(const Event& event);
    void reset() noexcept;

private:
    bool has_last_exchange_{false};
    bool has_last_gateway_{false};
    bool has_last_processing_{false};
    Timestamp last_exchange_timestamp_{0};
    Timestamp last_gateway_timestamp_{0};
    Timestamp last_processing_timestamp_{0};
    std::unordered_set<EventId> seen_event_ids_{};
};

class EventNormalizer {
public:
    [[nodiscard]] Event normalize(const Event& event) const;
};

} // namespace microstructure
