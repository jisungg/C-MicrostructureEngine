#include "microstructure/features.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace microstructure {

namespace {

double to_double(const std::optional<Price> value) {
    return value.has_value() ? static_cast<double>(*value) : 0.0;
}

double safe_ratio(const double numerator, const double denominator) {
    if (std::abs(denominator) < 1e-12) {
        return 0.0;
    }
    return numerator / denominator;
}

} // namespace

IncrementalFeatureEngine::IncrementalFeatureEngine(FeatureConfig config)
    : config_(std::move(config)) {}

void IncrementalFeatureEngine::reset() {
    latest_ = FeatureSnapshot{};
    volatility_ = 0.0;
    has_previous_mid_ = false;
    previous_mid_ = 0.0;
    last_hawkes_timestamp_ = 0;
    has_hawkes_timestamp_ = false;
    hawkes_ = HawkesHooks{};
    ofi_window_.clear();
    window_ofi_ = 0.0;
    activity_window_.clear();
    window_add_volume_ = 0;
    window_cancel_volume_ = 0;
    window_fill_volume_ = 0;
    event_window_.clear();
    hidden_trackers_.clear();
    hidden_tracker_expiry_.clear();
}

DepthMetrics IncrementalFeatureEngine::compute_depth(const BookView& book) const {
    const std::size_t required_levels = std::max<std::size_t>(config_.depth_levels, 10U);
    const auto bids = book.top_levels(Side::Bid, required_levels);
    const auto asks = book.top_levels(Side::Ask, required_levels);

    DepthMetrics depth;
    if (!bids.empty()) {
        depth.top_depth += bids.front().total_volume;
    }
    if (!asks.empty()) {
        depth.top_depth += asks.front().total_volume;
    }

    for (std::size_t i = 0; i < bids.size(); ++i) {
        if (i < 5U) {
            depth.bid_depth_5 += bids[i].total_volume;
        }
        if (i < 10U) {
            depth.bid_depth_10 += bids[i].total_volume;
        }
    }
    for (std::size_t i = 0; i < asks.size(); ++i) {
        if (i < 5U) {
            depth.ask_depth_5 += asks[i].total_volume;
        }
        if (i < 10U) {
            depth.ask_depth_10 += asks[i].total_volume;
        }
    }

    depth.depth_5 = depth.bid_depth_5 + depth.ask_depth_5;
    depth.depth_10 = depth.bid_depth_10 + depth.ask_depth_10;
    return depth;
}

std::vector<LiquiditySurfacePoint> IncrementalFeatureEngine::compute_liquidity_surface(const BookView& book,
                                                                                        const Side side) const {
    std::vector<LiquiditySurfacePoint> surface;
    const auto best_bid = book.best_bid();
    const auto best_ask = book.best_ask();
    if (!best_bid.has_value() || !best_ask.has_value()) {
        return surface;
    }

    const double mid = (static_cast<double>(*best_bid) + static_cast<double>(*best_ask)) / 2.0;
    const auto levels = book.top_levels(side, config_.surface_levels);
    Quantity cumulative = 0;
    surface.reserve(levels.size());

    for (const BookLevelState& level : levels) {
        cumulative += level.total_volume;
        surface.push_back(LiquiditySurfacePoint{
            side,
            static_cast<double>(level.price) - mid,
            cumulative});
    }

    return surface;
}

double IncrementalFeatureEngine::compute_liquidity_slope(const std::vector<LiquiditySurfacePoint>& surface) const {
    if (surface.empty()) {
        return 0.0;
    }

    const LiquiditySurfacePoint& farthest = surface.back();
    const double distance = std::abs(farthest.distance_from_mid);
    if (distance < 1e-12) {
        return 0.0;
    }
    return static_cast<double>(farthest.cumulative_volume) / distance;
}

double IncrementalFeatureEngine::compute_depth_ratio(const DepthMetrics& depth) const {
    if (depth.ask_depth_5 == 0) {
        return depth.bid_depth_5 > 0 ? std::numeric_limits<double>::infinity() : 0.0;
    }
    return static_cast<double>(depth.bid_depth_5) / static_cast<double>(depth.ask_depth_5);
}

LiquidityRegime IncrementalFeatureEngine::classify_regime(const double spread,
                                                          const double volatility,
                                                          const DepthMetrics& depth,
                                                          const double cancel_rate) const {
    if (spread <= static_cast<double>(config_.tight_spread_threshold) &&
        depth.depth_5 >= config_.tight_depth_threshold &&
        cancel_rate < config_.tight_cancel_rate_threshold &&
        volatility < config_.tight_volatility_threshold) {
        return LiquidityRegime::Tight;
    }
    if (spread <= static_cast<double>(config_.normal_spread_threshold) &&
        depth.depth_5 >= config_.normal_depth_threshold &&
        cancel_rate < config_.stressed_cancel_rate) {
        return LiquidityRegime::Normal;
    }
    if (spread <= static_cast<double>(config_.stressed_spread_threshold) &&
        depth.depth_5 >= config_.stressed_depth_threshold &&
        cancel_rate < config_.illiquid_cancel_rate) {
        return LiquidityRegime::Stressed;
    }
    return LiquidityRegime::Illiquid;
}

void IncrementalFeatureEngine::evict_window_state(const Timestamp now) {
    const Timestamp cutoff = now - config_.flow_window;

    while (!ofi_window_.empty() && ofi_window_.front().timestamp < cutoff) {
        window_ofi_ -= ofi_window_.front().delta;
        ofi_window_.pop_front();
    }

    while (!activity_window_.empty() && activity_window_.front().timestamp < cutoff) {
        window_add_volume_ -= activity_window_.front().added_volume;
        window_cancel_volume_ -= activity_window_.front().canceled_volume;
        window_fill_volume_ -= activity_window_.front().filled_volume;
        activity_window_.pop_front();
    }

    while (!event_window_.empty() && event_window_.front() < cutoff) {
        event_window_.pop_front();
    }
}

void IncrementalFeatureEngine::update_window_state(const Event& event, const BookUpdate& update) {
    const Timestamp now = event.processing_timestamp();

    double event_ofi_delta = 0.0;
    for (const PriceLevelDelta& delta : update.deltas) {
        if (delta.side == Side::Bid) {
            event_ofi_delta += static_cast<double>(delta.delta_volume);
        } else {
            event_ofi_delta -= static_cast<double>(delta.delta_volume);
        }
    }

    ofi_window_.push_back(TimedOfiSample{now, event_ofi_delta});
    window_ofi_ += event_ofi_delta;

    activity_window_.push_back(TimedActivitySample{
        now,
        update.added_volume,
        update.canceled_volume,
        update.filled_volume});
    window_add_volume_ += update.added_volume;
    window_cancel_volume_ += update.canceled_volume;
    window_fill_volume_ += update.filled_volume;

    event_window_.push_back(now);
    evict_window_state(now);
}

void IncrementalFeatureEngine::update_hawkes(const Event& event, const BookUpdate& update) {
    if (event.event_type() == EventType::Snapshot) {
        hawkes_ = HawkesHooks{};
        has_hawkes_timestamp_ = false;
        last_hawkes_timestamp_ = 0;
        return;
    }

    const Timestamp now = event.processing_timestamp();
    if (has_hawkes_timestamp_) {
        const double dt = static_cast<double>(now - last_hawkes_timestamp_);
        const double decay = std::exp(-config_.hawkes_decay * dt);
        hawkes_.trade_intensity *= decay;
        hawkes_.order_arrival_intensity *= decay;
        hawkes_.cancel_intensity *= decay;
    }
    has_hawkes_timestamp_ = true;
    last_hawkes_timestamp_ = now;

    if (event.event_type() == EventType::Trade) {
        hawkes_.trade_intensity += 1.0;
    }
    if (update.added_volume > 0 || update.repriced_added_volume > 0) {
        hawkes_.order_arrival_intensity += 1.0;
    }
    if (update.canceled_volume > 0 || update.repriced_removed_volume > 0) {
        hawkes_.cancel_intensity += 1.0;
    }
}

void IncrementalFeatureEngine::prune_hidden_trackers(const Timestamp now) {
    const Timestamp cutoff = now - config_.hidden_tracker_ttl;
    while (!hidden_tracker_expiry_.empty() && hidden_tracker_expiry_.front().first < cutoff) {
        const Timestamp timestamp = hidden_tracker_expiry_.front().first;
        const Price price = hidden_tracker_expiry_.front().second;
        hidden_tracker_expiry_.pop_front();
        const auto tracker_it = hidden_trackers_.find(price);
        if (tracker_it != hidden_trackers_.end() && tracker_it->second.last_trade_timestamp == timestamp) {
            hidden_trackers_.erase(tracker_it);
        }
    }
}

void IncrementalFeatureEngine::update_hidden_liquidity(const Event& event, const BookUpdate& update) {
    latest_.hidden_liquidity = HiddenLiquiditySignal{};
    prune_hidden_trackers(event.processing_timestamp());

    if (event.event_type() == EventType::Snapshot) {
        hidden_trackers_.clear();
        hidden_tracker_expiry_.clear();
        return;
    }

    if (event.event_type() == EventType::Trade && update.trade.has_value()) {
        HiddenTracker& tracker = hidden_trackers_[event.price()];
        tracker.executed_volume += event.size();
        tracker.displayed_depth_before = update.trade->visible_depth_before;
        tracker.repeated_fills += 1;
        tracker.last_trade_timestamp = event.processing_timestamp();
        hidden_tracker_expiry_.push_back({tracker.last_trade_timestamp, event.price()});

        const bool exceeds_displayed = tracker.executed_volume > tracker.displayed_depth_before;
        const bool repeated_and_refilled =
            update.trade->remaining_at_price_after == 0 &&
            tracker.repeated_fills >= 2 &&
            tracker.refill_count > 0;

        latest_.hidden_liquidity = HiddenLiquiditySignal{
            exceeds_displayed || repeated_and_refilled,
            event.price(),
            tracker.executed_volume,
            tracker.displayed_depth_before,
            tracker.repeated_fills,
            tracker.refill_count};
        return;
    }

    const bool refill_candidate = event.event_type() == EventType::Add ||
                                  (event.event_type() == EventType::Modify && update.added_volume > 0);
    if (!refill_candidate) {
        return;
    }

    for (const PriceLevelDelta& delta : update.deltas) {
        if (delta.delta_volume <= 0 || delta.price != event.price()) {
            continue;
        }
        auto tracker_it = hidden_trackers_.find(delta.price);
        if (tracker_it == hidden_trackers_.end()) {
            continue;
        }
        HiddenTracker& tracker = tracker_it->second;
        if (event.processing_timestamp() - tracker.last_trade_timestamp <= config_.hidden_refill_window) {
            tracker.refill_count += 1;
            latest_.hidden_liquidity = HiddenLiquiditySignal{
                tracker.repeated_fills >= 2 && tracker.refill_count > 0,
                delta.price,
                tracker.executed_volume,
                tracker.displayed_depth_before,
                tracker.repeated_fills,
                tracker.refill_count};
        }
    }
}

FeatureSnapshot IncrementalFeatureEngine::on_event(const Event& event,
                                                   const BookView& book,
                                                   const BookUpdate& update) {
    if (update.snapshot_restored) {
        reset();
    }

    update_window_state(event, update);

    const auto best_bid = book.best_bid();
    const auto best_ask = book.best_ask();
    const double bid_volume = static_cast<double>(book.best_bid_volume());
    const double ask_volume = static_cast<double>(book.best_ask_volume());
    const double total_bid = static_cast<double>(book.total_volume(Side::Bid));
    const double total_ask = static_cast<double>(book.total_volume(Side::Ask));

    latest_.imbalance = safe_ratio(total_bid - total_ask, total_bid + total_ask);
    latest_.spread = (best_bid.has_value() && best_ask.has_value())
                         ? static_cast<double>(*best_ask - *best_bid)
                         : 0.0;
    latest_.microprice = (best_bid.has_value() && best_ask.has_value() && bid_volume + ask_volume > 0.0)
                             ? ((static_cast<double>(*best_ask) * bid_volume) +
                                (static_cast<double>(*best_bid) * ask_volume)) /
                                   (bid_volume + ask_volume)
                             : 0.0;
    latest_.ofi = window_ofi_;

    if (best_bid.has_value() && best_ask.has_value()) {
        const double mid = (to_double(best_bid) + to_double(best_ask)) / 2.0;
        if (has_previous_mid_ && previous_mid_ > 0.0) {
            const double move = std::abs(mid - previous_mid_) / previous_mid_;
            volatility_ = (config_.volatility_alpha * move) + ((1.0 - config_.volatility_alpha) * volatility_);
        }
        previous_mid_ = mid;
        has_previous_mid_ = true;
    }
    latest_.volatility = volatility_;

    latest_.depth = compute_depth(book);
    latest_.depth_ratio = compute_depth_ratio(latest_.depth);

    const auto bid_top = book.top_levels(Side::Bid, 1);
    const auto ask_top = book.top_levels(Side::Ask, 1);
    latest_.queue.queue_depth = (bid_top.empty() ? 0U : bid_top.front().queue_depth) +
                                (ask_top.empty() ? 0U : ask_top.front().queue_depth);

    const double activity_volume = static_cast<double>(window_add_volume_ + window_cancel_volume_ + window_fill_volume_);
    latest_.queue.cancel_rate = safe_ratio(static_cast<double>(window_cancel_volume_), activity_volume);
    latest_.queue.fill_rate = safe_ratio(static_cast<double>(window_fill_volume_), activity_volume);

    double events_per_unit_time = 0.0;
    if (event_window_.size() >= 2U) {
        const Timestamp duration = event_window_.back() - event_window_.front();
        if (duration > 0) {
            events_per_unit_time =
                static_cast<double>(event_window_.size() - 1U) / static_cast<double>(duration);
        }
    }
    const double hazard_per_event = latest_.queue.cancel_rate + latest_.queue.fill_rate;
    const double hazard_per_time = hazard_per_event * events_per_unit_time;
    latest_.queue.queue_half_life = hazard_per_time > 0.0 ? std::log(2.0) / hazard_per_time : 0.0;

    latest_.bid_liquidity_surface = compute_liquidity_surface(book, Side::Bid);
    latest_.ask_liquidity_surface = compute_liquidity_surface(book, Side::Ask);
    latest_.liquidity_surface = latest_.bid_liquidity_surface;
    latest_.liquidity_surface.insert(latest_.liquidity_surface.end(),
                                     latest_.ask_liquidity_surface.begin(),
                                     latest_.ask_liquidity_surface.end());
    std::sort(latest_.liquidity_surface.begin(),
              latest_.liquidity_surface.end(),
              [](const LiquiditySurfacePoint& lhs, const LiquiditySurfacePoint& rhs) {
                  return lhs.distance_from_mid < rhs.distance_from_mid;
              });

    latest_.bid_liquidity_slope = compute_liquidity_slope(latest_.bid_liquidity_surface);
    latest_.ask_liquidity_slope = compute_liquidity_slope(latest_.ask_liquidity_surface);
    latest_.liquidity_slope = (latest_.bid_liquidity_slope + latest_.ask_liquidity_slope) / 2.0;
    latest_.regime = classify_regime(latest_.spread,
                                     latest_.volatility,
                                     latest_.depth,
                                     latest_.queue.cancel_rate);
    latest_.latency = event.latency();
    if (update.trade.has_value()) {
        latest_.last_trade_aggressor = update.trade->aggressor;
    }

    update_hawkes(event, update);
    latest_.hawkes = hawkes_;
    update_hidden_liquidity(event, update);

    latest_.signal = SignalVector{
        latest_.imbalance,
        latest_.microprice,
        latest_.spread,
        latest_.ofi,
        latest_.depth_ratio,
        latest_.queue.cancel_rate,
        latest_.queue.queue_half_life,
        latest_.liquidity_slope};

    return latest_;
}

const FeatureSnapshot& IncrementalFeatureEngine::latest() const noexcept {
    return latest_;
}

} // namespace microstructure
