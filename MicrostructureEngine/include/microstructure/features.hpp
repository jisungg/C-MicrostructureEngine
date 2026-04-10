#pragma once

#include <cstddef>
#include <deque>
#include <unordered_map>

#include "microstructure/book_view.hpp"
#include "microstructure/order_book.hpp"
#include "microstructure/types.hpp"

namespace microstructure {

struct FeatureConfig {
    std::size_t depth_levels{10};
    std::size_t surface_levels{10};
    Timestamp flow_window{1'000'000'000};
    double volatility_alpha{0.2};
    double hawkes_decay{1e-9};
    Timestamp hidden_refill_window{1'000'000'000};
    Timestamp hidden_tracker_ttl{10'000'000'000};
    Price tight_spread_threshold{1};
    Price normal_spread_threshold{2};
    Price stressed_spread_threshold{5};
    Quantity tight_depth_threshold{100};
    Quantity normal_depth_threshold{40};
    Quantity stressed_depth_threshold{10};
    double stressed_cancel_rate{0.35};
    double illiquid_cancel_rate{0.60};
    double tight_cancel_rate_threshold{0.15};
    double tight_volatility_threshold{0.01};
};

class IncrementalFeatureEngine {
public:
    explicit IncrementalFeatureEngine(FeatureConfig config = {});

    void reset();
    [[nodiscard]] FeatureSnapshot on_event(const Event& event,
                                           const BookView& book,
                                           const BookUpdate& update);
    [[nodiscard]] const FeatureSnapshot& latest() const noexcept;

private:
    struct TimedOfiSample {
        Timestamp timestamp{0};
        double delta{0.0};
    };

    struct TimedActivitySample {
        Timestamp timestamp{0};
        Quantity added_volume{0};
        Quantity canceled_volume{0};
        Quantity filled_volume{0};
    };

    struct HiddenTracker {
        Quantity executed_volume{0};
        Quantity displayed_depth_before{0};
        std::size_t repeated_fills{0};
        std::size_t refill_count{0};
        Timestamp last_trade_timestamp{0};
    };

    FeatureConfig config_{};
    FeatureSnapshot latest_{};
    double volatility_{0.0};
    bool has_previous_mid_{false};
    double previous_mid_{0.0};
    Timestamp last_hawkes_timestamp_{0};
    bool has_hawkes_timestamp_{false};
    HawkesHooks hawkes_{};
    std::deque<TimedOfiSample> ofi_window_{};
    double window_ofi_{0.0};
    std::deque<TimedActivitySample> activity_window_{};
    Quantity window_add_volume_{0};
    Quantity window_cancel_volume_{0};
    Quantity window_fill_volume_{0};
    std::deque<Timestamp> event_window_{};
    std::unordered_map<Price, HiddenTracker> hidden_trackers_{};
    std::deque<std::pair<Timestamp, Price>> hidden_tracker_expiry_{};

    [[nodiscard]] DepthMetrics compute_depth(const BookView& book) const;
    [[nodiscard]] std::vector<LiquiditySurfacePoint> compute_liquidity_surface(const BookView& book,
                                                                               Side side) const;
    [[nodiscard]] double compute_liquidity_slope(const std::vector<LiquiditySurfacePoint>& surface) const;
    [[nodiscard]] double compute_depth_ratio(const DepthMetrics& depth) const;
    [[nodiscard]] LiquidityRegime classify_regime(double spread,
                                                  double volatility,
                                                  const DepthMetrics& depth,
                                                  double cancel_rate) const;
    void update_window_state(const Event& event, const BookUpdate& update);
    void evict_window_state(Timestamp now);
    void update_hawkes(const Event& event, const BookUpdate& update);
    void update_hidden_liquidity(const Event& event, const BookUpdate& update);
    void prune_hidden_trackers(Timestamp now);
};

} // namespace microstructure
