# VisualizationLayer

A replay-first, deterministic visualization layer built directly on top of
`MicrostructureEngine`. All frame data is derived exclusively from the real
engine — no analytics are duplicated or approximated here.

---

## Where it lives

```
c++/
├── CMakeLists.txt              ← root build (engine + visualization)
├── MicrostructureEngine/       ← existing engine (unchanged)
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

The primary integration components are `FrameCapture` and `FrameExtractor`.

```
MicrostructurePipeline::process(Event)     ← engine processes each event
       ↓  PipelineResult + book state
FrameExtractor::extract(...)               ← converts engine state to VisualizationFrame
       ↓  VisualizationFrame
ReplayWalker / JsonSerializer / HtmlExporter / TerminalRenderer
```

`FrameCapture` owns the `MicrostructurePipeline` and drives the replay loop.
`FrameExtractor` is the only component that calls engine methods directly.

`VisualizationFrame` mirrors several engine enum types (`EventType`, `Venue`,
`LiquidityRegime`, `TradeAggressor`) for convenience — downstream components
reference these through the frame rather than importing engine headers directly.
The synthetic generators (`SyntheticEventGenerator`, `RealisticSyntheticGenerator`)
necessarily import engine event types to produce valid engine inputs.

### What each frame contains

| Field | Source |
|---|---|
| `best_bid` / `best_ask` | `PipelineResult::book` (BookSummary) |
| `spread`, `mid` | computed from `best_bid` / `best_ask` |
| `microprice` | `FeatureSnapshot::microprice` |
| `bid_levels[]` / `ask_levels[]` | `OrderBookStateEngine::top_levels()` |
| `imbalance`, `ofi`, `depth_ratio` | `FeatureSnapshot` |
| `cancel_rate`, `queue_half_life` | `FeatureSnapshot::queue` |
| `liquidity_slope` | `FeatureSnapshot::liquidity_slope` |
| `regime` | `FeatureSnapshot::regime` (LiquidityRegime) |
| `is_trade`, `trade` | `PipelineResult::trade` (TradeExecution) |
| `last_trade_aggressor` | `FeatureSnapshot::last_trade_aggressor` |
| `network_latency` / `gateway_latency` | `FeatureSnapshot::latency` |

---

## How to build

### Option A — root build (recommended, builds everything)

```bash
cd c++
cmake -S . -B build_viz
cmake --build build_viz --parallel 8
```

### Option B — engine only (unchanged from before)

```bash
cd c++/MicrostructureEngine
cmake -S . -B build
cmake --build build --parallel 8
```

---

## How to run the demo

```bash
# From c++/
./build_viz/VisualizationLayer/viz_demo replay.html
```

This replays a built-in 20-event market scenario and:
1. Prints terminal frames (first 3, last 3, final frame)
2. Exports `replay.html` — open in any browser

With a custom output path:
```bash
./build_viz/VisualizationLayer/viz_demo /path/to/output.html
```

### Simple synthetic mode

Generate N events using `SyntheticEventGenerator`:

```bash
# 2000 simple synthetic events → replay.html
./build_viz/VisualizationLayer/viz_demo replay.html --synthetic 2000
```

The simple generator uses uniform-random event-type selection with basic shadow-book
tracking. Useful for load/stress testing and baseline validation.

### Realistic synthetic mode

Generate N events using `RealisticSyntheticGenerator`:

```bash
# 2000 realistic events → replay.html
./build_viz/VisualizationLayer/viz_demo replay.html --realistic 2000

# 10000 events to a custom path
./build_viz/VisualizationLayer/viz_demo /tmp/big_replay.html --realistic 10000
```

The realistic generator models actual LOB dynamics: near-touch order clustering,
volatility-driven mid-price, imbalance-biased trade direction, stale-quote
cancellation, regime switching, and post-trade refill bursts. The HTML
visualization produced shows evolving spread, depth changes, and signal variation.

Both modes are fully deterministic (default seed=42).

---

## How to open the HTML artifact

```bash
open replay.html        # macOS
xdg-open replay.html    # Linux
# or double-click the file in Finder
```

**HTML replay controls:**

| Control | Action |
|---|---|
| `First` / `Last` | Jump to first or last frame |
| `Prev` / `Next` | Step one frame |
| `Play` / `Pause` | Auto-advance at 200ms/frame |
| Jump input + `Go` | Jump to a specific frame index |
| `←` / `→` arrow keys | Prev / Next |
| `Space` | Play / Pause |
| `Home` / `End` | First / Last |

**What is shown:**

- Order book ladder (bid in green, ask in red, volume bars)
- Spread · mid · microprice line
- Signal panel: imbalance, OFI, depth ratio, cancel rate, queue half-life, liquidity slope, regime, aggressor
- Trade badge (when the current frame is a trade execution)

---

## How to run terminal mode

The demo executable always runs terminal mode before exporting HTML.
To print just a single frame from your own code:

```cpp
#include "visualization/terminal_renderer.hpp"

visualization::TerminalRenderer renderer;
renderer.print_frame(frame);           // stdout
std::string text = renderer.render_frame(frame);  // string API
```

---

## How to run all tests

### Engine + visualization (from the repo root `c++/`)

```bash
# From c++/
ctest --test-dir build_viz --output-on-failure
```

### Engine + visualization (from an arbitrary working directory)

```bash
ctest --test-dir /path/to/c++/build_viz --output-on-failure
```

### Engine only (standalone)

```bash
ctest --test-dir MicrostructureEngine/build --output-on-failure
```

### Individual visualization test suites

```bash
# Run from c++/
./build_viz/VisualizationLayer/viz_test_frame_extractor      <test_case>
./build_viz/VisualizationLayer/viz_test_replay_walker        <test_case>
./build_viz/VisualizationLayer/viz_test_exporters            <test_case>
./build_viz/VisualizationLayer/viz_test_synthetic_generator  <test_case>
./build_viz/VisualizationLayer/viz_test_realistic_generator  <test_case>
```

---

## JSON frame schema

Schema version: **1** (defined in `include/visualization/export_schema.hpp`).

Increment `kSchemaVersion` whenever field names, types, or ordering change;
this forces explicit test updates.

### Required fields (always present)

```
schema_version       int
frame_index          uint
event_id             uint
event_type           string  (ADD|CANCEL|MODIFY|TRADE|SNAPSHOT)
venue                string  (NASDAQ|ARCA|BATS|IEX)
exchange_timestamp   int64   ns
best_bid_volume      int64
best_ask_volume      int64
spread               double  ticks
mid                  double  ticks
microprice           double  ticks
imbalance            double
ofi                  double
depth_ratio          double
cancel_rate          double
queue_half_life      double
liquidity_slope      double
regime               string  (tight|normal|stressed|illiquid)
is_trade             bool
last_trade_aggressor string  (UNKNOWN|BUY_AGGRESSOR|SELL_AGGRESSOR)
network_latency      int64   ns
gateway_latency      int64   ns
processing_latency   int64   ns
bid_levels           array<DepthLevel>
ask_levels           array<DepthLevel>
```

### Optional fields (null when absent)

```
best_bid   int64 | null   (null when book is one-sided or empty)
best_ask   int64 | null
trade      object | null  (present iff is_trade == true)
```

### DepthLevel object

```
price        int64
volume       int64
queue_depth  uint
order_flow   double
cancel_rate  double
fill_rate    double
```

### TradeExecution object (within "trade")

```
price                    int64
size                     int64
resting_side             string
aggressor                string
visible_depth_before     int64
remaining_at_price_after int64
```

### Ordering guarantees

- `bid_levels[0]` = best bid (highest price)
- `ask_levels[0]` = best ask (lowest price)
- Frame array ordered by `frame_index` ascending

---

## SyntheticEventGenerator

`SyntheticEventGenerator` produces deterministic, valid `microstructure::Event`
sequences for testing and demonstration. All generated events satisfy the engine's
validation rules (monotonic timestamps, unique IDs, no crossed-book adds,
cancel/modify reference live orders only, trade sizes ≤ level volume).

### API

```cpp
#include "visualization/synthetic_event_generator.hpp"

visualization::SyntheticConfig cfg;
cfg.total_events          = 2'000;   // number of events to generate
cfg.depth_levels          = 5;       // target levels per side
cfg.add_probability       = 0.50;
cfg.cancel_probability    = 0.15;
cfg.trade_probability     = 0.20;
cfg.modify_probability    = 0.10;    // remainder → more adds
cfg.initial_mid_price     = 1'000;   // starting mid in ticks
cfg.tick_size             = 1;
cfg.order_size_min        = 100;
cfg.order_size_max        = 1'000;
cfg.price_drift_probability = 0.05; // per-event mid drift probability
cfg.seed                  = 42;     // deterministic RNG seed
cfg.venue                 = microstructure::Venue::Nasdaq;

visualization::SyntheticEventGenerator gen{cfg};
std::vector<microstructure::Event> events = gen.generate();
```

Calling `generate()` twice returns identical sequences. The generator resets its
internal state at the start of each call.

### Trade semantics

Trade events use `order_id=0`, which instructs the engine to perform an anonymous
fill (front-of-queue, no named-order priority check). This is the only safe way
to generate trades without tracking queue position. See engine source
`order_book.cpp` line ~367 for the relevant branch.

### Shadow-book tracking

The generator maintains a shadow copy of the order book to guarantee valid event
references. When a trade consumes a price level, all tracked orders at that level
are removed from the "safe" pool so they cannot be referenced by later Cancel or
Modify events.

---

## Current limitations

- Single-venue replay only (multi-venue `CrossVenueMicrostructurePipeline` not yet wired)
- No live/real-time mode — replay from pre-built event vectors only
- No file-based event stream ingestion (events must be constructed in code)
- HTML replay speed fixed at 200ms/frame; no speed control UI
- `testdata/` directory reserved but no file-based fixture tests yet
- JS renderer uses ES5 for broadest compatibility; no TypeScript or bundling
- No browser automation tests for HTML rendering (JSON layer is tested instead)

---

## How to extend

### Add live stepping from the pipeline

`FrameCapture::capture()` drives a full replay at once. For interactive
stepping, instantiate `MicrostructurePipeline` directly and call
`FrameExtractor::extract()` after each `pipeline.process(event)`.

### Add multi-venue support

Replace `MicrostructurePipeline` in `FrameCapture` with
`CrossVenueMicrostructurePipeline`. The `FrameExtractor` interface is
unchanged; depth ladder extraction still works via `BookView::top_levels()`.

### Add file-based event ingestion

Implement an `EventLoader` that reads a binary/CSV/JSON file and returns
`std::vector<Event>`. Pass to `FrameCapture::capture()` unchanged.

### Add file-based regression fixtures

Place expected JSON snapshots in `testdata/` and add test cases to
`test_exporters.cpp` that compare `serialize_frame()` output byte-for-byte.

### Add a richer renderer

The `HtmlExporter::render_html()` returns a `std::string`. Replace the
embedded JS with a React/Chart.js renderer served from a local dev server,
keeping the C++ JSON serialization layer unchanged.

### Upgrade JSON to nlohmann/json

The hand-rolled serializer in `JsonSerializer` is straightforward to replace.
Add nlohmann/json via CMake `FetchContent` and update `json_serializer.cpp`.
The schema contract in `export_schema.hpp` and the tests remain unchanged.
