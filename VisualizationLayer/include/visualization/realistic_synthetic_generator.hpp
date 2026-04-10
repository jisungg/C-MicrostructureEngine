#pragma once

// RealisticSyntheticGenerator — a state-aware, book-aware synthetic order flow
// model that produces significantly more realistic LOB dynamics than the simple
// uniform-probability SyntheticEventGenerator.
//
// Key realistic properties:
//   - Exponential near-touch price distribution (orders cluster near the spread)
//   - Gaussian random-walk mid price with regime-modulated volatility
//   - Imbalance-weighted aggressor direction for trades
//   - Distance-weighted cancel selection (stale quotes cancelled preferentially)
//   - Markov regime switching (Tight → Normal → Stressed)
//   - Post-trade add boost (market-maker refill behaviour)
//   - Order size increases with depth (institutional vs. market-maker sizing)
//
// Engine compatibility: all generated events pass directly into
// MicrostructurePipeline::process() — no bypasses.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <random>
#include <vector>

#include "microstructure/event.hpp"
#include "microstructure/types.hpp"

namespace visualization {

// Liquidity regime experienced by the synthetic book.
enum class SyntheticRegime { Tight, Normal, Stressed };

// Configuration for the realistic synthetic generator.
// Intensities describe relative event frequencies; they do not need to sum to
// 1.0 — any remainder is assigned to additional Add events.
struct RealisticSyntheticConfig {
    std::uint32_t seed{42};
    std::size_t   total_events{2'000};
    std::size_t   initial_depth_levels{5};

    microstructure::Price    initial_mid_price{1'000};
    microstructure::Price    tick_size{1};
    microstructure::Quantity order_size_min{50};
    microstructure::Quantity order_size_max{2'000};

    // ── Event intensities ─────────────────────────────────────────────────
    // Remainder of [0, 1.0] after these four adds to the Add intensity.
    double add_intensity{0.45};
    double cancel_intensity{0.15};
    double trade_intensity{0.25};
    double modify_intensity{0.10};

    // ── Price distribution ────────────────────────────────────────────────
    // Decay constant λ for the exponential near-touch distribution.
    // P(adding at depth k from touch) ∝ exp(-add_lambda * k)
    // Higher value = more concentrated near the spread.
    double add_lambda{0.7};

    // ── Mid-price dynamics ────────────────────────────────────────────────
    // Standard deviation of per-event mid-price noise, in ticks.
    // At σ=0.4: ~21% of events shift mid ±1 tick.
    double midprice_volatility{0.4};

    // ── Trade / imbalance ─────────────────────────────────────────────────
    // How strongly book imbalance biases aggressor direction [0, 1].
    // 0 = uniform; 1 = fully determined by imbalance.
    double aggressor_imbalance_sensitivity{0.4};

    // ── Cancel staleness ─────────────────────────────────────────────────
    // Exponential sensitivity for distance-weighted cancel selection.
    // Higher = far-from-mid orders cancel much more often.
    double cancel_staleness_sensitivity{0.3};

    // ── Regime switching ─────────────────────────────────────────────────
    // Per-event probability of transitioning out of the current regime.
    double regime_shift_probability{0.005};

    // ── Post-trade refill ─────────────────────────────────────────────────
    // Extra add intensity applied immediately after a trade (decays by 0.08
    // per event).  Simulates market-maker refilling after a fill.
    double post_trade_refill_boost{0.25};

    microstructure::Venue venue{microstructure::Venue::Nasdaq};
};

// Generates a deterministic, state-aware event stream.
// Same RealisticSyntheticConfig + seed → identical events on every call.
class RealisticSyntheticGenerator {
public:
    explicit RealisticSyntheticGenerator(RealisticSyntheticConfig config = {});

    // Generate the full event sequence.  Resets internal state each call.
    [[nodiscard]] std::vector<microstructure::Event> generate();

    // Expose the regime sequence for testing (populated after generate()).
    [[nodiscard]] const std::vector<SyntheticRegime>& regime_trace() const noexcept {
        return regime_trace_;
    }

private:
    // ── Shadow-book order tracking ────────────────────────────────────────
    struct TrackedOrder {
        microstructure::OrderId   order_id;
        microstructure::Price     price;
        microstructure::Quantity  size;
        microstructure::Side      side;
    };

    RealisticSyntheticConfig config_;
    std::mt19937             rng_;

    std::map<microstructure::Price, microstructure::Quantity,
             std::greater<microstructure::Price>> bid_levels_;
    std::map<microstructure::Price, microstructure::Quantity> ask_levels_;
    std::vector<TrackedOrder>  safe_orders_;

    microstructure::OrderId    next_order_id_{1};
    microstructure::EventId    next_event_id_{1};
    microstructure::Timestamp  next_ts_{1'000'000'000LL};
    microstructure::Price      mid_price_{0};

    SyntheticRegime regime_{SyntheticRegime::Normal};
    double          post_trade_add_boost_{0.0};
    std::vector<SyntheticRegime> regime_trace_;  // one per generated event

    void reset_state();

    microstructure::Event emit(microstructure::EventType  et,
                               microstructure::OrderId    oid,
                               microstructure::Price      px,
                               microstructure::Quantity   sz,
                               microstructure::Side       sd);

    // ── Per-event state updates ───────────────────────────────────────────
    void maybe_shift_regime();
    void evolve_mid_price();

    // ── Sampling helpers ──────────────────────────────────────────────────
    // Sample distance k from touch via geometric(exp(-add_lambda)) distribution.
    [[nodiscard]] std::size_t sample_depth_offset();
    // Imbalance in [-1, +1]: positive = bid-heavy.
    [[nodiscard]] double compute_imbalance() const noexcept;
    // Uniform double in [0, 1).
    [[nodiscard]] double uniform_double() noexcept;

    // ── Event generators ─────────────────────────────────────────────────
    microstructure::Event                generate_add();
    std::optional<microstructure::Event> try_generate_cancel();
    std::optional<microstructure::Event> try_generate_trade();
    std::optional<microstructure::Event> try_generate_modify();
    [[nodiscard]] microstructure::Quantity sample_order_size(std::size_t depth_k);

    // ── Shadow-book helpers ───────────────────────────────────────────────
    [[nodiscard]] std::optional<microstructure::Price> best_bid() const noexcept;
    [[nodiscard]] std::optional<microstructure::Price> best_ask() const noexcept;

    // Regime-modulated intensity multipliers.
    [[nodiscard]] double regime_add_mult()    const noexcept;
    [[nodiscard]] double regime_cancel_mult() const noexcept;
    [[nodiscard]] double regime_trade_mult()  const noexcept;
    [[nodiscard]] double regime_lambda_mult() const noexcept;
};

} // namespace visualization
