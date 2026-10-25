# Protocol 模块

## 需求
在游戏传输层承载握手、音频帧、混音流、字幕和位置更新，并保持服务端与客户端可协商。

## 实际状态
`src/core/protocol/Message.h` 定义消息模型和协议版本，`MessageCodec` 负责二进制编解码；核心层可独立测试。服务端已校验 peer/player ID、协议版本和位置数值，客户端完整网络适配尚未完成。

已登记的消息类型：`Hello/Welcome/AudioData/MixStream/SttText/Control/PosUpdate/UiForm/VolumeBalance`。其中
`VolumeBalance`（C→S，`enabled + strength + targetRms`）承载听者的音量平衡参数，并配合 `CapabilityProximity`
能力位：客户端在 `Hello` 声明、服务端在 `Welcome.serverCapabilities` 回传交集，客户端据此判断服务端是否启用
近距离语音并上报参数。未知类型/未知能力位一律忽略，旧端不会因新增消息断链。

## 风险与 TODO
- 协议版本仍需兼容矩阵和未知消息策略。
- 音频包大小、频率和速率限制需在公网压力测试中确认。
- 位置消息与 `VolumeBalance` 均已接入服务端空间混音；真机双端互通仍待验收。
