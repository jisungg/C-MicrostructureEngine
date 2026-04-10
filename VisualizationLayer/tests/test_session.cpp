#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "visualization/session.hpp"

using visualization::SourceMode;
using visualization::VisualizationSession;

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

// ── session_roundtrip ─────────────────────────────────────────────────────────
// to_json / from_json must preserve every field exactly.
void test_session_roundtrip() {
    VisualizationSession s;
    s.source_path   = "data/test.csv";
    s.source_mode   = SourceMode::Csv;
    s.frame_count   = 1000;
    s.first_ts      = 1'000'000'000LL;
    s.last_ts       = 2'000'000'000LL;
    s.current_frame = 42;
    s.active_filter = "TRADE";
    s.bookmarks     = {0, 10, 42, 99};

    const auto json = s.to_json();
    const auto s2   = VisualizationSession::from_json(json);

    expect_eq(s2.source_path,   s.source_path,   "source_path");
    expect_eq(static_cast<int>(s2.source_mode),
              static_cast<int>(s.source_mode),   "source_mode");
    expect_eq(s2.frame_count,   s.frame_count,   "frame_count");
    expect_eq(s2.first_ts,      s.first_ts,      "first_ts");
    expect_eq(s2.last_ts,       s.last_ts,       "last_ts");
    expect_eq(s2.current_frame, s.current_frame, "current_frame");
    expect_eq(s2.active_filter, s.active_filter, "active_filter");
    expect_eq(s2.bookmarks.size(), s.bookmarks.size(), "bookmarks.size");
    for (std::size_t i = 0; i < s.bookmarks.size(); ++i)
        expect_eq(s2.bookmarks[i], s.bookmarks[i], "bookmark[" + std::to_string(i) + "]");
}

// ── session_default_roundtrip ─────────────────────────────────────────────────
// Default-constructed session must serialise and deserialise correctly.
void test_session_default_roundtrip() {
    VisualizationSession s;
    const auto json = s.to_json();
    const auto s2   = VisualizationSession::from_json(json);

    expect_eq(s2.source_path,   std::string{},   "default source_path empty");
    expect_eq(s2.frame_count,   std::size_t{0},  "default frame_count zero");
    expect_eq(s2.current_frame, std::size_t{0},  "default current_frame zero");
    expect_true(s2.bookmarks.empty(),             "default bookmarks empty");
}

// ── session_all_source_modes ──────────────────────────────────────────────────
// All four SourceMode variants survive a JSON round-trip.
void test_session_all_source_modes() {
    const SourceMode modes[] = {
        SourceMode::Demo, SourceMode::Synthetic,
        SourceMode::Realistic, SourceMode::Csv
    };
    for (auto m : modes) {
        VisualizationSession s;
        s.source_mode = m;
        const auto json = s.to_json();
        const auto s2   = VisualizationSession::from_json(json);
        expect_eq(static_cast<int>(s2.source_mode), static_cast<int>(m),
                  "source_mode round-trip");
    }
}

// ── session_matches_all_filter ────────────────────────────────────────────────
// Empty or "ALL" filter always returns true for any event type.
void test_session_matches_all_filter() {
    VisualizationSession s;
    s.active_filter = "ALL";
    expect_true(s.matches("ADD"),      "ALL matches ADD");
    expect_true(s.matches("TRADE"),    "ALL matches TRADE");
    expect_true(s.matches("CANCEL"),   "ALL matches CANCEL");
    expect_true(s.matches("MODIFY"),   "ALL matches MODIFY");
    expect_true(s.matches("SNAPSHOT"), "ALL matches SNAPSHOT");

    s.active_filter = "";
    expect_true(s.matches("ADD"),   "empty filter matches ADD");
    expect_true(s.matches("TRADE"), "empty filter matches TRADE");
}

// ── session_matches_specific_filter ──────────────────────────────────────────
// Specific filter only returns true for the matching event type.
void test_session_matches_specific_filter() {
    VisualizationSession s;
    s.active_filter = "TRADE";
    expect_true( s.matches("TRADE"),  "TRADE filter matches TRADE");
    expect_true(!s.matches("ADD"),    "TRADE filter rejects ADD");
    expect_true(!s.matches("CANCEL"), "TRADE filter rejects CANCEL");

    s.active_filter = "ADD";
    expect_true( s.matches("ADD"),   "ADD filter matches ADD");
    expect_true(!s.matches("TRADE"), "ADD filter rejects TRADE");
}

// ── session_save_load ─────────────────────────────────────────────────────────
// Save to a temp file and load back; all fields must match.
void test_session_save_load() {
    VisualizationSession s;
    s.source_path   = "replay_data.csv";
    s.source_mode   = SourceMode::Realistic;
    s.frame_count   = 500;
    s.first_ts      = 9'000'000'000LL;
    s.last_ts       = 9'500'000'000LL;
    s.current_frame = 77;
    s.active_filter = "CANCEL";
    s.bookmarks     = {3, 50, 77};

    const std::string tmp = "/tmp/viz_test_session_save_load.json";
    expect_true(s.save(tmp), "save returns true");

    const auto s2 = VisualizationSession::load(tmp);
    expect_eq(s2.source_path,   s.source_path,   "save/load source_path");
    expect_eq(static_cast<int>(s2.source_mode),
              static_cast<int>(s.source_mode),   "save/load source_mode");
    expect_eq(s2.frame_count,   s.frame_count,   "save/load frame_count");
    expect_eq(s2.first_ts,      s.first_ts,      "save/load first_ts");
    expect_eq(s2.last_ts,       s.last_ts,       "save/load last_ts");
    expect_eq(s2.current_frame, s.current_frame, "save/load current_frame");
    expect_eq(s2.active_filter, s.active_filter, "save/load active_filter");
    expect_eq(s2.bookmarks.size(), s.bookmarks.size(), "save/load bookmarks.size");

    std::remove(tmp.c_str());
}

// ── session_load_bad_file ─────────────────────────────────────────────────────
// load() on a non-existent file must throw.
void test_session_load_bad_file() {
    bool threw = false;
    try {
        (void)VisualizationSession::load("/tmp/this_file_does_not_exist_viz_test.json");
    } catch (const std::exception&) {
        threw = true;
    }
    expect_true(threw, "load non-existent file must throw");
}

// ── session_from_json_bad_input ───────────────────────────────────────────────
// from_json on garbage must throw rather than silently succeed.
void test_session_from_json_bad_input() {
    bool threw = false;
    try {
        (void)VisualizationSession::from_json("this is not json");
    } catch (const std::exception&) {
        threw = true;
    }
    expect_true(threw, "from_json on garbage must throw");
}

// ── session_json_contains_keys ────────────────────────────────────────────────
// The JSON emitted by to_json() must contain all expected keys.
void test_session_json_contains_keys() {
    VisualizationSession s;
    s.source_path   = "x.csv";
    s.active_filter = "ADD";
    s.bookmarks     = {1, 2, 3};
    const auto json = s.to_json();

    expect_true(contains(json, "\"source_path\""),   "json has source_path");
    expect_true(contains(json, "\"source_mode\""),   "json has source_mode");
    expect_true(contains(json, "\"frame_count\""),   "json has frame_count");
    expect_true(contains(json, "\"first_ts\""),      "json has first_ts");
    expect_true(contains(json, "\"last_ts\""),       "json has last_ts");
    expect_true(contains(json, "\"current_frame\""), "json has current_frame");
    expect_true(contains(json, "\"active_filter\""), "json has active_filter");
    expect_true(contains(json, "\"bookmarks\""),     "json has bookmarks");
}

// ── session_bookmark_persistence ─────────────────────────────────────────────
// Empty and non-empty bookmark lists survive a round-trip without corruption.
void test_session_bookmark_persistence() {
    // Empty bookmarks
    {
        VisualizationSession s;
        s.bookmarks = {};
        const auto s2 = VisualizationSession::from_json(s.to_json());
        expect_true(s2.bookmarks.empty(), "empty bookmarks round-trip");
    }
    // Single bookmark
    {
        VisualizationSession s;
        s.bookmarks = {42};
        const auto s2 = VisualizationSession::from_json(s.to_json());
        expect_eq(s2.bookmarks.size(), std::size_t{1}, "single bookmark count");
        expect_eq(s2.bookmarks[0], std::size_t{42}, "single bookmark value");
    }
    // Many bookmarks
    {
        VisualizationSession s;
        for (std::size_t i = 0; i < 50; ++i) s.bookmarks.push_back(i * 10);
        const auto s2 = VisualizationSession::from_json(s.to_json());
        expect_eq(s2.bookmarks.size(), std::size_t{50}, "50 bookmarks count");
        for (std::size_t i = 0; i < 50; ++i)
            expect_eq(s2.bookmarks[i], i * 10, "bookmark[" + std::to_string(i) + "]");
    }
}

// ── Dispatch ──────────────────────────────────────────────────────────────────

int run_test(const std::string& name) {
    try {
        if      (name == "session_roundtrip")              test_session_roundtrip();
        else if (name == "session_default_roundtrip")      test_session_default_roundtrip();
        else if (name == "session_all_source_modes")       test_session_all_source_modes();
        else if (name == "session_matches_all_filter")     test_session_matches_all_filter();
        else if (name == "session_matches_specific_filter")test_session_matches_specific_filter();
        else if (name == "session_save_load")              test_session_save_load();
        else if (name == "session_load_bad_file")          test_session_load_bad_file();
        else if (name == "session_from_json_bad_input")    test_session_from_json_bad_input();
        else if (name == "session_json_contains_keys")     test_session_json_contains_keys();
        else if (name == "session_bookmark_persistence")   test_session_bookmark_persistence();
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
        std::cerr << "Usage: viz_test_session <test_name>\n";
        return 1;
    }
    return run_test(std::string{argv[1]});
}
