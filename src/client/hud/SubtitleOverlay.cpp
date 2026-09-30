#include "client/hud/SubtitleOverlay.h"

#include <algorithm>

namespace bsc::client::hud {

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

    // 按玩家 + utteranceId 精确定位；多人交错时仍替换原 partial，
    // final 也会替换该话语的最后 partial，不再追加重复条目。
    if (line.utteranceId != 0) {
        for (auto& existing : lines_) {
            if (existing.speaker == line.speaker && existing.utteranceId == line.utteranceId) {
                if (existing.text == line.text && existing.isFinal == line.isFinal) return;
                existing = std::move(line);
                return;
            }
        }
    } else if (!lines_.empty() && !line.isFinal && !lines_.back().isFinal
               && lines_.back().speaker == line.speaker) {
        if (lines_.back().text == line.text) return;
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

} // namespace bsc::client::hud
