# 语音聊天全范围执行状态

## 需求
覆盖协议、服务端会话与混音、管理策略、客户端采集/播放、可选 STT，以及配置、测试和发布链路。

## 当前实现
- 核心：Opus 编解码、JSON 配置、消息编解码、抖动缓冲和 host 单测已实现。
- 协议：Hello/Welcome、AudioData、MixStream、STT 文本和位置更新消息已存在；版本常量仍由协议头维护。
- 服务端：会话、音频队列、120ms 混音 tick、待发队列背压、STT 接口已实现。
- 空间混音：`ServerMixer` 已使用 `MixerCore`/`SpatialPolicy` 构建逐接收者混音，支持自我抑制、维度隔离、距离衰减和位置新鲜度；环境 `envFlags` 仍只保留扩展输入，未实现 DSP。
- 管理：`AdminPolicy`/`AdminStore` 已实现线程安全策略与 JSON 持久化；LeviLamina 命令入口和运行时装配尚未完成。
- 客户端：已接通游戏生命周期、网络载具、WASAPI shared-mode 采集/播放、PCM 有界队列、AGC、Opus 上行、MixStream 抖动缓冲/解码和播放接线；字幕 HUD 与 Dear-OreUI 页面暂不纳入本阶段。
- 双端自检：客户端进服后待命，玩家输入服务端命令 `/voicechat test`（由服务端 `CommandRegistrar::getServerInstance()` 注册）触发，服务端以 `Control(SmokeTest)` 通知发起者客户端启动 `SmokeTest`；服务端 `ServerRuntime` 通过 `LogSink` 输出同一套 `[smoke]` 判定行，单设备即可验证握手、上行、下行完整闭环。详见 [单设备自动冒烟测试](smoke-test.md)。

## 风险与 TODO
1. 服务端 Config 新增 `maxSessions`、`maxPending` 用于资源上限；负值/极端值的数值校验仍需补齐。
2. `maxPending` 目前只在 ServerRuntime 装配混音器时生效；配置热加载未实现。
3. `maxChatters` 仅保留配置字段，尚未实现按加入时间淘汰。
4. 管理命令、OP/白名单权限来源和审计日志仍未接入；在此之前不能视为生产级封禁系统。
5. LeviLamina 26.10.14 客户端生命周期、输入和世界事件已接入；客户端 target 与服务端/测试 target 均已成功构建，且已导出统一内存分配标记 `ll_memory_operator_overrided`，可进入真实客户端加载及双端音频测试。
6. 已取得 `D:\BSChat\lib\Dear-OreUI` 源码并核实公开 C ABI `DearOreUI_QueryApi`、`IDearOreUIApi`、`PageScope::Settings` 和 `registerPage`/`registerPanel`；已完成可选 DLL 加载和 Settings 面板注册。客户端 enable 失败时，新版构建会输出前置对象和四类事件监听注册诊断。

## 验证
`voicechat-tests` 的测试目标已纳入配置、协议、会话、混音、STT、空间策略、管理策略和客户端核心测试，最新一次全量运行 `Total failures: 0`。两个 flavor 均可构建，产物分别落在 `artifacts/server/voicechat` 与 `artifacts/client/voicechat`，并已通过符号分离校验：服务端 DLL 含 `ServerLevelTickEvent` 且不含 `ClientJoinLevelEvent`/`KeyInputEvent`，客户端反之。构建命令与校验方法见 [building.md](building.md)，双端自检判定见 [smoke-test.md](smoke-test.md)。
