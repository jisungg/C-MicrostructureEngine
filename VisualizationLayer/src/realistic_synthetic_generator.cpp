#include "visualization/realistic_synthetic_generator.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

namespace visualization {

// Harden config: clamp/fix values that would cause UB or invalid events.
static RealisticSyntheticConfig harden(RealisticSyntheticConfig c) {
    if (c.tick_size <= 0)                          c.tick_size           = 1;
    if (c.initial_mid_price < c.tick_size)         c.initial_mid_price   = c.tick_size;
    if (c.order_size_min <= 0)                     c.order_size_min      = 1;
    if (c.order_size_max < c.order_size_min)       c.order_size_max      = c.order_size_min;
    if (c.initial_depth_levels == 0)               c.initial_depth_levels = 1;
    if (c.add_intensity    < 0.0)                  c.add_intensity       = 0.0;
    if (c.cancel_intensity < 0.0)                  c.cancel_intensity    = 0.0;
    if (c.trade_intensity  < 0.0)                  c.trade_intensity     = 0.0;
    if (c.modify_intensity < 0.0)                  c.modify_intensity    = 0.0;
    if (c.add_lambda <= 0.0)                       c.add_lambda          = 0.01;
    if (c.midprice_volatility < 0.0)               c.midprice_volatility = 0.0;
    if (c.regime_shift_probability < 0.0)          c.regime_shift_probability = 0.0;
    if (c.regime_shift_probability > 1.0)          c.regime_shift_probability = 1.0;
    if (c.post_trade_refill_boost < 0.0)            c.post_trade_refill_boost  = 0.0;
    // Spread targets: tight ≥ 1; normal ≥ tight; stressed ≥ normal.
    if (c.target_spread_tight < 1)
        c.target_spread_tight = 1;
    if (c.target_spread_normal < c.target_spread_tight)
        c.target_spread_normal = c.target_spread_tight;
    if (c.target_spread_stressed < c.target_spread_normal)
        c.target_spread_stressed = c.target_spread_normal;
    return c;
}

// ── Constructor ───────────────────────────────────────────────────────────────

RealisticSyntheticGenerator::RealisticSyntheticGenerator(RealisticSyntheticConfig config)
    : config_{harden(std::move(config))}
    , rng_{config_.seed}
    , mid_price_{config_.initial_mid_price}
{
}

// ── Internal state reset ──────────────────────────────────────────────────────

void RealisticSyntheticGenerator::reset_state() {
    rng_.seed(config_.seed);
    bid_levels_.clear();
    ask_levels_.clear();
    safe_orders_.clear();
    next_order_id_      = 1;
    next_event_id_      = 1;
    next_ts_            = 1'000'000'000LL;
    mid_price_          = config_.initial_mid_price;
    regime_             = SyntheticRegime::Normal;
    post_trade_add_boost_ = 0.0;
    regime_trace_.clear();
    regime_trace_.reserve(config_.total_events);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

double RealisticSyntheticGenerator::uniform_double() noexcept {
    constexpr double max_val = static_cast<double>(std::mt19937::max());
    return static_cast<double>(rng_()) / (max_val + 1.0);
}

std::optional<microstructure::Price>
RealisticSyntheticGenerator::best_bid() const noexcept {
    if (bid_levels_.empty()) return std::nullopt;
    return bid_levels_.begin()->first;
}

std::optional<microstructure::Price>
RealisticSyntheticGenerator::best_ask() const noexcept {
    if (ask_levels_.empty()) return std::nullopt;
    return ask_levels_.begin()->first;
}

double RealisticSyntheticGenerator::compute_imbalance() const noexcept {
    double bid_vol = 0.0;
    for (const auto& [p, v] : bid_levels_) bid_vol += static_cast<double>(v);
    double ask_vol = 0.0;
    for (const auto& [p, v] : ask_levels_) ask_vol += static_cast<double>(v);
    const double total = bid_vol + ask_vol;
    if (total < 1.0) return 0.0;
    return (bid_vol - ask_vol) / total;
}

// Regime multipliers for event intensities and lambda
double RealisticSyntheticGenerator::regime_add_mult()    const noexcept {
    switch (regime_) {
    case SyntheticRegime::Tight:    return 1.2;
    case SyntheticRegime::Normal:   return 1.0;
    case SyntheticRegime::Stressed: return 0.7;
    }
    return 1.0;
}
double RealisticSyntheticGenerator::regime_cancel_mult() const noexcept {
    switch (regime_) {
    case SyntheticRegime::Tight:    return 0.6;
    case SyntheticRegime::Normal:   return 1.0;
    case SyntheticRegime::Stressed: return 2.0;
    }
    return 1.0;
}
double RealisticSyntheticGenerator::regime_trade_mult()  const noexcept {
    switch (regime_) {
    case SyntheticRegime::Tight:    return 0.7;
    case SyntheticRegime::Normal:   return 1.0;
    case SyntheticRegime::Stressed: return 1.5;
    }
    return 1.0;
}
double RealisticSyntheticGenerator::regime_lambda_mult() const noexcept {
    // Higher lambda → more touch-concentrated order placement
    switch (regime_) {
    case SyntheticRegime::Tight:    return 1.5;  // very concentrated near touch
    case SyntheticRegime::Normal:   return 1.0;
    case SyntheticRegime::Stressed: return 0.5;  // spread out — thin near touch
    }
    return 1.0;
}

microstructure::Price
RealisticSyntheticGenerator::regime_spread_target() const noexcept {
    switch (regime_) {
    case SyntheticRegime::Tight:    return config_.target_spread_tight;
    case SyntheticRegime::Normal:   return config_.target_spread_normal;
    case SyntheticRegime::Stressed: return config_.target_spread_stressed;
    }
    return config_.target_spread_normal;
}

// ── Regime transition ─────────────────────────────────────────────────────────

void RealisticSyntheticGenerator::maybe_shift_regime() {
    const double u = uniform_double();
    const double p = config_.regime_shift_probability;
    switch (regime_) {
    case SyntheticRegime::Tight:
        if (u < p) regime_ = SyntheticRegime::Normal;
        break;
    case SyntheticRegime::Normal:
        if (u < p * 0.4) {
            regime_ = SyntheticRegime::Tight;
        } else if (u < p) {
            regime_ = SyntheticRegime::Stressed;
        }
        break;
    case SyntheticRegime::Stressed:
        // Stress tends to resolve: transition rate doubled
        if (u < p * 2.0) regime_ = SyntheticRegime::Normal;
        break;
    }
}

// ── Mid-price evolution ───────────────────────────────────────────────────────

void RealisticSyntheticGenerator::evolve_mid_price() {
    // Gaussian noise scaled by regime volatility
    const double regime_vol_mult = (regime_ == SyntheticRegime::Stressed) ? 2.0
                                 : (regime_ == SyntheticRegime::Tight)    ? 0.5
                                 : 1.0;
    const double sigma = config_.midprice_volatility * regime_vol_mult;

    // Apply imbalance bias to the noise mean: bid-heavy → upward drift
    const double imbalance = compute_imbalance();
    const double bias = imbalance * config_.aggressor_imbalance_sensitivity * 0.5;

    // Sample from N(bias, sigma)
    // Box-Muller transform using two uniform samples
    const double u1 = std::max(1e-12, uniform_double());
    const double u2 = uniform_double();
    const double z  = std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307 * u2);
    const double noise = bias + sigma * z;

    const int drift_ticks = static_cast<int>(std::round(noise));
    const auto drift = static_cast<microstructure::Price>(drift_ticks);
    mid_price_ += drift * config_.tick_size;
    // Floor at 1 tick
    if (mid_price_ < config_.tick_size) mid_price_ = config_.tick_size;
}

// ── Near-touch depth sampling ─────────────────────────────────────────────────

std::size_t RealisticSyntheticGenerator::sample_depth_offset() {
    const double lambda = config_.add_lambda * regime_lambda_mult();
    // Inverse CDF of Geometric(p=1-exp(-lambda)) distribution:
    // k = floor(-log(u) / lambda) where u ~ Uniform(0, 1)
    const double u = std::max(1e-15, uniform_double());
    const auto k = static_cast<std::size_t>(std::floor(-std::log(u) / lambda));
    const std::size_t max_depth = std::max(std::size_t{1}, config_.initial_depth_levels);
    return std::min(k, max_depth - 1U);
}

// ── Event emission ────────────────────────────────────────────────────────────

microstructure::Event RealisticSyntheticGenerator::emit(
    microstructure::EventType  et,
    microstructure::OrderId    oid,
    microstructure::Price      px,
    microstructure::Quantity   sz,
    microstructure::Side       sd)
{
    const microstructure::EventId   eid = next_event_id_++;
    const microstructure::Timestamp ts  = next_ts_;
    next_ts_ += 1'000'000LL;
    return microstructure::Event{eid, et, oid, px, sz, sd, ts, ts + 1, ts + 2, config_.venue};
}

// ── Add ───────────────────────────────────────────────────────────────────────
//
// Regime-driven spread targeting replaces the old touch_fill_prob heuristic.
//
// Every Add event anchors its k=0 price at:
//   bid: best_ask - regime_spread_target() * tick
//   ask: best_bid + regime_spread_target() * tick
//
// This creates three behaviours depending on the current spread vs target:
//   spread < target  → anchor is below (bid) / above (ask) the current inside.
//                      New orders accumulate there.  When trades clear the old
//                      inside level the spread widens toward target naturally.
//   spread == target → anchor coincides with current best; orders join/maintain.
//   spread > target  → anchor is inside the current spread; new order improves
//                      the touch and tightens the spread toward target.
//
// Because regime switches between Tight (target=1), Normal (target=2), and
// Stressed (target=3), the spread cycles between narrow and wide throughout
// the full replay — not just in the first warmup window.

microstructure::Event RealisticSyntheticGenerator::generate_add() {
    const bool is_bid = ((rng_() & 1U) == 0U);
    const microstructure::Side side =
        is_bid ? microstructure::Side::Bid : microstructure::Side::Ask;

    const std::size_t k = sample_depth_offset();
    const auto offset = static_cast<microstructure::Price>(k);

    // Target spread for the current regime (ticks).
    const microstructure::Price tgt = regime_spread_target();

    microstructure::Price price{};

    if (is_bid) {
        const auto ba = best_ask();
        const auto bb = best_bid();

        // Anchor: best_ask - tgt (the price at which best_bid should sit).
        // Fall back to mid - 1 tick when no ask exists yet.
        const microstructure::Price anchor = ba.has_value()
            ? (*ba - tgt * config_.tick_size)
            : (mid_price_ - config_.tick_size);

        price = anchor - offset * config_.tick_size;
        if (price < config_.tick_size) price = config_.tick_size;

        // Hard guard: bid must never reach or cross best_ask.
        if (ba.has_value() && price >= *ba) {
            price = *ba - config_.tick_size;
            if (price < config_.tick_size) {
                // No room for a bid on this side; emit an ask instead.
                const microstructure::Price ask_anchor = bb.has_value()
                    ? (*bb + tgt * config_.tick_size)
                    : (mid_price_ + config_.tick_size);
                const microstructure::Price ask_price =
                    ask_anchor + offset * config_.tick_size;
                const microstructure::Quantity sz = sample_order_size(k);
                const microstructure::OrderId  oid = next_order_id_++;
                const auto ev = emit(microstructure::EventType::Add, oid,
                                     ask_price, sz, microstructure::Side::Ask);
                ask_levels_[ask_price] += sz;
                safe_orders_.push_back(
                    TrackedOrder{oid, ask_price, sz, microstructure::Side::Ask});
                return ev;
            }
        }
    } else {
        const auto bb = best_bid();

        // Anchor: best_bid + tgt (the price at which best_ask should sit).
        // Fall back to mid + 1 tick when no bid exists yet.
        const microstructure::Price anchor = bb.has_value()
            ? (*bb + tgt * config_.tick_size)
            : (mid_price_ + config_.tick_size);

        price = anchor + offset * config_.tick_size;

        // Hard guard: ask must never reach or cross best_bid.
        if (bb.has_value() && price <= *bb) price = *bb + config_.tick_size;
    }

    const microstructure::Quantity sz = sample_order_size(k);
    const microstructure::OrderId oid = next_order_id_++;
    const auto ev = emit(microstructure::EventType::Add, oid, price, sz, side);

    if (is_bid) bid_levels_[price] += sz;
    else        ask_levels_[price] += sz;
    safe_orders_.push_back(TrackedOrder{oid, price, sz, side});
    return ev;
}

// Order size: larger orders placed deeper in the book (institutional sizing).
// Touch (k=0): ~ order_size_min
// Depth k: ~ order_size_min * (1 + 0.4*k), capped at order_size_max
microstructure::Quantity
RealisticSyntheticGenerator::sample_order_size(std::size_t k) {
    const double scale = 1.0 + 0.4 * static_cast<double>(k);
    const auto lo = config_.order_size_min;
    const auto hi = std::min(config_.order_size_max,
        static_cast<microstructure::Quantity>(
            static_cast<double>(config_.order_size_min) * scale));
    const auto range = static_cast<std::uint64_t>(hi - lo);
    return lo + static_cast<microstructure::Quantity>(
        range > 0U ? static_cast<std::uint64_t>(rng_()) % (range + 1U) : 0U);
}

// ── Cancel ────────────────────────────────────────────────────────────────────

std::optional<microstructure::Event>
RealisticSyntheticGenerator::try_generate_cancel() {
    if (safe_orders_.empty()) return std::nullopt;

    // Distance-weighted sampling: far-from-mid orders more likely cancelled
    const double beta = config_.cancel_staleness_sensitivity;
    const double mid  = static_cast<double>(mid_price_);
    const double tick = static_cast<double>(config_.tick_size);

    std::vector<double> weights(safe_orders_.size());
    double total_weight = 0.0;
    for (std::size_t i = 0; i < safe_orders_.size(); ++i) {
        const double dist = std::abs(static_cast<double>(safe_orders_[i].price) - mid) / tick;
        weights[i] = std::exp(beta * dist);
        total_weight += weights[i];
    }

    const double u = uniform_double() * total_weight;
    double cum = 0.0;
    std::size_t chosen = safe_orders_.size() - 1U;
    for (std::size_t i = 0; i < safe_orders_.size(); ++i) {
        cum += weights[i];
        if (u <= cum) { chosen = i; break; }
    }

    const TrackedOrder ord = safe_orders_[chosen];
    safe_orders_[chosen]   = safe_orders_.back();
    safe_orders_.pop_back();

    // Update shadow level
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

// ── Trade ─────────────────────────────────────────────────────────────────────

std::optional<microstructure::Event>
RealisticSyntheticGenerator::try_generate_trade() {
    const auto bb = best_bid();
    const auto ba = best_ask();
    if (!bb.has_value() && !ba.has_value()) return std::nullopt;

    // Imbalance-biased aggressor direction
    // Positive imbalance (bid-heavy) → buy aggressor more likely → lifts ask
    const double imbalance = compute_imbalance();
    const double p_buy_agg = std::clamp(
        0.5 + config_.aggressor_imbalance_sensitivity * imbalance,
        0.1, 0.9);

    const bool buy_aggressor = (uniform_double() < p_buy_agg);
    // buy_aggressor lifts the ask → resting side = Ask
    const bool resting_is_ask = buy_aggressor;

    microstructure::Price    resting_price{};
    microstructure::Side     resting_side{};
    microstructure::Quantity level_volume{};

    if (resting_is_ask && ba.has_value()) {
        resting_price = *ba;
        resting_side  = microstructure::Side::Ask;
        level_volume  = ask_levels_.begin()->second;
    } else if (!resting_is_ask && bb.has_value()) {
        resting_price = *bb;
        resting_side  = microstructure::Side::Bid;
        level_volume  = bid_levels_.begin()->second;
    } else if (bb.has_value()) {
        resting_price = *bb;
        resting_side  = microstructure::Side::Bid;
        level_volume  = bid_levels_.begin()->second;
    } else {
        resting_price = *ba;
        resting_side  = microstructure::Side::Ask;
        level_volume  = ask_levels_.begin()->second;
    }

    if (level_volume <= 0) return std::nullopt;

    // Trade size: 5%–30% of level volume (partial fills are common)
    const auto lv = static_cast<std::uint64_t>(level_volume);
    const auto lo = std::max(std::uint64_t{1}, lv / 20U);
    const auto hi = std::max(lo, lv * 3U / 10U);
    const std::uint64_t range = hi - lo;
    const microstructure::Quantity trade_size = static_cast<microstructure::Quantity>(
        lo + (range > 0U ? static_cast<std::uint64_t>(rng_()) % (range + 1U) : 0U));

    // Update shadow book and purge safe_orders at the resting level
    if (resting_side == microstructure::Side::Bid) {
        auto it = bid_levels_.find(resting_price);
        if (it != bid_levels_.end()) {
            it->second -= trade_size;
            if (it->second <= 0) bid_levels_.erase(it);
        }
        safe_orders_.erase(
            std::remove_if(safe_orders_.begin(), safe_orders_.end(),
                [&](const TrackedOrder& o) noexcept {
                    return o.side == microstructure::Side::Bid && o.price == resting_price;
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
                    return o.side == microstructure::Side::Ask && o.price == resting_price;
                }),
            safe_orders_.end());
    }

    // Activate post-trade refill boost
    post_trade_add_boost_ = config_.post_trade_refill_boost;

    // Mid price nudge in trade direction (buy aggressor → slight upward pressure)
    if (resting_is_ask) {
        // Buy aggressor lifted ask → upward pressure with 30% probability
        if (uniform_double() < 0.3) mid_price_ += config_.tick_size;
    } else {
        // Sell aggressor hit bid → downward pressure with 30% probability
        if (uniform_double() < 0.3) {
            mid_price_ = std::max(config_.tick_size, mid_price_ - config_.tick_size);
        }
    }

    return emit(microstructure::EventType::Trade,
                microstructure::OrderId{0},
                resting_price, trade_size, resting_side);
}

// ── Modify ────────────────────────────────────────────────────────────────────

std::optional<microstructure::Event>
RealisticSyntheticGenerator::try_generate_modify() {
    if (safe_orders_.empty()) return std::nullopt;

    const std::size_t idx = static_cast<std::size_t>(
        static_cast<std::uint64_t>(rng_()) % safe_orders_.size());
    TrackedOrder& ord = safe_orders_[idx];

    // Reduce size by 10%–50%
    const auto half  = static_cast<std::uint64_t>(ord.size) / 2U;
    const auto tenth = std::max(std::uint64_t{1}, static_cast<std::uint64_t>(ord.size) / 10U);
    const std::uint64_t range = (half > tenth) ? (half - tenth) : 0U;
    const microstructure::Quantity reduction = static_cast<microstructure::Quantity>(
        tenth + (range > 0U ? static_cast<std::uint64_t>(rng_()) % (range + 1U) : 0U));
    const microstructure::Quantity new_size =
        std::max(microstructure::Quantity{1}, ord.size - reduction);
    const microstructure::Quantity actual_reduction = ord.size - new_size;

    const microstructure::Event ev = emit(
        microstructure::EventType::Modify, ord.order_id, ord.price, new_size, ord.side);

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

// ── Public generate() ─────────────────────────────────────────────────────────

std::vector<microstructure::Event> RealisticSyntheticGenerator::generate() {
    reset_state();

    std::vector<microstructure::Event> events;
    events.reserve(config_.total_events);

    // ── Warmup: build initial book with near-touch orders ─────────────────
    const std::size_t warmup = std::min(config_.initial_depth_levels * 2U,
                                        config_.total_events);
    for (std::size_t i = 0; i < warmup; ++i) {
        const bool is_bid   = (i < config_.initial_depth_levels);
        const std::size_t k = is_bid ? i : (i - config_.initial_depth_levels);

        const microstructure::Side side =
            is_bid ? microstructure::Side::Bid : microstructure::Side::Ask;

        microstructure::Price price = is_bid
            ? mid_price_ - static_cast<microstructure::Price>(k + 1U) * config_.tick_size
            : mid_price_ + static_cast<microstructure::Price>(k + 1U) * config_.tick_size;

        // Clamp bid price to positive
        if (is_bid && price < config_.tick_size) price = config_.tick_size;

        const microstructure::Quantity sz = sample_order_size(k);
        const microstructure::OrderId  oid = next_order_id_++;

        events.push_back(emit(microstructure::EventType::Add, oid, price, sz, side));
        if (is_bid) bid_levels_[price] += sz;
        else        ask_levels_[price] += sz;
        safe_orders_.push_back(TrackedOrder{oid, price, sz, side});
        regime_trace_.push_back(regime_);
    }

    // ── Main loop ─────────────────────────────────────────────────────────
    for (std::size_t i = warmup; i < config_.total_events; ++i) {
        maybe_shift_regime();
        evolve_mid_price();
        regime_trace_.push_back(regime_);

        // Effective intensities after regime and post-trade boost
        const double eff_add    = config_.add_intensity * regime_add_mult()
                                  + post_trade_add_boost_;
        const double eff_cancel = config_.cancel_intensity * regime_cancel_mult();
        const double eff_trade  = config_.trade_intensity  * regime_trade_mult();
        const double eff_modify = config_.modify_intensity;

        // Decay refill boost
        post_trade_add_boost_ = std::max(0.0, post_trade_add_boost_ - 0.08);

        const double total = eff_add + eff_cancel + eff_trade + eff_modify;
        const double roll  = uniform_double() * total;

        std::optional<microstructure::Event> ev;
        const double t1 = eff_add;
        const double t2 = t1 + eff_cancel;
        const double t3 = t2 + eff_trade;

        if (roll < t1) {
            ev = generate_add();
        } else if (roll < t2) {
            ev = try_generate_cancel();
        } else if (roll < t3) {
            ev = try_generate_trade();
        } else {
            ev = try_generate_modify();
        }

        if (!ev.has_value()) ev = generate_add(); // fallback
        events.push_back(std::move(*ev));
    }

    return events;
}

} // namespace visualization
