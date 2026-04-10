#pragma once

#include <cstdint>
#include <vector>

#include "microstructure/pipeline.hpp"

namespace microstructure {

struct ReplayOptions {
    Timestamp network_latency_offset{0};
    Timestamp gateway_latency_offset{0};
    bool verify_signals{false};
};

struct ReplayResult {
    std::vector<PipelineResult> steps{};
    std::uint64_t deterministic_signature{0};
    bool signal_verified{true};
};

class HistoricalReplayEngine {
public:
    explicit HistoricalReplayEngine(FeatureConfig config = {});

    [[nodiscard]] ReplayResult replay(const std::vector<Event>& events,
                                      const ReplayOptions& options = {}) const;

private:
    FeatureConfig config_{};

    [[nodiscard]] ReplayResult replay_once(const std::vector<Event>& events,
                                           const ReplayOptions& options) const;
};

} // namespace microstructure
