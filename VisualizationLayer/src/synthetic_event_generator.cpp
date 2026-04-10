#include "visualization/synthetic_event_generator.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <stdexcept>

namespace visualization {

// Harden config: clamp/fix values that would cause UB or invalid events.
static SyntheticConfig harden(SyntheticConfig c) {
    if (c.tick_size <= 0)                     c.tick_size         = 1;
    if (c.initial_mid_price < c.tick_size)    c.initial_mid_price = c.tick_size;
    if (c.order_size_min <= 0)                c.order_size_min    = 1;
    if (c.order_size_max < c.order_size_min)  c.order_size_max    = c.order_size_min;
    if (c.depth_levels == 0)                  c.depth_levels      = 1;
    if (c.add_probability < 0.0)              c.add_probability   = 0.0;
    if (c.cancel_probability < 0.0)           c.cancel_probability = 0.0;
    if (c.trade_probability < 0.0)            c.trade_probability  = 0.0;
    if (c.modify_probability < 0.0)           c.modify_probability = 0.0;
    if (c.price_drift_probability < 0.0)      c.price_drift_probability = 0.0;
    if (c.price_drift_probability > 1.0)      c.price_drift_probability = 1.0;
    return c;
}

SyntheticEventGenerator::SyntheticEventGenerator(SyntheticConfig config)
    : config_{harden(std::move(config))}
    , rng_{config_.seed}
    , mid_price_{config_.initial_mid_price}
{
}

// ── Helpers ──────────────────────────────────────────────────────────────────

void SyntheticEventGenerator::reset_state() {
    rng_.seed(config_.seed);
    bid_levels_.clear();
    ask_levels_.clear();
    safe_orders_.clear();
    next_order_id_ = 1;
    next_event_id_ = 1;
    next_ts_        = 1'000'000'000LL;
    mid_price_      = config_.initial_mid_price;
}

std::optional<microstructure::Price>
SyntheticEventGenerator::best_bid() const noexcept {
    if (bid_levels_.empty()) return std::nullopt;
    return bid_levels_.begin()->first; // map is descending — first = highest
}

std::optional<microstructure::Price>
SyntheticEventGenerator::best_ask() const noexcept {
    if (ask_levels_.empty()) return std::nullopt;
    return ask_levels_.begin()->first; // map is ascending — first = lowest
}

microstructure::Event SyntheticEventGenerator::emit(
    microstructure::EventType  et,
    microstructure::OrderId    oid,
    microstructure::Price      px,
    microstructure::Quantity   sz,
    microstructure::Side       sd)
{
    const microstructure::EventId   eid = next_event_id_++;
    const microstructure::Timestamp ts  = next_ts_;
    next_ts_ += 1'000'000LL; // 1 ms between events
    return microstructure::Event{eid, et, oid, px, sz, sd, ts, ts + 1, ts + 2, config_.venue};
}

// ── Event generators ─────────────────────────────────────────────────────────

microstructure::Event SyntheticEventGenerator::generate_add() {
    // Randomly choose a side
    const bool is_bid = ((rng_() & 1U) == 0U);
    const microstructure::Side side =
        is_bid ? microstructure::Side::Bid : microstructure::Side::Ask;

    // Pick a price offset away from mid (1..depth_levels ticks from mid)
    const auto depth = static_cast<std::uint64_t>(
        std::max(std::size_t{1}, config_.depth_levels));
    const auto rand_off = static_cast<microstructure::Price>(
        static_cast<std::uint64_t>(rng_()) % depth);
    const microstructure::Price offset = rand_off + microstructure::Price{1};

    // Determine the price, with crossing prevention.  If the chosen side
    // has no valid price (e.g. ask fills all ticks ≥ tick_size), flip sides.
    microstructure::Price price{};
    auto compute_bid_price = [&]() -> std::optional<microstructure::Price> {
        microstructure::Price p = mid_price_ - offset * config_.tick_size;
        if (p < config_.tick_size) p = config_.tick_size;
        const auto ba = best_ask();
        if (ba.has_value() && p >= *ba) {
            p = *ba - config_.tick_size;
            if (p < config_.tick_size) return std::nullopt; // no room for a bid
        }
        return p;
    };

    if (is_bid) {
        const auto maybe = compute_bid_price();
        if (!maybe.has_value()) {
            // No valid bid price — generate an ask instead
            const microstructure::Side ask_side = microstructure::Side::Ask;
            microstructure::Price ask_price = mid_price_ + offset * config_.tick_size;
            const auto bb = best_bid();
            if (bb.has_value() && ask_price <= *bb) ask_price = *bb + config_.tick_size;

            const auto size_range = static_cast<std::uint64_t>(
                config_.order_size_max - config_.order_size_min);
            const microstructure::Quantity sz = config_.order_size_min +
                static_cast<microstructure::Quantity>(
                    size_range > 0
                        ? static_cast<std::uint64_t>(rng_()) % (size_range + 1U)
                        : 0U);
            const microstructure::OrderId oid2 = next_order_id_++;
            const auto ev2 = emit(microstructure::EventType::Add, oid2, ask_price, sz, ask_side);
            ask_levels_[ask_price] += sz;
            safe_orders_.push_back(TrackedOrder{oid2, ask_price, sz, ask_side});
            return ev2;
        }
        price = *maybe;
    } else {
        price = mid_price_ + offset * config_.tick_size;
        // Clamp: must not cross best bid
        const auto bb = best_bid();
        if (bb.has_value() && price <= *bb) {
            price = *bb + config_.tick_size;
        }
    }

    // Size: uniform in [order_size_min, order_size_max]
    const auto size_range =
        static_cast<std::uint64_t>(config_.order_size_max - config_.order_size_min);
    const microstructure::Quantity size = config_.order_size_min +
        static_cast<microstructure::Quantity>(
            size_range > 0
                ? static_cast<std::uint64_t>(rng_()) % (size_range + 1U)
                : 0U);

    const microstructure::OrderId oid = next_order_id_++;
    const microstructure::Event   ev  = emit(microstructure::EventType::Add, oid, price, size, side);

    // Update shadow book
    if (is_bid) {
        bid_levels_[price] += size;
    } else {
        ask_levels_[price] += size;
    }
    safe_orders_.push_back(TrackedOrder{oid, price, size, side});
    return ev;
}

std::optional<microstructure::Event>
SyntheticEventGenerator::try_generate_cancel() {
    if (safe_orders_.empty()) return std::nullopt;

    const auto idx = static_cast<std::size_t>(
        static_cast<std::uint64_t>(rng_()) % safe_orders_.size());
    const TrackedOrder ord = safe_orders_[idx];

    // Remove from safe_orders_ (swap-and-pop)
    safe_orders_[idx] = safe_orders_.back();
    safe_orders_.pop_back();

    // Update shadow level volume
    if (ord.side == microstructure::Side::Bid) {
        auto it = bid_levels_.find(ord.price);
        if (it != bid_levels_.end()) {
            it->second -= ord.size;
            if (it->second <= 0) bid_levels_.erase(it);
        }
    } else {
        auto it = ask_levels_.find(ord.price);
        if (it != ask_levels_.end()) {
            it->second -= ord.size;
            if (it->second <= 0) ask_levels_.erase(it);
        }
    }

    return emit(microstructure::EventType::Cancel,
                ord.order_id, ord.price, ord.size, ord.side);
}

std::optional<microstructure::Event>
SyntheticEventGenerator::try_generate_trade() {
    const auto bb = best_bid();
    const auto ba = best_ask();
    if (!bb.has_value() && !ba.has_value()) return std::nullopt;

    // Randomly decide which resting side to aggress against
    const bool hit_bid = ((rng_() & 1U) == 0U);

    microstructure::Price    resting_price{};
    microstructure::Side     resting_side{};
    microstructure::Quantity level_volume{};

    if (hit_bid && bb.has_value()) {
        resting_price  = *bb;
        resting_side   = microstructure::Side::Bid;
        level_volume   = bid_levels_.begin()->second;
    } else if (!hit_bid && ba.has_value()) {
        resting_price  = *ba;
        resting_side   = microstructure::Side::Ask;
        level_volume   = ask_levels_.begin()->second;
    } else if (bb.has_value()) {
        resting_price  = *bb;
        resting_side   = microstructure::Side::Bid;
        level_volume   = bid_levels_.begin()->second;
    } else {
        resting_price  = *ba;
        resting_side   = microstructure::Side::Ask;
        level_volume   = ask_levels_.begin()->second;
    }

    if (level_volume <= 0) return std::nullopt;

    // Trade size: 10% to 50% of level volume, at least 1
    const auto lv = static_cast<std::uint64_t>(level_volume);
    const auto lo = std::max(std::uint64_t{1}, lv / 10U);
    const auto hi = std::max(lo, lv / 2U);
    const std::uint64_t range = hi - lo;
    const microstructure::Quantity trade_size = static_cast<microstructure::Quantity>(
        lo + (range > 0U ? static_cast<std::uint64_t>(rng_()) % (range + 1U) : 0U));

    // Update shadow book: reduce level volume, purge safe_orders at that level
    if (resting_side == microstructure::Side::Bid) {
        auto it = bid_levels_.find(resting_price);
        if (it != bid_levels_.end()) {
            it->second -= trade_size;
            if (it->second <= 0) bid_levels_.erase(it);
        }
        // Remove all safe_orders at this bid price — they may have been consumed
        safe_orders_.erase(
            std::remove_if(safe_orders_.begin(), safe_orders_.end(),
                [&](const TrackedOrder& o) noexcept {
                    return o.side  == microstructure::Side::Bid
                        && o.price == resting_price;
                }),
            safe_orders_.end());
    } else {
        auto it = ask_levels_.find(resting_price);
        if (it != ask_levels_.end()) {
            it->second -= trade_size;
            if (it->second <= 0) ask_levels_.erase(it);
        }
        safe_orders_.erase(
            std::remove_if(safe_orders_.begin(), safe_orders_.end(),
                [&](const TrackedOrder& o) noexcept {
                    return o.side  == microstructure::Side::Ask
                        && o.price == resting_price;
                }),
            safe_orders_.end());
    }

    // order_id=0: engine skips price-time priority check
    return emit(microstructure::EventType::Trade,
                microstructure::OrderId{0},
                resting_price, trade_size, resting_side);
}

std::optional<microstructure::Event>
SyntheticEventGenerator::try_generate_modify() {
    if (safe_orders_.empty()) return std::nullopt;

    const auto idx = static_cast<std::size_t>(
        static_cast<std::uint64_t>(rng_()) % safe_orders_.size());
    TrackedOrder& ord = safe_orders_[idx];

    // Reduce size by 10%–50%; keep at least 1
    const auto half = static_cast<std::uint64_t>(ord.size) / 2U;
    const auto tenth = std::max(std::uint64_t{1}, static_cast<std::uint64_t>(ord.size) / 10U);
    const std::uint64_t range = (half > tenth) ? (half - tenth) : 0U;
    const microstructure::Quantity reduction = static_cast<microstructure::Quantity>(
        tenth + (range > 0U ? static_cast<std::uint64_t>(rng_()) % (range + 1U) : 0U));
    const microstructure::Quantity new_size =
        std::max(microstructure::Quantity{1}, ord.size - reduction);

    const microstructure::Quantity actual_reduction = ord.size - new_size;

    const microstructure::Event ev = emit(
        microstructure::EventType::Modify,
        ord.order_id, ord.price, new_size, ord.side);

    // Update shadow book volume
    if (ord.side == microstructure::Side::Bid) {
        auto it = bid_levels_.find(ord.price);
        if (it != bid_levels_.end()) it->second -= actual_reduction;
    } else {
        auto it = ask_levels_.find(ord.price);
        if (it != ask_levels_.end()) it->second -= actual_reduction;
    }
    ord.size = new_size;

    return ev;
}

void SyntheticEventGenerator::maybe_drift_mid() {
    // Compare rng output (range [0, 2^32-1]) against threshold
    constexpr double max_val = static_cast<double>(std::mt19937::max());
    const auto       raw     = static_cast<double>(rng_());
    if (raw / max_val < config_.price_drift_probability) {
        if ((rng_() & 1U) != 0U) {
            mid_price_ += config_.tick_size;
        } else {
            mid_price_ = std::max(config_.tick_size,
                                  mid_price_ - config_.tick_size);
        }
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

std::vector<microstructure::Event> SyntheticEventGenerator::generate() {
    reset_state();

    std::vector<microstructure::Event> events;
    events.reserve(config_.total_events);

    // ── Warmup: place depth_levels orders on each side ────────────────────
    const std::size_t warmup =
        std::min(config_.depth_levels * 2U, config_.total_events);

    for (std::size_t i = 0; i < warmup; ++i) {
        const bool is_bid    = (i < config_.depth_levels);
        const std::size_t level = is_bid ? i : (i - config_.depth_levels);

        const microstructure::Side side =
            is_bid ? microstructure::Side::Bid : microstructure::Side::Ask;

        microstructure::Price price = is_bid
            ? mid_price_ - static_cast<microstructure::Price>(level + 1U) * config_.tick_size
            : mid_price_ + static_cast<microstructure::Price>(level + 1U) * config_.tick_size;
        // Clamp bid prices to at least tick_size (price must be positive)
        if (is_bid && price < config_.tick_size) price = config_.tick_size;

        const microstructure::Quantity size = config_.order_size_max;
        const microstructure::OrderId  oid  = next_order_id_++;

        events.push_back(
            emit(microstructure::EventType::Add, oid, price, size, side));

        if (is_bid) {
            bid_levels_[price] += size;
        } else {
            ask_levels_[price] += size;
        }
        safe_orders_.push_back(TrackedOrder{oid, price, size, side});
    }

    // ── Random events ─────────────────────────────────────────────────────
    for (std::size_t i = warmup; i < config_.total_events; ++i) {
        maybe_drift_mid();

        // Map rng output to [0, 1)
        constexpr double max_val = static_cast<double>(std::mt19937::max());
        const double     roll    = static_cast<double>(rng_()) / (max_val + 1.0);

        std::optional<microstructure::Event> ev;

        const double t1 = config_.add_probability;
        const double t2 = t1 + config_.cancel_probability;
        const double t3 = t2 + config_.trade_probability;
        const double t4 = t3 + config_.modify_probability;

        if (roll < t1) {
            ev = generate_add();
        } else if (roll < t2) {
            ev = try_generate_cancel();
        } else if (roll < t3) {
            ev = try_generate_trade();
        } else if (roll < t4) {
            ev = try_generate_modify();
        } else {
            ev = generate_add();
        }

        // Fallback to Add if the chosen type was not possible
        if (!ev.has_value()) {
            ev = generate_add();
        }

        events.push_back(std::move(*ev));
    }

    return events;
}

} // namespace visualization
