# Client 模块

## 需求
完成麦克风采集、按键/VAD 触发、Opus 编码上传、混音流播放、抖动缓冲和字幕展示，并接入 i18n/UI 生命周期。

## 实际状态
客户端入口已按 LeviLamina 26.10.14 的已确认签名接入：`ClientMod.cpp` 使用 `LL_REGISTER_MOD` 注册 load/enable/disable/unload，并监听 `ClientJoinLevelEvent`、`ClientExitLevelEvent`、`ClientLevelTickEvent` 与 `KeyInputEvent`。运行时通过 `GamePacketTransport`、本地玩家状态适配器和 steady clock 注入 `ClientRuntime`；PTT 依据配置虚拟键码切换 talking 状态，另新增设置面板键 `ClientConfig::settingsKey`（默认 `0x4A`=J）。UI 载体已定性：HUD 为**原生 2D 自绘**（挂 `ll::event::render::AfterUIRenderEvent`），客户端设置页与管理员面板为**服务端中继 / 服务端下发的原生表单**（`ll::form`）；不采用 ImGui，也不采用资源包 JSON UI / DDUI。HUD 与面板模块均已实现：HUD 在 `src/client/hud/`（字幕 + 状态文案原生自绘），设置面板经服务端零知识中继（`src/client/ui/PanelRelay` + `src/shared/ui/{FormPanelBuilder,PanelRegistry,FormPlan,PanelDefinition}` + `src/server/ui/PanelRelay`），管理员面板由服务端本地 `FormPanelBuilder` 构建并 `CustomForm::sendTo` 下发。客户端事件 ID 绑定集中在 `src/client/entry/ClientEventIds.h`；早期用于通道验证的临时探针 `src/client/spike/UiChannelProbe.{h,cpp}` 已随正式实现删除。

当前已完成麦克风采集、播放和设备管理的最小真实链路：WASAPI shared-mode 采集/渲染线程、浮点 PCM 有界队列、AGC、Opus 上行、MixStream 抖动缓冲/解码和渲染接线均已接入。字幕已接入 HUD（服务端 STT 文本经 `SttText` 推入 `SubtitleOverlay`），设备热切换仍未完成，真实双端设备联调待执行。Dear-OreUI Settings 面板已被原生表单 + 服务端中继方案取代，相关集成代码与文档已删除，不再作为验收项。

客户端内置进服自检 `vc::client::SmokeTest`（`src/client/entry/SmokeTest.{h,cpp}`）：进入世界后处于待命状态，由服务端 `/voicechat test` 命令经 `Control(SmokeTest)` 消息请求后触发（早期版本把命令注册在客户端，实测在专用服务器上会被服务端下发的命令表覆盖，玩家只会看到“未知命令”，现已改为服务端命令）。客户端收到请求只置原子标志，由 `ClientLevelTickEvent`（主线程）取出并调用 `SmokeTest::begin()`，走一次真实上行（`setTalking(true)` + 定时 `submitPcm` 合成正弦）并等待服务端回传，把 `handshake`/`uplink`/`downlink`/`overall` 四行 `[smoke]` 结果写入客户端日志；`ClientExitLevelEvent` 时销毁。该类零 LeviLamina 依赖，日志经 `setLogSink` 注入的 `std::function` 输出，并由 `ClientMod` 同时写入宿主日志和 `<模组配置目录>/voicechat-client.log`，便于无控制台环境取证。详见 [单设备自动冒烟测试](smoke-test.md)。

## 风险与 TODO
- Ninja 锁和 ATL 头文件阻塞已排除；客户端 DLL 已加入 LeviLamina `MemoryOperators.h` 的统一内存分配操作符，并导出 `ll_memory_operator_overrided`，已用 `dumpbin` 验证。
- 需要真实 Bedrock 客户端设备联调，确认音频线程和主线程边界。
- 自检只覆盖协议与编解码闭环，不覆盖麦克风采集与人工听感：自检期间 `playedMixFrames_` 由渲染 sink 回调计数，即使本机无可用播放设备也会增长，因此“听到声音”仍需人工确认。
- WASAPI 设备失败时当前停止本次音频设备并保留语音运行时；设备热切换、格式重采样和更细粒度用户提示仍是 TODO。
- HUD 与面板已实现（提交 `bc19a8b`/`f6e7c0b`：HUD `src/client/hud/`，面板 `src/client/ui/` + `src/shared/ui/` + `src/server/ui/`；改键闭环见 `79bb7af` 与 `src/client/input/KeyNames`），但存在以下已知缺口，勿按已完善理解：状态覆盖层目前只有文字、**没有图标**（贴图获取路径未实现）；字幕只显示文本、**没有说话者名字**（客户端缺 `PlayerId → 玩家名` 来源）；HUD 场景过滤仅用 `isInWorldAndNotShowingAnyMenuScreens()` + 首次渲染打印一次屏幕名做诊断，**未按屏幕名精确过滤**；布局偏移量为常量，**未按不同 GUI 缩放实测校准**；面板提交响应的解析格式来自 LeviLamina 源码推断，**需真机验证提交后配置真的被改写**；管理员面板目前只暴露 `voiceEnabled` 一个开关，权限用 OP 判定（`src/server/admin` 里没有命令级管理员判定）；本端麦克风静音（`StatusInputs::muted`）尚未实现，状态位预留。
- Dear-OreUI 集成已被「原生表单 + 服务端中继」方案取代，`src/client/DearOreUiIntegration.*` 与 `docs/dear-oreui.md` 已删除，`DearOreUI_QueryApi` 运行时依赖与「嵌入游戏 Settings tab」形态作废；xmake 侧的 Dear-OreUI include 与 `VOICECHAT_HAS_DEAR_OREUI` 宏同步移除。历史调研见 git 记录。
- 客户端与服务端协议能力协商、位置上报及双端互聊仍需真机验收。
- LeviLamina 客户端事件 emitter 位于 SDK 静态库的自注册目标中；client 链接保留 LeviLamina 初始化代码，并在 enable 阶段按 `EventBus::hasEvent` 对缺失事件建立合法事件流，避免 SDK 构建差异导致监听器全部注册失败。
- 面板依赖服务端已安装本模组：客户端设置页经服务端零知识中继下发原生表单（`UiForm` 消息），单机（无本模组服务端）无面板可用；服务端中继已实现约束——仅受理 `Request`、仅受理已建立会话的玩家、payload 上限 32 KiB、同玩家请求间隔下限 1 s、队列上限，且不解析 payload。客户端侧请求节流 1 s、超时清理 30 s。
