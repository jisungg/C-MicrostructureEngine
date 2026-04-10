#pragma once

#include <cstddef>
#include <vector>

#include "microstructure/book_view.hpp"
#include "microstructure/types.hpp"

namespace microstructure {

class ResearchMlInterface {
public:
    [[nodiscard]] BookGraph export_graph(const BookView& book) const;
    // Layout per depth level: [bid_distance, bid_volume, ask_distance, ask_volume].
    [[nodiscard]] std::vector<double> book_embedding(const BookView& book,
                                                     std::size_t depth_levels = 10) const;
    [[nodiscard]] std::vector<LiquidityDensityPoint> kernel_density(const BookView& book,
                                                                    Side side,
                                                                    std::size_t depth_levels = 10,
                                                                    double bandwidth = 1.0) const;
    [[nodiscard]] std::vector<HeatmapBucket> liquidity_heatmap(const BookView& book,
                                                               std::size_t depth_levels = 10) const;
};

} // namespace microstructure
