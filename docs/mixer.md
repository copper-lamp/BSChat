# Mixer 模块

## 需求
按固定音频帧解码各会话、完成混音、编码并向在线会话派发，同时不阻塞游戏主线程。

## 实际状态
`ServerMixer` 使用独立线程和 120ms tick；无活跃说话者时不推流；待发队列超过 `maxPending` 时丢弃最旧消息。STT 通过接口驱动，不可用时不影响语音链路。空间字段已进入 Config，但空间衰减/距离筛选的最终实现由空间混音 agent 负责。

新增**文件声源**（真实音频回放自检）：`FilePlaybackSource` 把一段 WAV 切帧，`tickOnce` 每 tick 取一帧以虚拟声源标识 `kFilePlaybackSourceId`（全 0xFF）加入混音，并为每个接收者把该声源增益固定为 1.0（广播、不参与空间选路与 `maxTalkers`）。它不等于任何接收者本人，因此自我抑制不会命中——这是单客户端也能听到音频的关键。命令入口为服务端 `/bsc play <file>`、`/bsc stop`，文件为 WAV（PCM/float，任意采样率与声道，服务端下混+线性重采样），复用既有 `MixStream`，协议无改动。

**节拍与帧调度修正**（用真实音频回放才暴露出来）：`tickOnce` 由 `ServerLevelTickEvent`（50ms）驱动，却按 `tickMs`（默认 120ms）设计——每个服务器 tick 都产出 `framesPerTick=2` 帧 60ms 音频，下行被放大到 ~2.4 倍实时速率（实测 96 秒回放客户端收到 3404 帧 ≈ 2.13 倍），客户端抖动缓冲与渲染队列持续溢出丢帧。现在 `tickOnce` 按 `config_.tickMs` 限流；同时上行帧改为按说话者排队（`uploadQueues_`），同一 tick 内的多个子帧各取一段不同音频——此前 `drain` 后直接 `addSpeakerFrame`（覆盖语义）会把同一帧编码多次并丢掉其余上行帧。

**音频参数与服务端权威**：服务端配置里的 `audio.*` 是下行编码的权威值（`bitrateKbps` / `complexity` / `enableDtx` / `frameSizeMs`），编码器按它们创建。注意 `enableDtx` 以前只存在于配置里、编码器被硬编码为开，现已接线；关掉 DTX 后安静段落也照常出帧，连续音频听感明显更好。帧长由服务端在 `Welcome` 里下发，客户端采用（客户端只强制采样率一致）。

## 风险与 TODO
- 编码、混音和待发队列仍需长时压测。
- `maxPending` 是消息条目上限，不是按玩家公平配额。
- 多声道、DTX 和服务端动态配置尚未完整验证。
- 文件声源整段载入内存、仅支持 WAV，且会参与 MixerCore 的 1/sqrt(N) 归一化（与真人语音同时活跃时互相压低音量）；语音档编码参数下音乐听感会明显劣化，详见 `bschat-server-playback.md`。
