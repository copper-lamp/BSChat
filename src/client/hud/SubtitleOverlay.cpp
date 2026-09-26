#include "client/hud/SubtitleOverlay.h"

#include <algorithm>

namespace vc::client::hud {

SubtitleOverlay::SubtitleOverlay(Options options) : options_(options) {
    if (options_.maxLines < 1) options_.maxLines = 1;
    if (options_.fadeMs < 0) options_.fadeMs = 0;
}

void SubtitleOverlay::setOptions(Options options) {
    std::lock_guard<std::mutex> lock(mutex_);
    options_ = options;
    if (options_.maxLines < 1) options_.maxLines = 1;
    if (options_.fadeMs < 0) options_.fadeMs = 0;
}

SubtitleOverlay::Options SubtitleOverlay::options() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return options_;
}

void SubtitleOverlay::setEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    options_.enabled = enabled;
}

bool SubtitleOverlay::enabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return options_.enabled;
}

void SubtitleOverlay::push(SubtitleLine line, int64_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!options_.enabled) return;

    line.shownAtMs = nowMs;

    // 部分结果持续覆盖同一说话者的最后一行，避免逐字打字机式堆行；
    // 说话者一旦给出最终结果，下一条部分结果就是新的一句。
    bool const replaceLast = !lines_.empty() && !line.isFinal && !lines_.back().isFinal
                          && lines_.back().speaker == line.speaker;
    if (replaceLast) {
        lines_.back() = std::move(line);
        return;
    }

    lines_.push_back(std::move(line));
    while (static_cast<int>(lines_.size()) > options_.maxLines) {
        lines_.erase(lines_.begin());
    }
}

std::vector<SubtitleLine> SubtitleOverlay::visibleLines(int64_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!options_.enabled) return {};

    lines_.erase(
        std::remove_if(
            lines_.begin(),
            lines_.end(),
            [&](SubtitleLine const& line) { return nowMs - line.shownAtMs > options_.fadeMs; }
        ),
        lines_.end()
    );
    return lines_;
}

void SubtitleOverlay::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    lines_.clear();
}

} // namespace vc::client::hud
