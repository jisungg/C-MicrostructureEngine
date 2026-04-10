cmake_minimum_required(VERSION 3.20)

# ── Visualization library ─────────────────────────────────────────────────
add_library(visualization
    src/frame_extractor.cpp
    src/frame_capture.cpp
    src/replay_walker.cpp
    src/json_serializer.cpp
    src/html_exporter.cpp
    src/terminal_renderer.cpp
    src/synthetic_event_generator.cpp
    src/realistic_synthetic_generator.cpp
    src/csv_event_loader.cpp
)

target_include_directories(visualization PUBLIC include)

# Link against microstructure (brings in its headers and warning flags).
target_link_libraries(visualization PUBLIC microstructure)

# ── Demo executable ───────────────────────────────────────────────────────
add_executable(viz_demo apps/viz_demo.cpp)
target_link_libraries(viz_demo PRIVATE visualization)

# ── Test executables ──────────────────────────────────────────────────────
add_executable(viz_test_frame_extractor tests/test_frame_extractor.cpp)
target_link_libraries(viz_test_frame_extractor PRIVATE visualization)

add_executable(viz_test_replay_walker tests/test_replay_walker.cpp)
target_link_libraries(viz_test_replay_walker PRIVATE visualization)

add_executable(viz_test_exporters tests/test_exporters.cpp)
target_link_libraries(viz_test_exporters PRIVATE visualization)

add_executable(viz_test_synthetic_generator tests/test_synthetic_generator.cpp)
target_link_libraries(viz_test_synthetic_generator PRIVATE visualization)

add_executable(viz_test_realistic_generator tests/test_realistic_generator.cpp)
target_link_libraries(viz_test_realistic_generator PRIVATE visualization)

add_executable(viz_test_event_loader tests/test_event_loader.cpp)
target_link_libraries(viz_test_event_loader PRIVATE visualization)

# ── CTest registration ────────────────────────────────────────────────────
set(VIZ_FRAME_EXTRACTOR_TESTS
    frame_extract_simple_add
    frame_extract_best_bid_ask
    frame_extract_spread_computed
    frame_extract_depth_levels
    frame_extract_trade_marker
    frame_extract_one_sided_book
    frame_index_sequential
    frame_extract_regime
    frame_engine_consistency
)

foreach(tc IN LISTS VIZ_FRAME_EXTRACTOR_TESTS)
    add_test(NAME viz_${tc} COMMAND viz_test_frame_extractor ${tc})
endforeach()

set(VIZ_WALKER_TESTS
    walker_next_prev
    walker_at_boundaries
    walker_jump_to
    walker_empty_guard
    walker_to_first_to_last
)

foreach(tc IN LISTS VIZ_WALKER_TESTS)
    add_test(NAME viz_${tc} COMMAND viz_test_replay_walker ${tc})
endforeach()

set(VIZ_EXPORTER_TESTS
    json_schema_version_present
    json_deterministic
    json_optional_null_fields
    json_string_escape
    json_depth_array
    json_trade_fields
    terminal_render_non_empty
    terminal_render_includes_price
    html_render_non_empty
    html_contains_frame_count
    html_deterministic
    html_schema_version_marker
    html_contains_frames_json
    json_no_inf_or_nan
    html_no_inf_or_nan
)

foreach(tc IN LISTS VIZ_EXPORTER_TESTS)
    add_test(NAME viz_${tc} COMMAND viz_test_exporters ${tc})
endforeach()

set(VIZ_SYNTHETIC_TESTS
    synthetic_count_matches
    synthetic_deterministic
    synthetic_valid_events
    synthetic_timestamps_monotonic
    synthetic_unique_event_ids
    synthetic_stress
    synthetic_frame_capture
    synthetic_low_mid_price
    synthetic_all_seeds_valid
    synthetic_high_cancel_modify
)

foreach(tc IN LISTS VIZ_SYNTHETIC_TESTS)
    add_test(NAME viz_${tc} COMMAND viz_test_synthetic_generator ${tc})
endforeach()

set(VIZ_REALISTIC_TESTS
    realistic_deterministic
    realistic_count_matches
    realistic_valid_events
    realistic_timestamps_monotonic
    realistic_unique_event_ids
    realistic_touch_concentration
    realistic_no_trade_empty_side
    realistic_regime_trace
    realistic_imbalance_trade_bias
    realistic_stress
    realistic_frame_signals_vary
    realistic_low_mid_price
)

foreach(tc IN LISTS VIZ_REALISTIC_TESTS)
    add_test(NAME viz_${tc} COMMAND viz_test_realistic_generator ${tc})
endforeach()

set(VIZ_EVENT_LOADER_TESTS
    csv_load_basic
    csv_source_description
    csv_skip_comments_and_blanks
    csv_no_header
    csv_all_venues
    csv_all_event_types
    csv_pipeline_integration
    csv_error_bad_file
    csv_error_too_few_fields
    csv_error_bad_event_type
    csv_error_bad_side
    csv_whitespace_trimming
)

foreach(tc IN LISTS VIZ_EVENT_LOADER_TESTS)
    add_test(NAME viz_${tc} COMMAND viz_test_event_loader ${tc})
endforeach()
