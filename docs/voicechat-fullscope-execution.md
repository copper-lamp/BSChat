# 语音聊天全范围执行状态

## 需求
覆盖协议、服务端会话与混音、管理策略、客户端采集/播放、可选 STT，以及配置、测试和发布链路。

## 当前实现
- 核心：Opus 编解码、JSON 配置、消息编解码、抖动缓冲和 host 单测已实现。
- 协议：Hello/Welcome、AudioData、MixStream、STT 文本和位置更新消息已存在；版本常量仍由协议头维护。
- 服务端：会话、音频队列、120ms 混音 tick、待发队列背压、STT 接口已实现。
- 空间混音：`ServerMixer` 已使用 `MixerCore`/`SpatialPolicy` 构建逐接收者混音，支持自我抑制、维度隔离、距离衰减和位置新鲜度；环境 `envFlags` 仍只保留扩展输入，未实现 DSP。
- 管理：`AdminPolicy`/`AdminStore` 已实现线程安全策略与 JSON 持久化；LeviLamina 命令入口和运行时装配尚未完成。
- 客户端：已实现可 host 测试的 P4 握手/PTT/VAD/AGC 与 WASAPI 设备探测；完整 WASAPI 流式采集播放、游戏生命周期、网络载具、ImGui HUD、Dear-OreUI 页面仍未接通。

## 风险与 TODO
1. 服务端 Config 新增 `maxSessions`、`maxPending` 用于资源上限；负值/极端值的数值校验仍需补齐。
2. `maxPending` 目前只在 ServerRuntime 装配混音器时生效；配置热加载未实现。
3. `maxChatters` 仅保留配置字段，尚未实现按加入时间淘汰。
4. 管理命令、OP/白名单权限来源和审计日志仍未接入；在此之前不能视为生产级封禁系统。
5. 客户端 SDK、ImGui、Dear-OreUI 和 BDS 生命周期缺少仓库可验证 API，必须补齐依赖后进行真实联调。

## 验证
`voicechat-tests` 的测试目标已纳入配置、协议、会话、混音、STT、空间策略、管理策略和客户端核心测试；本轮未能完成 xmake clean/reconfigure/build，因为环境在 Ninja 包锁阶段失败（`cannot create filelock for package(ninja)!`）。客户端核心测试已使用本机 Clang 直接编译运行通过（3 个用例）；完整测试与真实目标构建仍待解除环境阻塞。构建命令见 [building.md](building.md)。
