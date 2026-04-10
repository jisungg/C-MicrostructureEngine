#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "microstructure/features.hpp"
#include "microstructure/pipeline.hpp"
#include "visualization/frame_capture.hpp"
#include "visualization/html_exporter.hpp"
#include "visualization/json_serializer.hpp"
#include "visualization/synthetic_event_generator.hpp"
#include "visualization/terminal_renderer.hpp"

using visualization::FrameCapture;
using visualization::HtmlExporter;
using visualization::JsonSerializer;
using visualization::SyntheticConfig;
using visualization::SyntheticEventGenerator;
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

// ── synthetic_count_matches ───────────────────────────────────────────────────
// Generated event count must exactly equal config.total_events.
void test_synthetic_count_matches() {
    SyntheticConfig cfg;
    cfg.total_events = 200;
    cfg.seed         = 1;

    SyntheticEventGenerator gen{cfg};
    const auto events = gen.generate();
    expect_eq(events.size(), std::size_t{200}, "event count must equal total_events");
}

// ── synthetic_deterministic ───────────────────────────────────────────────────
// Same config and seed must produce the identical event stream each call.
void test_synthetic_deterministic() {
    SyntheticConfig cfg;
    cfg.total_events = 100;
    cfg.seed         = 42;

    SyntheticEventGenerator gen{cfg};
    const auto run1 = gen.generate();
    const auto run2 = gen.generate();

    expect_eq(run1.size(), run2.size(), "run sizes must match");
    for (std::size_t i = 0; i < run1.size(); ++i) {
        expect_eq(run1[i].event_id(),        run2[i].event_id(),        "event_id mismatch");
        expect_eq(run1[i].event_type(),      run2[i].event_type(),      "event_type mismatch");
        expect_eq(run1[i].order_id(),        run2[i].order_id(),        "order_id mismatch");
        expect_eq(run1[i].price(),           run2[i].price(),           "price mismatch");
        expect_eq(run1[i].size(),            run2[i].size(),            "size mismatch");
        expect_eq(run1[i].side(),            run2[i].side(),            "side mismatch");
        expect_eq(run1[i].exchange_timestamp(), run2[i].exchange_timestamp(), "ts mismatch");
    }
}

// ── synthetic_valid_events ────────────────────────────────────────────────────
// All 500 generated events must pass through the real MicrostructurePipeline.
void test_synthetic_valid_events() {
    SyntheticConfig cfg;
    cfg.total_events = 500;
    cfg.seed         = 42;

    SyntheticEventGenerator gen{cfg};
    const auto events = gen.generate();

    microstructure::MicrostructurePipeline pipeline;
    for (const auto& ev : events) {
        const auto result = pipeline.process(ev);
        (void)result;
    }
}

// ── synthetic_timestamps_monotonic ───────────────────────────────────────────
// exchange_timestamp must be strictly non-decreasing.
void test_synthetic_timestamps_monotonic() {
    SyntheticConfig cfg;
    cfg.total_events = 300;
    cfg.seed         = 7;

    SyntheticEventGenerator gen{cfg};
    const auto events = gen.generate();

    for (std::size_t i = 1; i < events.size(); ++i) {
        expect_true(
            events[i].exchange_timestamp() >= events[i - 1].exchange_timestamp(),
            "timestamps must be non-decreasing");
    }
}

// ── synthetic_unique_event_ids ────────────────────────────────────────────────
// event_id must be unique across all generated events.
void test_synthetic_unique_event_ids() {
    SyntheticConfig cfg;
    cfg.total_events = 300;
    cfg.seed         = 99;

    SyntheticEventGenerator gen{cfg};
    const auto events = gen.generate();

    std::vector<microstructure::EventId> ids;
    ids.reserve(events.size());
    for (const auto& ev : events) ids.push_back(ev.event_id());
    std::sort(ids.begin(), ids.end());
    const bool all_unique = std::adjacent_find(ids.begin(), ids.end()) == ids.end();
    expect_true(all_unique, "event IDs must all be unique");
}

// ── synthetic_stress ──────────────────────────────────────────────────────────
// 10,000 events through the full pipeline + all exporters — no exception.
void test_synthetic_stress() {
    SyntheticConfig cfg;
    cfg.total_events = 10'000;
    cfg.depth_levels = 5;
    cfg.seed         = 42;

    SyntheticEventGenerator gen{cfg};
    const auto events = gen.generate();

    expect_eq(events.size(), std::size_t{10'000}, "stress: event count");

    FrameCapture capture;
    const auto frames = capture.capture(events);
    expect_eq(frames.size(), events.size(), "stress: frame count");

    JsonSerializer   json;
    const std::string json_str = json.serialize_frames(frames);
    expect_true(!json_str.empty(), "stress: JSON must be non-empty");

    HtmlExporter html;
    const std::string html_str = html.render_html(frames);
    expect_true(!html_str.empty(), "stress: HTML must be non-empty");

    TerminalRenderer term;
    const std::string term_str = term.render_frame(frames.back());
    expect_true(!term_str.empty(), "stress: terminal render must be non-empty");
}

// ── synthetic_frame_capture ───────────────────────────────────────────────────
// Synthetic events must produce one frame per event in FrameCapture.
void test_synthetic_frame_capture() {
    SyntheticConfig cfg;
    cfg.total_events = 200;
    cfg.seed         = 13;

    SyntheticEventGenerator gen{cfg};
    const auto events = gen.generate();

    FrameCapture capture;
    const auto frames = capture.capture(events);
    expect_eq(frames.size(), events.size(), "one frame per event");

    for (std::size_t i = 0; i < frames.size(); ++i) {
        expect_eq(frames[i].frame_index, i, "frame_index must be sequential");
    }
}

// ── synthetic_low_mid_price ───────────────────��───────────────────────────────
// Regression: warmup bid prices must be clamped to positive values when
// initial_mid_price is close to zero.  Previously, warmup generated price=0
// or negative prices which the engine rejects with ValidationError.
void test_synthetic_low_mid_price() {
    SyntheticConfig cfg;
    cfg.initial_mid_price = 2;
    cfg.tick_size         = 1;
    cfg.depth_levels      = 3;  // would produce bids at 1, 0, -1 without clamp
    cfg.total_events      = 100;
    cfg.seed              = 5;

    SyntheticEventGenerator gen{cfg};
    const auto events = gen.generate();
    for (const auto& ev : events) {
        expect_true(ev.price() > 0, "all prices must be positive");
    }

    // Must also pass through the engine
    microstructure::MicrostructurePipeline pipeline;
    for (const auto& ev : events) {
        const auto r = pipeline.process(ev);
        (void)r;
    }
}

// ── synthetic_all_seeds_valid ────────────────────────────��────────────────────
// Regression: the generator must produce valid events for a range of seeds,
// exercising different random paths through the event-type selection logic.
void test_synthetic_all_seeds_valid() {
    for (std::uint32_t s = 1; s <= 20; ++s) {
        SyntheticConfig cfg;
        cfg.total_events = 200;
        cfg.seed         = s;

        SyntheticEventGenerator gen{cfg};
        const auto events = gen.generate();
        expect_eq(events.size(), std::size_t{200}, "count must match for seed");

        microstructure::MicrostructurePipeline pipeline;
        for (const auto& ev : events) {
            const auto r = pipeline.process(ev);
            (void)r;
        }
    }
}

// ── synthetic_high_cancel_modify ───────────────────────��─────────────────────
// Regression: generator must fall back to Add when cancel/modify is requested
// but no safe orders are available, without producing an engine violation.
void test_synthetic_high_cancel_modify() {
    SyntheticConfig cfg;
    cfg.add_probability    = 0.20;
    cfg.cancel_probability = 0.35;
    cfg.trade_probability  = 0.10;
    cfg.modify_probability = 0.35;
    cfg.total_events       = 500;
    cfg.seed               = 99;

    SyntheticEventGenerator gen{cfg};
    const auto events = gen.generate();
    expect_eq(events.size(), std::size_t{500}, "count must match");

    microstructure::MicrostructurePipeline pipeline;
    for (const auto& ev : events) {
        const auto r = pipeline.process(ev);
        (void)r;
    }
}

// ── Dispatch ────────────────────────────────────────────────���────────────────

int run_test(const std::string& name) {
    try {
        if (name == "synthetic_count_matches")     { test_synthetic_count_matches();     }
        else if (name == "synthetic_deterministic"){ test_synthetic_deterministic();     }
        else if (name == "synthetic_valid_events") { test_synthetic_valid_events();      }
        else if (name == "synthetic_timestamps_monotonic") { test_synthetic_timestamps_monotonic(); }
        else if (name == "synthetic_unique_event_ids")     { test_synthetic_unique_event_ids();     }
        else if (name == "synthetic_stress")       { test_synthetic_stress();            }
        else if (name == "synthetic_frame_capture"){ test_synthetic_frame_capture();     }
        else if (name == "synthetic_low_mid_price"){ test_synthetic_low_mid_price();     }
        else if (name == "synthetic_all_seeds_valid") { test_synthetic_all_seeds_valid(); }
        else if (name == "synthetic_high_cancel_modify") { test_synthetic_high_cancel_modify(); }
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
        std::cerr << "Usage: viz_test_synthetic_generator <test_name>\n";
        return 1;
    }
    return run_test(std::string{argv[1]});
}
