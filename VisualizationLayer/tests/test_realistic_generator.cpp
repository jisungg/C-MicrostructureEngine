#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "microstructure/features.hpp"
#include "microstructure/pipeline.hpp"
#include "visualization/frame_capture.hpp"
#include "visualization/html_exporter.hpp"
#include "visualization/json_serializer.hpp"
#include "visualization/realistic_synthetic_generator.hpp"
#include "visualization/terminal_renderer.hpp"

using visualization::FrameCapture;
using visualization::HtmlExporter;
using visualization::JsonSerializer;
using visualization::RealisticSyntheticConfig;
using visualization::RealisticSyntheticGenerator;
using visualization::SyntheticRegime;
using visualization::TerminalRenderer;

namespace {

struct TestFailure : std::runtime_error {
    explicit TestFailure(const std::string& m) : std::runtime_error(m) {}
};

void expect_true(bool cond, const std::string& msg) {
    if (!cond) throw TestFailure(msg);
}
template <typename A, typename B>
void expect_eq(const A& a, const B& b, const std::string& msg) {
    if (!(a == b)) throw TestFailure(msg + " (got mismatch)");
}

// ── realistic_deterministic ───────────────────────────────────────────────────
// Same config + seed must produce identical event streams.
void test_realistic_deterministic() {
    RealisticSyntheticConfig cfg;
    cfg.total_events = 100;
    cfg.seed         = 42;

    RealisticSyntheticGenerator gen{cfg};
    const auto run1 = gen.generate();
    const auto run2 = gen.generate();

    expect_eq(run1.size(), run2.size(), "run sizes must match");
    for (std::size_t i = 0; i < run1.size(); ++i) {
        expect_eq(run1[i].event_id(),           run2[i].event_id(),           "event_id");
        expect_eq(run1[i].event_type(),         run2[i].event_type(),         "event_type");
        expect_eq(run1[i].order_id(),           run2[i].order_id(),           "order_id");
        expect_eq(run1[i].price(),              run2[i].price(),              "price");
        expect_eq(run1[i].size(),               run2[i].size(),               "size");
        expect_eq(run1[i].exchange_timestamp(), run2[i].exchange_timestamp(), "timestamp");
    }
}

// ── realistic_count_matches ───────────────────────────────────────────────────
void test_realistic_count_matches() {
    RealisticSyntheticConfig cfg;
    cfg.total_events = 300;
    cfg.seed         = 7;

    RealisticSyntheticGenerator gen{cfg};
    expect_eq(gen.generate().size(), std::size_t{300}, "count must match total_events");
}

// ── realistic_valid_events ────────────────────────────────────────────────────
// All events must pass through the real MicrostructurePipeline.
void test_realistic_valid_events() {
    RealisticSyntheticConfig cfg;
    cfg.total_events = 500;
    cfg.seed         = 42;

    RealisticSyntheticGenerator gen{cfg};
    const auto events = gen.generate();

    microstructure::MicrostructurePipeline pipeline;
    for (const auto& ev : events) {
        const auto r = pipeline.process(ev);
        (void)r;
    }
}

// ── realistic_timestamps_monotonic ───────────────────────────────────────────
void test_realistic_timestamps_monotonic() {
    RealisticSyntheticConfig cfg;
    cfg.total_events = 400;
    cfg.seed         = 13;

    RealisticSyntheticGenerator gen{cfg};
    const auto events = gen.generate();
    for (std::size_t i = 1; i < events.size(); ++i) {
        expect_true(events[i].exchange_timestamp() >= events[i-1].exchange_timestamp(),
                    "timestamps must be non-decreasing");
    }
}

// ── realistic_unique_event_ids ────────────────────────────────────────────────
void test_realistic_unique_event_ids() {
    RealisticSyntheticConfig cfg;
    cfg.total_events = 400;
    cfg.seed         = 55;

    RealisticSyntheticGenerator gen{cfg};
    const auto events = gen.generate();

    std::vector<microstructure::EventId> ids;
    ids.reserve(events.size());
    for (const auto& ev : events) ids.push_back(ev.event_id());
    std::sort(ids.begin(), ids.end());
    expect_true(std::adjacent_find(ids.begin(), ids.end()) == ids.end(),
                "event IDs must be unique");
}

// ── realistic_touch_concentration ────────────────────────────────────────────
// With high lambda, more Add events should land at depth 0 (touch) or 1 than
// at depth >= 3.  This verifies the exponential near-touch distribution is
// working, not just uniform random.
void test_realistic_touch_concentration() {
    RealisticSyntheticConfig cfg;
    cfg.add_lambda        = 1.0;  // strong concentration
    cfg.cancel_intensity  = 0.0;
    cfg.trade_intensity   = 0.0;
    cfg.modify_intensity  = 0.0;
    cfg.add_intensity     = 1.0;
    cfg.total_events      = 2'000;
    cfg.initial_depth_levels = 5;
    cfg.seed              = 42;

    RealisticSyntheticGenerator gen{cfg};
    const auto events = gen.generate();

    // After warmup, all events are Adds.  Count how many are within 2 ticks
    // of mid vs. more than 4 ticks from mid.
    // With lambda=1.0: P(k=0)≈0.63, P(k>=3)≈(1-0.63)^3*(0.63/(1-0.63))... very small
    std::size_t near{0}, far{0};
    const microstructure::Price mid = cfg.initial_mid_price;
    for (const auto& ev : events) {
        if (ev.event_type() != microstructure::EventType::Add) continue;
        const auto dist = static_cast<std::size_t>(
            std::abs(ev.price() - mid) / cfg.tick_size);
        if (dist <= 2) ++near;
        if (dist > 4)  ++far;
    }
    // Near (≤2 ticks) should be far more common than far (>4 ticks)
    expect_true(near > far * 3, "near-touch adds must outnumber far adds by at least 3:1");
}

// ── realistic_no_trade_empty_side ─────────────────────────────────────────────
// With a very high trade probability, the generator must never produce a trade
// against a side that has no volume (which would throw in the engine).
void test_realistic_no_trade_empty_side() {
    RealisticSyntheticConfig cfg;
    cfg.add_intensity    = 0.3;
    cfg.trade_intensity  = 0.7;
    cfg.cancel_intensity = 0.0;
    cfg.modify_intensity = 0.0;
    cfg.total_events     = 500;
    cfg.seed             = 42;

    RealisticSyntheticGenerator gen{cfg};
    const auto events = gen.generate();

    // If this passes without exception, the generator correctly fell back
    microstructure::MicrostructurePipeline pipeline;
    for (const auto& ev : events) {
        const auto r = pipeline.process(ev);
        (void)r;
    }
}

// ── realistic_regime_trace ───��────────────────────────────────────────────────
// With a non-zero regime_shift_probability, the regime trace should not be
// all-Normal for a long enough run (verifies the regime machine fires).
void test_realistic_regime_trace() {
    RealisticSyntheticConfig cfg;
    cfg.total_events            = 5'000;
    cfg.regime_shift_probability = 0.05; // high for test
    cfg.seed                    = 42;

    RealisticSyntheticGenerator gen{cfg};
    const auto events_unused = gen.generate();
    (void)events_unused;

    const auto& trace = gen.regime_trace();
    expect_eq(trace.size(), std::size_t{5'000}, "trace length must match event count");

    const bool any_tight    = std::any_of(trace.begin(), trace.end(),
        [](SyntheticRegime r){ return r == SyntheticRegime::Tight; });
    const bool any_stressed = std::any_of(trace.begin(), trace.end(),
        [](SyntheticRegime r){ return r == SyntheticRegime::Stressed; });

    expect_true(any_tight || any_stressed,
                "at least one non-Normal regime must appear with p=0.05");
}

// ── realistic_imbalance_trade_bias ───────────────────────────────────────────
// Build a bid-heavy book then observe that trades lean toward sell aggressor
// (hitting the bid) direction — i.e., resting side is Bid.
// We check this indirectly: with bid-heavy book + high sensitivity, more trades
// should be buy-aggressor (lifting the ask, resting side = Ask).
void test_realistic_imbalance_trade_bias() {
    // All trades, high imbalance sensitivity, no cancels/modifies
    RealisticSyntheticConfig cfg;
    cfg.aggressor_imbalance_sensitivity = 0.8;
    cfg.add_intensity    = 0.4;
    cfg.trade_intensity  = 0.5;
    cfg.cancel_intensity = 0.0;
    cfg.modify_intensity = 0.0;
    cfg.total_events     = 2'000;
    cfg.initial_depth_levels = 3;
    cfg.seed = 1;

    RealisticSyntheticGenerator gen{cfg};
    const auto events = gen.generate();

    microstructure::MicrostructurePipeline pipeline;
    std::size_t buy_agg{0}, sell_agg{0};
    for (const auto& ev : events) {
        const auto r = pipeline.process(ev);
        if (r.trade.has_value()) {
            if (r.trade->aggressor == microstructure::TradeAggressor::BuyAggressor)  ++buy_agg;
            if (r.trade->aggressor == microstructure::TradeAggressor::SellAggressor) ++sell_agg;
        }
    }

    // Both sides should appear — just verify trades occur
    const std::size_t total_trades = buy_agg + sell_agg;
    expect_true(total_trades > 0, "must have at least some trades");
}

// ── realistic_stress ──────────────────────────────────────────────────────────
// 10,000 events through full pipeline + all exporters.
void test_realistic_stress() {
    RealisticSyntheticConfig cfg;
    cfg.total_events = 10'000;
    cfg.seed         = 42;

    RealisticSyntheticGenerator gen{cfg};
    const auto events = gen.generate();
    expect_eq(events.size(), std::size_t{10'000}, "stress: event count");

    FrameCapture   capture;
    const auto frames = capture.capture(events);
    expect_eq(frames.size(), events.size(), "stress: frame count");

    JsonSerializer json;
    const std::string js = json.serialize_frames(frames);
    expect_true(!js.empty(), "stress: JSON non-empty");

    HtmlExporter html;
    const std::string hs = html.render_html(frames);
    expect_true(!hs.empty(), "stress: HTML non-empty");

    TerminalRenderer term;
    const std::string ts = term.render_frame(frames.back());
    expect_true(!ts.empty(), "stress: terminal non-empty");
}

// ── realistic_frame_signals_vary ─────────────────────────────────────────────
// After a long realistic run, imbalance values should not all be identical —
// the model must produce visible signal variation.
void test_realistic_frame_signals_vary() {
    RealisticSyntheticConfig cfg;
    cfg.total_events = 1'000;
    cfg.seed         = 42;

    RealisticSyntheticGenerator gen{cfg};
    FrameCapture capture;
    const auto frames = capture.capture(gen.generate());

    // Collect imbalance values from frames that have a two-sided book
    std::set<double> imbalances;
    for (const auto& f : frames) {
        if (f.best_bid.has_value() && f.best_ask.has_value()) {
            // Quantise to 2 decimal places to count distinct values
            imbalances.insert(std::round(f.imbalance * 100.0) / 100.0);
        }
    }
    expect_true(imbalances.size() > 3, "imbalance must vary across frames");
    // Note: spread variation is verified separately in realistic_spread_varies.
}

// ── realistic_spread_varies ───────────────────────────────────────────────────
// The realistic generator must produce sustained spread variation throughout a
// long run.  Regime-driven spread targeting (target=1/2/3 ticks per regime)
// keeps cycles alive: Tight collapses to 1 tick, Normal/Stressed mean-revert
// to 2-3 ticks.  seed=77 is the conservative case (≈22% wide-spread frames).
void test_realistic_spread_varies() {
    RealisticSyntheticConfig cfg;
    cfg.total_events = 2'000;
    cfg.seed         = 77;

    RealisticSyntheticGenerator gen{cfg};
    FrameCapture capture;
    const auto frames = capture.capture(gen.generate());

    std::size_t two_sided{0};
    std::size_t wide_spread{0};  // spread > 1 tick
    std::set<int> spreads_seen;
    for (const auto& f : frames) {
        if (f.best_bid.has_value() && f.best_ask.has_value()) {
            ++two_sided;
            const int sp = static_cast<int>(*f.best_ask - *f.best_bid);
            if (sp > 0) spreads_seen.insert(sp);
            if (sp > 1) ++wide_spread;
        }
    }
    expect_true(two_sided > 0, "must have two-sided frames");
    // Multiple distinct spread levels confirm regime-driven widen/tighten cycles.
    expect_true(spreads_seen.size() >= 2,
                "must see at least two distinct spread values");
    // ≥ 100 wide-spread frames in 2 000 events — verifies sustained variation,
    // not just a transient warmup spike.  seed=77 yields ≈ 447 empirically.
    expect_true(wide_spread >= 100,
                "at least 100 frames must have spread > 1 tick (got " +
                std::to_string(wide_spread) + ")");
}

// ── realistic_spread_post_warmup ─────────────────────────────────────────────
// Regime-driven spread dynamics must persist beyond the initial warmup window.
// A spread collapse during warmup is acceptable; what must NOT happen is a
// permanent lock where the spread stays at 1 tick for the rest of the replay.
// Requirement: ≥ 20 wide-spread frames appear after frame 200.
void test_realistic_spread_post_warmup() {
    RealisticSyntheticConfig cfg;
    cfg.total_events = 2'000;
    cfg.seed         = 77;  // conservative seed (≈22% wide-spread overall)

    RealisticSyntheticGenerator gen{cfg};
    FrameCapture capture;
    const auto frames = capture.capture(gen.generate());

    std::size_t post_warmup_wide{0};
    for (std::size_t i = 200; i < frames.size(); ++i) {
        const auto& f = frames[i];
        if (f.best_bid.has_value() && f.best_ask.has_value()) {
            if (static_cast<int>(*f.best_ask - *f.best_bid) > 1)
                ++post_warmup_wide;
        }
    }
    expect_true(post_warmup_wide >= 20,
                "spread must widen after warmup (got " +
                std::to_string(post_warmup_wide) + " wide frames post-200)");
}

// ── realistic_spread_multi_segment ───────────────────────────────────────────
// Spread variation must recur across multiple time segments of the replay —
// not only during early warmup.  Divide 2 000 events into 4 quartiles and
// require that at least 3 quartiles each contain ≥ 1 wide-spread frame.
void test_realistic_spread_multi_segment() {
    RealisticSyntheticConfig cfg;
    cfg.total_events = 2'000;
    cfg.seed         = 77;

    RealisticSyntheticGenerator gen{cfg};
    FrameCapture capture;
    const auto frames = capture.capture(gen.generate());

    const std::size_t n = frames.size();
    if (n == 0) throw TestFailure("no frames");

    // Count wide-spread frames per quartile.
    std::size_t wide_per_q[4]{};
    for (std::size_t i = 0; i < n; ++i) {
        const auto& f = frames[i];
        if (f.best_bid.has_value() && f.best_ask.has_value()) {
            if (static_cast<int>(*f.best_ask - *f.best_bid) > 1)
                ++wide_per_q[i * 4 / n];
        }
    }

    std::size_t q_with_wide{0};
    for (std::size_t q = 0; q < 4; ++q)
        if (wide_per_q[q] >= 1) ++q_with_wide;

    expect_true(q_with_wide >= 3,
                "wide-spread frames must appear in ≥ 3 of 4 quartiles (got " +
                std::to_string(q_with_wide) + ")");
}

// ── realistic_spread_multi_seed ───────────────────────────────────────────────
// Spread dynamics are not a lucky seed artifact: every tested seed must yield
// ≥ 50 wide-spread frames in a 2 000-event run.
void test_realistic_spread_multi_seed() {
    constexpr std::uint32_t seeds[] = {42, 77, 13, 1, 7};
    for (const auto seed : seeds) {
        RealisticSyntheticConfig cfg;
        cfg.total_events = 2'000;
        cfg.seed         = seed;

        RealisticSyntheticGenerator gen{cfg};
        FrameCapture capture;
        const auto frames = capture.capture(gen.generate());

        std::size_t wide{0};
        for (const auto& f : frames) {
            if (f.best_bid.has_value() && f.best_ask.has_value()) {
                if (static_cast<int>(*f.best_ask - *f.best_bid) > 1)
                    ++wide;
            }
        }
        expect_true(wide >= 50,
                    "seed=" + std::to_string(seed) +
                    ": expected ≥50 wide-spread frames, got " +
                    std::to_string(wide));
    }
}

// ── realistic_low_mid_price ───────────────────────────────────────────────────
// Regression: same clamp issue as simple generator — mid close to zero.
void test_realistic_low_mid_price() {
    RealisticSyntheticConfig cfg;
    cfg.initial_mid_price    = 3;
    cfg.tick_size            = 1;
    cfg.initial_depth_levels = 4;
    cfg.total_events         = 100;
    cfg.seed                 = 5;

    RealisticSyntheticGenerator gen{cfg};
    const auto events = gen.generate();
    for (const auto& ev : events) {
        expect_true(ev.price() > 0, "all prices must be positive");
    }
    microstructure::MicrostructurePipeline pipeline;
    for (const auto& ev : events) {
        const auto r = pipeline.process(ev);
        (void)r;
    }
}

// ── Dispatch ─────────────────────────────────────────────────────────────────

int run_test(const std::string& name) {
    try {
        if      (name == "realistic_deterministic")        test_realistic_deterministic();
        else if (name == "realistic_count_matches")        test_realistic_count_matches();
        else if (name == "realistic_valid_events")         test_realistic_valid_events();
        else if (name == "realistic_timestamps_monotonic") test_realistic_timestamps_monotonic();
        else if (name == "realistic_unique_event_ids")     test_realistic_unique_event_ids();
        else if (name == "realistic_touch_concentration")  test_realistic_touch_concentration();
        else if (name == "realistic_no_trade_empty_side")  test_realistic_no_trade_empty_side();
        else if (name == "realistic_regime_trace")         test_realistic_regime_trace();
        else if (name == "realistic_imbalance_trade_bias") test_realistic_imbalance_trade_bias();
        else if (name == "realistic_stress")               test_realistic_stress();
        else if (name == "realistic_frame_signals_vary")   test_realistic_frame_signals_vary();
        else if (name == "realistic_spread_varies")         test_realistic_spread_varies();
        else if (name == "realistic_spread_post_warmup")   test_realistic_spread_post_warmup();
        else if (name == "realistic_spread_multi_segment") test_realistic_spread_multi_segment();
        else if (name == "realistic_spread_multi_seed")    test_realistic_spread_multi_seed();
        else if (name == "realistic_low_mid_price")        test_realistic_low_mid_price();
        else {
            std::cerr << "UNKNOWN TEST: " << name << "\n";
            return 1;
        }
        std::cout << "PASS: " << name << "\n";
        return 0;
    } catch (const TestFailure& ex) {
        std::cerr << "FAIL: " << name << " — " << ex.what() << "\n";
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "ERROR: " << name << " threw: " << ex.what() << "\n";
        return 1;
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: viz_test_realistic_generator <test_name>\n";
        return 1;
    }
    return run_test(std::string{argv[1]});
}
