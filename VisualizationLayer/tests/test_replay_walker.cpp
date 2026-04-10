#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "visualization/frame.hpp"
#include "visualization/replay_walker.hpp"

using visualization::ReplayWalker;
using visualization::VisualizationFrame;
using visualization::WalkerError;

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

// Build N synthetic frames with sequential frame_index values.
std::vector<VisualizationFrame> make_frames(std::size_t n) {
    std::vector<VisualizationFrame> frames;
    frames.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        VisualizationFrame f;
        f.frame_index = i;
        f.event_id    = static_cast<microstructure::EventId>(i + 1);
        frames.push_back(f);
    }
    return frames;
}

// ── Tests ──────────────────────────────────────────────────────────────────

void test_walker_next_prev() {
    const auto frames = make_frames(5);
    ReplayWalker w{frames};

    expect_eq(w.current_index(), std::size_t{0}, "starts at 0");
    expect_true(w.next(), "next from 0");
    expect_eq(w.current_index(), std::size_t{1}, "at 1 after next");
    expect_true(w.next(), "next from 1");
    expect_true(w.next(), "next from 2");
    expect_true(w.next(), "next from 3");
    expect_true(!w.next(), "no next from end (4)");
    expect_eq(w.current_index(), std::size_t{4}, "still at 4");

    expect_true(w.prev(), "prev from 4");
    expect_eq(w.current_index(), std::size_t{3}, "at 3");
    expect_true(w.prev(), "prev from 3");
    expect_true(w.prev(), "prev from 2");
    expect_true(w.prev(), "prev from 1");
    expect_true(!w.prev(), "no prev from 0");
    expect_eq(w.current_index(), std::size_t{0}, "still at 0");
}

void test_walker_at_boundaries() {
    const auto frames = make_frames(3);
    ReplayWalker w{frames};

    expect_true(w.at_start(), "at_start at index 0");
    expect_true(!w.at_end(), "not at_end at index 0");

    w.to_last();
    expect_true(!w.at_start(), "not at_start at last");
    expect_true(w.at_end(), "at_end at last");

    // Single-frame sequence
    const auto one = make_frames(1);
    ReplayWalker wo{one};
    expect_true(wo.at_start(), "single: at_start");
    expect_true(wo.at_end(), "single: also at_end");
}

void test_walker_jump_to() {
    const auto frames = make_frames(10);
    ReplayWalker w{frames};

    expect_true(w.jump_to(7), "jump to 7");
    expect_eq(w.current_index(), std::size_t{7}, "at 7");
    expect_eq(w.current().frame_index, std::size_t{7}, "current frame_index == 7");

    expect_true(w.jump_to(0), "jump to 0");
    expect_eq(w.current_index(), std::size_t{0}, "at 0");

    expect_true(!w.jump_to(10), "jump to out-of-range fails");
    expect_eq(w.current_index(), std::size_t{0}, "cursor unchanged after failed jump");
}

void test_walker_empty_guard() {
    const std::vector<VisualizationFrame> empty;
    ReplayWalker w{empty};

    expect_true(!w.has_frames(), "has_frames false");
    expect_eq(w.size(), std::size_t{0}, "size 0");
    expect_true(w.at_start(), "at_start true on empty");
    expect_true(w.at_end(), "at_end true on empty");
    expect_true(!w.next(), "next returns false on empty");
    expect_true(!w.prev(), "prev returns false on empty");
    expect_true(!w.jump_to(0), "jump_to 0 fails on empty");

    bool threw = false;
    try {
        (void)w.current();
    } catch (const WalkerError&) {
        threw = true;
    }
    expect_true(threw, "current() throws on empty");
}

void test_walker_to_first_to_last() {
    const auto frames = make_frames(8);
    ReplayWalker w{frames};

    w.jump_to(4);
    w.to_first();
    expect_eq(w.current_index(), std::size_t{0}, "to_first → 0");

    w.to_last();
    expect_eq(w.current_index(), std::size_t{7}, "to_last → 7");
    expect_eq(w.current().frame_index, std::size_t{7}, "last frame_index == 7");
}

// ── Runner ────────────────────────────────────────────────────────────────

using TestFn = void(*)();
struct TestCase { const char* name; TestFn fn; };

const TestCase TESTS[] = {
    {"walker_next_prev",      test_walker_next_prev},
    {"walker_at_boundaries",  test_walker_at_boundaries},
    {"walker_jump_to",        test_walker_jump_to},
    {"walker_empty_guard",    test_walker_empty_guard},
    {"walker_to_first_to_last", test_walker_to_first_to_last},
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: viz_test_replay_walker <test_case>\n";
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
