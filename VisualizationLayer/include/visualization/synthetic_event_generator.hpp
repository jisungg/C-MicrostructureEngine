#pragma once

// SyntheticEventGenerator — produces deterministic, valid microstructure event
// streams for testing and demonstration.
//
// All events produced are valid inputs for MicrostructurePipeline::process():
//   - monotonically non-decreasing timestamps
//   - unique event IDs
//   - no crossed-book Add events
//   - Cancel and Modify reference only orders confirmed live in the shadow book
//   - Trade events use order_id=0 (anonymous fill, skips priority check) and
//     size <= volume at the resting price level

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <random>
#include <vector>

#include "microstructure/event.hpp"
#include "microstructure/types.hpp"

namespace visualization {

// Configuration for synthetic event generation.
// Probabilities are independent; if their sum < 1.0 the remainder is treated
// as additional Add probability.  If a chosen event type cannot be generated
// (e.g. no live orders for Cancel), the generator falls back to Add.
struct SyntheticConfig {
    std::size_t  total_events{1'000};
    std::size_t  depth_levels{5};        // target price levels per side

    double add_probability{0.50};
    double cancel_probability{0.15};
    double trade_probability{0.20};
    double modify_probability{0.10};
    // remainder (~0.05) treated as additional Add

    microstructure::Price    initial_mid_price{1'000};
    microstructure::Price    tick_size{1};
    microstructure::Quantity order_size_min{100};
    microstructure::Quantity order_size_max{1'000};

    // Per-event probability that the synthetic mid drifts ±1 tick.
    double price_drift_probability{0.05};

    std::uint32_t            seed{42};
    microstructure::Venue    venue{microstructure::Venue::Nasdaq};
};

// Generates a reproducible sequence of microstructure::Event objects.
// The same SyntheticConfig (including seed) always produces an identical
// sequence across runs.
class SyntheticEventGenerator {
public:
    explicit SyntheticEventGenerator(SyntheticConfig config = {});

    // Generate all events specified by config.total_events.
    // Resets internal state each call — calling generate() twice returns the
    // same result.
    [[nodiscard]] std::vector<microstructure::Event> generate();

private:
    struct TrackedOrder {
        microstructure::OrderId   order_id;
        microstructure::Price     price;
        microstructure::Quantity  size;
        microstructure::Side      side;
    };

    SyntheticConfig config_;
    std::mt19937    rng_;

    // Shadow book: mirrors engine state so we can generate valid references
    std::map<microstructure::Price, microstructure::Quantity,
             std::greater<microstructure::Price>> bid_levels_;
    std::map<microstructure::Price, microstructure::Quantity> ask_levels_;
    // Orders we are confident are still live (not at a traded price level)
    std::vector<TrackedOrder> safe_orders_;

    microstructure::OrderId   next_order_id_{1};
    microstructure::EventId   next_event_id_{1};
    microstructure::Timestamp next_ts_{1'000'000'000LL};
    microstructure::Price     mid_price_{0};

    void reset_state();

    // Build and return a single engine event, advancing counters.
    microstructure::Event emit(microstructure::EventType   et,
                               microstructure::OrderId     oid,
                               microstructure::Price       px,
                               microstructure::Quantity    sz,
                               microstructure::Side        sd);

    microstructure::Event                    generate_add();
    std::optional<microstructure::Event>     try_generate_cancel();
    std::optional<microstructure::Event>     try_generate_trade();
    std::optional<microstructure::Event>     try_generate_modify();

    void maybe_drift_mid();

    [[nodiscard]] std::optional<microstructure::Price> best_bid() const noexcept;
    [[nodiscard]] std::optional<microstructure::Price> best_ask() const noexcept;
};

} // namespace visualization
