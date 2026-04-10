#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "microstructure/event.hpp"
#include "microstructure/types.hpp"
#include "visualization/export_schema.hpp"
#include "visualization/frame.hpp"
#include "visualization/frame_capture.hpp"
#include "visualization/html_exporter.hpp"
#include "visualization/json_serializer.hpp"
#include "visualization/terminal_renderer.hpp"

using microstructure::Event;
using microstructure::EventType;
using microstructure::Side;
using microstructure::Venue;
using visualization::DepthLevel;
using visualization::FrameCapture;
using visualization::HtmlExporter;
using visualization::JsonSerializer;
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

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

Event make_event(microstructure::EventId   eid,
                 EventType                  et,
                 microstructure::OrderId    oid,
                 microstructure::Price      px,
                 microstructure::Quantity   sz,
                 Side                       sd,
                 microstructure::Timestamp  ts)
{
    return Event{eid, et, oid, px, sz, sd, ts, ts + 1, ts + 2, Venue::Nasdaq};
}

// A simple two-sided replay: one bid + one ask.
std::vector<VisualizationFrame> two_sided_frames() {
    std::vector<Event> evs;
    evs.push_back(make_event(1, EventType::Add, 101, 100, 200, Side::Bid, 1000));
    evs.push_back(make_event(2, EventType::Add, 201, 101, 150, Side::Ask, 2000));
    return FrameCapture{}.capture(evs);
}

// A frame with a trade.
std::vector<VisualizationFrame> trade_frames() {
    std::vector<Event> evs;
    evs.push_back(make_event(1, EventType::Add,   101, 100, 500, Side::Bid, 1000));
    evs.push_back(make_event(2, EventType::Add,   201, 101, 300, Side::Ask, 2000));
    evs.push_back(make_event(3, EventType::Trade, 101, 100, 200, Side::Bid, 3000));
    return FrameCapture{}.capture(evs);
}

// ── JSON tests ─────────────────────────────────────────────────────────────

void test_json_schema_version_present() {
    const auto frames = two_sided_frames();
    JsonSerializer ser;
    const std::string json = ser.serialize_frame(frames[1]);
    expect_true(contains(json, "\"schema_version\":1"), "schema_version present");
}

void test_json_deterministic() {
    const auto frames = two_sided_frames();
    JsonSerializer ser;
    const std::string a = ser.serialize_frame(frames[1]);
    const std::string b = ser.serialize_frame(frames[1]);
    expect_eq(a, b, "serialize_frame is deterministic");

    const std::string ca = ser.serialize_frames(frames);
    const std::string cb = ser.serialize_frames(frames);
    expect_eq(ca, cb, "serialize_frames is deterministic");
}

void test_json_optional_null_fields() {
    const auto frames = two_sided_frames();
    JsonSerializer ser;
    // Frame 0: bid only → best_ask is null
    const std::string json0 = ser.serialize_frame(frames[0]);
    expect_true(contains(json0, "\"best_ask\":null"), "best_ask null when absent");
    expect_true(!contains(json0, "\"best_bid\":null"), "best_bid not null when present");

    // Frame 1: no trade → trade is null
    const std::string json1 = ser.serialize_frame(frames[1]);
    expect_true(contains(json1, "\"trade\":null"), "trade null when not a trade");
    expect_true(contains(json1, "\"is_trade\":false"), "is_trade false");
}

void test_json_string_escape() {
    JsonSerializer ser;
    VisualizationFrame f;
    // Force special characters: the escape_string path is exercised via regime,
    // event_type, venue strings.  These are ASCII-safe, so verify the normal path.
    const std::string json = ser.serialize_frame(f);
    // Default frame has EventType::Add → "ADD"
    expect_true(contains(json, "\"event_type\":\"ADD\""), "event_type ADD");
    expect_true(contains(json, "\"venue\":\"NASDAQ\""), "venue NASDAQ");
    expect_true(contains(json, "\"regime\":\"illiquid\""), "regime illiquid default");

    // HTML-safe: < and > should not appear raw in output
    expect_true(!contains(json, "<script>"), "no raw <script> in JSON");
}

void test_json_depth_array() {
    std::vector<Event> evs;
    evs.push_back(make_event(1, EventType::Add, 101, 100, 300, Side::Bid, 1000));
    evs.push_back(make_event(2, EventType::Add, 102,  99, 200, Side::Bid, 2000));
    evs.push_back(make_event(3, EventType::Add, 201, 101, 400, Side::Ask, 3000));
    const auto frames = FrameCapture{2}.capture(evs); // 2 levels
    JsonSerializer ser;
    const std::string json = ser.serialize_frame(frames.back());
    expect_true(contains(json, "\"bid_levels\":"), "bid_levels key present");
    expect_true(contains(json, "\"ask_levels\":"), "ask_levels key present");
    expect_true(contains(json, "\"price\":100"),   "bid level price 100");
    expect_true(contains(json, "\"price\":101"),   "ask level price 101");
    expect_true(contains(json, "\"volume\":"),     "volume field present");
    expect_true(contains(json, "\"queue_depth\":"), "queue_depth field present");
}

void test_json_trade_fields() {
    const auto frames = trade_frames();
    JsonSerializer ser;
    const std::string json = ser.serialize_frame(frames[2]); // trade frame
    expect_true(contains(json, "\"is_trade\":true"), "is_trade true");
    expect_true(!contains(json, "\"trade\":null"), "trade not null");
    expect_true(contains(json, "\"resting_side\":"), "resting_side present");
    expect_true(contains(json, "\"aggressor\":"),    "aggressor present");
    expect_true(contains(json, "\"visible_depth_before\":"), "visible_depth_before present");
    expect_true(contains(json, "\"remaining_at_price_after\":"), "remaining present");
}

// ── Terminal renderer tests ────────────────────────────────────────────────

void test_terminal_render_non_empty() {
    const auto frames = two_sided_frames();
    TerminalRenderer r;
    const std::string out = r.render_frame(frames[1]);
    expect_true(!out.empty(), "render_frame returns non-empty string");
    expect_true(contains(out, "Frame 1"), "contains frame index");
    expect_true(contains(out, "ASK"),     "contains ASK header");
    expect_true(contains(out, "BID"),     "contains BID header");
}

void test_terminal_render_includes_price() {
    const auto frames = two_sided_frames();
    TerminalRenderer r;
    const std::string out = r.render_frame(frames[1]);
    expect_true(contains(out, "100"), "bid price 100 in output");
    expect_true(contains(out, "101"), "ask price 101 in output");
}

// ── HTML exporter tests ────────────────────────────────────────────────────

void test_html_render_non_empty() {
    const auto frames = two_sided_frames();
    HtmlExporter exp;
    const std::string html = exp.render_html(frames);
    expect_true(!html.empty(), "render_html non-empty");
    expect_true(html.size() > 1000, "render_html produces substantial output");
}

void test_html_contains_frame_count() {
    const auto frames = two_sided_frames();
    HtmlExporter exp;
    const std::string html = exp.render_html(frames);
    // The build_html function injects <!-- frame_count:2 -->
    expect_true(contains(html, "frame_count:2"), "frame_count comment present");
}

void test_html_deterministic() {
    const auto frames = two_sided_frames();
    HtmlExporter exp;
    const std::string a = exp.render_html(frames);
    const std::string b = exp.render_html(frames);
    expect_eq(a, b, "render_html is deterministic");
}

void test_html_schema_version_marker() {
    const auto frames = two_sided_frames();
    HtmlExporter exp;
    const std::string html = exp.render_html(frames);
    expect_true(contains(html, "schema_version:1"), "schema_version comment present");
}

void test_html_contains_frames_json() {
    const auto frames = two_sided_frames();
    HtmlExporter exp;
    const std::string html = exp.render_html(frames);
    // The embedded JSON must contain core frame fields.
    expect_true(contains(html, "\"schema_version\":1"), "JSON embedded in HTML");
    expect_true(contains(html, "\"frame_index\":0"),    "frame_index in HTML JSON");
    expect_true(contains(html, "\"event_type\":"),      "event_type in HTML JSON");
    expect_true(contains(html, "FRAMES="),              "JS FRAMES variable assigned");
}

// ── Regression: no inf/nan in JSON or HTML ───────────────────────────────────
// Regression for: depth_ratio = +inf on one-sided books was serialized as
// "inf", producing invalid JSON and broken embedded JavaScript.
// The serializer must emit "null" for any non-finite double.
void test_json_no_inf_or_nan() {
    // Frame 0 of two_sided_frames() has only a bid (one-sided) → depth_ratio=+inf
    const auto frames = two_sided_frames();
    JsonSerializer ser;
    const std::string json0 = ser.serialize_frame(frames[0]);
    expect_true(!contains(json0, ":inf"),       "no raw inf in one-sided frame JSON");
    expect_true(!contains(json0, ":-inf"),      "no raw -inf in one-sided frame JSON");
    expect_true(!contains(json0, ":nan"),       "no raw nan in one-sided frame JSON");
    expect_true(!contains(json0, ": inf"),      "no spaced inf");
    expect_true(!contains(json0, ":Infinity"),  "no Infinity in JSON");
    // depth_ratio for one-sided frame must be null
    expect_true(contains(json0, "\"depth_ratio\":null"),
                "depth_ratio must be null for one-sided book");

    // Full frame array must also be clean
    const std::string json_all = ser.serialize_frames(frames);
    expect_true(!contains(json_all, ":inf"),  "no inf in full frame array");
    expect_true(!contains(json_all, ":nan"),  "no nan in full frame array");
}

void test_html_no_inf_or_nan() {
    const auto frames = two_sided_frames();
    HtmlExporter exp;
    const std::string html = exp.render_html(frames);
    expect_true(!contains(html, ":inf"),      "no raw inf in HTML output");
    expect_true(!contains(html, ":nan"),      "no raw nan in HTML output");
    expect_true(!contains(html, ":Infinity"), "no Infinity in HTML output");
}

// ── Workflow feature presence tests ──────────────────────────────────────────
// These verify that the HTML viewer contains the new analyst workflow elements.

// Jump-to-event-id input is present in the HTML.
void test_html_has_event_id_jump() {
    const auto frames = two_sided_frames();
    HtmlExporter exp;
    const std::string html = exp.render_html(frames);
    expect_true(contains(html, "id=\"jeid\""),         "event_id jump input present");
    expect_true(contains(html, "jumpEventId()"),        "jumpEventId function called");
}

// Bookmark controls are present.
void test_html_has_bookmarks() {
    const auto frames = two_sided_frames();
    HtmlExporter exp;
    const std::string html = exp.render_html(frames);
    expect_true(contains(html, "id=\"bk-btn\""),       "bookmark toggle button present");
    expect_true(contains(html, "id=\"bk-panel\""),     "bookmark panel present");
    expect_true(contains(html, "toggleBookmark()"),     "toggleBookmark function present");
    expect_true(contains(html, "prevBookmark()"),       "prevBookmark function present");
    expect_true(contains(html, "nextBookmark()"),       "nextBookmark function present");
    expect_true(contains(html, "loadBookmarks()"),      "loadBookmarks called at init");
    expect_true(contains(html, "localStorage"),         "localStorage used for persistence");
}

// Search overlay is present.
void test_html_has_search_overlay() {
    const auto frames = two_sided_frames();
    HtmlExporter exp;
    const std::string html = exp.render_html(frames);
    expect_true(contains(html, "id=\"srch-overlay\""), "search overlay present");
    expect_true(contains(html, "id=\"srch-input\""),   "search input present");
    expect_true(contains(html, "openSearch()"),         "openSearch function present");
    expect_true(contains(html, "srchResolve"),          "srchResolve function present");
    // ts: search uses BigInt for nanosecond precision
    expect_true(contains(html, "BigInt"),               "BigInt used for timestamp precision");
}

// Frame snapshot export is present.
void test_html_has_export() {
    const auto frames = two_sided_frames();
    HtmlExporter exp;
    const std::string html = exp.render_html(frames);
    expect_true(contains(html, "exportSnapshot()"),    "exportSnapshot function present");
    expect_true(contains(html, "createObjectURL"),     "blob download mechanism present");
    expect_true(contains(html, "application/json"),    "JSON MIME type present");
}

// Event tape search filter is present.
void test_html_has_tape_filter() {
    const auto frames = two_sided_frames();
    HtmlExporter exp;
    const std::string html = exp.render_html(frames);
    expect_true(contains(html, "id=\"tape-flt\""),     "tape filter input present");
    expect_true(contains(html, "tape-flt"),             "tape filter referenced in JS");
}

// Chart crosshair hover is wired.
void test_html_has_chart_crosshair() {
    const auto frames = two_sided_frames();
    HtmlExporter exp;
    const std::string html = exp.render_html(frames);
    expect_true(contains(html, "xhairFrac"),           "crosshair state variable present");
    expect_true(contains(html, "id=\"xhair-tip\""),    "tooltip element present");
    expect_true(contains(html, "CHART_IDS"),            "CHART_IDS array present");
    expect_true(contains(html, "mousemove"),            "mousemove event listener present");
}

// ── Regression: spread/mid/microprice null for one-sided books ────────────────
// Frame 0 of two_sided_frames() has only a bid → spread/mid/microprice must be
// null, not a misleading 0.0, so the time-series charts correctly gap those frames.
void test_json_one_sided_spread_null() {
    const auto frames = two_sided_frames();
    JsonSerializer ser;
    const std::string json0 = ser.serialize_frame(frames[0]); // bid-only frame
    expect_true(contains(json0, "\"spread\":null"),     "spread null on one-sided frame");
    expect_true(contains(json0, "\"mid\":null"),        "mid null on one-sided frame");
    expect_true(contains(json0, "\"microprice\":null"), "microprice null on one-sided frame");

    // Two-sided frame must still emit numeric values
    const std::string json1 = ser.serialize_frame(frames[1]); // both sides present
    expect_true(!contains(json1, "\"spread\":null"),     "spread not null on two-sided frame");
    expect_true(!contains(json1, "\"mid\":null"),        "mid not null on two-sided frame");
    expect_true(!contains(json1, "\"microprice\":null"), "microprice not null on two-sided frame");
}

// ── Regression: terminal renderer must not print "inf" for any signal ─────────
// When the book is one-sided (only bid), depth_ratio = +inf.  Before the fix,
// fmt4 used "%.4f" which formatted infinity as "   inf".  After the fix it must
// produce " n/a" so terminal output remains human-readable.
void test_terminal_no_inf_signals() {
    // Build a bid-only frame (frame 0 from two_sided_frames is the first Add, bid-only)
    const auto frames = two_sided_frames();
    TerminalRenderer r;
    const std::string out0 = r.render_frame(frames[0]); // bid-only
    expect_true(!contains(out0, "inf"), "no raw 'inf' in terminal output for one-sided book");
    expect_true(!contains(out0, "nan"), "no raw 'nan' in terminal output");

    // Two-sided frame should also render without inf/nan
    const std::string out1 = r.render_frame(frames[1]);
    expect_true(!contains(out1, "inf"), "no raw 'inf' in two-sided terminal output");
}

// ── html_has_comparison_mode ──────────────────────────────────────────────────
// HTML must contain the before/after comparison panel elements and JS functions.
void test_html_has_comparison_mode() {
    const auto frames = two_sided_frames();
    HtmlExporter exp;
    const std::string html = exp.render_html(frames);

    // Panel element
    expect_true(contains(html, "cmp-panel"),   "html has cmp-panel");
    expect_true(contains(html, "cmp-anchor"),  "html has cmp-anchor element");
    expect_true(contains(html, "cmp-list"),    "html has cmp-list element");

    // JS functions
    expect_true(contains(html, "setCmp"),      "html has setCmp function");
    expect_true(contains(html, "clearCmp"),    "html has clearCmp function");
    expect_true(contains(html, "renderCmp"),   "html has renderCmp function");

    // Keyboard shortcut wiring
    expect_true(contains(html, "'c'"),         "html has 'c' key for setCmp");
    expect_true(contains(html, "'C'"),         "html has 'C' key for clearCmp");

    // Delta fields
    expect_true(contains(html, "\\u0394Spread"),    "html has delta-spread label");
    expect_true(contains(html, "\\u0394Imbalance"), "html has delta-imbalance label");
    expect_true(contains(html, "cmp-pos"),          "html has cmp-pos CSS class");
    expect_true(contains(html, "cmp-neg"),          "html has cmp-neg CSS class");
}

// ── html_filter_coherence ─────────────────────────────────────────────────────
// goFirst / goLast must use fltIdxs(), not hard-coded 0 / FRAMES.length-1.
void test_html_filter_coherence() {
    const auto frames = two_sided_frames();
    HtmlExporter exp;
    const std::string html = exp.render_html(frames);

    // The fixed goFirst must call fltIdxs() not nav(0) directly
    expect_true(contains(html, "function goFirst"), "html has goFirst");
    expect_true(contains(html, "function goLast"),  "html has goLast");
    // Both functions must reference fltIdxs (filter-aware navigation)
    // We check that the string "fltIdxs" appears inside goFirst's body
    // by verifying it appears within close proximity of "goFirst" definition
    const std::size_t goFirstPos = html.find("function goFirst");
    const std::size_t goLastPos  = html.find("function goLast");
    expect_true(goFirstPos != std::string::npos, "goFirst present");
    expect_true(goLastPos  != std::string::npos, "goLast present");
    // fltIdxs must appear after goFirst (in its body or after) before goNext
    const std::size_t goNextPos  = html.find("function goNext");
    expect_true(goNextPos != std::string::npos, "goNext present");
    const std::string between = html.substr(goFirstPos, goNextPos - goFirstPos);
    expect_true(contains(between, "fltIdxs"), "goFirst/goLast use fltIdxs");
}

// ── Runner ────────────────────────────────────────────────────────────────

using TestFn = void(*)();
struct TestCase { const char* name; TestFn fn; };

const TestCase TESTS[] = {
    {"json_schema_version_present", test_json_schema_version_present},
    {"json_deterministic",          test_json_deterministic},
    {"json_optional_null_fields",   test_json_optional_null_fields},
    {"json_string_escape",          test_json_string_escape},
    {"json_depth_array",            test_json_depth_array},
    {"json_trade_fields",           test_json_trade_fields},
    {"terminal_render_non_empty",   test_terminal_render_non_empty},
    {"terminal_render_includes_price", test_terminal_render_includes_price},
    {"html_render_non_empty",       test_html_render_non_empty},
    {"html_contains_frame_count",   test_html_contains_frame_count},
    {"html_deterministic",          test_html_deterministic},
    {"html_schema_version_marker",  test_html_schema_version_marker},
    {"html_contains_frames_json",   test_html_contains_frames_json},
    {"json_no_inf_or_nan",          test_json_no_inf_or_nan},
    {"html_no_inf_or_nan",          test_html_no_inf_or_nan},
    {"json_one_sided_spread_null",  test_json_one_sided_spread_null},
    {"terminal_no_inf_signals",     test_terminal_no_inf_signals},
    {"html_has_event_id_jump",      test_html_has_event_id_jump},
    {"html_has_bookmarks",          test_html_has_bookmarks},
    {"html_has_search_overlay",     test_html_has_search_overlay},
    {"html_has_export",             test_html_has_export},
    {"html_has_tape_filter",        test_html_has_tape_filter},
    {"html_has_chart_crosshair",    test_html_has_chart_crosshair},
    {"html_has_comparison_mode",    test_html_has_comparison_mode},
    {"html_filter_coherence",       test_html_filter_coherence},
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: viz_test_exporters <test_case>\n";
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
