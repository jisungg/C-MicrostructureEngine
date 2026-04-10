# Microstructure Engine

`MicrostructureEngine` is a C++20 market microstructure and order book engine focused on deterministic state updates, replayable event processing, strict invariants, and machine-learning-ready exports.

This directory is the computational core of the larger workspace. The sibling visualization project at [../VisualizationLayer/docs/VISUALIZATION.md](../VisualizationLayer/docs/VISUALIZATION.md) now consumes this engine for replay capture and HTML/terminal inspection, while this subproject remains library-first.

## Current State

Today the engine includes:

- immutable market event objects for `ADD`, `CANCEL`, `MODIFY`, `TRADE`, and `SNAPSHOT`
- validation and normalization before state mutation
- a deterministic price-time-priority order book engine
- incremental microstructure features, including imbalance, microprice, spread, OFI, depth metrics, queue metrics, liquidity surfaces, Hawkes hooks, hidden-liquidity heuristics, and liquidity regime labels
- historical replay with latency-offset simulation and deterministic verification
- research and ML exports, including a graph view, fixed-width book embedding, liquidity density output, and liquidity heatmap export
- cross-venue consolidated-book infrastructure
- a focused CTest suite built from many small feature and regression tests rather than a single monolithic test

## What This Directory Is Right Now

This subproject should currently be viewed as:

- a serious engine foundation for market microstructure research
- a deterministic C++ library with replay and export surfaces
- a base for execution modeling, feature research, and downstream ML pipelines

This subproject should not be viewed as:

- a standalone end-user application
- the full analyst workflow layer
- the finished production deployment package for the overall workspace

## Core Design Goals

- deterministic replay for identical event streams
- explicit validation before state mutation
- strict state invariants around book consistency
- modular separation of event handling, state updates, features, replay, and exports
- warning-clean C++20 code under strict compiler settings
- ML-friendly outputs without tying the engine to one specific model stack

## Repository Layout

```text
MicrostructureEngine/
├── include/      # Public headers
├── src/          # Engine implementation
├── tests/        # Focused unit and regression tests
├── changelog/    # Project notes and remediation history
└── CMakeLists.txt
```

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

The suite is intentionally split into many small cases so failures isolate to one behavior or regression path.

## Current Usage

The engine is consumed primarily as a C++ library.

The main entry points today are:

- `MicrostructurePipeline` for single-venue validated event processing
- `CrossVenueMicrostructurePipeline` for consolidated multi-venue processing
- `HistoricalReplayEngine` for deterministic replay with optional latency offsets
- `ResearchInterface` exports for graph, embedding, density, and heatmap generation

Minimal single-venue usage looks like this:

```cpp
#include "microstructure/pipeline.hpp"

using namespace microstructure;

int main() {
    MicrostructurePipeline pipeline;

    const Event add_bid{
        1, EventType::Add, 101, 100, 10, Side::Bid,
        1, 2, 3, Venue::Nasdaq
    };
    const Event add_ask{
        2, EventType::Add, 201, 102, 12, Side::Ask,
        4, 5, 6, Venue::Nasdaq
    };

    pipeline.process(add_bid);
    const PipelineResult result = pipeline.process(add_ask);

    return result.signal.spread == 2.0 ? 0 : 1;
}
```

The workspace now also includes a companion visualization layer in `../VisualizationLayer` that replays engine output into terminal and HTML views. That visualization tooling is outside this subdirectory and does not change the engine’s library-first design.

## Current Limitations

The core engine is strong, but the overall platform is not yet in its finished state.

Known limitations at the current stage include:

- this subproject does not provide its own standalone CLI or UI
- persistent analyst workflow features such as bookmarks, saved sessions, timestamp jumps, and comparison views are not part of the engine layer
- consolidated-book infrastructure exists, but richer operator-facing consolidated workflows are still limited
- the project should still be treated as a strong deterministic engine foundation rather than the final product layer

## Production Readiness

The engine itself is best described as a strong research and infrastructure foundation.

It is suitable for continued replay work, downstream analytics integration, and visualization consumption, but the overall workspace should not yet be described as a finished production analyst platform.

## Expected Finished State

The finished system is intended to be more than a library. The target end state extends the current engine with:

- richer replay and visualization tooling
- clearer real-data ingestion paths
- stronger consolidated-book inspection workflows
- market-impact exploration and scenario tooling
- more complete persistence, auditability, and deployment workflows

## Current Gap Between Now and Finished

What exists now is the computational core plus a companion visualization prototype in the workspace.

What still needs to mature into the finished platform is the surrounding workflow layer:

- richer replay UX
- session and bookmark workflows
- broader ingestion and dataset-management tooling
- stronger production-operability support
- final end-user documentation beyond developer/research usage
