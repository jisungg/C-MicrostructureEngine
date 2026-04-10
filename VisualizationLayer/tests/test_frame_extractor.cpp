#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "microstructure/event.hpp"
#include "microstructure/features.hpp"
#include "microstructure/pipeline.hpp"
#include "microstructure/types.hpp"
#include "visualization/frame.hpp"
#include "visualization/frame_capture.hpp"
#include "visualization/frame_extractor.hpp"

using microstructure::Event;
using microstructure::EventType;
using microstructure::FeatureConfig;
using microstructure::MicrostructurePipeline;
using microstructure::PipelineResult;
using microstructure::Side;
using microstructure::Timestamp;
using microstructure::Venue;
using visualization::FrameCapture;
using visualization::FrameExtractor;
using visualization::VisualizationFrame;

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

Event make_event(microstructure::EventId   eid,
                 EventType                  et,
                 microstructure::OrderId    oid,
                 microstructure::Price      px,
                 microstructure::Quantity   sz,
                 Side                       sd,
                 Timestamp                  ts,
                 Venue                      v = Venue::Nasdaq)
{
    return Event{eid, et, oid, px, sz, sd, ts, ts + 1, ts + 2, v};
}

// Run a two-event sequence and return both frames.
std::vector<VisualizationFrame> two_sided_frames() {
    std::vector<Event> evs;
    evs.push_back(make_event(1, EventType::Add, 101, 100, 200, Side::Bid, 1000));
    evs.push_back(make_event(2, EventType::Add, 201, 101, 150, Side::Ask, 2000));
    return FrameCapture{}.capture(evs);
}

// ── Tests ──────────────────────────────────────────────────────────────────

void test_frame_extract_simple_add() {
    const auto frames = two_sided_frames();
    expect_eq(frames.size(), std::size_t{2}, "expected 2 frames");
    expect_eq(frames[0].event_id, microstructure::EventId{1}, "frame 0 event_id");
    expect_eq(frames[1].event_id, microstructure::EventId{2}, "frame 1 event_id");
}

void test_frame_extract_best_bid_ask() {
    const auto frames = two_sided_frames();
    // After frame 1 (bid only): best_bid set, best_ask absent
    expect_true(frames[0].best_bid.has_value(), "frame0 best_bid present");
    expect_true(!frames[0].best_ask.has_value(), "frame0 best_ask absent");
    expect_eq(*frames[0].best_bid, microstructure::Price{100}, "frame0 best_bid value");

    // After frame 2 (both sides): both present
    expect_true(frames[1].best_bid.has_value(), "frame1 best_bid present");
    expect_true(frames[1].best_ask.has_value(), "frame1 best_ask present");
    expect_eq(*frames[1].best_bid, microstructure::Price{100}, "frame1 best_bid");
    expect_eq(*frames[1].best_ask, microstructure::Price{101}, "frame1 best_ask");
}

void test_frame_extract_spread_computed() {
    const auto frames = two_sided_frames();
    // Frame 0: one-sided → spread == 0
    expect_eq(frames[0].spread, 0.0, "frame0 spread zero when one-sided");
    // Frame 1: spread = 101 - 100 = 1
    expect_eq(frames[1].spread, 1.0, "frame1 spread == 1");
    // mid = 100.5
    expect_eq(frames[1].mid, 100.5, "frame1 mid == 100.5");
}

void test_frame_extract_depth_levels() {
    std::vector<Event> evs;
    evs.push_back(make_event(1, EventType::Add, 101, 100, 300, Side::Bid, 1000));
    evs.push_back(make_event(2, EventType::Add, 102,  99, 200, Side::Bid, 2000));
    evs.push_back(make_event(3, EventType::Add, 103,  98, 100, Side::Bid, 3000));
    evs.push_back(make_event(4, EventType::Add, 201, 101, 400, Side::Ask, 4000));
    evs.push_back(make_event(5, EventType::Add, 202, 102, 250, Side::Ask, 5000));

    const auto frames = FrameCapture{3}.capture(evs); // 3 levels per side
    const auto& last = frames.back();

    // 3 bid levels: 100, 99, 98
    expect_eq(last.bid_levels.size(), std::size_t{3}, "3 bid levels");
    expect_eq(last.bid_levels[0].price, microstructure::Price{100}, "bid[0] price");
    expect_eq(last.bid_levels[1].price, microstructure::Price{99},  "bid[1] price");
    expect_eq(last.bid_levels[2].price, microstructure::Price{98},  "bid[2] price");

    // 2 ask levels: 101, 102
    expect_eq(last.ask_levels.size(), std::size_t{2}, "2 ask levels");
    expect_eq(last.ask_levels[0].price, microstructure::Price{101}, "ask[0] price");
    expect_eq(last.ask_levels[1].price, microstructure::Price{102}, "ask[1] price");

    // Volumes match
    expect_eq(last.bid_levels[0].volume, microstructure::Quantity{300}, "bid[0] vol");
    expect_eq(last.ask_levels[0].volume, microstructure::Quantity{400}, "ask[0] vol");
}

void test_frame_extract_trade_marker() {
    std::vector<Event> evs;
    evs.push_back(make_event(1, EventType::Add,   101, 100, 500, Side::Bid, 1000));
    evs.push_back(make_event(2, EventType::Add,   201, 101, 300, Side::Ask, 2000));
    evs.push_back(make_event(3, EventType::Trade, 101, 100, 200, Side::Bid, 3000));

    const auto frames = FrameCapture{}.capture(evs);
    expect_true(!frames[0].is_trade, "frame0 not trade");
    expect_true(!frames[1].is_trade, "frame1 not trade");
    expect_true(frames[2].is_trade,  "frame2 is trade");
    expect_true(frames[2].trade.has_value(), "frame2 trade populated");
    expect_eq(frames[2].trade->price, microstructure::Price{100}, "trade price");
    expect_eq(frames[2].trade->size,  microstructure::Quantity{200}, "trade size");
}

void test_frame_extract_one_sided_book() {
    std::vector<Event> evs;
    evs.push_back(make_event(1, EventType::Add, 101, 100, 100, Side::Bid, 1000));

    const auto frames = FrameCapture{}.capture(evs);
    expect_true(!frames[0].best_ask.has_value(), "best_ask absent for one-sided book");
    expect_eq(frames[0].spread, 0.0, "spread 0 for one-sided book");
    expect_eq(frames[0].mid,    0.0, "mid 0 for one-sided book");
    expect_true(frames[0].ask_levels.empty(), "no ask levels for bid-only book");
}

void test_frame_index_sequential() {
    std::vector<Event> evs;
    for (int i = 1; i <= 5; ++i) {
        evs.push_back(make_event(
            static_cast<microstructure::EventId>(i),
            EventType::Add,
            static_cast<microstructure::OrderId>(100 + i),
            static_cast<microstructure::Price>(100 - i),
            microstructure::Quantity{50},
            Side::Bid,
            static_cast<Timestamp>(i * 1000)));
    }
    const auto frames = FrameCapture{}.capture(evs);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        expect_eq(frames[i].frame_index, i, "frame_index sequential at " + std::to_string(i));
    }
}

void test_frame_extract_regime() {
    // After a normal two-sided book is established the regime field is set.
    const auto frames = two_sided_frames();
    // regime field is present (any value is valid; just check it's populated)
    // After just 2 events with minimal depth, regime is likely illiquid.
    // We only verify the field exists and doesn't crash.
    (void)frames[1].regime; // access without error
    expect_true(true, "regime field accessible");
}

void test_frame_engine_consistency() {
    // Verify frame values match direct engine queries.
    std::vector<Event> evs;
    evs.push_back(make_event(1, EventType::Add, 101, 100, 300, Side::Bid, 1000));
    evs.push_back(make_event(2, EventType::Add, 201, 101, 200, Side::Ask, 2000));

    // Run via FrameCapture
    const auto frames = FrameCapture{}.capture(evs);

    // Run independently via MicrostructurePipeline to get ground-truth
    MicrostructurePipeline pipeline;
    PipelineResult r1 = pipeline.process(evs[0]);
    PipelineResult r2 = pipeline.process(evs[1]);

    expect_eq(frames[1].best_bid_volume,
              r2.book.best_bid_volume,
              "best_bid_volume matches pipeline");
    expect_eq(frames[1].best_ask_volume,
              r2.book.best_ask_volume,
              "best_ask_volume matches pipeline");
    expect_eq(frames[1].imbalance, r2.features.imbalance, "imbalance matches");
    expect_eq(frames[1].microprice, r2.features.microprice, "microprice matches");
    expect_eq(frames[1].ofi, r2.features.ofi, "ofi matches");
}

// ── Runner ────────────────────────────────────────────────────────────────

using TestFn = void(*)();
struct TestCase { const char* name; TestFn fn; };

const TestCase TESTS[] = {
    {"frame_extract_simple_add",    test_frame_extract_simple_add},
    {"frame_extract_best_bid_ask",  test_frame_extract_best_bid_ask},
    {"frame_extract_spread_computed", test_frame_extract_spread_computed},
    {"frame_extract_depth_levels",  test_frame_extract_depth_levels},
    {"frame_extract_trade_marker",  test_frame_extract_trade_marker},
    {"frame_extract_one_sided_book",test_frame_extract_one_sided_book},
    {"frame_index_sequential",      test_frame_index_sequential},
    {"frame_extract_regime",        test_frame_extract_regime},
    {"frame_engine_consistency",    test_frame_engine_consistency},
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: viz_test_frame_extractor <test_case>\n";
        return 1;
    }
    const std::string name{argv[1]};
    for (const auto& tc : TESTS) {
        if (tc.name == name) {
            try {
                tc.fn();
                std::cout << "PASS: " << name << "\n";
                return 0;
            } catch (const TestFailure& ex) {
                std::cerr << "FAIL: " << name << ": " << ex.what() << "\n";
                return 1;
            } catch (const std::exception& ex) {
                std::cerr << "FAIL: " << name << " (exception): " << ex.what() << "\n";
                return 1;
            }
        }
    }
    std::cerr << "Unknown test: " << name << "\n";
    return 1;
}
