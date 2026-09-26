#include "server/mixer/FilePlaybackSource.h"

#include <algorithm>

#include "core/audio/WavFile.h"

namespace bsc::server {

bool FilePlaybackSource::load(const std::filesystem::path& path, int targetSampleRate, std::string& error) {
    std::vector<float> pcm;
    if (!audio::loadWavMono(path.string(), targetSampleRate, pcm, error)) {
        return false;
    }
    std::lock_guard lock(mutex_);
    pcm_ = std::move(pcm);
    cursor_ = 0;
    sampleRate_ = targetSampleRate;
    fileName_ = path.filename().string();
    return true;
}

void FilePlaybackSource::stop() {
    std::lock_guard lock(mutex_);
    pcm_.clear();
    cursor_ = 0;
    fileName_.clear();
}

bool FilePlaybackSource::active() const {
    std::lock_guard lock(mutex_);
    return cursor_ < pcm_.size();
}

std::vector<float> FilePlaybackSource::nextFrame(std::size_t frameSamples) {
    if (frameSamples == 0) return {};
    std::lock_guard lock(mutex_);
    if (cursor_ >= pcm_.size()) return {};

    const std::size_t remaining = pcm_.size() - cursor_;
    std::vector<float> frame(frameSamples, 0.0F);
    const std::size_t take = std::min(remaining, frameSamples);
    std::copy_n(pcm_.begin() + static_cast<std::ptrdiff_t>(cursor_), static_cast<std::ptrdiff_t>(take), frame.begin());
    cursor_ += take;
    return frame;
}

std::size_t FilePlaybackSource::remainingSamples() const {
    std::lock_guard lock(mutex_);
    return pcm_.size() - cursor_;
}

int FilePlaybackSource::sampleRate() const {
    std::lock_guard lock(mutex_);
    return sampleRate_;
}

std::string FilePlaybackSource::fileName() const {
    std::lock_guard lock(mutex_);
    return fileName_;
}

} // namespace bsc::server
