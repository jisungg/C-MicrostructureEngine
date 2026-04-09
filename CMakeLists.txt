cmake_minimum_required(VERSION 3.20)
project(MicrostructureEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(microstructure_warnings INTERFACE)
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(microstructure_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wconversion
        -Wsign-conversion
        -Wshadow
    )
endif()

add_library(microstructure
    src/event.cpp
    src/order_book.cpp
    src/features.cpp
    src/ml.cpp
    src/pipeline.cpp
    src/replay.cpp
    src/consolidated_book.cpp
)

target_include_directories(microstructure PUBLIC include)
target_link_libraries(microstructure PUBLIC microstructure_warnings)

add_executable(microstructure_tests
    tests/test_main.cpp
)

target_link_libraries(microstructure_tests PRIVATE microstructure)

enable_testing()

set(MICROSTRUCTURE_TEST_CASES
    order_insertion_and_best_quotes
    full_cancel_regression
    modify_requeue_and_price_change_semantics
    trade_execution_partial_and_full
    snapshot_restore_and_exception_safety
    event_validation_and_cross_prevention
    feature_calculations_exact
    windowed_ofi_and_windowed_rates
    queue_half_life_time_normalized
    depth_ratio_one_sided_book
    hidden_liquidity_and_tracker_expiry
    hawkes_decay_and_snapshot_reset
    market_impact_edge_cases
    empty_book_features_and_ml_exports
    replay_determinism_and_latency_offsets
    export_determinism_and_ordering
    consolidated_book_and_consolidated_features
    consolidated_clear_and_duplicate_event_id_validation
    latency_metrics_exact
    trade_aggressor_sell_side
    non_monotonic_timestamps_rejected
    invalid_enum_values_rejected
    snapshot_followed_by_deltas
    book_update_delta_encoding
    replay_graph_and_embedding_match_live
    snapshot_queue_priority
    consolidated_is_crossed_and_locked
)

foreach(test_case IN LISTS MICROSTRUCTURE_TEST_CASES)
    add_test(NAME ${test_case} COMMAND microstructure_tests ${test_case})
endforeach()
