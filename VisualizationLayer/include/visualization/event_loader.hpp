#pragma once

// event_loader.hpp — abstract interface for loading microstructure event
// sequences from external sources (files, streams, databases).
//
// Design contract
// ───────────────
// • load() returns a chronologically ordered (non-decreasing exchange_timestamp)
//   sequence of valid microstructure::Event objects.
// • The caller (FrameCapture, viz_demo) passes the result directly to
//   MicrostructurePipeline::process(); any ordering or validation violation
//   will be caught there.
// • Implementors must not duplicate analytics logic — the raw event fields
//   are the only output.
//
// Ingestion boundary
// ──────────────────
// EventLoader is the intended boundary between "real data" and the replay
// pipeline.  Synthetic generators (SyntheticEventGenerator,
// RealisticSyntheticGenerator) implement the same calling convention but are
// not EventLoader subclasses because their state is internal, not file-based.

#include <string>
#include <vector>

#include "microstructure/event.hpp"

namespace visualization {

// Abstract base: load a sequence of engine events from an external source.
class EventLoader {
public:
    virtual ~EventLoader() = default;

    // Load all events.  Throws std::runtime_error on I/O or parse failure.
    [[nodiscard]] virtual std::vector<microstructure::Event> load() const = 0;

    // Short human-readable description used in logging and the demo UI.
    [[nodiscard]] virtual std::string source_description() const = 0;
};

// ── CsvEventLoader ────────────────────────────────────────────────────────────
//
// Loads events from a UTF-8 CSV file.
//
// Expected header row (first non-comment line):
//   event_id,event_type,order_id,price,size,side,exchange_ts,receive_ts,processed_ts,venue
//
// Field formats
// ─────────────
//   event_id      uint64  monotonically increasing, 1-based
//   event_type    string  ADD | CANCEL | MODIFY | TRADE | SNAPSHOT
//   order_id      uint64  0 for anonymous (market) trades
//   price         int64   in ticks (positive)
//   size          int64   in lots  (positive)
//   side          string  BID | ASK
//   exchange_ts   int64   nanoseconds since epoch (exchange clock)
//   receive_ts    int64   nanoseconds since epoch (gateway receive clock)
//   processed_ts  int64   nanoseconds since epoch (local processing clock)
//   venue         string  NASDAQ | ARCA | BATS | IEX
//
// Rules
// ─────
//   • Lines beginning with '#' are treated as comments and skipped.
//   • Blank lines are skipped.
//   • Leading/trailing whitespace on each field is stripped.
//   • A std::runtime_error is thrown on the first malformed line (includes
//     the line number in the message).
//
// Example
// ───────
//   # market-data snapshot 2024-01-15
//   event_id,event_type,order_id,price,size,side,exchange_ts,receive_ts,processed_ts,venue
//   1,ADD,1001,10050,500,BID,1705276800000000000,1705276800000001000,1705276800000002000,NASDAQ
//   2,ADD,2001,10051,300,ASK,1705276800001000000,1705276800001001000,1705276800001002000,NASDAQ
//   3,TRADE,0,10050,200,BID,1705276800002000000,1705276800002001000,1705276800002002000,NASDAQ
class CsvEventLoader final : public EventLoader {
public:
    // path: filesystem path to the CSV file.
    explicit CsvEventLoader(std::string path);

    [[nodiscard]] std::vector<microstructure::Event> load() const override;
    [[nodiscard]] std::string source_description() const override;

private:
    std::string path_;
};

} // namespace visualization
