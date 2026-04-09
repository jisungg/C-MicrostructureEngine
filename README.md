# Microstructure Engine

`MicrostructureEngine` is a C++20 market microstructure and order book research engine focused on deterministic state updates, replayable event processing, and machine-learning-ready exports.

The repository is currently in the "core engine" stage: the matching-state logic, replay path, analytics layer, and ML export surface exist, while the operator-facing product layer is still ahead of it.

## Current State

Today the project includes:

- Immutable market event objects for `ADD`, `CANCEL`, `MODIFY`, `TRADE`, and `SNAPSHOT`
- Event validation and normalization before state mutation
- A deterministic price-time-priority order book engine
- Incremental microstructure features, including imbalance, microprice, spread, OFI, depth metrics, queue metrics, liquidity surfaces, Hawkes hooks, hidden-liquidity heuristics, and liquidity regime labels
- Historical replay with latency-offset simulation and deterministic verification
- Research and ML exports, including a graph view, fixed-width book embedding, liquidity density output, and liquidity heatmap export
- Cross-venue consolidated-book infrastructure
- A focused CTest suite built from many small feature and regression tests rather than a single monolithic test

## What This Repository Is Right Now

This repository should currently be viewed as:

- a serious engine foundation for market microstructure research
- a deterministic C++ library with test coverage and replay tooling
- a good base for feature research, execution modeling, and downstream ML pipelines

This repository should not yet be viewed as:

- a finished operator-facing application
- a complete visualization platform
- a final production deployment package

Visualization, ingestion tooling, richer operator workflows, and the final presentation layer are still expected future work.

## Core Design Goals

- Deterministic replay for identical event streams
- Explicit validation before state mutation
- Strict state invariants around book consistency
- Modular separation of event handling, state updates, features, replay, and exports
- Warning-clean C++20 code under strict compiler settings
- ML-friendly outputs without tying the engine to one specific model stack

## Repository Layout

```text
MicrostructureEngine/
├── include/      # Public headers
├── src/          # Engine implementation
├── tests/        # Focused unit and regression tests
├── changelog/    # Project notes and development history
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

The test suite is intentionally split into many small cases so failures are isolated to a single behavior or regression path.

## Expected Finished State

The finished system is intended to be more than a library. The target end state includes:

- A replay and visualization layer for stepping through market events in time order
- Real-time and historical views of the limit order book, top-of-book changes, and queue dynamics
- Depth-profile and liquidity-surface visualizations
- Venue-aware consolidated-book visualization
- Signal dashboards for spread, imbalance, OFI, queue metrics, regime shifts, and hidden-liquidity heuristics
- Market-impact exploration tools for execution and liquidity-consumption scenarios
- Cleaner ingestion paths for historical datasets and live research feeds
- More complete production hardening around persistence, auditability, and deployment workflows

## Expected Visualization Layer

The planned visualization layer is expected to include:

- Book ladder and top-of-book timeline views
- Depth curve and cumulative liquidity views
- Liquidity heatmaps around mid-price
- Replay controls for stepping event by event or by time window
- Cross-venue comparative views
- Feature panels for per-event signal inspection
- Exportable research snapshots for notebooks and downstream analytics

## Current Gap Between Now and Finished

What exists now is the computational core.

What still needs to mature into the finished platform is the surrounding product layer:

- visualization
- polished replay UX
- ingestion and session-management tooling
- broader production-operability support
- final documentation for end users beyond developers and researchers
