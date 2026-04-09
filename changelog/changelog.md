# MicrostructureEngine — Production Remediation Changelog

All fixes are listed in priority order (critical → correctness → analytical → infrastructure).
Every entry includes the error class, the root cause, the exact fix applied, and the test that
pins the regression.

---

## B-1 · Critical UB — Dangling Reference in `cancel_order`

**Severity:** Critical (undefined behaviour, silent data corruption)  
**File:** `src/order_book.cpp` — `OrderBookStateEngine::cancel_order`

**Root Cause:**  
`OrderHandle& handle = order_it->second;` was used after `orders_.erase(order_it)`.  
The handle's `side` and `price` were read via the dangling reference when calling
`erase_level_if_empty`, triggering UB on every full cancel.

**Fix:**  
Capture `side` and `price` by value before erasing the map entry:
```cpp
const Side   saved_side  = handle.side;
const Price  saved_price = handle.price;
if (order.size == 0) {
    level_ptr->queue.erase(handle.iterator);
    orders_.erase(order_it);
}
erase_level_if_empty(saved_side, saved_price);
```

**Regression test:** `test_full_cancel_regression`

---

## B-2 · Type Error — `last_trade_aggressor` Declared as `Timestamp`

**Severity:** Critical (type mismatch, silently stores wrong value)  
**File:** `include/microstructure/types.hpp` — `FeatureSnapshot`

**Root Cause:**  
`Timestamp last_trade_aggressor` stored a nanosecond timestamp in a field intended to
carry `TradeAggressor`. Signal generators and ML export code read the wrong value.

**Fix:**  
Changed the field type to `TradeAggressor`:
```cpp
TradeAggressor last_trade_aggressor{TradeAggressor::Unknown};
```

**Regression test:** `test_trade_aggressor_sell_side`

---

## B-3 · Dead Read — `SignalVectorGenerator` Reads from `snapshot.signal`

**Severity:** High (signal always 0 on first pass; circular dependency)  
**File:** `src/features.cpp` — `SignalVectorGenerator::generate`

**Root Cause:**  
`depth_ratio` and `liquidity_slope` were read from `snapshot.signal.*` instead of the
top-level `snapshot.*` fields, creating a circular read from an as-yet-unwritten field.

**Fix:**  
Read directly from the `FeatureSnapshot` members:
```cpp
signal.depth_ratio    = snapshot.depth_ratio;
signal.liquidity_slope = snapshot.liquidity_slope;
```

**Regression test:** `test_feature_calculations_exact`

---

## B-4 · Dead Condition — Hidden Liquidity Detection Always False

**Severity:** High (feature permanently zero)  
**File:** `src/features.cpp` — hidden liquidity tracker logic

**Root Cause:**  
Detection predicate included `event.size() > update.trade->visible_depth_before`, which
is always false because the validator already rejects trades larger than visible depth.

**Fix:**  
Replaced with semantically correct check against the tracker's own state:
```cpp
if (tracker.executed_volume > tracker.displayed_depth_before
    && remaining_at_price_after == 0) {
    // hidden liquidity suspected
}
```

**Regression test:** `test_hidden_liquidity_and_tracker_expiry`

---

## C-1 · Analytical Error — OFI Was Cumulative, Not Windowed

**Severity:** High (grows without bound; non-stationary feature)  
**File:** `src/features.cpp` — `IncrementalFeatureEngine::on_event`

**Root Cause:**  
Order flow imbalance was accumulated globally instead of over a rolling time window,
making it monotonically increasing and unsuitable as an ML feature.

**Fix:**  
Added `std::deque<TimedOfiSample> ofi_window_` with `evict_window_state()` that drops
samples older than `config_.flow_window` nanoseconds on every event.

**Regression test:** `test_windowed_ofi_and_windowed_rates`

---

## C-2 · Analytical Error — Cancel/Fill Rates Were Global, Not Windowed

**Severity:** High (same unbounded accumulation as OFI)  
**File:** `src/features.cpp`

**Root Cause:**  
`cancel_rate` and `fill_rate` used total-lifetime counters, not rolling window counts.

**Fix:**  
Added `activity_window_` deque (evicted same as OFI window) tracking
`window_cancel_volume_` and `window_fill_volume_` over the rolling window.
`modify` deltas are tracked separately via `repriced_removed_volume` /
`repriced_added_volume` and are NOT counted as cancels/adds in the rate denominators.

**Regression test:** `test_windowed_ofi_and_windowed_rates`

---

## C-3 · Analytical Error — Queue Half-Life in Event Units, Not Time Units

**Severity:** Medium (scale depends on event frequency; not comparable across sessions)  
**File:** `src/features.cpp` — `compute_queue_metrics`

**Root Cause:**  
The hazard rate was computed per-event. Half-life was therefore measured in event counts,
not nanoseconds, making it uninterpretable across varying event rates.

**Fix:**  
Derive `events_per_unit_time` from the first/last timestamps in `event_window_`, then
convert the per-event hazard to a per-nanosecond hazard before computing half-life:
```cpp
double events_per_unit_time = (event_window_.size() - 1) / duration;
double hazard_per_time      = hazard_per_event * events_per_unit_time;
queue_half_life             = std::log(2.0) / hazard_per_time;
```

**Regression test:** `test_queue_half_life_time_normalized`

---

## C-4 · Analytical Error — Bid/Ask Liquidity Surfaces Were Mixed

**Severity:** Medium (bid and ask slopes were averaged together, losing directional info)  
**File:** `src/features.cpp` — `compute_liquidity_surface`

**Root Cause:**  
A single surface was computed across both sides, making `liquidity_slope` an average of
bid-side and ask-side decay, discarding directional liquidity information.

**Fix:**  
Bid and ask surfaces are computed independently:
- `bid_liquidity_surface` / `bid_liquidity_slope` from bid levels only
- `ask_liquidity_surface` / `ask_liquidity_slope` from ask levels only
- `liquidity_surface` / `liquidity_slope` are retained as the combined view

New `FeatureSnapshot` fields: `bid_liquidity_surface`, `ask_liquidity_surface`,
`bid_liquidity_slope`, `ask_liquidity_slope`.

**Regression test:** `test_feature_calculations_exact`

---

## C-5 · Silent Failure — `estimate_market_impact` Accepted `Unknown` Aggressor

**Severity:** Medium (returns a nonsensical estimate instead of failing loudly)  
**File:** `src/order_book.cpp` — `OrderBookStateEngine::estimate_market_impact`

**Root Cause:**  
`TradeAggressor::Unknown` fell through without a throw, returning a zero-filled result.

**Fix:**  
Added explicit guard at function entry:
```cpp
if (aggressor == TradeAggressor::Unknown) {
    throw ValidationError("market impact requires a known trade aggressor");
}
```

**Regression test:** `test_market_impact_edge_cases`

---

## C-6 · Analytical Error — Modify Double-Counted in Rate Denominators

**Severity:** Medium (cancel_rate and fill_rate inflated by reprices)  
**File:** `src/order_book.cpp`, `src/features.cpp`

**Root Cause:**  
`modify_order` emitted a cancel+add delta pair that `features.cpp` counted as a cancel
event and a new-add event. This inflated both cancel rate and add volume in the window.

**Fix:**  
Added `repriced_removed_volume` / `repriced_added_volume` fields to `BookUpdate`.
`modify_order` sets only those fields; `canceled_volume` / `added_volume` are untouched.
Feature engine increments windowed rates only from `canceled_volume` / `added_volume`.

**Regression test:** `test_windowed_ofi_and_windowed_rates`

---

## C-7 · Analytical Error — Hawkes Decay Default Wrong for Nanosecond Timestamps

**Severity:** Medium (decay was effectively instant; intensity always ≈ 0)  
**File:** `include/microstructure/features.hpp` — `FeatureConfig`

**Root Cause:**  
Default `hawkes_decay = 1e-6` decays to ε in ~14 microseconds when timestamps are in
nanoseconds — appropriate for millisecond timestamps, not nanosecond timestamps.

**Fix:**  
Changed default to `1e-9`:
```cpp
double hawkes_decay{1e-9};
```
Tests that need fast decay for short event sequences override this explicitly via
`short_window_config()`.

**Regression test:** `test_hawkes_decay_and_snapshot_reset`

---

## C-8 · Missing Feature — No Consolidated Analytics Pipeline

**Severity:** Medium (consolidated NBBO had no feature engine)  
**File:** `include/microstructure/consolidated_book.hpp`, `src/consolidated_book.cpp`

**Root Cause:**  
`CrossVenueConsolidatedBook` had no counterpart to `MicrostructurePipeline`.  
Feature engine, signal generator, and research interface were all single-venue only.

**Fix:**  
Added `CrossVenueMicrostructurePipeline` with full `IncrementalFeatureEngine`,
`SignalVectorGenerator`, and `ResearchInterface` operating over the consolidated `BookView`.

**Regression test:** `test_consolidated_book_and_consolidated_features`

---

## C-9 · Invariant Check — `validate_full_invariants` Only Checked One Direction

**Severity:** Medium (could miss orphaned `aggregated_depth_` entries)  
**File:** `src/order_book.cpp` — `OrderBookStateEngine::validate_full_invariants`

**Root Cause:**  
The validation iterated `bid_levels_` and `ask_levels_` to confirm they appear in
`aggregated_depth_`, but never iterated `aggregated_depth_` in the reverse direction
to confirm every aggregated entry has a matching level.

**Fix:**  
Added reverse iteration: for every price in `aggregated_depth_`, assert that at least
one of `bid_levels_` or `ask_levels_` contains that price.

**Regression test:** `test_order_insertion_and_best_quotes` (invariant check at end)

---

## C-10 · Analytical Error — `depth_ratio` Was 0 for One-Sided Book

**Severity:** Low-Medium (misleading: should be ∞ when bid exists but ask does not)  
**File:** `src/features.cpp` — `compute_depth_ratio`

**Root Cause:**  
Division by zero was caught as a `0/0` edge case returning `0.0`, but `bid/0` should
return positive infinity to distinguish "no ask side" from "equal depth".

**Fix:**  
```cpp
if (ask_depth == 0 && bid_depth > 0) {
    return std::numeric_limits<double>::infinity();
}
```

**Regression test:** `test_depth_ratio_one_sided_book`

---

## N-1 · New Bug — Crossed Consolidated Book Incorrectly Threw

**Severity:** High (runtime abort on legitimate multi-venue NBBO state)  
**File:** `src/consolidated_book.cpp` — `apply_consolidated_delta`

**Root Cause:**  
`apply_consolidated_delta` threw `BookInvariantError("consolidated book became crossed")`
whenever the NBBO appeared locked or crossed. A crossed/locked consolidated order book is
a legitimate and common market microstructure state (SIP latency, venue queue positioning,
smart order routing), not a data error. This caused crashes whenever any two venues had
overlapping quotes.

**Fix:**  
Removed the throw entirely. Added observable query methods instead:
```cpp
[[nodiscard]] bool is_crossed() const;
[[nodiscard]] bool is_locked() const;
```
Single-venue per-book cross prevention (`ensure_not_crossed`) is retained — that remains
correct and important for intra-venue invariants.

**Regression test:** `test_consolidated_is_crossed_and_locked`

---

## N-2 · New Bug — Snapshot Restores Ignored Queue Priority

**Severity:** Medium (post-snapshot queue order was non-deterministic for same-price orders)  
**File:** `include/microstructure/types.hpp`, `src/event.cpp`

**Root Cause:**  
`SnapshotOrder` had no `queue_priority` field. Same-price orders after a snapshot restore
were always ordered by `order_id` ascending, which does not reflect true time-priority.
In live markets, order_id assignment order can differ from queue position.

**Fix:**  
Added `std::uint64_t queue_priority{0}` to `SnapshotOrder`. Updated `snapshot_order_less`
comparator to sort by `queue_priority` (ascending, lower = earlier) when either order
has a non-zero priority; falls back to `order_id` when both are zero (backwards compatible).

**Regression test:** `test_snapshot_queue_priority`

---

## I-1 · Infrastructure — Event ID Uniqueness Not Enforced

**Severity:** High (duplicate replay or feed glitches silently corrupted book state)  
**File:** `src/event.cpp` — `EventValidator`

**Root Cause:**  
`EventValidator` checked timestamp monotonicity and field validity but never tracked
seen event IDs, allowing the same event to be processed multiple times.

**Fix:**  
Added `std::unordered_set<EventId> seen_event_ids_` to `EventValidator`. Every validated
event is inserted; duplicate event_id throws `ValidationError("duplicate event id detected")`.
`reset()` clears the set. The consolidated book's shared validator propagates this check
globally across all venues.

**Regression test:** `test_consolidated_clear_and_duplicate_event_id_validation`

---

## I-2 · Infrastructure — Snapshot Could Contain Duplicate Order IDs

**Severity:** Medium (silently inserted duplicate entries into the order map)  
**File:** `src/event.cpp` — `EventValidator::validate` (Snapshot branch)

**Root Cause:**  
Snapshot payload validation iterated orders to check price/size positivity but did not
detect duplicate `order_id` values within the same snapshot, which `insert_order` would
then insert as distinct entries, corrupting the order-to-handle map.

**Fix:**  
Added per-snapshot deduplication using a local `std::unordered_set<OrderId>`:
```cpp
if (!seen_snapshot_orders.insert(order.order_id).second) {
    throw ValidationError("snapshot contains duplicate order ids");
}
```

**Regression test:** `test_snapshot_restore_and_exception_safety`

---

## I-3 · Infrastructure — `restore_snapshot` Was Not Exception-Safe

**Severity:** Medium (partial restore left book in inconsistent state on throw)  
**File:** `src/order_book.cpp` — `OrderBookStateEngine::restore_snapshot`

**Root Cause:**  
`restore_snapshot` cleared the book, then inserted orders one by one. Any validation
error mid-way (e.g. duplicate order from a malformed snapshot) left the book half-rebuilt.

**Fix:**  
Applied the copy-swap idiom: build a complete temporary engine first, then atomically
swap it into `*this` only if the build succeeds:
```cpp
OrderBookStateEngine restored;
for (const SnapshotOrder& order : event.snapshot()->orders) {
    restored.insert_order(order.order_id, order.price, order.size, order.side, order.venue, false);
}
swap(restored);               // noexcept — only reaches here on success
validate_full_invariants();
```

**Regression test:** `test_snapshot_restore_and_exception_safety`

---

## I-4 · Infrastructure — IntelliSense False Positives (VS Code Configuration)

**Severity:** Low (developer experience only; actual compilation was clean)  
**File:** `.vscode/c_cpp_properties.json` (new file)

**Root Cause:**  
VS Code C++ IntelliSense did not know the build directory or C++ standard. It reported
100+ errors (missing headers, `std::optional` not found, unknown attributes) that were
absent in the actual `-std=c++20` Clang build.

**Fix:**  
Created `.vscode/c_cpp_properties.json` pointing at `build/compile_commands.json`
(generated by CMake's `CMAKE_EXPORT_COMPILE_COMMANDS=ON`) with `"cppStandard": "c++20"`.

---

## Final Test Status

| # | Test | Result |
|---|------|--------|
| 1 | order_insertion_and_best_quotes | PASS |
| 2 | full_cancel_regression | PASS |
| 3 | modify_requeue_and_price_change_semantics | PASS |
| 4 | trade_execution_partial_and_full | PASS |
| 5 | snapshot_restore_and_exception_safety | PASS |
| 6 | event_validation_and_cross_prevention | PASS |
| 7 | feature_calculations_exact | PASS |
| 8 | windowed_ofi_and_windowed_rates | PASS |
| 9 | queue_half_life_time_normalized | PASS |
| 10 | depth_ratio_one_sided_book | PASS |
| 11 | hidden_liquidity_and_tracker_expiry | PASS |
| 12 | hawkes_decay_and_snapshot_reset | PASS |
| 13 | market_impact_edge_cases | PASS |
| 14 | empty_book_features_and_ml_exports | PASS |
| 15 | replay_determinism_and_latency_offsets | PASS |
| 16 | export_determinism_and_ordering | PASS |
| 17 | consolidated_book_and_consolidated_features | PASS |
| 18 | consolidated_clear_and_duplicate_event_id_validation | PASS |
| 19 | latency_metrics_exact | PASS |
| 20 | trade_aggressor_sell_side | PASS |
| 21 | non_monotonic_timestamps_rejected | PASS |
| 22 | invalid_enum_values_rejected | PASS |
| 23 | snapshot_followed_by_deltas | PASS |
| 24 | book_update_delta_encoding | PASS |
| 25 | replay_graph_and_embedding_match_live | PASS |
| 26 | snapshot_queue_priority | PASS |
| 27 | consolidated_is_crossed_and_locked | PASS |

**27 / 27 PASS. Zero warnings. Zero UB.**
