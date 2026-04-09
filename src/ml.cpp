#include "microstructure/ml.hpp"

#include <algorithm>
#include <cmath>

#include "microstructure/exceptions.hpp"

namespace microstructure {

namespace {

double midpoint(const std::optional<Price>& bid, const std::optional<Price>& ask) {
    if (bid.has_value() && ask.has_value()) {
        return (static_cast<double>(*bid) + static_cast<double>(*ask)) / 2.0;
    }
    if (bid.has_value()) {
        return static_cast<double>(*bid);
    }
    if (ask.has_value()) {
        return static_cast<double>(*ask);
    }
    return 0.0;
}

} // namespace

BookGraph ResearchMlInterface::export_graph(const BookView& book) const {
    BookGraph graph;
    const auto bids = book.all_levels(Side::Bid);
    const auto asks = book.all_levels(Side::Ask);

    graph.nodes.reserve(bids.size() + asks.size());
    std::size_t next_id = 0;

    for (std::size_t i = 0; i < bids.size(); ++i) {
        const BookLevelState& level = bids[i];
        graph.nodes.push_back(GraphNode{
            next_id++,
            Side::Bid,
            level.price,
            level.total_volume,
            level.queue_depth,
            level.order_flow,
            level.cancel_rate,
            level.fill_rate,
            i + 1});
    }
    for (std::size_t i = 0; i < asks.size(); ++i) {
        const BookLevelState& level = asks[i];
        graph.nodes.push_back(GraphNode{
            next_id++,
            Side::Ask,
            level.price,
            level.total_volume,
            level.queue_depth,
            level.order_flow,
            level.cancel_rate,
            level.fill_rate,
            i + 1});
    }

    for (std::size_t i = 1; i < bids.size(); ++i) {
        graph.edges.push_back(GraphEdge{
            i - 1,
            i,
            static_cast<double>(bids[i].total_volume - bids[i - 1].total_volume),
            bids[i].order_flow - bids[i - 1].order_flow,
            true});
    }

    const std::size_t ask_offset = bids.size();
    for (std::size_t i = 1; i < asks.size(); ++i) {
        graph.edges.push_back(GraphEdge{
            ask_offset + i - 1,
            ask_offset + i,
            static_cast<double>(asks[i].total_volume - asks[i - 1].total_volume),
            asks[i].order_flow - asks[i - 1].order_flow,
            true});
    }

    if (!bids.empty() && !asks.empty()) {
        const std::size_t cross_levels = std::min(bids.size(), asks.size());
        for (std::size_t i = 0; i < cross_levels; ++i) {
            graph.edges.push_back(GraphEdge{
                i,
                ask_offset + i,
                static_cast<double>(asks[i].total_volume - bids[i].total_volume),
                asks[i].order_flow - bids[i].order_flow,
                false});
        }
    }

    return graph;
}

std::vector<double> ResearchMlInterface::book_embedding(const BookView& book,
                                                        const std::size_t depth_levels) const {
    std::vector<double> embedding;
    embedding.reserve(depth_levels * 4);

    const auto bids = book.top_levels(Side::Bid, depth_levels);
    const auto asks = book.top_levels(Side::Ask, depth_levels);
    const double mid = midpoint(book.best_bid(), book.best_ask());
    const double total_depth = static_cast<double>(book.total_volume(Side::Bid) + book.total_volume(Side::Ask));
    const double safe_total_depth = total_depth > 0.0 ? total_depth : 1.0;

    for (std::size_t i = 0; i < depth_levels; ++i) {
        if (i < bids.size()) {
            embedding.push_back(static_cast<double>(bids[i].price) - mid);
            embedding.push_back(static_cast<double>(bids[i].total_volume) / safe_total_depth);
        } else {
            embedding.push_back(0.0);
            embedding.push_back(0.0);
        }
        if (i < asks.size()) {
            embedding.push_back(static_cast<double>(asks[i].price) - mid);
            embedding.push_back(static_cast<double>(asks[i].total_volume) / safe_total_depth);
        } else {
            embedding.push_back(0.0);
            embedding.push_back(0.0);
        }
    }

    return embedding;
}

std::vector<LiquidityDensityPoint> ResearchMlInterface::kernel_density(const BookView& book,
                                                                       const Side side,
                                                                       const std::size_t depth_levels,
                                                                       const double bandwidth) const {
    if (bandwidth <= 0.0) {
        throw ValidationError("bandwidth must be positive");
    }

    const auto levels = book.top_levels(side, depth_levels);
    std::vector<LiquidityDensityPoint> density;
    if (levels.empty()) {
        return density;
    }

    const double mid = midpoint(book.best_bid(), book.best_ask());
    density.reserve(levels.size());

    for (const BookLevelState& sample : levels) {
        const double x = static_cast<double>(sample.price) - mid;
        double value = 0.0;
        for (const BookLevelState& level : levels) {
            const double xi = static_cast<double>(level.price) - mid;
            const double diff = x - xi;
            const double kernel = std::exp(-(diff * diff) / (2.0 * bandwidth * bandwidth));
            value += static_cast<double>(level.total_volume) * kernel;
        }
        density.push_back(LiquidityDensityPoint{x, value});
    }

    std::sort(density.begin(), density.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.relative_price < rhs.relative_price;
    });
    return density;
}

std::vector<HeatmapBucket> ResearchMlInterface::liquidity_heatmap(const BookView& book,
                                                                  const std::size_t depth_levels) const {
    std::vector<HeatmapBucket> heatmap;
    const double mid = midpoint(book.best_bid(), book.best_ask());
    const auto bids = book.top_levels(Side::Bid, depth_levels);
    const auto asks = book.top_levels(Side::Ask, depth_levels);
    heatmap.reserve(bids.size() + asks.size());

    for (const BookLevelState& level : bids) {
        heatmap.push_back(HeatmapBucket{
            static_cast<double>(level.price) - mid,
            level.total_volume});
    }
    for (const BookLevelState& level : asks) {
        heatmap.push_back(HeatmapBucket{
            static_cast<double>(level.price) - mid,
            level.total_volume});
    }

    std::sort(heatmap.begin(), heatmap.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.distance_from_mid < rhs.distance_from_mid;
    });
    return heatmap;
}

} // namespace microstructure
