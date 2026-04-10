#include "visualization/replay_walker.hpp"

namespace visualization {

ReplayWalker::ReplayWalker(const std::vector<VisualizationFrame>& frames)
    : frames_{frames}, index_{0} {}

bool ReplayWalker::has_frames() const noexcept {
    return !frames_.empty();
}

std::size_t ReplayWalker::size() const noexcept {
    return frames_.size();
}

std::size_t ReplayWalker::current_index() const noexcept {
    return index_;
}

bool ReplayWalker::at_start() const noexcept {
    return index_ == 0;
}

bool ReplayWalker::at_end() const noexcept {
    if (frames_.empty()) return true;
    return index_ == frames_.size() - 1;
}

const VisualizationFrame& ReplayWalker::current() const {
    if (frames_.empty()) {
        throw WalkerError("ReplayWalker::current() called on empty frame sequence");
    }
    return frames_[index_];
}

bool ReplayWalker::next() noexcept {
    if (frames_.empty() || index_ >= frames_.size() - 1) return false;
    ++index_;
    return true;
}

bool ReplayWalker::prev() noexcept {
    if (index_ == 0) return false;
    --index_;
    return true;
}

void ReplayWalker::to_first() noexcept {
    index_ = 0;
}

void ReplayWalker::to_last() noexcept {
    if (!frames_.empty()) {
        index_ = frames_.size() - 1;
    }
}

bool ReplayWalker::jump_to(std::size_t index) noexcept {
    if (index >= frames_.size()) return false;
    index_ = index;
    return true;
}

} // namespace visualization
