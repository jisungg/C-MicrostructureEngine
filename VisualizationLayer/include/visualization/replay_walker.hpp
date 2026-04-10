#pragma once

// ReplayWalker — cursor-based navigation over a captured frame sequence.
//
// Holds a const reference to the frame vector; does not own or copy frames.
// All navigation is O(1).

#include <cstddef>
#include <stdexcept>
#include <vector>

#include "visualization/frame.hpp"

namespace visualization {

// Thrown when current() is called on an empty or uninitialised walker.
struct WalkerError : std::runtime_error {
    explicit WalkerError(const char* msg) : std::runtime_error(msg) {}
};

class ReplayWalker {
public:
    // frames must outlive this walker.
    explicit ReplayWalker(const std::vector<VisualizationFrame>& frames);

    // ── State queries ─────────────────────────────────────────────────────
    [[nodiscard]] bool        has_frames()     const noexcept;
    [[nodiscard]] std::size_t size()           const noexcept;
    [[nodiscard]] std::size_t current_index()  const noexcept;
    [[nodiscard]] bool        at_start()       const noexcept;
    [[nodiscard]] bool        at_end()         const noexcept;

    // ── Frame access ──────────────────────────────────────────────────────
    // Throws WalkerError if has_frames() == false.
    [[nodiscard]] const VisualizationFrame& current() const;

    // ── Navigation ────────────────────────────────────────────────────────
    // Returns false (without moving) when already at the boundary.
    bool next()    noexcept;
    bool prev()    noexcept;

    // Always succeeds (clamps to valid range) when has_frames().
    void to_first() noexcept;
    void to_last()  noexcept;

    // Returns false if index >= size(); cursor unchanged in that case.
    bool jump_to(std::size_t index) noexcept;

private:
    const std::vector<VisualizationFrame>& frames_;
    std::size_t                            index_{0};
};

} // namespace visualization
