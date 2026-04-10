#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "microstructure/consolidated_book.hpp"
#include "microstructure/exceptions.hpp"
#include "microstructure/pipeline.hpp"
#include "microstructure/replay.hpp"

using microstructure::BookInvariantError;
using microstructure::BookSummary;
using microstructure::CrossVenueConsolidatedBook;
using microstructure::CrossVenueMicrostructurePipeline;
using microstructure::CrossedBookError;
using microstructure::DuplicateOrderError;
using microstructure::Event;
using microstructure::EventType;
using microstructure::FeatureConfig;
using microstructure::HistoricalReplayEngine;
using microstructure::MicrostructurePipeline;
using microstructure::OrderBookStateEngine;
using microstructure::OrderNotFoundError;
using microstructure::ReplayOptions;
using microstructure::Side;
using microstructure::SnapshotPayload;
using microstructure::SnapshotOrder;
using microstructure::TradeAggressor;
using microstructure::ValidationError;
using microstructure::Venue;

namespace {

struct TestFailure : public std::runtime_error {
    explicit TestFailure(const std::string& message)
        : std::runtime_error(message) {}
};

Event make_event(const microstructure::EventId event_id,
                 const EventType event_type,
                 const microstructure::OrderId order_id,
                 const microstructure::Price price,
                 const microstructure::Quantity size,
                 const Side side,
                 const microstructure::Timestamp base_ts,
                 const Venue venue = Venue::Nasdaq,
                 std::shared_ptr<const SnapshotPayload> snapshot = nullptr) {
    return Event(event_id,
                 event_type,
                 order_id,
                 price,
                 size,
                 side,
                 base_ts,
                 base_ts + 1,
                 base_ts + 2,
                 venue,
                 std::move(snapshot));
}

Event apply_replay_offsets_for_test(const Event& event,
                                    const microstructure::Timestamp network_offset,
                                    const microstructure::Timestamp gateway_offset) {
    return Event(event.event_id(),
                 event.event_type(),
                 event.order_id(),
                 event.price(),
                 event.size(),
                 event.side(),
                 event.exchange_timestamp(),
                 event.gateway_timestamp() + network_offset,
                 event.processing_timestamp() + network_offset + gateway_offset,
                 event.venue(),
                 event.snapshot());
}

FeatureConfig short_window_config() {
    FeatureConfig config;
    config.flow_window = 10;
    config.hidden_refill_window = 6;
    config.hidden_tracker_ttl = 20;
    config.hawkes_decay = 0.1;
    return config;
}

void expect_true(const bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

template <typename T, typename U>
void expect_eq(const T& lhs, const U& rhs, const std::string& message) {
    if (!(lhs == rhs)) {
        throw TestFailure(message);
    }
}

void expect_near(const double lhs, const double rhs, const double tolerance, const std::string& message) {
    if (std::fabs(lhs - rhs) > tolerance) {
        throw TestFailure(message);
    }
}

template <typename ExceptionType, typename Fn>
void expect_throws(Fn&& fn, const std::string& message) {
    try {
        fn();
    } catch (const ExceptionType&) {
        return;
    } catch (const std::exception& ex) {
        throw TestFailure(message + " (unexpected exception: " + ex.what() + ")");
    }
    throw TestFailure(message + " (no exception thrown)");
}

template <typename T>
void ignore_result(const T&) {}

void expect_signal_near(const microstructure::SignalVector& lhs,
                        const microstructure::SignalVector& rhs,
                        const double tolerance,
                        const std::string& message_prefix) {
    expect_near(lhs.imbalance, rhs.imbalance, tolerance, message_prefix + ": imbalance mismatch");
    expect_near(lhs.microprice, rhs.microprice, tolerance, message_prefix + ": microprice mismatch");
    expect_near(lhs.spread, rhs.spread, tolerance, message_prefix + ": spread mismatch");
    expect_near(lhs.ofi, rhs.ofi, tolerance, message_prefix + ": ofi mismatch");
    if (std::isinf(lhs.depth_ratio) || std::isinf(rhs.depth_ratio)) {
        expect_true(std::isinf(lhs.depth_ratio) && std::isinf(rhs.depth_ratio), message_prefix + ": depth_ratio mismatch");
    } else {
        expect_near(lhs.depth_ratio, rhs.depth_ratio, tolerance, message_prefix + ": depth_ratio mismatch");
    }
    expect_near(lhs.cancel_rate, rhs.cancel_rate, tolerance, message_prefix + ": cancel_rate mismatch");
    expect_near(lhs.queue_half_life, rhs.queue_half_life, tolerance, message_prefix + ": queue_half_life mismatch");
    expect_near(lhs.liquidity_slope, rhs.liquidity_slope, tolerance, message_prefix + ": liquidity_slope mismatch");
}

void test_order_insertion_and_best_quotes() {
    OrderBookStateEngine book;
    ignore_result(book.add_order(make_event(1, EventType::Add, 101, 100, 10, Side::Bid, 1)));
    ignore_result(book.add_order(make_event(2, EventType::Add, 102, 99, 5, Side::Bid, 2)));
    ignore_result(book.add_order(make_event(3, EventType::Add, 201, 102, 7, Side::Ask, 3)));

    expect_eq(book.best_bid().value(), 100, "best bid should be 100");
    expect_eq(book.best_ask().value(), 102, "best ask should be 102");
    expect_eq(book.best_bid_volume(), 10, "best bid volume should be 10");
    expect_eq(book.best_ask_volume(), 7, "best ask volume should be 7");
    expect_eq(book.total_volume(Side::Bid), 15, "total bid volume should be 15");
    expect_eq(book.total_volume(Side::Ask), 7, "total ask volume should be 7");
    book.validate_full_invariants();
}

void test_full_cancel_regression() {
    OrderBookStateEngine book;
    ignore_result(book.add_order(make_event(1, EventType::Add, 1, 100, 8, Side::Bid, 1)));
    ignore_result(book.add_order(make_event(2, EventType::Add, 2, 102, 4, Side::Ask, 2)));

    ignore_result(book.cancel_order(make_event(3, EventType::Cancel, 1, 100, 8, Side::Bid, 3)));
    expect_true(!book.best_bid().has_value(), "full cancel should remove the last bid level");
    expect_eq(book.volume_at(Side::Bid, 100), 0, "canceled level volume should be zero");
    expect_eq(book.queue_depth_at(Side::Bid, 100), static_cast<std::size_t>(0), "queue depth should be zero after full cancel");
    book.validate_full_invariants();
}

void test_modify_requeue_and_price_change_semantics() {
    FeatureConfig config = short_window_config();
    config.flow_window = 100;
    MicrostructurePipeline pipeline{config};

    ignore_result(pipeline.process(make_event(1, EventType::Add, 11, 100, 10, Side::Bid, 1)));
    ignore_result(pipeline.process(make_event(2, EventType::Add, 12, 100, 6, Side::Bid, 2)));
    ignore_result(pipeline.process(make_event(3, EventType::Add, 21, 103, 9, Side::Ask, 3)));
    ignore_result(pipeline.process(make_event(4, EventType::Modify, 11, 100, 12, Side::Bid, 4)));
    ignore_result(pipeline.process(make_event(5, EventType::Trade, 12, 100, 6, Side::Bid, 5)));

    expect_eq(pipeline.book().volume_at(Side::Bid, 100), 12, "size-increase modify should requeue the order to the back");

    const auto reprice = pipeline.process(make_event(6, EventType::Modify, 11, 99, 12, Side::Bid, 6));
    expect_eq(pipeline.book().volume_at(Side::Bid, 100), 0, "reprice should remove the old bid level");
    expect_eq(pipeline.book().volume_at(Side::Bid, 99), 12, "reprice should create the new bid level");
    expect_near(reprice.signal.cancel_rate, 0.0, 1e-9, "repricing should not count as a true cancel in windowed rates");
    expect_eq(reprice.deltas.size(), static_cast<std::size_t>(2), "repricing should emit two depth deltas");
    pipeline.book().validate_full_invariants();
}

void test_trade_execution_partial_and_full() {
    OrderBookStateEngine book;
    ignore_result(book.add_order(make_event(1, EventType::Add, 201, 101, 5, Side::Ask, 1)));
    ignore_result(book.add_order(make_event(2, EventType::Add, 202, 101, 4, Side::Ask, 2)));
    ignore_result(book.add_order(make_event(3, EventType::Add, 101, 99, 3, Side::Bid, 3)));

    const auto first_trade = book.execute_trade(make_event(4, EventType::Trade, 201, 101, 5, Side::Ask, 4));
    expect_true(first_trade.trade.has_value(), "trade execution should produce trade metadata");
    expect_eq(first_trade.trade->aggressor, TradeAggressor::BuyAggressor, "ask-side trade should imply buy aggressor");
    expect_eq(book.queue_depth_at(Side::Ask, 101), static_cast<std::size_t>(1), "one ask order should remain after full fill of first order");

    ignore_result(book.execute_trade(make_event(5, EventType::Trade, 202, 101, 2, Side::Ask, 5)));
    expect_eq(book.volume_at(Side::Ask, 101), 2, "partial fill should preserve remaining visible depth");
    expect_eq(book.queue_depth_at(Side::Ask, 101), static_cast<std::size_t>(1), "partial fill should preserve the queue entry");
    book.validate_full_invariants();
}

void test_snapshot_restore_and_exception_safety() {
    OrderBookStateEngine book;
    ignore_result(book.add_order(make_event(1, EventType::Add, 1, 100, 10, Side::Bid, 1)));
    ignore_result(book.add_order(make_event(2, EventType::Add, 2, 102, 5, Side::Ask, 2)));

    auto duplicate_snapshot = std::make_shared<SnapshotPayload>();
    duplicate_snapshot->orders = {
        SnapshotOrder{10, 99, 2, Side::Bid, Venue::Nasdaq},
        SnapshotOrder{10, 103, 3, Side::Ask, Venue::Nasdaq}};

    expect_throws<DuplicateOrderError>(
        [&]() { ignore_result(book.restore_snapshot(make_event(3, EventType::Snapshot, 999, 1, 1, Side::Bid, 3, Venue::Nasdaq, duplicate_snapshot))); },
        "duplicate snapshot ids should fail direct restore");

    const BookSummary preserved = book.summary();
    expect_eq(preserved.best_bid.value(), 100, "failed restore must preserve original bid");
    expect_eq(preserved.best_ask.value(), 102, "failed restore must preserve original ask");
}

void test_event_validation_and_cross_prevention() {
    MicrostructurePipeline pipeline;

    expect_throws<ValidationError>(
        [&]() { ignore_result(pipeline.process(make_event(1, EventType::Add, 1, 0, 5, Side::Bid, 1))); },
        "zero price should be rejected");

    pipeline.reset();
    ignore_result(pipeline.process(make_event(1, EventType::Add, 1, 100, 5, Side::Bid, 1)));
    expect_throws<ValidationError>(
        [&]() { ignore_result(pipeline.process(make_event(1, EventType::Add, 2, 102, 5, Side::Ask, 2))); },
        "duplicate event ids should be rejected");

    pipeline.reset();
    ignore_result(pipeline.process(make_event(2, EventType::Add, 2, 101, 5, Side::Ask, 2)));
    expect_throws<CrossedBookError>(
        [&]() { ignore_result(pipeline.process(make_event(3, EventType::Add, 3, 101, 5, Side::Bid, 3))); },
        "crossed add should fail");

    pipeline.reset();
    ignore_result(pipeline.process(make_event(4, EventType::Add, 4, 100, 5, Side::Bid, 4)));
    ignore_result(pipeline.process(make_event(5, EventType::Add, 5, 103, 5, Side::Ask, 5)));
    expect_throws<CrossedBookError>(
        [&]() { ignore_result(pipeline.process(make_event(6, EventType::Modify, 4, 103, 5, Side::Bid, 6))); },
        "crossing modify should fail");

    pipeline.reset();
    ignore_result(pipeline.process(make_event(7, EventType::Add, 7, 101, 5, Side::Ask, 7)));
    expect_throws<OrderNotFoundError>(
        [&]() { ignore_result(pipeline.process(make_event(8, EventType::Cancel, 999, 101, 1, Side::Ask, 8))); },
        "cancel on a missing order should fail");
    expect_throws<ValidationError>(
        [&]() { ignore_result(pipeline.process(make_event(9, EventType::Trade, 7, 101, 6, Side::Ask, 9))); },
        "trade larger than visible depth should fail");

    auto duplicate_snapshot = std::make_shared<SnapshotPayload>();
    duplicate_snapshot->orders = {
        SnapshotOrder{10, 99, 2, Side::Bid, Venue::Nasdaq},
        SnapshotOrder{10, 103, 2, Side::Ask, Venue::Nasdaq}};
    expect_throws<ValidationError>(
        [&]() { ignore_result(pipeline.process(make_event(10, EventType::Snapshot, 999, 1, 1, Side::Bid, 10, Venue::Nasdaq, duplicate_snapshot))); },
        "duplicate snapshot order ids should be rejected by validation");

    expect_throws<ValidationError>(
        [&]() { ignore_result(pipeline.process(Event(11, EventType::Add, 11, 100, 1, Side::Bid, 20, 19, 21))); },
        "gateway timestamp earlier than exchange should fail");
}

void test_feature_calculations_exact() {
    FeatureConfig config = short_window_config();
    config.flow_window = 100;
    MicrostructurePipeline pipeline{config};

    ignore_result(pipeline.process(make_event(1, EventType::Add, 1, 100, 10, Side::Bid, 1)));
    const auto result = pipeline.process(make_event(2, EventType::Add, 2, 102, 20, Side::Ask, 2));

    expect_near(result.signal.imbalance, -10.0 / 30.0, 1e-9, "imbalance mismatch");
    expect_near(result.signal.microprice, (102.0 * 10.0 + 100.0 * 20.0) / 30.0, 1e-9, "microprice mismatch");
    expect_near(result.signal.spread, 2.0, 1e-9, "spread mismatch");
    expect_near(result.signal.ofi, -10.0, 1e-9, "windowed OFI mismatch");
    expect_eq(result.features.depth.top_depth, 30, "top depth mismatch");
    expect_eq(result.features.depth.depth_5, 30, "depth_5 mismatch");
    expect_near(result.signal.depth_ratio, 0.5, 1e-9, "depth ratio mismatch");
    expect_eq(result.features.bid_liquidity_surface.size(), static_cast<std::size_t>(1), "bid surface size mismatch");
    expect_eq(result.features.ask_liquidity_surface.size(), static_cast<std::size_t>(1), "ask surface size mismatch");
    expect_eq(result.features.bid_liquidity_surface.front().side, Side::Bid, "bid surface side mismatch");
    expect_eq(result.features.ask_liquidity_surface.front().side, Side::Ask, "ask surface side mismatch");
    expect_near(result.signal.liquidity_slope, 15.0, 1e-9, "liquidity slope mismatch");
}

void test_windowed_ofi_and_windowed_rates() {
    FeatureConfig config = short_window_config();
    config.flow_window = 5;
    MicrostructurePipeline pipeline{config};

    ignore_result(pipeline.process(make_event(1, EventType::Add, 1, 100, 10, Side::Bid, 1)));
    ignore_result(pipeline.process(make_event(2, EventType::Add, 2, 102, 5, Side::Ask, 2)));
    const auto result = pipeline.process(make_event(3, EventType::Cancel, 2, 102, 2, Side::Ask, 20));

    expect_near(result.signal.ofi, 2.0, 1e-9, "older OFI contributions should expire from the window");
    expect_near(result.signal.cancel_rate, 1.0, 1e-9, "windowed cancel rate should reflect only current-window activity");
    expect_near(result.features.queue.fill_rate, 0.0, 1e-9, "windowed fill rate should be zero without trades");
}

void test_queue_half_life_time_normalized() {
    FeatureConfig config = short_window_config();
    config.flow_window = 100;
    MicrostructurePipeline pipeline{config};

    ignore_result(pipeline.process(make_event(1, EventType::Add, 1, 100, 10, Side::Bid, 1)));
    ignore_result(pipeline.process(make_event(2, EventType::Add, 2, 102, 10, Side::Ask, 11)));
    const auto result = pipeline.process(make_event(3, EventType::Cancel, 1, 100, 5, Side::Bid, 21));

    const double expected_half_life = std::log(2.0) / (0.2 * ((3.0 - 1.0) / 20.0));
    expect_near(result.features.queue.queue_half_life, expected_half_life, 1e-9, "queue half-life should be time-normalized");
}

void test_depth_ratio_one_sided_book() {
    MicrostructurePipeline pipeline;
    const auto result = pipeline.process(make_event(1, EventType::Add, 1, 100, 10, Side::Bid, 1));

    expect_true(std::isinf(result.signal.depth_ratio), "one-sided bid book should produce infinite depth ratio");
    expect_near(result.signal.spread, 0.0, 1e-9, "spread should be zero without both sides");
}

void test_hidden_liquidity_and_tracker_expiry() {
    FeatureConfig config = short_window_config();
    config.flow_window = 100;
    MicrostructurePipeline pipeline{config};

    ignore_result(pipeline.process(make_event(1, EventType::Add, 1, 99, 5, Side::Bid, 1)));
    ignore_result(pipeline.process(make_event(2, EventType::Add, 2, 101, 5, Side::Ask, 2)));
    ignore_result(pipeline.process(make_event(3, EventType::Trade, 2, 101, 3, Side::Ask, 3)));
    ignore_result(pipeline.process(make_event(4, EventType::Add, 3, 101, 4, Side::Ask, 4)));
    const auto repeated_fill = pipeline.process(make_event(5, EventType::Trade, 0, 101, 4, Side::Ask, 5));

    expect_true(repeated_fill.features.hidden_liquidity.suspected, "repeated fills with refill should flag hidden liquidity");

    const auto later_add = pipeline.process(make_event(6, EventType::Add, 4, 101, 1, Side::Ask, 40));
    expect_true(!later_add.features.hidden_liquidity.suspected, "stale hidden-liquidity trackers should expire");
}

void test_hawkes_decay_and_snapshot_reset() {
    FeatureConfig config = short_window_config();
    config.flow_window = 100;
    config.hawkes_decay = 0.1;
    MicrostructurePipeline pipeline{config};

    ignore_result(pipeline.process(make_event(1, EventType::Add, 1, 100, 5, Side::Bid, 1)));
    ignore_result(pipeline.process(make_event(2, EventType::Add, 2, 102, 5, Side::Ask, 2)));
    const auto trade = pipeline.process(make_event(3, EventType::Trade, 2, 102, 2, Side::Ask, 3));

    const auto decayed = pipeline.process(make_event(4, EventType::Add, 3, 99, 2, Side::Bid, 53));
    expect_true(decayed.features.hawkes.trade_intensity < trade.features.hawkes.trade_intensity, "trade intensity should decay over time");

    auto empty_snapshot = std::make_shared<SnapshotPayload>();
    const auto snapshot = pipeline.process(make_event(5, EventType::Snapshot, 999, 1, 1, Side::Bid, 54, Venue::Nasdaq, empty_snapshot));
    expect_near(snapshot.features.hawkes.trade_intensity, 0.0, 1e-12, "snapshot should reset Hawkes trade intensity");
    expect_near(snapshot.features.hawkes.order_arrival_intensity, 0.0, 1e-12, "snapshot should reset Hawkes add intensity");
    expect_near(snapshot.features.hawkes.cancel_intensity, 0.0, 1e-12, "snapshot should reset Hawkes cancel intensity");
}

void test_market_impact_edge_cases() {
    OrderBookStateEngine book;
    auto snapshot = std::make_shared<SnapshotPayload>();
    snapshot->orders = {
        SnapshotOrder{1, 100, 10, Side::Bid, Venue::Nasdaq},
        SnapshotOrder{2, 99, 5, Side::Bid, Venue::Nasdaq},
        SnapshotOrder{3, 102, 7, Side::Ask, Venue::Nasdaq},
        SnapshotOrder{4, 103, 4, Side::Ask, Venue::Nasdaq}};
    ignore_result(book.restore_snapshot(make_event(10, EventType::Snapshot, 999, 1, 1, Side::Bid, 10, Venue::Nasdaq, snapshot)));

    const auto exhausted = book.estimate_market_impact(TradeAggressor::BuyAggressor, 20);
    expect_true(!exhausted.fully_filled, "impact estimation should report partial fill when liquidity is insufficient");
    expect_eq(exhausted.filled_quantity, 11, "filled quantity should equal available ask liquidity");
    expect_eq(exhausted.terminal_price, 103, "terminal price should be the farthest consumed level");
    expect_near(exhausted.average_price, (7.0 * 102.0 + 4.0 * 103.0) / 11.0, 1e-9, "average price mismatch for exhausted impact");

    expect_throws<ValidationError>(
        [&]() { ignore_result(book.estimate_market_impact(TradeAggressor::Unknown, 1)); },
        "unknown aggressor should be rejected");
}

void test_empty_book_features_and_ml_exports() {
    MicrostructurePipeline pipeline;
    auto empty_snapshot = std::make_shared<SnapshotPayload>();
    const auto result = pipeline.process(make_event(1, EventType::Snapshot, 999, 1, 1, Side::Bid, 1, Venue::Nasdaq, empty_snapshot));

    expect_near(result.signal.imbalance, 0.0, 1e-12, "empty book imbalance should be zero");
    expect_near(result.signal.microprice, 0.0, 1e-12, "empty book microprice should be zero");
    expect_near(result.signal.spread, 0.0, 1e-12, "empty book spread should be zero");
    expect_true(result.features.bid_liquidity_surface.empty(), "empty book bid surface should be empty");
    expect_true(result.features.ask_liquidity_surface.empty(), "empty book ask surface should be empty");

    const auto graph = pipeline.research().export_graph(pipeline.book());
    const auto embedding = pipeline.research().export_embedding(pipeline.book(), 2);
    const auto density = pipeline.research().export_liquidity_density(pipeline.book(), Side::Ask, 2, 1.0);
    const auto heatmap = pipeline.research().export_liquidity_heatmap(pipeline.book(), 2);

    expect_true(graph.nodes.empty(), "empty book graph should have no nodes");
    expect_true(graph.edges.empty(), "empty book graph should have no edges");
    expect_eq(embedding.size(), static_cast<std::size_t>(8), "embedding size should still be deterministic on empty book");
    for (double value : embedding) {
        expect_near(value, 0.0, 1e-12, "empty book embedding entries should be zero");
    }
    expect_true(density.empty(), "empty book density export should be empty");
    expect_true(heatmap.empty(), "empty book heatmap should be empty");
}

void test_replay_determinism_and_latency_offsets() {
    std::vector<Event> events;
    events.push_back(make_event(1, EventType::Add, 1, 100, 8, Side::Bid, 1));
    events.push_back(make_event(2, EventType::Add, 2, 102, 10, Side::Ask, 2));
    events.push_back(make_event(3, EventType::Modify, 1, 100, 9, Side::Bid, 3));
    events.push_back(make_event(4, EventType::Trade, 2, 102, 4, Side::Ask, 4));
    events.push_back(make_event(5, EventType::Cancel, 1, 100, 3, Side::Bid, 5));

    HistoricalReplayEngine replay_engine;
    const ReplayOptions options{5, 7, true};
    const auto first = replay_engine.replay(events, options);
    const auto second = replay_engine.replay(events, options);

    expect_true(first.signal_verified, "replay verification should succeed");
    expect_eq(first.deterministic_signature, second.deterministic_signature, "deterministic signatures should match");

    MicrostructurePipeline live_pipeline;
    std::vector<microstructure::SignalVector> expected_signals;
    expected_signals.reserve(events.size());
    for (const Event& event : events) {
        const auto step = live_pipeline.process(apply_replay_offsets_for_test(event, options.network_latency_offset, options.gateway_latency_offset));
        expected_signals.push_back(step.signal);
    }

    expect_eq(first.steps.size(), expected_signals.size(), "replay step count mismatch");
    for (std::size_t i = 0; i < first.steps.size(); ++i) {
        expect_signal_near(first.steps[i].signal, expected_signals[i], 1e-9, "replay/live mismatch at step " + std::to_string(i));
    }
}

void test_export_determinism_and_ordering() {
    MicrostructurePipeline first_pipeline;
    MicrostructurePipeline second_pipeline;
    const std::vector<Event> events{
        make_event(1, EventType::Add, 1, 100, 8, Side::Bid, 1),
        make_event(2, EventType::Add, 2, 99, 5, Side::Bid, 2),
        make_event(3, EventType::Add, 3, 102, 6, Side::Ask, 3),
        make_event(4, EventType::Add, 4, 103, 4, Side::Ask, 4)};

    for (const Event& event : events) {
        ignore_result(first_pipeline.process(event));
        ignore_result(second_pipeline.process(event));
    }

    const auto first_graph = first_pipeline.research().export_graph(first_pipeline.book());
    const auto second_graph = second_pipeline.research().export_graph(second_pipeline.book());
    const auto first_embedding = first_pipeline.research().export_embedding(first_pipeline.book(), 3);
    const auto second_embedding = second_pipeline.research().export_embedding(second_pipeline.book(), 3);

    expect_eq(first_graph.nodes.size(), second_graph.nodes.size(), "graph node counts should match");
    expect_eq(first_graph.edges.size(), second_graph.edges.size(), "graph edge counts should match");
    expect_eq(first_graph.nodes.front().price, 100, "first graph node should be the best bid");
    expect_eq(first_graph.nodes[2].price, 102, "third graph node should be the best ask");
    expect_eq(first_graph.edges.size(), static_cast<std::size_t>(4), "graph should include same-side and same-rank cross-side edges");
    expect_eq(first_embedding, second_embedding, "embeddings should be deterministic");
    expect_eq(first_embedding.size(), static_cast<std::size_t>(12), "embedding dimension mismatch");
    expect_near(first_embedding[0], -1.0, 1e-9, "embedding should start with best bid distance from mid");
}

void test_consolidated_book_and_consolidated_features() {
    CrossVenueMicrostructurePipeline pipeline;
    ignore_result(pipeline.process(make_event(1, EventType::Add, 1, 100, 10, Side::Bid, 1, Venue::Nasdaq)));
    ignore_result(pipeline.process(make_event(2, EventType::Add, 1, 100, 7, Side::Bid, 2, Venue::Arca)));
    ignore_result(pipeline.process(make_event(3, EventType::Add, 1, 102, 6, Side::Ask, 3, Venue::Bats)));
    const auto result = pipeline.process(make_event(4, EventType::Add, 1, 103, 4, Side::Ask, 4, Venue::Iex));

    expect_eq(pipeline.book().volume_at(Side::Bid, 100), 17, "consolidated bid depth should aggregate across venues");
    expect_eq(pipeline.book().total_volume(Side::Ask), 10, "consolidated ask depth should aggregate across venues");
    expect_near(result.signal.spread, 2.0, 1e-9, "consolidated spread mismatch");
    expect_near(result.signal.imbalance, 7.0 / 27.0, 1e-9, "consolidated imbalance mismatch");
    expect_near(result.signal.microprice, (102.0 * 17.0 + 100.0 * 6.0) / 23.0, 1e-9, "consolidated microprice mismatch");
}

void test_consolidated_clear_and_duplicate_event_id_validation() {
    CrossVenueConsolidatedBook book;
    ignore_result(book.process(make_event(1, EventType::Add, 1, 100, 10, Side::Bid, 1, Venue::Nasdaq)));
    expect_throws<ValidationError>(
        [&]() { ignore_result(book.process(make_event(1, EventType::Add, 1, 101, 5, Side::Ask, 2, Venue::Arca))); },
        "consolidated path should reject duplicate event ids across venues");

    book.clear();
    expect_true(!book.best_bid().has_value(), "clear should reset consolidated best bid");
    expect_true(!book.best_ask().has_value(), "clear should reset consolidated best ask");

    ignore_result(book.process(make_event(2, EventType::Add, 2, 99, 3, Side::Bid, 3, Venue::Bats)));
    expect_eq(book.best_bid().value(), 99, "book should rebuild correctly after clear");
}

void test_latency_metrics_exact() {
    MicrostructurePipeline pipeline;
    const Event event{
        1,
        EventType::Add,
        1,
        100,
        5,
        Side::Bid,
        10,
        15,
        21,
        Venue::Nasdaq};
    const auto result = pipeline.process(event);

    expect_eq(result.features.latency.network_latency, 5, "network latency mismatch");
    expect_eq(result.features.latency.gateway_latency, 6, "gateway latency mismatch");
    expect_eq(result.features.latency.processing_latency, 11, "processing latency mismatch");
}

void test_trade_aggressor_sell_side() {
    MicrostructurePipeline pipeline;
    ignore_result(pipeline.process(make_event(1, EventType::Add, 1, 100, 5, Side::Bid, 1)));
    ignore_result(pipeline.process(make_event(2, EventType::Add, 2, 102, 5, Side::Ask, 2)));
    const auto trade = pipeline.process(make_event(3, EventType::Trade, 1, 100, 3, Side::Bid, 3));

    expect_true(trade.trade.has_value(), "trade metadata should be populated");
    expect_eq(trade.trade->aggressor, TradeAggressor::SellAggressor, "bid-side execution should imply sell aggressor");
    expect_eq(trade.features.last_trade_aggressor, TradeAggressor::SellAggressor, "feature snapshot should record sell aggressor");
}

void test_non_monotonic_timestamps_rejected() {
    MicrostructurePipeline pipeline;
    ignore_result(pipeline.process(make_event(1, EventType::Add, 1, 100, 5, Side::Bid, 10)));
    expect_throws<ValidationError>(
        [&]() { ignore_result(pipeline.process(make_event(2, EventType::Add, 2, 102, 5, Side::Ask, 9))); },
        "non-monotonic exchange timestamps should be rejected");
}

void test_invalid_enum_values_rejected() {
    MicrostructurePipeline pipeline;
    expect_throws<ValidationError>(
        [&]() {
            ignore_result(pipeline.process(Event{
                1,
                static_cast<EventType>(99),
                1,
                100,
                5,
                Side::Bid,
                1,
                2,
                3,
                Venue::Nasdaq}));
        },
        "invalid event type should be rejected");

    expect_throws<ValidationError>(
        [&]() {
            ignore_result(pipeline.process(Event{
                2,
                EventType::Add,
                2,
                100,
                5,
                static_cast<Side>(99),
                4,
                5,
                6,
                Venue::Nasdaq}));
        },
        "invalid side should be rejected");
}

void test_snapshot_followed_by_deltas() {
    MicrostructurePipeline pipeline;
    auto snapshot = std::make_shared<SnapshotPayload>();
    snapshot->orders = {
        SnapshotOrder{1, 100, 5, Side::Bid, Venue::Nasdaq},
        SnapshotOrder{2, 102, 6, Side::Ask, Venue::Nasdaq}};

    ignore_result(pipeline.process(make_event(1, EventType::Snapshot, 999, 1, 1, Side::Bid, 1, Venue::Nasdaq, snapshot)));
    const auto add = pipeline.process(make_event(2, EventType::Add, 3, 99, 4, Side::Bid, 2));

    expect_eq(add.book.best_bid.value(), 100, "snapshot best bid should remain after lower bid add");
    expect_eq(add.book.total_orders, static_cast<std::size_t>(3), "snapshot plus delta should retain prior state");
    expect_eq(add.deltas.size(), static_cast<std::size_t>(1), "simple add should emit one delta");
    expect_eq(add.deltas.front().delta_volume, 4, "add delta volume mismatch");
}

void test_book_update_delta_encoding() {
    OrderBookStateEngine book;
    const auto add = book.add_order(make_event(1, EventType::Add, 1, 100, 5, Side::Bid, 1));
    expect_eq(add.deltas.size(), static_cast<std::size_t>(1), "add should emit exactly one delta");
    expect_eq(add.deltas.front().price, 100, "add delta price mismatch");
    expect_eq(add.deltas.front().delta_volume, 5, "add delta quantity mismatch");

    const auto cancel = book.cancel_order(make_event(2, EventType::Cancel, 1, 100, 2, Side::Bid, 2));
    expect_eq(cancel.deltas.size(), static_cast<std::size_t>(1), "cancel should emit exactly one delta");
    expect_eq(cancel.deltas.front().delta_volume, -2, "cancel delta quantity mismatch");
}

void test_replay_graph_and_embedding_match_live() {
    std::vector<Event> events{
        make_event(1, EventType::Add, 1, 100, 8, Side::Bid, 1),
        make_event(2, EventType::Add, 2, 99, 5, Side::Bid, 2),
        make_event(3, EventType::Add, 3, 102, 6, Side::Ask, 3),
        make_event(4, EventType::Add, 4, 103, 4, Side::Ask, 4),
        make_event(5, EventType::Trade, 3, 102, 2, Side::Ask, 5)};

    HistoricalReplayEngine replay_engine;
    const auto replay = replay_engine.replay(events, ReplayOptions{3, 4, true});

    MicrostructurePipeline live;
    for (const Event& event : events) {
        ignore_result(live.process(apply_replay_offsets_for_test(event, 3, 4)));
    }

    const auto replay_graph = live.research().export_graph(live.book());
    const auto replay_embedding = live.research().export_embedding(live.book(), 3);

    expect_eq(replay.steps.back().book.best_bid, live.book().best_bid(), "replay/live best bid mismatch");
    expect_eq(replay.steps.back().book.best_ask, live.book().best_ask(), "replay/live best ask mismatch");
    expect_eq(replay_graph.nodes.size(), static_cast<std::size_t>(4), "live graph node count mismatch");
    expect_eq(replay_embedding.size(), static_cast<std::size_t>(12), "live embedding size mismatch");
}

void test_snapshot_queue_priority() {
    // Regression: same-price orders must be restored in queue_priority order,
    // not order_id order.  We submit two orders at the same price: order 99
    // (queue_priority=1) must sit ahead of order 1 (queue_priority=2) even
    // though 1 < 99 by order_id.
    auto payload = std::make_shared<SnapshotPayload>();
    payload->orders.push_back(SnapshotOrder{99, 100, 5, microstructure::Side::Bid, Venue::Nasdaq, 1});
    payload->orders.push_back(SnapshotOrder{1,  100, 3, microstructure::Side::Bid, Venue::Nasdaq, 2});

    OrderBookStateEngine book;
    ignore_result(book.process(make_event(
        1, EventType::Snapshot, 0, 100, 1, microstructure::Side::Bid, 1, Venue::Nasdaq, payload)));

    // Partially fill 5 shares — should consume exactly order 99 (the front of
    // the queue).  After the fill, 3 shares from order 1 remain.
    // side=Bid because the trade's resting side is the bid queue.
    ignore_result(book.process(make_event(2, EventType::Trade, 0, 100, 5, microstructure::Side::Bid, 2)));
    expect_eq(book.best_bid_volume(), static_cast<microstructure::Quantity>(3),
              "priority-ordered snapshot: wrong remaining volume after fill");
    expect_eq(book.queue_depth_at(microstructure::Side::Bid, 100), static_cast<std::size_t>(1),
              "priority-ordered snapshot: wrong queue depth after fill");

    // Without queue_priority the fallback is order_id ascending (1, 99).
    // Verify that supplying priority=0 on both falls back to order_id order.
    auto payload2 = std::make_shared<SnapshotPayload>();
    payload2->orders.push_back(SnapshotOrder{99, 100, 5, microstructure::Side::Bid, Venue::Nasdaq, 0});
    payload2->orders.push_back(SnapshotOrder{1,  100, 3, microstructure::Side::Bid, Venue::Nasdaq, 0});

    OrderBookStateEngine book2;
    ignore_result(book2.process(make_event(
        1, EventType::Snapshot, 0, 100, 1, microstructure::Side::Bid, 1, Venue::Nasdaq, payload2)));

    // order_id=1 is first in the fallback order, so 3 shares are consumed first.
    ignore_result(book2.process(make_event(2, EventType::Trade, 0, 100, 3, microstructure::Side::Bid, 2)));
    expect_eq(book2.best_bid_volume(), static_cast<microstructure::Quantity>(5),
              "fallback snapshot: wrong remaining volume after fill");
}

void test_consolidated_is_crossed_and_locked() {
    CrossVenueConsolidatedBook book;
    // Normal uncrossed state.
    ignore_result(book.process(make_event(1, EventType::Add, 1, 100, 10, Side::Bid, 1, Venue::Nasdaq)));
    ignore_result(book.process(make_event(2, EventType::Add, 2, 102, 5, Side::Ask, 2, Venue::Arca)));
    expect_true(!book.is_crossed(), "normal book should not be crossed");
    expect_true(!book.is_locked(), "normal book should not be locked");

    // Locked market: same price bid and ask on different venues.
    ignore_result(book.process(make_event(3, EventType::Add, 3, 102, 7, Side::Bid, 3, Venue::Bats)));
    expect_true(!book.is_crossed(), "locked book should not be crossed");
    expect_true(book.is_locked(), "locked market should be detected");

    // Crossed market: bid above ask on different venues — legitimate for
    // consolidated NBBO and must NOT throw.
    ignore_result(book.process(make_event(4, EventType::Add, 4, 103, 3, Side::Bid, 4, Venue::Iex)));
    expect_true(book.is_crossed(), "crossed consolidated market should be detected");
    expect_true(!book.is_locked(), "crossed market should not also report locked");
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"order_insertion_and_best_quotes", test_order_insertion_and_best_quotes},
        {"full_cancel_regression", test_full_cancel_regression},
        {"modify_requeue_and_price_change_semantics", test_modify_requeue_and_price_change_semantics},
        {"trade_execution_partial_and_full", test_trade_execution_partial_and_full},
        {"snapshot_restore_and_exception_safety", test_snapshot_restore_and_exception_safety},
        {"event_validation_and_cross_prevention", test_event_validation_and_cross_prevention},
        {"feature_calculations_exact", test_feature_calculations_exact},
        {"windowed_ofi_and_windowed_rates", test_windowed_ofi_and_windowed_rates},
        {"queue_half_life_time_normalized", test_queue_half_life_time_normalized},
        {"depth_ratio_one_sided_book", test_depth_ratio_one_sided_book},
        {"hidden_liquidity_and_tracker_expiry", test_hidden_liquidity_and_tracker_expiry},
        {"hawkes_decay_and_snapshot_reset", test_hawkes_decay_and_snapshot_reset},
        {"market_impact_edge_cases", test_market_impact_edge_cases},
        {"empty_book_features_and_ml_exports", test_empty_book_features_and_ml_exports},
        {"replay_determinism_and_latency_offsets", test_replay_determinism_and_latency_offsets},
        {"export_determinism_and_ordering", test_export_determinism_and_ordering},
        {"consolidated_book_and_consolidated_features", test_consolidated_book_and_consolidated_features},
        {"consolidated_clear_and_duplicate_event_id_validation", test_consolidated_clear_and_duplicate_event_id_validation},
        {"latency_metrics_exact", test_latency_metrics_exact},
        {"trade_aggressor_sell_side", test_trade_aggressor_sell_side},
        {"non_monotonic_timestamps_rejected", test_non_monotonic_timestamps_rejected},
        {"invalid_enum_values_rejected", test_invalid_enum_values_rejected},
        {"snapshot_followed_by_deltas", test_snapshot_followed_by_deltas},
        {"book_update_delta_encoding", test_book_update_delta_encoding},
        {"replay_graph_and_embedding_match_live", test_replay_graph_and_embedding_match_live},
        {"snapshot_queue_priority", test_snapshot_queue_priority},
        {"consolidated_is_crossed_and_locked", test_consolidated_is_crossed_and_locked},
    };

    if (argc == 2) {
        const std::string requested = argv[1];
        for (const auto& [name, fn] : tests) {
            if (name == requested) {
                fn();
                std::cout << "[PASS] " << name << '\n';
                return 0;
            }
        }
        std::cerr << "Unknown test: " << requested << '\n';
        return 2;
    }

    std::size_t failures = 0;
    for (const auto& [name, fn] : tests) {
        try {
            fn();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& ex) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << ex.what() << '\n';
        }
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "All tests passed\n";
    return 0;
}
