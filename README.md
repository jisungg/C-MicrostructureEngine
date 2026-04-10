# C++ Market Microstructure Workspace

This repository is a C++20 workspace for deterministic market microstructure research and replay tooling.

It currently consists of two connected projects:

- `MicrostructureEngine/`: the core engine for validated event processing, deterministic order book state transitions, incremental feature computation, replay, and ML-oriented exports
- `VisualizationLayer/`: the replay and presentation layer that captures engine output into deterministic frames and renders it through terminal output, JSON export, and a self-contained HTML replay viewer

The root [CMakeLists.txt](./CMakeLists.txt) builds both components together.

## Current Workspace Status

The repository is past the “engine only” stage.

Today the workspace includes:

- a deterministic C++20 order book and analytics engine
- a replay capture layer that turns engine output into `VisualizationFrame` objects
- a visualization stack with:
  - terminal rendering
  - JSON serialization
  - HTML replay export
  - a demo executable
  - CSV-backed replay input
  - simple and realistic synthetic event generators
- focused CTest coverage across both subprojects

At the same time, this is still not the finished analyst platform. The current repo does not yet provide:

- bookmark or marker workflows
- jump-to-timestamp or jump-to-event-id
- saved analyst sessions or session restore
- before/after comparison mode
- browser automation coverage for the HTML viewer

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

## Core Engine Summary

`MicrostructureEngine` is the computational foundation of the workspace.

It currently provides:

- immutable market event objects for `ADD`, `CANCEL`, `MODIFY`, `TRADE`, and `SNAPSHOT`
- validation and normalization before state mutation
- deterministic single-venue order book updates with price-time priority
- consolidated-book infrastructure for cross-venue processing
- incremental analytics such as imbalance, microprice, spread, OFI, depth metrics, queue metrics, liquidity surfaces, regime labels, hidden-liquidity heuristics, and Hawkes hooks
- historical replay and deterministic verification
- ML and research exports such as graph representations, embeddings, density outputs, and heatmap-oriented data

For engine-specific details, see [MicrostructureEngine/README.md](./MicrostructureEngine/README.md).

## Visualization Layer Summary

`VisualizationLayer` is the replay-facing tooling that sits on top of the engine.

It currently provides:

- `FrameCapture` for replaying engine events into visualization frames
- `FrameExtractor` for converting engine state into `VisualizationFrame`
- `ReplayWalker` for in-memory frame navigation
- `JsonSerializer`, `HtmlExporter`, and `TerminalRenderer`
- `CsvEventLoader` for file-backed replay input
- `SyntheticEventGenerator` and `RealisticSyntheticGenerator` for deterministic synthetic feeds
- `viz_demo` for exporting HTML replay artifacts from built-in, CSV, or synthetic event streams

The current HTML viewer includes:

- an order book ladder
- signal panels
- time-series charts
- a liquidity heatmap toggle
- an event tape
- a scrubber
- frame-index jump
- event-type-filtered navigation

For visualization-specific details, see [VisualizationLayer/docs/VISUALIZATION.md](./VisualizationLayer/docs/VISUALIZATION.md).

## Build From The Repository Root

```bash
cmake -S . -B build_viz
cmake --build build_viz --parallel 8
```

The build directory name is only an example; any out-of-tree build directory works.

## Run Tests From The Repository Root

```bash
ctest --test-dir build_viz --output-on-failure
```

The current root workspace build registers more than 100 focused CTest cases across the engine and visualization layer.

## Quick Start

Run the built-in demo scenario:

```bash
./build_viz/VisualizationLayer/viz_demo replay.html
```

Run from CSV:

```bash
./build_viz/VisualizationLayer/viz_demo replay.html --from-csv /path/to/events.csv
```

Run from the simple synthetic generator:

```bash
./build_viz/VisualizationLayer/viz_demo replay.html --synthetic 2000
```

Run from the realistic synthetic generator:

```bash
./build_viz/VisualizationLayer/viz_demo replay.html --realistic 2000
```

## Documentation Index

The main detailed documents in this workspace are:

- Engine changelog: [MicrostructureEngine/changelog/changelog.md](./MicrostructureEngine/changelog/changelog.md)
- Engine production checklist: [MicrostructureEngine/changelog/checklist.md](./MicrostructureEngine/changelog/checklist.md)
- Visualization documentation: [VisualizationLayer/docs/VISUALIZATION.md](./VisualizationLayer/docs/VISUALIZATION.md)

## What The Repository Is Not Yet

This repository is already a serious engine-and-tooling workspace, but it is not yet the final analyst product.

The remaining gap is around workflow polish and operator-facing usability:

- richer replay navigation and analyst session workflows
- broader real-data ingestion formats and dataset management
- stronger consolidated-book visualization paths
- comparison workflows and export ergonomics
- more complete packaging and downstream integration support
