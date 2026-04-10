#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "microstructure/types.hpp"

namespace microstructure
{

  class BookView
  {
  public:
    virtual ~BookView() = default;

    [[nodiscard]] virtual std::optional<Price> best_bid() const = 0;
    [[nodiscard]] virtual std::optional<Price> best_ask() const = 0;
    [[nodiscard]] virtual Quantity best_bid_volume() const = 0;
    [[nodiscard]] virtual Quantity best_ask_volume() const = 0;
    [[nodiscard]] virtual Quantity total_volume(Side side) const = 0;
    [[nodiscard]] virtual std::vector<BookLevelState> top_levels(Side side, std::size_t levels) const = 0;
    [[nodiscard]] virtual std::vector<BookLevelState> all_levels(Side side) const = 0;
    [[nodiscard]] virtual BookSummary summary() const = 0;
  };

} // namespace microstructure
