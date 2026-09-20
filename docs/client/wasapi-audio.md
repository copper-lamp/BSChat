# WASAPI 音频边界

## 需求
为 Windows shared client 提供真实的默认设备 capture/render 流式边界，同时保持 `probeDefaultDevices()` 兼容。要求 start/stop 可跨线程安全调用，不能让音频线程因消费者暂时不可用而阻塞。

## 架构
`WasapiAudioDevice` 在独立 worker 中初始化 COM、MMDeviceEnumerator 和共享模式 `IAudioClient`。capture 使用 `IAudioCaptureClient` 将设备格式转换为归一化交错 float PCM，写入有界 capture queue，并可同步调用捕获回调；render 从有界 PCM queue 取帧，使用 `IAudioRenderClient` 填充设备缓冲，不足数据以静音填充。队列溢出策略是丢弃最旧 capture 或拒绝新 render，避免阻塞实时线程。Windows 以外保留不可运行但可编译的边界。

## 备注
当前设备格式由 capture 默认 shared mix format 决定，并用于 render 初始化；上层应在编解码前检查 `sampleRate/channels` 并负责必要的重采样/声道布局转换。`stop()` 会请求 worker 退出并等待其释放 WASAPI 客户端；回调不得重入 `stop()`。构建验证时如遇到其他 client 目标已有的 `JitterBuffer` 命名空间错误，应独立修复后再宣称完整 client 链路通过。
