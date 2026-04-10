#include "microstructure/replay.hpp"

#include <bit>

#include "microstructure/exceptions.hpp"

namespace microstructure {

namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void hash_combine(std::uint64_t& hash, const std::uint64_t value) {
    hash ^= value;
    hash *= kFnvPrime;
}

void hash_combine_signed(std::uint64_t& hash, const std::int64_t value) {
    hash_combine(hash, static_cast<std::uint64_t>(value));
}

void hash_combine_double(std::uint64_t& hash, const double value) {
    hash_combine(hash, std::bit_cast<std::uint64_t>(value));
}

std::uint64_t signature_for_step(const PipelineResult& step) {
    std::uint64_t hash = kFnvOffset;
    hash_combine(hash, step.event_id);
    hash_combine(hash, static_cast<std::uint64_t>(step.event_type));
    hash_combine(hash, static_cast<std::uint64_t>(step.venue));
    hash_combine_signed(hash, step.book.best_bid.value_or(0));
    hash_combine_signed(hash, step.book.best_ask.value_or(0));
    hash_combine_signed(hash, step.book.best_bid_volume);
    hash_combine_signed(hash, step.book.best_ask_volume);
    hash_combine_signed(hash, step.book.total_bid_volume);
    hash_combine_signed(hash, step.book.total_ask_volume);
    hash_combine(hash, step.book.bid_levels);
    hash_combine(hash, step.book.ask_levels);
    hash_combine(hash, step.book.total_orders);
    for (const PriceLevelDelta& delta : step.deltas) {
        hash_combine(hash, static_cast<std::uint64_t>(delta.side));
        hash_combine_signed(hash, delta.price);
        hash_combine_signed(hash, delta.delta_volume);
        hash_combine(hash, static_cast<std::uint64_t>(delta.venue));
    }
    if (step.trade.has_value()) {
        hash_combine_signed(hash, step.trade->price);
        hash_combine_signed(hash, step.trade->size);
        hash_combine(hash, static_cast<std::uint64_t>(step.trade->resting_side));
        hash_combine(hash, static_cast<std::uint64_t>(step.trade->aggressor));
    }
    hash_combine_double(hash, step.signal.imbalance);
    hash_combine_double(hash, step.signal.microprice);
    hash_combine_double(hash, step.signal.spread);
    hash_combine_double(hash, step.signal.ofi);
    hash_combine_double(hash, step.signal.depth_ratio);
    hash_combine_double(hash, step.signal.cancel_rate);
    hash_combine_double(hash, step.signal.queue_half_life);
    hash_combine_double(hash, step.signal.liquidity_slope);
    return hash;
}

Event apply_latency_offsets(const Event& event, const ReplayOptions& options) {
    if (options.network_latency_offset < 0 || options.gateway_latency_offset < 0) {
        throw ReplayError("latency offsets must be non-negative");
    }

    return Event(event.event_id(),
                 event.event_type(),
                 event.order_id(),
                 event.price(),
                 event.size(),
                 event.side(),
                 event.exchange_timestamp(),
                 event.gateway_timestamp() + options.network_latency_offset,
                 event.processing_timestamp() + options.network_latency_offset + options.gateway_latency_offset,
                 event.venue(),
                 event.snapshot());
}

} // namespace

HistoricalReplayEngine::HistoricalReplayEngine(FeatureConfig config)
    : config_(std::move(config)) {}

ReplayResult HistoricalReplayEngine::replay_once(const std::vector<Event>& events,
                                                 const ReplayOptions& options) const {
    MicrostructurePipeline pipeline{config_};
    ReplayResult result;
    result.deterministic_signature = kFnvOffset;

    for (const Event& raw_event : events) {
        const Event event = apply_latency_offsets(raw_event, options);
        PipelineResult step = pipeline.process(event);
        hash_combine(result.deterministic_signature, signature_for_step(step));
        result.steps.push_back(std::move(step));
    }

    return result;
}

ReplayResult HistoricalReplayEngine::replay(const std::vector<Event>& events,
                                            const ReplayOptions& options) const {
    ReplayResult result = replay_once(events, options);
    if (options.verify_signals) {
        ReplayResult verification = replay_once(events, options);
        result.signal_verified = result.deterministic_signature == verification.deterministic_signature;
        if (!result.signal_verified) {
            throw ReplayError("signal verification failed under deterministic replay");
        }
    }
    return result;
}

} // namespace microstructure
