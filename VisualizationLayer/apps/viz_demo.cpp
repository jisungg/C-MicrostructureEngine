#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "microstructure/event.hpp"
#include "microstructure/types.hpp"
#include "visualization/event_loader.hpp"
#include "visualization/frame_capture.hpp"
#include "visualization/html_exporter.hpp"
#include "visualization/replay_walker.hpp"
#include "visualization/realistic_synthetic_generator.hpp"
#include "visualization/session.hpp"
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

// Parse a named string flag, e.g. "--from-csv data.csv".  Returns "" if absent.
std::string parse_flag_string(int argc, char** argv, const char* flag) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string{argv[i]} == flag) {
            return std::string{argv[i + 1]};
        }
    }
    return {};
}

int main(int argc, char** argv) {
    // Output path: first non-flag positional argument (default: replay.html)
    std::string output_path = "replay.html";
    for (int i = 1; i < argc; ++i) {
        const std::string arg{argv[i]};
        const std::string prev = (i > 1) ? std::string{argv[i-1]} : "";
        const bool is_flag_value = (prev == "--synthetic" || prev == "--realistic"
                                    || prev == "--from-csv" || prev == "--session");
        if (!is_flag_value && arg[0] != '-') { output_path = arg; break; }
    }

    const std::size_t synthetic_count = parse_flag_count(argc, argv, "--synthetic");
    const std::size_t realistic_count = parse_flag_count(argc, argv, "--realistic");
    const std::string csv_path        = parse_flag_string(argc, argv, "--from-csv");
    const std::string session_path    = parse_flag_string(argc, argv, "--session");

    // Reject conflicting source flags.
    const int source_count = (csv_path.empty() ? 0 : 1)
                           + (synthetic_count > 0 ? 1 : 0)
                           + (realistic_count > 0 ? 1 : 0);
    if (source_count > 1) {
        std::cerr << "ERROR: conflicting source flags: specify at most one of "
                     "--from-csv, --synthetic, or --realistic\n";
        return EXIT_FAILURE;
    }

    // ── Load existing session (if requested) ──────────────────────────────
    if (!session_path.empty()) {
        try {
            const auto prev = visualization::VisualizationSession::load(session_path);
            // Print prior session metadata so analysts know what state was last saved.
            std::cout << "Previous session (" << session_path << "):\n"
                      << "  frame_count:   " << prev.frame_count << "\n"
                      << "  current_frame: " << prev.current_frame << "\n"
                      << "  active_filter: "
                      << (prev.active_filter.empty() ? "ALL" : prev.active_filter) << "\n"
                      << "  bookmarks:     " << prev.bookmarks.size() << "\n\n";
        } catch (const std::exception& ex) {
            std::cout << "No previous session at " << session_path
                      << " (will create): " << ex.what() << "\n\n";
        }
    }

    std::cout << "MicrostructureEngine Visualization Demo\n";
    std::cout << std::string(42, '=') << "\n\n";

    // ── Build event sequence ──────────────────────────────────────────────
    std::vector<Event> events;
    if (!csv_path.empty()) {
        visualization::CsvEventLoader loader{csv_path};
        try {
            events = loader.load();
        } catch (const std::exception& ex) {
            std::cerr << "ERROR: " << ex.what() << "\n";
            return EXIT_FAILURE;
        }
        std::cout << "Loaded " << events.size() << " events from "
                  << loader.source_description() << ".\n";
    } else if (realistic_count > 0) {
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

    // ── Save session metadata ──────────────────────────────────────────────
    // Determine session file path: explicit --session flag or <output>.session.
    const std::string sess_file = !session_path.empty()
        ? session_path
        : output_path + ".session";

    visualization::VisualizationSession sess;
    sess.source_path   = csv_path;
    sess.source_mode   = !csv_path.empty()        ? visualization::SourceMode::Csv
                       : realistic_count > 0      ? visualization::SourceMode::Realistic
                       : synthetic_count > 0      ? visualization::SourceMode::Synthetic
                                                  : visualization::SourceMode::Demo;
    sess.frame_count   = frames.size();
    sess.first_ts      = frames.empty() ? 0 : frames.front().exchange_timestamp;
    sess.last_ts       = frames.empty() ? 0 : frames.back().exchange_timestamp;
    sess.current_frame = 0;       // viewer always starts at frame 0
    sess.active_filter = "ALL";
    // bookmarks are managed by the browser viewer (localStorage); C++ side starts empty.

    if (sess.save(sess_file)) {
        std::cout << "Session saved to: " << sess_file << "\n";
    } else {
        std::cerr << "WARN: could not write session file: " << sess_file << "\n";
    }

    return EXIT_SUCCESS;
}
