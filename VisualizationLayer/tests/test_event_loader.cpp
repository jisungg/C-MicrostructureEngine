#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "microstructure/event.hpp"
#include "microstructure/types.hpp"
#include "visualization/event_loader.hpp"
#include "visualization/frame_capture.hpp"

using microstructure::EventType;
using microstructure::Side;
using microstructure::Venue;
using visualization::CsvEventLoader;
using visualization::FrameCapture;

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

// Write a string to a temp file and return the path.
std::string write_tmp(const std::string& content) {
    const std::string path = "/tmp/viz_csv_test_" + std::to_string(std::rand()) + ".csv";
    std::ofstream f{path};
    if (!f.is_open()) throw std::runtime_error("cannot create temp file: " + path);
    f << content;
    return path;
}

// Minimal 2-event CSV: one bid add, one ask add.
const char* kSimpleCsv = R"(# test fixture
event_id,event_type,order_id,price,size,side,exchange_ts,receive_ts,processed_ts,venue
1,ADD,1001,10050,500,BID,1000000000,1000000001,1000000002,NASDAQ
2,ADD,2001,10051,300,ASK,2000000000,2000000001,2000000002,NASDAQ
)";

// ── csv_load_basic ────────────────────────────────────────────────────────────
// Happy-path: header + comment + two rows parse cleanly.
void test_csv_load_basic() {
    const std::string path = write_tmp(kSimpleCsv);
    CsvEventLoader loader{path};
    const auto events = loader.load();
    std::remove(path.c_str());

    expect_eq(events.size(), std::size_t{2}, "event count");
    expect_eq(events[0].event_id(),           microstructure::EventId{1},   "ev[0].event_id");
    expect_eq(events[0].event_type(),         EventType::Add,               "ev[0].event_type");
    expect_eq(events[0].order_id(),           microstructure::OrderId{1001},"ev[0].order_id");
    expect_eq(events[0].price(),              microstructure::Price{10050}, "ev[0].price");
    expect_eq(events[0].size(),               microstructure::Quantity{500},"ev[0].size");
    expect_eq(events[0].side(),               Side::Bid,                   "ev[0].side");
    expect_eq(events[0].exchange_timestamp(), microstructure::Timestamp{1000000000}, "ev[0].ts");
    expect_eq(events[0].venue(),              Venue::Nasdaq,               "ev[0].venue");

    expect_eq(events[1].event_id(),   microstructure::EventId{2},    "ev[1].event_id");
    expect_eq(events[1].event_type(), EventType::Add,                "ev[1].event_type");
    expect_eq(events[1].side(),       Side::Ask,                     "ev[1].side");
    expect_eq(events[1].price(),      microstructure::Price{10051},  "ev[1].price");
}

// ── csv_source_description ────────────────────────────────────────────────────
void test_csv_source_description() {
    CsvEventLoader loader{"/some/path/data.csv"};
    const std::string desc = loader.source_description();
    expect_true(desc.find("CSV:") != std::string::npos, "source_description contains 'CSV:'");
    expect_true(desc.find("data.csv") != std::string::npos, "source_description contains filename");
}

// ── csv_skip_comments_and_blanks ──────────────────────────────────────────────
void test_csv_skip_comments_and_blanks() {
    const std::string csv =
        "# comment 1\n"
        "\n"
        "event_id,event_type,order_id,price,size,side,exchange_ts,receive_ts,processed_ts,venue\n"
        "# comment 2\n"
        "\n"
        "1,ADD,101,1000,100,BID,1000000000,1000000001,1000000002,NASDAQ\n"
        "# trailing comment\n";
    const std::string path = write_tmp(csv);
    CsvEventLoader loader{path};
    const auto events = loader.load();
    std::remove(path.c_str());
    expect_eq(events.size(), std::size_t{1}, "skip comments and blanks: event count");
}

// ── csv_no_header ─────────────────────────────────────────────────────────────
// The header is optional — headerless files should also parse correctly.
void test_csv_no_header() {
    const std::string csv =
        "1,ADD,101,1000,100,BID,1000000000,1000000001,1000000002,NASDAQ\n"
        "2,ADD,201,1001,200,ASK,2000000000,2000000001,2000000002,ARCA\n";
    const std::string path = write_tmp(csv);
    CsvEventLoader loader{path};
    const auto events = loader.load();
    std::remove(path.c_str());
    expect_eq(events.size(), std::size_t{2}, "no-header: event count");
    expect_eq(events[1].venue(), Venue::Arca, "no-header: venue ARCA");
}

// ── csv_all_venues ────────────────────────────────────────────────────────────
void test_csv_all_venues() {
    const std::string csv =
        "1,ADD,1,1000,100,BID,1000000000,1000000001,1000000002,NASDAQ\n"
        "2,ADD,2,1001,100,ASK,2000000000,2000000001,2000000002,ARCA\n"
        "3,ADD,3,1000,100,BID,3000000000,3000000001,3000000002,BATS\n"
        "4,ADD,4,1001,100,ASK,4000000000,4000000001,4000000002,IEX\n";
    const std::string path = write_tmp(csv);
    CsvEventLoader loader{path};
    const auto events = loader.load();
    std::remove(path.c_str());
    expect_eq(events.size(), std::size_t{4},  "all venues: event count");
    expect_eq(events[0].venue(), Venue::Nasdaq, "venue NASDAQ");
    expect_eq(events[1].venue(), Venue::Arca,   "venue ARCA");
    expect_eq(events[2].venue(), Venue::Bats,   "venue BATS");
    expect_eq(events[3].venue(), Venue::Iex,    "venue IEX");
}

// ── csv_all_event_types ───────────────────────────────────────────────────────
void test_csv_all_event_types() {
    // Valid sequence: add bid, add ask, cancel the bid, modify ask, trade, snapshot
    const std::string csv =
        "1,ADD,101,1000,500,BID,1000000000,1000000001,1000000002,NASDAQ\n"
        "2,ADD,201,1001,300,ASK,2000000000,2000000001,2000000002,NASDAQ\n"
        "3,CANCEL,101,1000,500,BID,3000000000,3000000001,3000000002,NASDAQ\n"
        "4,MODIFY,201,1001,200,ASK,4000000000,4000000001,4000000002,NASDAQ\n"
        "5,ADD,102,1000,200,BID,5000000000,5000000001,5000000002,NASDAQ\n"
        "6,TRADE,0,1000,100,BID,6000000000,6000000001,6000000002,NASDAQ\n";
    const std::string path = write_tmp(csv);
    CsvEventLoader loader{path};
    const auto events = loader.load();
    std::remove(path.c_str());
    expect_eq(events.size(), std::size_t{6}, "all_event_types: count");
    expect_eq(events[0].event_type(), EventType::Add,    "Add");
    expect_eq(events[2].event_type(), EventType::Cancel, "Cancel");
    expect_eq(events[3].event_type(), EventType::Modify, "Modify");
    expect_eq(events[5].event_type(), EventType::Trade,  "Trade");
    expect_eq(events[5].order_id(),   microstructure::OrderId{0}, "anonymous trade oid=0");
}

// ── csv_pipeline_integration ──────────────────────────────────────────────────
// Events loaded from CSV must pass through the real MicrostructurePipeline.
void test_csv_pipeline_integration() {
    const std::string csv =
        "event_id,event_type,order_id,price,size,side,exchange_ts,receive_ts,processed_ts,venue\n"
        "1,ADD,101,10000,500,BID,1000000000,1000000001,1000000002,NASDAQ\n"
        "2,ADD,201,10001,300,ASK,2000000000,2000000001,2000000002,NASDAQ\n"
        "3,ADD,102,9999, 200,BID,3000000000,3000000001,3000000002,NASDAQ\n"
        "4,ADD,202,10002,400,ASK,4000000000,4000000001,4000000002,NASDAQ\n"
        "5,TRADE,0,10000,200,BID,5000000000,5000000001,5000000002,NASDAQ\n";
    const std::string path = write_tmp(csv);
    CsvEventLoader loader{path};
    const auto events = loader.load();
    std::remove(path.c_str());

    // Should capture without throwing
    FrameCapture capture;
    const auto frames = capture.capture(events);
    expect_eq(frames.size(), std::size_t{5}, "pipeline_integration: frame count");
    expect_true(frames.back().is_trade, "last frame is a trade");
}

// ── csv_error_bad_file ────────────────────────────────────────────────────────
void test_csv_error_bad_file() {
    CsvEventLoader loader{"/nonexistent/path/data.csv"};
    bool threw = false;
    try {
        const auto events = loader.load();
        (void)events;
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect_true(threw, "bad file path must throw runtime_error");
}

// ── csv_error_too_few_fields ──────────────────────────────────────────────────
void test_csv_error_too_few_fields() {
    const std::string csv = "1,ADD,101,1000,500,BID,1000000000,1000000001\n"; // 9 fields
    const std::string path = write_tmp(csv);
    CsvEventLoader loader{path};
    bool threw = false;
    try {
        const auto events = loader.load();
        (void)events;
    } catch (const std::runtime_error&) {
        threw = true;
    }
    std::remove(path.c_str());
    expect_true(threw, "too few fields must throw");
}

// ── csv_error_bad_event_type ──────────────────────────────────────────────────
void test_csv_error_bad_event_type() {
    const std::string csv = "1,UNKNOWN,101,1000,500,BID,1000000000,1000000001,1000000002,NASDAQ\n";
    const std::string path = write_tmp(csv);
    CsvEventLoader loader{path};
    bool threw = false;
    try {
        const auto events = loader.load();
        (void)events;
    } catch (const std::runtime_error&) {
        threw = true;
    }
    std::remove(path.c_str());
    expect_true(threw, "bad event_type must throw");
}

// ── csv_error_bad_side ────────────────────────────────────────────────────────
void test_csv_error_bad_side() {
    const std::string csv = "1,ADD,101,1000,500,SELL,1000000000,1000000001,1000000002,NASDAQ\n";
    const std::string path = write_tmp(csv);
    CsvEventLoader loader{path};
    bool threw = false;
    try {
        const auto events = loader.load();
        (void)events;
    } catch (const std::runtime_error&) {
        threw = true;
    }
    std::remove(path.c_str());
    expect_true(threw, "bad side must throw");
}

// ── csv_whitespace_trimming ───────────────────────────────────────────────────
void test_csv_whitespace_trimming() {
    const std::string csv = " 1 , ADD , 101 , 1000 , 500 , BID , 1000000000 , 1000000001 , 1000000002 , NASDAQ \n";
    const std::string path = write_tmp(csv);
    CsvEventLoader loader{path};
    const auto events = loader.load();
    std::remove(path.c_str());
    expect_eq(events.size(), std::size_t{1}, "whitespace trimming: event count");
    expect_eq(events[0].event_id(), microstructure::EventId{1}, "whitespace trimming: event_id");
}

// ── Dispatch ──────────────────────────────────────────────────────────────────

int run_test(const std::string& name) {
    try {
        if      (name == "csv_load_basic")             test_csv_load_basic();
        else if (name == "csv_source_description")     test_csv_source_description();
        else if (name == "csv_skip_comments_and_blanks") test_csv_skip_comments_and_blanks();
        else if (name == "csv_no_header")              test_csv_no_header();
        else if (name == "csv_all_venues")             test_csv_all_venues();
        else if (name == "csv_all_event_types")        test_csv_all_event_types();
        else if (name == "csv_pipeline_integration")   test_csv_pipeline_integration();
        else if (name == "csv_error_bad_file")         test_csv_error_bad_file();
        else if (name == "csv_error_too_few_fields")   test_csv_error_too_few_fields();
        else if (name == "csv_error_bad_event_type")   test_csv_error_bad_event_type();
        else if (name == "csv_error_bad_side")         test_csv_error_bad_side();
        else if (name == "csv_whitespace_trimming")    test_csv_whitespace_trimming();
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
        std::cerr << "Usage: viz_test_event_loader <test_name>\n";
        return 1;
    }
    return run_test(std::string{argv[1]});
}
