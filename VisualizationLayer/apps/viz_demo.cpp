#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "microstructure/event.hpp"
#include "microstructure/types.hpp"
#include "visualization/frame_capture.hpp"
#include "visualization/html_exporter.hpp"
#include "visualization/replay_walker.hpp"
#include "visualization/realistic_synthetic_generator.hpp"
#include "visualization/synthetic_event_generator.hpp"
#include "visualization/terminal_renderer.hpp"

using microstructure::Event;
using microstructure::EventType;
using microstructure::Side;
using microstructure::Timestamp;
using microstructure::Venue;

namespace {

// Build a synthetic but realistic market replay scenario demonstrating:
//   - initial book build (adds on both sides)
//   - depth across multiple price levels
//   - a cancel
//   - a trade execution
//   - a modify
//   - post-trade re-quote
std::vector<Event> make_demo_events() {
    std::vector<Event> evs;
    Timestamp          ts = 1'000'000'000LL; // 1 s in ns
    auto               e  = [&](microstructure::EventId   eid,
                                EventType                  et,
                                microstructure::OrderId    oid,
                                microstructure::Price      px,
                                microstructure::Quantity   sz,
                                Side                       sd) {
        evs.emplace_back(eid, et, oid, px, sz, sd, ts, ts + 1, ts + 2, Venue::Nasdaq);
        ts += 1'000'000LL; // 1 ms between events
    };

    // ── Build initial bid ladder (100 → 97) ──────────────────────────────
    e(1, EventType::Add, 101, 100, 500, Side::Bid);
    e(2, EventType::Add, 102, 100, 300, Side::Bid); // second order at same level
    e(3, EventType::Add, 103,  99, 400, Side::Bid);
    e(4, EventType::Add, 104,  98, 600, Side::Bid);
    e(5, EventType::Add, 105,  97, 200, Side::Bid);

    // ── Build initial ask ladder (101 → 104) ─────────────────────────────
    e(6,  EventType::Add, 201, 101, 450, Side::Ask);
    e(7,  EventType::Add, 202, 101, 250, Side::Ask); // second order at same level
    e(8,  EventType::Add, 203, 102, 350, Side::Ask);
    e(9,  EventType::Add, 204, 103, 500, Side::Ask);
    e(10, EventType::Add, 205, 104, 150, Side::Ask);

    // ── Cancel one bid ────────────────────────────────────────────────────
    e(11, EventType::Cancel, 103, 99, 400, Side::Bid);

    // ── Modify ask volume at 101 (reduce order 201: 450→200) ──────────────
    e(12, EventType::Modify, 201, 101, 200, Side::Ask);

    // ── Trade: seller hits best bid at 100 (order 101 is front of queue) ──
    // order_id=101, price=100, size=300, resting side=Bid
    e(13, EventType::Trade, 101, 100, 300, Side::Bid);

    // ── Post-trade: refresh bid ────────────────────────────────────────────
    e(14, EventType::Add, 106, 100, 250, Side::Bid);

    // ── Add deeper bid ─────────────────────────────────────────────────────
    e(15, EventType::Add, 107, 96, 800, Side::Bid);

    // ── Buyer lifts best ask at 101: trade against order 201 (front) ──────
    // order 201 has 200 shares after the earlier Modify
    e(16, EventType::Trade, 201, 101, 200, Side::Ask);

    // ── After clearing order 201, order 202 is now front of ask@101 ───────
    // Partial fill of order 202 (250 shares, take 100)
    e(17, EventType::Trade, 202, 101, 100, Side::Ask);

    // ── Post-trade ask refresh and book widening ───────────────────────────
    e(18, EventType::Add, 206, 101, 400, Side::Ask);
    e(19, EventType::Cancel, 102, 100, 300, Side::Bid);
    e(20, EventType::Add,    108, 99, 350, Side::Bid);

    return evs;
}

} // namespace

// Parse a named integer flag, e.g. "--synthetic 500".  Returns 0 if absent.
std::size_t parse_flag_count(int argc, char** argv, const char* flag) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string{argv[i]} == flag) {
            try {
                const long n = std::stol(argv[i + 1]);
                if (n <= 0) {
                    std::cerr << "WARN: " << flag << " requires a positive integer\n";
                    return 0;
                }
                return static_cast<std::size_t>(n);
            } catch (const std::exception&) {
                std::cerr << "WARN: " << flag << " requires an integer argument\n";
                return 0;
            }
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    // Output path: first non-flag positional argument (default: replay.html)
    std::string output_path = "replay.html";
    for (int i = 1; i < argc; ++i) {
        const std::string arg{argv[i]};
        const std::string prev = (i > 1) ? std::string{argv[i-1]} : "";
        const bool is_flag_value = (prev == "--synthetic" || prev == "--realistic");
        if (!is_flag_value && arg[0] != '-') { output_path = arg; break; }
    }

    const std::size_t synthetic_count = parse_flag_count(argc, argv, "--synthetic");
    const std::size_t realistic_count = parse_flag_count(argc, argv, "--realistic");

    std::cout << "MicrostructureEngine Visualization Demo\n";
    std::cout << std::string(42, '=') << "\n\n";

    // ── Build event sequence ──────────────────────────────────────────────
    std::vector<Event> events;
    if (realistic_count > 0) {
        visualization::RealisticSyntheticConfig cfg;
        cfg.total_events = realistic_count;
        visualization::RealisticSyntheticGenerator gen{cfg};
        events = gen.generate();
        std::cout << "Generated " << events.size() << " realistic synthetic events.\n";
    } else if (synthetic_count > 0) {
        visualization::SyntheticConfig cfg;
        cfg.total_events = synthetic_count;
        visualization::SyntheticEventGenerator gen{cfg};
        events = gen.generate();
        std::cout << "Generated " << events.size() << " synthetic events.\n";
    } else {
        events = make_demo_events();
    }

    // ── Capture frames ────────────────────────────────────────────────────
    std::cout << "Replaying " << events.size() << " events...\n";

    visualization::FrameCapture capture;
    std::vector<visualization::VisualizationFrame> frames;
    try {
        frames = capture.capture(events);
    } catch (const std::exception& ex) {
        std::cerr << "ERROR: capture failed: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "Captured " << frames.size() << " frames.\n\n";

    // ── Terminal: print first and last 3 frames ───────────────────────────
    visualization::TerminalRenderer renderer;

    const std::size_t preview = std::min(std::size_t{3}, frames.size());
    std::cout << "=== First " << preview << " frames ===\n";
    for (std::size_t i = 0; i < preview; ++i) {
        renderer.print_frame(frames[i]);
    }

    if (frames.size() > preview * 2) {
        std::cout << "... (" << (frames.size() - preview * 2) << " frames omitted) ...\n\n";
    }

    if (frames.size() > preview) {
        std::cout << "=== Last " << preview << " frames ===\n";
        const std::size_t start = frames.size() - preview;
        for (std::size_t i = start; i < frames.size(); ++i) {
            renderer.print_frame(frames[i]);
        }
    }

    // ── ReplayWalker demo ─────────────────────────────────────────────────
    visualization::ReplayWalker walker{frames};
    walker.to_last();
    std::cout << "Final frame (index " << walker.current_index() << "):\n";
    renderer.print_frame(walker.current());

    // ── Export HTML ───────────────────────────────────────────────────────
    visualization::HtmlExporter exporter;
    std::cout << "Exporting HTML to: " << output_path << " ...\n";
    if (!exporter.write_html(frames, output_path)) {
        std::cerr << "ERROR: failed to write " << output_path << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "Done. Open " << output_path << " in a browser.\n";

    return EXIT_SUCCESS;
}
