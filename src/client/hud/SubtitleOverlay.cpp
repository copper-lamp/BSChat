#include "client/hud/SubtitleOverlay.h"

#include <algorithm>
#include <utility>

namespace bsc::client::hud {
namespace {

// UTF-8 首字节 → 该字符占用的字节数。非法首字节按 1 字节处理，宁可切出一个坏字符，
// 也不能把整条字幕拖死（服务端文本理论上都是合法 UTF-8，这里只做防御）。
std::size_t utf8CharLength(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

std::size_t codepointCount(std::string_view text) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size();) {
        std::size_t const length = utf8CharLength(static_cast<unsigned char>(text[i]));
        i += std::min(length, text.size() - i);
        ++count;
    }
    return count;
}

// 按码点硬换行；遇到英文单词时优先在最后一个空格处断开，避免单词被从中间劈开。
std::vector<std::string> wrapText(std::string_view text, int maxCharsPerLine) {
    int const limit = std::max(1, maxCharsPerLine);
    std::vector<std::string> rows;
    std::string current;
    int count = 0;
    std::size_t i = 0;

    while (i < text.size()) {
        unsigned char const lead = static_cast<unsigned char>(text[i]);
        if (lead == '\n') { // 服务端不产生换行；手工配置/异常文本也不应把内容吞掉
            rows.push_back(current);
            current.clear();
            count = 0;
            ++i;
            continue;
        }

        if (count >= limit) {
            std::size_t const space = current.rfind(' ');
            if (space != std::string::npos && space > 0) {
                std::string rest = current.substr(space + 1);
                current.erase(space);
                while (!current.empty() && current.back() == ' ') current.pop_back();
                rows.push_back(current);
                current = std::move(rest);
                count = static_cast<int>(codepointCount(current));
            } else {
                rows.push_back(current);
                current.clear();
                count = 0;
            }
            // 新行不以空格开头，否则居中文本左侧会出现难以察觉的偏移。
            while (i < text.size() && text[i] == ' ') ++i;
            continue;
        }

        std::size_t const length = std::min(utf8CharLength(lead), text.size() - i);
        current.append(text.substr(i, length));
        ++count;
        i += length;
    }

    if (!current.empty()) rows.push_back(std::move(current));
    if (rows.empty()) rows.push_back(std::string{});
    return rows;
}

} // namespace

SubtitleOverlay::SubtitleOverlay(Options options) : options_(options) {
    if (options_.maxLines < 1) options_.maxLines = 1;
    options_.maxCharsPerLine = std::clamp(options_.maxCharsPerLine, 1, 200);
    if (options_.fadeMs < 0) options_.fadeMs = 0;
}

void SubtitleOverlay::setOptions(Options options) {
    std::lock_guard<std::mutex> lock(mutex_);
    options_ = options;
    if (options_.maxLines < 1) options_.maxLines = 1;
    options_.maxCharsPerLine = std::clamp(options_.maxCharsPerLine, 1, 200);
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

std::vector<std::string> SubtitleOverlay::wrapForDisplay(const SubtitleLine& line, int maxCharsPerLine) {
    // 说话者前缀计入行长：屏幕上真正占位的是「名字：文本」。
    std::string display = line.speakerName.empty()
        ? line.text
        : line.speakerName + "：" + line.text;
    return wrapText(display, maxCharsPerLine);
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

    // 从最新的一条往回铺行，铺满 maxLines 就停：一句超长的话被切成多行时，
    // 优先保留它最后（最新）的几段，而不是把正在说的话截掉尾巴。
    std::vector<SubtitleLine> rows;
    rows.reserve(static_cast<std::size_t>(options_.maxLines));
    for (auto it = lines_.rbegin(); it != lines_.rend(); ++it) {
        std::vector<std::string> const segments = wrapForDisplay(*it, options_.maxCharsPerLine);
        for (auto segment = segments.rbegin(); segment != segments.rend(); ++segment) {
            if (static_cast<int>(rows.size()) >= options_.maxLines) break;
            SubtitleLine row = *it;
            row.speakerName.clear(); // 前缀已并入第一段文本
            row.text = *segment;
            rows.push_back(std::move(row));
        }
        if (static_cast<int>(rows.size()) >= options_.maxLines) break;
    }
    std::reverse(rows.begin(), rows.end());
    return rows;
}

void SubtitleOverlay::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    lines_.clear();
}

} // namespace bsc::client::hud
