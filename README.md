# C++ Market Microstructure Workspace

This repository is a workspace for a deterministic market microstructure engine and a replay-first visualization layer built on top of it.

At a high level, the repo is organized around two connected parts:

- `MicrostructureEngine/`: the core C++20 engine for validated event processing, deterministic order book state transitions, incremental feature computation, replay, and ML-oriented exports
- `VisualizationLayer/`: the replay and presentation layer that captures engine output into visualization frames and renders them through terminal output, JSON export, and HTML replay artifacts

The root `CMakeLists.txt` builds both components together as one workspace.

## Repository Structure

```text
c++/
├── CMakeLists.txt
├── MicrostructureEngine/
│   ├── include/
│   ├── src/
│   ├── tests/
│   ├── changelog/
│   └── README.md
├── VisualizationLayer/
│   ├── include/
│   ├── src/
│   ├── apps/
│   ├── tests/
│   └── docs/
└── README.md
```

## What The Repository Is

This repository is a development and research workspace for market microstructure tooling.

It is intended to support:

- deterministic replay of market event streams
- price-time-priority order book state management
- incremental microstructure analytics
- machine-learning-ready state exports
- replay-driven visualization and inspection workflows

It should currently be understood as a serious engine-and-tooling codebase, not a finished end-user product.

## Core Engine Summary

`MicrostructureEngine` is the foundational system in the repository. It processes immutable market events such as `ADD`, `CANCEL`, `MODIFY`, `TRADE`, and `SNAPSHOT`, validates and normalizes them, applies them to a deterministic order book, updates incremental features, and exposes replay and research interfaces.

The engine currently covers:

- validated event intake and normalization
- deterministic single-venue order book updates
- cross-venue consolidated book infrastructure
- incremental features such as imbalance, microprice, spread, OFI, depth metrics, queue metrics, liquidity surfaces, regime labels, and Hawkes hooks
- replay and verification paths
- ML and research exports such as graph representations, embeddings, density outputs, and heatmap-oriented data
- focused unit and regression testing

For the engine-specific overview, see [MicrostructureEngine/README.md](./MicrostructureEngine/README.md).

## Visualization Layer Summary

`VisualizationLayer` sits on top of the engine and converts replayed engine state into view-friendly artifacts.

The visualization side currently centers on:

- frame capture from replayed engine processing
- frame extraction into visualization-oriented data structures
- replay walking and frame-by-frame navigation
- JSON export
- HTML replay export
- terminal rendering
- demo executable support
- synthetic event generation for visualization and replay testing

For the visualization-specific design and usage notes, see [VisualizationLayer/docs/VISUALIZATION.md](./VisualizationLayer/docs/VISUALIZATION.md).

## Current State Of Work

The repository already contains substantial core infrastructure:

- the engine is implemented as a strict, warning-clean C++20 library with deterministic behavior as a primary design goal
- the workspace builds both the engine and the visualization layer from the repo root
- the codebase includes dedicated test suites rather than relying on one monolithic test binary

The repository is still an active build-out rather than the final finished platform.

What still belongs to the broader finished state includes:

- more polished operator-facing replay workflows
- richer visualization and inspection UX
- ingestion and dataset-handling tooling
- more complete packaging, operationalization, and downstream integration support

## Build From The Repository Root

```bash
cmake -S . -B build_viz
cmake --build build_viz --parallel 8
```

## Run Tests From The Repository Root

```bash
ctest --test-dir build_viz --output-on-failure
```

## Documentation Index

The most important detailed documents in this workspace are:

- Engine changelog: [MicrostructureEngine/changelog/changelog.md](./MicrostructureEngine/changelog/changelog.md)
- Engine production checklist: [MicrostructureEngine/changelog/checklist.md](./MicrostructureEngine/changelog/checklist.md)
- Visualization documentation: [VisualizationLayer/docs/VISUALIZATION.md](./VisualizationLayer/docs/VISUALIZATION.md)

## Intended End State

The long-term goal of this repository is not just a reusable engine library. The expected finished system is a fuller market microstructure research and inspection platform built around the existing deterministic core.

That finished state is expected to include:

- robust replay and event inspection workflows
- richer visualization of book state, depth, and signals
- clearer downstream research and ML integration paths
- stronger operator-facing tooling around export, inspection, and scenario analysis

The current repository already contains the engine and replay foundation needed for that direction. The remaining work is primarily around making the surrounding tooling broader, more polished, and easier to operate.
