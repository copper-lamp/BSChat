#include "server/stt/SherpaStt.h"

#include <cstring>
#include <memory>
#include <utility>

#include "c-api.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace bsc::server {

namespace {

size_t samplesToMs(size_t samples) {
    return samples * 1000 / 16000;
}

struct Api {
    using CreateRecognizer = const SherpaOnnxOnlineRecognizer* (*)(const SherpaOnnxOnlineRecognizerConfig*);
    using DestroyRecognizer = void (*)(const SherpaOnnxOnlineRecognizer*);
    using CreateStream = const SherpaOnnxOnlineStream* (*)(const SherpaOnnxOnlineRecognizer*);
    using DestroyStream = void (*)(const SherpaOnnxOnlineStream*);
    using AcceptWaveform = void (*)(const SherpaOnnxOnlineStream*, int32_t, const float*, int32_t);
    using IsReady = int32_t (*)(const SherpaOnnxOnlineRecognizer*, const SherpaOnnxOnlineStream*);
    using Decode = void (*)(const SherpaOnnxOnlineRecognizer*, const SherpaOnnxOnlineStream*);
    using GetResult = const SherpaOnnxOnlineRecognizerResult* (*)(const SherpaOnnxOnlineRecognizer*, const SherpaOnnxOnlineStream*);
    using DestroyResult = void (*)(const SherpaOnnxOnlineRecognizerResult*);
    using InputFinished = void (*)(const SherpaOnnxOnlineStream*);

    CreateRecognizer createRecognizer = nullptr;
    DestroyRecognizer destroyRecognizer = nullptr;
    CreateStream createStream = nullptr;
    DestroyStream destroyStream = nullptr;
    AcceptWaveform acceptWaveform = nullptr;
    IsReady isReady = nullptr;
    Decode decode = nullptr;
    GetResult getResult = nullptr;
    DestroyResult destroyResult = nullptr;
    InputFinished inputFinished = nullptr;
};

template <typename T>
T loadSymbol(void* library, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<T>(GetProcAddress(static_cast<HMODULE>(library), name));
#else
    return reinterpret_cast<T>(dlsym(library, name));
#endif
}

void unloadLibrary(void* library) {
    if (!library) return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(library));
#else
    dlclose(library);
#endif
}

void* loadLibrary(const std::string& path) {
#ifdef _WIN32
    return static_cast<void*>(LoadLibraryA(path.empty() ? "sherpa-onnx-c-api.dll" : path.c_str()));
#else
    return dlopen(path.empty() ? "libsherpa-onnx-c-api.so" : path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

std::unique_ptr<Api> loadApi(void* library) {
    auto api = std::make_unique<Api>();
#define LOAD_API(name) api->name = loadSymbol<Api::decltype(api->name)>(library, "SherpaOnnx" #name)
    api->createRecognizer = loadSymbol<Api::CreateRecognizer>(library, "SherpaOnnxCreateOnlineRecognizer");
    api->destroyRecognizer = loadSymbol<Api::DestroyRecognizer>(library, "SherpaOnnxDestroyOnlineRecognizer");
    api->createStream = loadSymbol<Api::CreateStream>(library, "SherpaOnnxCreateOnlineStream");
    api->destroyStream = loadSymbol<Api::DestroyStream>(library, "SherpaOnnxDestroyOnlineStream");
    api->acceptWaveform = loadSymbol<Api::AcceptWaveform>(library, "SherpaOnnxOnlineStreamAcceptWaveform");
    api->isReady = loadSymbol<Api::IsReady>(library, "SherpaOnnxIsOnlineStreamReady");
    api->decode = loadSymbol<Api::Decode>(library, "SherpaOnnxDecodeOnlineStream");
    api->getResult = loadSymbol<Api::GetResult>(library, "SherpaOnnxGetOnlineStreamResult");
    api->destroyResult = loadSymbol<Api::DestroyResult>(library, "SherpaOnnxDestroyOnlineRecognizerResult");
    api->inputFinished = loadSymbol<Api::InputFinished>(library, "SherpaOnnxOnlineStreamInputFinished");
#undef LOAD_API
    if (!api->createRecognizer || !api->destroyRecognizer || !api->createStream || !api->destroyStream ||
        !api->acceptWaveform || !api->isReady || !api->decode || !api->getResult || !api->destroyResult ||
        !api->inputFinished) return nullptr;
    return api;
}

} // namespace

SherpaStt::SherpaStt(Options options, LogFn log)
: options_(std::move(options)), log_(std::move(log)) {
    library_ = loadLibrary(options_.libraryPath);
    if (library_) {
        auto api = loadApi(library_);
        if (api) {
            SherpaOnnxOnlineRecognizerConfig config{};
            config.feat_config.sample_rate = 16000;
            config.feat_config.feature_dim = 80;
            config.model_config.transducer.encoder = options_.encoderPath.c_str();
            config.model_config.transducer.decoder = options_.decoderPath.c_str();
            config.model_config.transducer.joiner = options_.joinerPath.c_str();
            config.model_config.tokens = options_.tokensPath.c_str();
            config.model_config.num_threads = options_.threads;
            config.model_config.provider = "cpu";
            config.decoding_method = "greedy_search";
            recognizer_ = const_cast<SherpaOnnxOnlineRecognizer*>(api->createRecognizer(&config));
            if (recognizer_) {
                api_ = api.release();
                available_ = true;
            } else {
                if (log_) log_("sherpa-onnx 识别器创建失败，转写停用");
            }
        } else if (log_) {
            log_("sherpa-onnx 动态库缺少所需 C API，转写停用");
        }
    } else if (log_) {
        log_("sherpa-onnx 动态库加载失败，转写停用");
    }
    if (!available_.load()) {
        unloadLibrary(library_);
        library_ = nullptr;
    }
    running_ = true;
    thread_ = std::thread(&SherpaStt::workerMain, this);
}

SherpaStt::~SherpaStt() {
    shutdown();
}

void SherpaStt::beginUtterance(const protocol::PlayerId& speakerId) {
    if (!available_.load()) return; // 引擎不可用 → 丢弃（降级）
    enqueue(Op{Op::Kind::Begin, speakerId, {}});
}

void SherpaStt::feedAudio(const protocol::PlayerId& speakerId, const std::vector<float>& pcm) {
    if (!available_.load() || pcm.empty()) return; // 不可用/静音帧 → 不喂入
    enqueue(Op{Op::Kind::Feed, speakerId, pcm});
}

void SherpaStt::endUtterance(const protocol::PlayerId& speakerId) {
    if (!available_.load()) return;
    enqueue(Op{Op::Kind::End, speakerId, {}});
}

void SherpaStt::setResultSink(ResultSink sink) {
    std::lock_guard lock(sinkMutex_);
    sink_ = std::move(sink);
}

void SherpaStt::shutdown() {
    {
        std::lock_guard lock(mutex_);
        running_ = false;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();

    auto* api = static_cast<Api*>(api_);
    if (api) {
        for (auto& [speakerId, context] : contexts_) {
            (void)speakerId;
            if (context.stream) api->destroyStream(static_cast<const SherpaOnnxOnlineStream*>(context.stream));
        }
        contexts_.clear();
        if (recognizer_) api->destroyRecognizer(static_cast<const SherpaOnnxOnlineRecognizer*>(recognizer_));
        delete api;
        api_ = nullptr;
    }
    recognizer_ = nullptr;
    unloadLibrary(library_);
    library_ = nullptr;
    activeStream_ = nullptr;
    available_ = false;
}

std::vector<float> SherpaStt::resample48kTo16k(const std::vector<float>& pcm48k) {
    if (pcm48k.size() < 3) return {};
    std::vector<float> out;
    out.reserve(pcm48k.size() / 3);
    for (size_t i = 2; i < pcm48k.size(); i += 3) {
        out.push_back((pcm48k[i - 2] + pcm48k[i - 1] + pcm48k[i]) / 3.0f);
    }
    return out;
}

std::string SherpaStt::transcribePartial(const std::vector<float>& pcm16k) {
    auto* api = static_cast<Api*>(api_);
    auto* stream = static_cast<const SherpaOnnxOnlineStream*>(activeStream_);
    auto* recognizer = static_cast<const SherpaOnnxOnlineRecognizer*>(recognizer_);
    if (!api || !stream || !recognizer || pcm16k.size() <= activeFedSamples_) return {};

    const auto* delta = pcm16k.data() + activeFedSamples_;
    const auto deltaSamples = pcm16k.size() - activeFedSamples_;
    api->acceptWaveform(stream, 16000, delta, static_cast<int32_t>(deltaSamples));
    activeFedSamples_ = pcm16k.size();
    while (api->isReady(recognizer, stream)) api->decode(recognizer, stream);
    auto* result = api->getResult(recognizer, stream);
    if (!result) return {};
    std::string text = result->text ? result->text : "";
    api->destroyResult(result);
    return text;
}

std::string SherpaStt::transcribeFinal(const std::vector<float>& pcm16k) {
    auto* api = static_cast<Api*>(api_);
    auto* stream = static_cast<const SherpaOnnxOnlineStream*>(activeStream_);
    auto* recognizer = static_cast<const SherpaOnnxOnlineRecognizer*>(recognizer_);
    if (!api || !stream || !recognizer) return {};

    if (pcm16k.size() > activeFedSamples_) {
        const auto* delta = pcm16k.data() + activeFedSamples_;
        const auto deltaSamples = pcm16k.size() - activeFedSamples_;
        api->acceptWaveform(stream, 16000, delta, static_cast<int32_t>(deltaSamples));
        activeFedSamples_ = pcm16k.size();
    }
    api->inputFinished(stream);
    while (api->isReady(recognizer, stream)) api->decode(recognizer, stream);
    auto* result = api->getResult(recognizer, stream);
    if (!result) return {};
    std::string text = result->text ? result->text : "";
    api->destroyResult(result);
    return text;
}

void SherpaStt::enqueue(Op op) {
    {
        std::lock_guard lock(mutex_);
        if (queue_.size() >= options_.maxQueued) {
            queue_.pop_front(); // 背压：丢最旧
        }
        queue_.push_back(std::move(op));
    }
    cv_.notify_one();
}

void SherpaStt::workerMain() {
    while (true) {
        Op op;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return !running_.load() || !queue_.empty(); });
            if (!running_.load() && queue_.empty()) return;
            op = std::move(queue_.front());
            queue_.pop_front();
        }

        switch (op.kind) {
        case Op::Kind::Begin: {
            auto* api = static_cast<Api*>(api_);
            auto& context = contexts_[op.speakerId];
            if (api && context.stream) api->destroyStream(static_cast<const SherpaOnnxOnlineStream*>(context.stream));
            context = SpeakerCtx{};
            activeFedSamples_ = 0;
            if (api && recognizer_) {
                context.stream = const_cast<SherpaOnnxOnlineStream*>(api->createStream(static_cast<const SherpaOnnxOnlineRecognizer*>(recognizer_)));
            }
            break;
        }
        case Op::Kind::Feed: {
            auto it = contexts_.find(op.speakerId);
            if (it == contexts_.end()) break; // 未 begin → 忽略
            auto& ctx = it->second;

            auto pcm16k = resample48kTo16k(std::move(op.pcm));
            if (pcm16k.empty()) break;
            ctx.pcm16k.insert(ctx.pcm16k.end(), pcm16k.begin(), pcm16k.end());

            const size_t totalMs = samplesToMs(ctx.pcm16k.size());
            if (totalMs >= options_.maxUtteranceMs) {
                auto* api = static_cast<Api*>(api_);
                activeStream_ = ctx.stream;
                auto finalText = transcribeFinal(ctx.pcm16k);
                activeStream_ = nullptr;
                if (!finalText.empty()) emitResult(op.speakerId, true, finalText);
                if (ctx.stream) api->destroyStream(static_cast<const SherpaOnnxOnlineStream*>(ctx.stream));
                ctx = SpeakerCtx{};
                activeFedSamples_ = 0;
                if (api && recognizer_) {
                    ctx.stream = const_cast<SherpaOnnxOnlineStream*>(api->createStream(static_cast<const SherpaOnnxOnlineRecognizer*>(recognizer_)));
                }
                break;
            }

            // 周期产出部分结果（增量字幕）
            if (totalMs - ctx.lastPartialMs >= static_cast<size_t>(options_.partialIntervalMs)) {
                activeStream_ = ctx.stream;
                auto text = transcribePartial(ctx.pcm16k);
                activeStream_ = nullptr;
                if (!text.empty()) {
                    ctx.lastPartialMs = totalMs;
                    emitResult(op.speakerId, false, text);
                }
            }
            break;
        }
        case Op::Kind::End: {
            auto it = contexts_.find(op.speakerId);
            if (it == contexts_.end()) break;
            // 空上下文（超长切分后未继续说话/仅标志帧）→ 不重复产出
            if (!it->second.pcm16k.empty()) {
                activeStream_ = it->second.stream;
                auto finalText = transcribeFinal(it->second.pcm16k);
                activeStream_ = nullptr;
                if (!finalText.empty()) emitResult(op.speakerId, true, finalText);
            }
            if (it->second.stream) {
                if (auto* api = static_cast<Api*>(api_)) {
                    api->destroyStream(static_cast<const SherpaOnnxOnlineStream*>(it->second.stream));
                }
            }
            contexts_.erase(it);
            activeFedSamples_ = 0;
            break;
        }
        }
    }
}

void SherpaStt::emitResult(const protocol::PlayerId& speakerId, bool isFinal, const std::string& text) {
    ResultSink sink;
    {
        std::lock_guard lock(sinkMutex_);
        sink = sink_;
    }
    if (!sink) return;

    pipeline::SttResult result;
    result.speakerId = speakerId;
    result.isFinal   = isFinal;
    result.text      = text;
    sink(result);
}

} // namespace bsc::server
