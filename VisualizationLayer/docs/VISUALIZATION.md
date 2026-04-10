# VisualizationLayer

A replay-first, deterministic visualization layer built directly on top of `MicrostructureEngine`.

All frame data is derived from real engine state. The visualization code captures and renders engine output; it does not reimplement core microstructure analytics.

## Current Status

`VisualizationLayer` is now a working replay and inspection layer, not just a placeholder plan.

Today it provides:

- frame capture from real engine replay
- frame extraction into deterministic `VisualizationFrame` objects
- CSV-backed replay input
- simple and realistic synthetic event generation
- terminal rendering
- deterministic JSON export
- self-contained HTML replay export
- a demo executable that can run built-in, CSV, or synthetic replays

The current HTML viewer includes:

- order book ladder rendering
- signal panels
- time-series charts
- a liquidity heatmap toggle
- an event tape (respects active event-type filter)
- a scrubber (dims non-matching frames when a filter is active)
- frame-index jump
- jump-to-event-id
- event-type-filtered navigation (First/Last/Prev/Next/Play all respect the active filter)
- bookmarks (persisted to localStorage, scoped per replay by frame count and event IDs)
- before/after comparison mode (delta panel for spread, mid, microprice, imbalance, OFI, and more)
- session save/load (C++ `VisualizationSession` persisted as JSON alongside the HTML artifact)

It is still not the finished analyst workflow layer. The current implementation does not yet include:

- jump-to-timestamp
- browser automation or end-to-end DOM tests for the replay UI

---

## Where it lives

```text
c++/
├── CMakeLists.txt              ← root build (engine + visualization)
├── MicrostructureEngine/       ← engine
└── VisualizationLayer/
    ├── include/visualization/  ← public headers
    ├── src/                    ← implementations
    ├── apps/viz_demo.cpp       ← demo executable
    ├── tests/                  ← deterministic test cases
    ├── testdata/               ← reserved for fixture snapshots
    └── docs/VISUALIZATION.md   ← this file
```

---

## How it integrates with MicrostructureEngine

The main integration path is:

```text
Event source
  (built-in demo / CSV / synthetic generator)
        ↓
FrameCapture
  (owns MicrostructurePipeline and replays events)
        ↓
FrameExtractor
  (converts engine state to VisualizationFrame)
        ↓
VisualizationFrame[]
        ↓
ReplayWalker / JsonSerializer / HtmlExporter / TerminalRenderer
```

`FrameExtractor` is the main engine-state-to-frame adapter.

`FrameCapture`, `CsvEventLoader`, and the synthetic generators also use engine types directly because they construct or replay `microstructure::Event` streams. Downstream rendering and export components consume `VisualizationFrame`.

### What each frame contains

| Field | Source |
|---|---|
| `best_bid` / `best_ask` | `PipelineResult::book` (BookSummary) |
| `spread`, `mid` | derived from `best_bid` / `best_ask` |
| `microprice` | `FeatureSnapshot::microprice` |
| `bid_levels[]` / `ask_levels[]` | `OrderBookStateEngine::top_levels()` |
| `imbalance`, `ofi`, `depth_ratio` | `FeatureSnapshot` |
| `cancel_rate`, `queue_half_life` | `FeatureSnapshot::queue` |
| `liquidity_slope` | `FeatureSnapshot::liquidity_slope` |
| `regime` | `FeatureSnapshot::regime` |
| `is_trade`, `trade` | `PipelineResult::trade` |
| `last_trade_aggressor` | `FeatureSnapshot::last_trade_aggressor` |
| `network_latency` / `gateway_latency` / `processing_latency` | `FeatureSnapshot::latency` |

---

## How to build

### Option A — root build (recommended)

```bash
cd c++
cmake -S . -B build_viz
cmake --build build_viz --parallel 8
```

### Option B — engine only

```bash
cd c++/MicrostructureEngine
cmake -S . -B build
cmake --build build --parallel 8
```

---

## How to run the demo

### Built-in demo scenario

```bash
./build_viz/VisualizationLayer/viz_demo replay.html
```

This replays a built-in 20-event market scenario, prints terminal previews, and exports an HTML replay artifact.

### CSV-backed replay

```bash
./build_viz/VisualizationLayer/viz_demo replay.html --from-csv /path/to/events.csv
```

`CsvEventLoader` is a lightweight source for real event files. The CSV must already be in replay order; ordering and semantic validity are still enforced by the engine pipeline during capture.

### Simple synthetic mode

```bash
./build_viz/VisualizationLayer/viz_demo replay.html --synthetic 2000
```

`SyntheticEventGenerator` produces deterministic, valid event streams for baseline replay, export, and stress testing.

### Realistic synthetic mode

```bash
./build_viz/VisualizationLayer/viz_demo replay.html --realistic 2000
./build_viz/VisualizationLayer/viz_demo /tmp/big_replay.html --realistic 10000
```

`RealisticSyntheticGenerator` adds regime-aware, imbalance-aware, and near-touch order-flow behavior for more realistic replay scenarios.

Both synthetic modes are deterministic by seed.

### Session save/load

After every run, `viz_demo` writes a session file alongside the HTML artifact (default: `<output>.session`).  On the next run with `--session`, the prior session metadata is printed before the new replay begins:

```bash
./build_viz/VisualizationLayer/viz_demo replay.html --realistic 2000
# writes replay.html and replay.html.session

./build_viz/VisualizationLayer/viz_demo replay.html --realistic 2000 --session replay.html.session
# prints prior frame_count, current_frame, active_filter, bookmark count
```

The session file is JSON and is readable/writable via `VisualizationSession::save()` / `VisualizationSession::load()`.

---

## How to open the HTML artifact

```bash
open replay.html        # macOS
xdg-open replay.html    # Linux
```

## HTML replay controls

| Control | Action |
|---|---|
| `First` / `Last` | Jump to first or last frame matching the active filter |
| `Prev` / `Next` | Step one frame (filter-aware) |
| `Play` / `Pause` | Auto-advance at the current speed (filter-aware) |
| Speed slider | Change autoplay interval |
| `Heatmap` | Toggle the liquidity heatmap panel |
| Filter select | Restrict all navigation and tape/scrubber display by event type |
| Jump input + `Go` | Jump to a specific frame index |
| Event-ID input + `Jump` | Jump to the frame with a given event ID |
| Scrubber click or drag | Seek to a replay position; non-matching frames are dimmed when a filter is active |
| Bookmark button | Toggle a bookmark on the current frame |
| Bookmark list | Navigate directly to any bookmarked frame |
| Compare button | Set the current frame as the comparison baseline |
| Clear Cmp | Remove the comparison baseline |
| `←` / `→` | Prev / Next |
| `Space` | Play / Pause |
| `Home` / `End` | First / Last |
| `h` | Toggle heatmap |

## What the HTML viewer currently shows

- order book ladder with bid/ask depth bars
- trade badge on trade frames
- signal panel for spread, mid, microprice, imbalance, OFI, depth ratio, cancel rate, queue half-life, liquidity slope, regime, and aggressor
- time-series charts for spread, imbalance, OFI, microprice, liquidity slope, and cancel rate
- optional liquidity heatmap
- event tape for recent frames
- replay scrubber with trade-density and regime coloring

---

## Terminal mode

The demo executable always prints terminal previews before exporting HTML.

To print a single frame from your own code:

```cpp
#include "visualization/terminal_renderer.hpp"

visualization::TerminalRenderer renderer;
renderer.print_frame(frame);
std::string text = renderer.render_frame(frame);
```

---

## How to run tests

### Entire workspace

```bash
ctest --test-dir build_viz --output-on-failure
```

### Visualization-specific executables

```bash
./build_viz/VisualizationLayer/viz_test_frame_extractor      <test_case>
./build_viz/VisualizationLayer/viz_test_replay_walker        <test_case>
./build_viz/VisualizationLayer/viz_test_exporters            <test_case>
./build_viz/VisualizationLayer/viz_test_synthetic_generator  <test_case>
./build_viz/VisualizationLayer/viz_test_realistic_generator  <test_case>
./build_viz/VisualizationLayer/viz_test_event_loader         <test_case>
./build_viz/VisualizationLayer/viz_test_boundaries           <test_case>
./build_viz/VisualizationLayer/viz_test_session              <test_case>
```

The visualization layer is covered by focused tests for:

- frame extraction
- replay walking
- JSON and HTML export (including filter coherence, comparison mode, bookmark staleness)
- terminal rendering
- simple and realistic synthetic generators (including spread diversity)
- CSV loading (including timestamp ordering)
- empty and boundary cases
- session JSON round-trips (including all escape sequences and edge cases)

---

## JSON frame schema

Schema version: **1** in `include/visualization/export_schema.hpp`.

The following keys are always present in each serialized frame object:

```text
schema_version       int
frame_index          uint
event_id             uint
event_type           string  (ADD|CANCEL|MODIFY|TRADE|SNAPSHOT)
venue                string  (NASDAQ|ARCA|BATS|IEX)
exchange_timestamp   int64   ns
best_bid             int64 | null
best_ask             int64 | null
best_bid_volume      int64
best_ask_volume      int64
spread               double | null
mid                  double | null
microprice           double | null
imbalance            double
ofi                  double
depth_ratio          double | null
cancel_rate          double
queue_half_life      double
liquidity_slope      double
regime               string
is_trade             bool
trade                object | null
last_trade_aggressor string
network_latency      int64
gateway_latency      int64
processing_latency   int64
bid_levels           array<DepthLevel>
ask_levels           array<DepthLevel>
```

### DepthLevel

```text
price        int64
volume       int64
queue_depth  uint
order_flow   double
cancel_rate  double
fill_rate    double
```

### TradeExecution

```text
price                    int64
size                     int64
resting_side             string
aggressor                string
visible_depth_before     int64
remaining_at_price_after int64
```

### Ordering guarantees

- frame array order is `frame_index` ascending
- `bid_levels[0]` is the best bid
- `ask_levels[0]` is the best ask

---

## Synthetic generators

### `SyntheticEventGenerator`

Produces deterministic, valid `microstructure::Event` sequences for baseline replay and export testing.

### `RealisticSyntheticGenerator`

Produces deterministic, state-aware event sequences with:

- near-touch order clustering
- regime-aware dynamics
- volatility-driven mid-price movement
- imbalance-biased trade direction
- stale-quote cancellation bias
- post-trade refill behavior

Both generators reset internal state on `generate()` and return identical sequences for identical seeds and configs.

---

## Current limitations

- single-venue frame capture only; consolidated replay is not wired into this layer yet
- CSV is the only file-backed replay source implemented today
- no jump-to-timestamp
- no browser automation or end-to-end DOM tests for the replay UI

---

## How to extend

### Add richer workflow state

The next step above the current replay viewer is an explicit workflow/session layer for bookmarks, event-id/timestamp jumps, saved sessions, and comparison views.

### Add consolidated replay support

Wire `FrameCapture` to a consolidated pipeline path and extend the extractor contract for consolidated-book visualization.

### Add richer real-data ingestion

Implement additional `EventLoader` variants for other file formats or dataset stores.

### Add browser-side regression coverage

Keep the deterministic JSON export layer and add DOM/runtime checks for the generated HTML viewer.
