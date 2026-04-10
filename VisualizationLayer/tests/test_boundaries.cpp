#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "microstructure/event.hpp"
#include "microstructure/types.hpp"
#include "visualization/frame.hpp"
#include "visualization/frame_capture.hpp"
#include "visualization/html_exporter.hpp"
#include "visualization/json_serializer.hpp"
#include "visualization/replay_walker.hpp"
#include "visualization/terminal_renderer.hpp"

using microstructure::Event;
using microstructure::EventType;
using microstructure::Side;
using microstructure::Venue;
using visualization::FrameCapture;
using visualization::HtmlExporter;
using visualization::JsonSerializer;
using visualization::ReplayWalker;
using visualization::TerminalRenderer;
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
    if (!(a == b)) throw TestFailure(msg + " (mismatch)");
}

bool contains(const std::string& h, const std::string& n) {
    return h.find(n) != std::string::npos;
}

static Event make_ev(microstructure::EventId   eid,
                     EventType                  et,
                     microstructure::OrderId    oid,
                     microstructure::Price      px,
                     microstructure::Quantity   sz,
                     Side                       sd,
                     microstructure::Timestamp  ts)
{
    return Event{eid, et, oid, px, sz, sd, ts, ts + 1, ts + 2, Venue::Nasdaq};
}

// ── empty_event_capture ───────────────────────────────────────────────────────
// FrameCapture on an empty event sequence must return 0 frames, not crash.
void test_empty_event_capture() {
    std::vector<Event> events;
    FrameCapture capture;
    const auto frames = capture.capture(events);
    expect_eq(frames.size(), std::size_t{0}, "empty events → 0 frames");
}

// ── single_event_capture ──────────────────────────────────────────────────────
// A single event produces exactly one frame; indices are correct.
void test_single_event_capture() {
    std::vector<Event> events;
    events.push_back(make_ev(1, EventType::Add, 101, 100, 200, Side::Bid, 1'000'000'000LL));
    FrameCapture capture;
    const auto frames = capture.capture(events);
    expect_eq(frames.size(), std::size_t{1}, "1 event → 1 frame");
    expect_eq(frames[0].frame_index, std::size_t{0}, "frame_index is 0");
    expect_eq(frames[0].event_id, microstructure::EventId{1}, "event_id matches");
}

// ── serializer_empty_sequence ────────────────────────────────────────────────
// serialize_frames on an empty vector must produce exactly "[]".
void test_serializer_empty_sequence() {
    JsonSerializer ser;
    std::vector<VisualizationFrame> frames;
    const std::string json = ser.serialize_frames(frames);
    expect_eq(json, std::string{"[]"}, "empty frames → \"[]\"");
}

// ── html_empty_sequence ───────────────────────────────────────────────────────
// render_html on an empty vector must produce valid HTML containing an empty
// FRAMES array — no crash, no assertion, no empty output.
void test_html_empty_sequence() {
    HtmlExporter exp;
    std::vector<VisualizationFrame> frames;
    const std::string html = exp.render_html(frames);
    expect_true(!html.empty(), "render_html on empty frames is non-empty");
    expect_true(contains(html, "FRAMES=[]"), "FRAMES is empty array in JS");
    expect_true(contains(html, "<!DOCTYPE html>"), "valid HTML doctype present");
}

// ── walker_on_empty_throws ────────────────────────────────────────────────────
// ReplayWalker::current() on an empty frame vector must throw.
void test_walker_on_empty_throws() {
    std::vector<VisualizationFrame> frames;
    ReplayWalker walker{frames};
    bool threw = false;
    try {
        (void)walker.current();
    } catch (const visualization::WalkerError&) {
        threw = true;
    } catch (const std::exception&) {
        threw = true;
    }
    expect_true(threw, "walker.current() on empty frames must throw");
}

// ── walker_jump_oob ───────────────────────────────────────────────────────────
// jump_to() with out-of-bounds index must return false and leave walker unchanged.
void test_walker_jump_oob() {
    std::vector<Event> evs;
    evs.push_back(make_ev(1, EventType::Add, 101, 100, 200, Side::Bid, 1'000));
    evs.push_back(make_ev(2, EventType::Add, 201, 101, 150, Side::Ask, 2'000));
    const auto frames = FrameCapture{}.capture(evs);

    ReplayWalker walker{frames};
    expect_eq(walker.current_index(), std::size_t{0}, "starts at 0");

    const bool ok = walker.jump_to(9999);
    expect_true(!ok, "jump_to OOB returns false");
    expect_eq(walker.current_index(), std::size_t{0}, "index unchanged after OOB jump");
}

// ── terminal_renderer_one_sided_no_crash ─────────────────────────────────────
// print_frame on a one-sided (bid-only) frame must not crash or print "inf"/"nan".
void test_terminal_one_sided_no_crash() {
    std::vector<Event> evs;
    // Only one bid — produces a bid-only book in frame 0
    evs.push_back(make_ev(1, EventType::Add, 101, 100, 500, Side::Bid, 1'000));
    const auto frames = FrameCapture{}.capture(evs);
    expect_eq(frames.size(), std::size_t{1}, "1 event 1 frame");

    TerminalRenderer r;
    std::string out;
    try {
        out = r.render_frame(frames[0]);
    } catch (...) {
        throw TestFailure("render_frame threw on one-sided frame");
    }
    expect_true(!out.empty(), "render_frame output non-empty");
    expect_true(!contains(out, "inf"), "no 'inf' in one-sided terminal output");
    expect_true(!contains(out, "nan"), "no 'nan' in one-sided terminal output");
}

// ── large_sequence_no_crash ───────────────────────────────────────────────────
// 10 000 synthetic events through the full capture pipeline must not crash or
// produce obviously wrong frame counts.
void test_large_sequence_no_crash() {
    std::vector<Event> evs;
    evs.reserve(10'000);

    microstructure::Timestamp ts = 1'000'000'000LL;
    // Alternating bid/ask adds
    for (int i = 1; i <= 10'000; ++i) {
        const bool is_bid = (i % 2 == 1);
        const microstructure::Price  px  = is_bid ? 1000 : 1001;
        const microstructure::Side   sd  = is_bid ? Side::Bid : Side::Ask;
        evs.push_back(make_ev(static_cast<microstructure::EventId>(i),
                              EventType::Add,
                              static_cast<microstructure::OrderId>(i),
                              px, 100, sd, ts));
        ts += 1'000LL;
    }

    FrameCapture capture;
    const auto frames = capture.capture(evs);
    expect_eq(frames.size(), std::size_t{10'000}, "large sequence: frame count matches");
    expect_eq(frames.back().frame_index, std::size_t{9'999}, "last frame index correct");
}

// ── Dispatch ──────────────────────────────────────────────────────────────────

int run_test(const std::string& name) {
    try {
        if      (name == "empty_event_capture")          test_empty_event_capture();
        else if (name == "single_event_capture")         test_single_event_capture();
        else if (name == "serializer_empty_sequence")    test_serializer_empty_sequence();
        else if (name == "html_empty_sequence")          test_html_empty_sequence();
        else if (name == "walker_on_empty_throws")       test_walker_on_empty_throws();
        else if (name == "walker_jump_oob")              test_walker_jump_oob();
        else if (name == "terminal_one_sided_no_crash")  test_terminal_one_sided_no_crash();
        else if (name == "large_sequence_no_crash")      test_large_sequence_no_crash();
        else {
            std::cerr << "UNKNOWN TEST: " << name << "\n";
            return 1;
        }
        std::cout << "PASS: " << name << "\n";
        return 0;
    } catch (const TestFailure& ex) {
        std::cerr << "FAIL: " << name << " \xe2\x80\x94 " << ex.what() << "\n";
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "ERROR: " << name << " threw: " << ex.what() << "\n";
        return 1;
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: viz_test_boundaries <test_name>\n";
        return 1;
    }
    return run_test(std::string{argv[1]});
}
