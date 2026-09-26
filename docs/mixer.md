# Mixer 模块

## 需求
按固定音频帧解码各会话、完成混音、编码并向在线会话派发，同时不阻塞游戏主线程。

## 实际状态
`ServerMixer` 使用独立线程和 120ms tick；无活跃说话者时不推流；待发队列超过 `maxPending` 时丢弃最旧消息。STT 通过接口驱动，不可用时不影响语音链路。空间字段已进入 Config，但空间衰减/距离筛选的最终实现由空间混音 agent 负责。

新增**文件声源**（真实音频回放自检）：`FilePlaybackSource` 把一段 WAV 切帧，`tickOnce` 每 tick 取一帧以虚拟声源标识 `kFilePlaybackSourceId`（全 0xFF）加入混音，并为每个接收者把该声源增益固定为 1.0（广播、不参与空间选路与 `maxTalkers`）。它不等于任何接收者本人，因此自我抑制不会命中——这是单客户端也能听到音频的关键。命令入口为服务端 `/voicechat play <file>`、`/voicechat stop`，文件为 WAV（PCM/float，任意采样率与声道，服务端下混+线性重采样），复用既有 `MixStream`，协议无改动。

## 风险与 TODO
- 编码、混音和待发队列仍需长时压测。
- `maxPending` 是消息条目上限，不是按玩家公平配额。
- 多声道、DTX 和服务端动态配置尚未完整验证。
- 文件声源整段载入内存、仅支持 WAV，且会参与 MixerCore 的 1/sqrt(N) 归一化（与真人语音同时活跃时互相压低音量）；语音档编码参数下音乐听感会明显劣化，详见 `voicechat-server-playback.md`。
