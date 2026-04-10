#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace microstructure
{

  using Price = std::int64_t;
  using Quantity = std::int64_t;
  using Timestamp = std::int64_t;
  using EventId = std::uint64_t;
  using OrderId = std::uint64_t;

  enum class EventType
  {
    Add,
    Cancel,
    Modify,
    Trade,
    Snapshot
  };

  enum class Side
  {
    Bid,
    Ask
  };

  enum class Venue
  {
    Nasdaq,
    Arca,
    Bats,
    Iex
  };

  enum class TradeAggressor
  {
    Unknown,
    BuyAggressor,
    SellAggressor
  };

  enum class LiquidityRegime
  {
    Tight,
    Normal,
    Stressed,
    Illiquid
  };

  inline std::string to_string(const EventType event_type)
  {
    switch (event_type)
    {
    case EventType::Add:
      return "ADD";
    case EventType::Cancel:
      return "CANCEL";
    case EventType::Modify:
      return "MODIFY";
    case EventType::Trade:
      return "TRADE";
    case EventType::Snapshot:
      return "SNAPSHOT";
    }
    return "UNKNOWN_EVENT";
  }

  inline std::string to_string(const Side side)
  {
    return side == Side::Bid ? "BID" : "ASK";
  }

  inline std::string to_string(const Venue venue)
  {
    switch (venue)
    {
    case Venue::Nasdaq:
      return "NASDAQ";
    case Venue::Arca:
      return "ARCA";
    case Venue::Bats:
      return "BATS";
    case Venue::Iex:
      return "IEX";
    }
    return "UNKNOWN_VENUE";
  }

  inline std::string to_string(const TradeAggressor aggressor)
  {
    switch (aggressor)
    {
    case TradeAggressor::Unknown:
      return "UNKNOWN";
    case TradeAggressor::BuyAggressor:
      return "BUY_AGGRESSOR";
    case TradeAggressor::SellAggressor:
      return "SELL_AGGRESSOR";
    }
    return "UNKNOWN_AGGRESSOR";
  }

  inline std::string to_string(const LiquidityRegime regime)
  {
    switch (regime)
    {
    case LiquidityRegime::Tight:
      return "tight";
    case LiquidityRegime::Normal:
      return "normal";
    case LiquidityRegime::Stressed:
      return "stressed";
    case LiquidityRegime::Illiquid:
      return "illiquid";
    }
    return "unknown";
  }

  struct SnapshotOrder
  {
    OrderId order_id{0};
    Price price{0};
    Quantity size{0};
    Side side{Side::Bid};
    Venue venue{Venue::Nasdaq};
    std::uint64_t queue_priority{0};
  };

  struct LatencyMetrics
  {
    Timestamp network_latency{0};
    Timestamp gateway_latency{0};
    Timestamp processing_latency{0};
  };

  struct PriceLevelDelta
  {
    Side side{Side::Bid};
    Price price{0};
    Quantity delta_volume{0};
    Venue venue{Venue::Nasdaq};
  };

  struct TradeExecution
  {
    Price price{0};
    Quantity size{0};
    Side resting_side{Side::Ask};
    TradeAggressor aggressor{TradeAggressor::Unknown};
    Quantity visible_depth_before{0};
    Quantity remaining_at_price_after{0};
  };

  struct BookLevelState
  {
    Side side{Side::Bid};
    Price price{0};
    Quantity total_volume{0};
    std::size_t queue_depth{0};
    double order_flow{0.0};
    double cancel_rate{0.0};
    double fill_rate{0.0};
  };

  struct MarketImpactEstimate
  {
    bool fully_filled{false};
    Quantity requested_quantity{0};
    Quantity filled_quantity{0};
    double total_notional{0.0};
    double average_price{0.0};
    Price terminal_price{0};
  };

  struct DepthMetrics
  {
    Quantity top_depth{0};
    Quantity depth_5{0};
    Quantity depth_10{0};
    Quantity bid_depth_5{0};
    Quantity ask_depth_5{0};
    Quantity bid_depth_10{0};
    Quantity ask_depth_10{0};
  };

  struct QueueMetrics
  {
    std::size_t queue_depth{0};
    double cancel_rate{0.0};
    double fill_rate{0.0};
    double queue_half_life{0.0};
  };

  struct SignalVector
  {
    double imbalance{0.0};
    double microprice{0.0};
    double spread{0.0};
    double ofi{0.0};
    double depth_ratio{0.0};
    double cancel_rate{0.0};
    double queue_half_life{0.0};
    double liquidity_slope{0.0};
  };

  struct LiquiditySurfacePoint
  {
    Side side{Side::Bid};
    double distance_from_mid{0.0};
    Quantity cumulative_volume{0};
  };

  struct HiddenLiquiditySignal
  {
    bool suspected{false};
    Price price{0};
    Quantity executed_volume{0};
    Quantity displayed_depth_before{0};
    std::size_t repeated_fills{0};
    std::size_t refill_count{0};
  };

  struct HawkesHooks
  {
    double trade_intensity{0.0};
    double order_arrival_intensity{0.0};
    double cancel_intensity{0.0};
  };

  struct FeatureSnapshot
  {
    double imbalance{0.0};
    double microprice{0.0};
    double spread{0.0};
    double ofi{0.0};
    double volatility{0.0};
    double depth_ratio{0.0};
    double liquidity_slope{0.0};
    double bid_liquidity_slope{0.0};
    double ask_liquidity_slope{0.0};
    DepthMetrics depth{};
    QueueMetrics queue{};
    std::vector<LiquiditySurfacePoint> liquidity_surface{};
    std::vector<LiquiditySurfacePoint> bid_liquidity_surface{};
    std::vector<LiquiditySurfacePoint> ask_liquidity_surface{};
    SignalVector signal{};
    LiquidityRegime regime{LiquidityRegime::Illiquid};
    HiddenLiquiditySignal hidden_liquidity{};
    HawkesHooks hawkes{};
    LatencyMetrics latency{};
    TradeAggressor last_trade_aggressor{TradeAggressor::Unknown};
  };

  struct BookSummary
  {
    std::optional<Price> best_bid{};
    std::optional<Price> best_ask{};
    Quantity best_bid_volume{0};
    Quantity best_ask_volume{0};
    Quantity total_bid_volume{0};
    Quantity total_ask_volume{0};
    std::size_t bid_levels{0};
    std::size_t ask_levels{0};
    std::size_t total_orders{0};
  };

  struct GraphNode
  {
    std::size_t node_id{0};
    Side side{Side::Bid};
    Price price{0};
    Quantity volume{0};
    std::size_t queue_depth{0};
    double order_flow{0.0};
    double cancel_rate{0.0};
    double fill_rate{0.0};
    std::size_t depth_rank{0};
  };

  struct GraphEdge
  {
    std::size_t from{0};
    std::size_t to{0};
    double liquidity_gradient{0.0};
    double flow_transition{0.0};
    bool adjacent{true};
  };

  struct BookGraph
  {
    std::vector<GraphNode> nodes{};
    std::vector<GraphEdge> edges{};
  };

  struct LiquidityDensityPoint
  {
    double relative_price{0.0};
    double density{0.0};
  };

  struct HeatmapBucket
  {
    double distance_from_mid{0.0};
    Quantity volume{0};
  };

} // namespace microstructure
