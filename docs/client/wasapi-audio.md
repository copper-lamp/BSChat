# WASAPI 音频边界

## 需求
为 Windows shared client 提供真实的默认设备 capture/render 流式边界，同时保持 `probeDefaultDevices()` 兼容。要求 start/stop 可跨线程安全调用，不能让音频线程因消费者暂时不可用而阻塞。

## 架构
`WasapiAudioDevice` 在独立 worker 中初始化 COM、MMDeviceEnumerator 和共享模式 `IAudioClient`。两端都以调用方给的管线统一格式（`WasapiAudioConfig` 的 sampleRate/channels/frameSamples，取自 `core/config` 的 audio 段）调用 `Initialize`，并用 `AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY` 把采样率/声道转换交给音频引擎，因此边界内不需要自己做重采样或声道布局转换。capture 使用 `IAudioCaptureClient` 把数据读成归一化交错 float PCM，写入有界 capture queue，并可同步调用捕获回调；render 从有界 PCM queue 取帧，使用 `IAudioRenderClient` 按“整帧”写入（缓冲时长也取一帧，只在能容下一整帧时才写），队列为空时补静音。队列溢出策略是丢弃最旧 capture 或拒绝新 render，避免阻塞实时线程。Windows 以外保留不可运行但可编译的边界。

采集与渲染两条链路**分别启动**：任一端点不可用只降级该侧，`start()` 只要一侧可用就返回 true，`captureActive()`/`renderActive()` 表明具体状态，`lastError()` 给出失败步骤 + HRESULT 或降级原因。这样没有麦克风的玩家仍能听到别人，反之亦然。

## 备注
- `AUTOCONVERTPCM` 必须与 `AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY` 同时使用，否则 `Initialize` 以 `E_INVALIDARG` 失败（早期版本只带前者，导致客户端每次进服都是笼统的 `WASAPI audio unavailable`）。
- 采集端默认设备先取 `eCommunications` 角色，失败再退回 `eConsole`；两者都取不到时通常是机器没有可用的录音设备，返回 `ERROR_NOT_FOUND(0x80070490)`，属环境问题而非代码缺陷。
- `stop()` 会请求 worker 退出并等待其释放 WASAPI 客户端；回调不得重入 `stop()`。
- `renderQueueCapacity` 溢出时拒绝新帧而不是阻塞；`enqueueRender` 在渲染侧未启用时直接返回 false。
