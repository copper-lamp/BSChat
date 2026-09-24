# Client 模块

## 需求
完成麦克风采集、按键/VAD 触发、Opus 编码上传、混音流播放、抖动缓冲和字幕展示，并接入 i18n/UI 生命周期。

## 实际状态
客户端入口已按 LeviLamina 26.10.14 的已确认签名接入：`ClientMod.cpp` 使用 `LL_REGISTER_MOD` 注册 load/enable/disable/unload，并监听 `ClientJoinLevelEvent`、`ClientExitLevelEvent`、`ClientLevelTickEvent` 与 `KeyInputEvent`。运行时通过 `GamePacketTransport`、本地玩家状态适配器和 steady clock 注入 `ClientRuntime`；PTT 仅依据配置虚拟键码切换 talking 状态。未加入任何 UI、ImGui 或 OreUI 假设。

当前已完成麦克风采集、播放和设备管理的最小真实链路：WASAPI shared-mode 采集/渲染线程、浮点 PCM 有界队列、AGC、Opus 上行、MixStream 抖动缓冲/解码和渲染接线均已接入。字幕 UI 与设备热切换仍未完成，真实双端设备联调待执行。Dear-OreUI Settings 面板保持可选动态加载，暂不作为本阶段验收项。

客户端内置进服自检 `vc::client::SmokeTest`（`src/client/entry/SmokeTest.{h,cpp}`）：`ClientJoinLevelEvent` 时武装，握手完成后自动走一次真实上行（`setTalking(true)` + 定时 `submitPcm` 合成正弦）并等待服务端回传，把 `handshake`/`uplink`/`downlink`/`overall` 四行 `[smoke]` 结果写入客户端日志；`ClientExitLevelEvent` 时销毁。该类零 LeviLamina 依赖，日志经 `setLogSink` 注入的 `std::function` 输出，并由 `ClientMod` 同时写入宿主日志和 `<模组配置目录>/voicechat-client.log`，便于无控制台环境取证。详见 [单设备自动冒烟测试](smoke-test.md)。

## 风险与 TODO
- Ninja 锁和 ATL 头文件阻塞已排除；客户端 DLL 已加入 LeviLamina `MemoryOperators.h` 的统一内存分配操作符，并导出 `ll_memory_operator_overrided`，已用 `dumpbin` 验证。
- 需要真实 Bedrock 客户端设备联调，确认音频线程和主线程边界。
- 自检只覆盖协议与编解码闭环，不覆盖麦克风采集与人工听感：自检期间 `playedMixFrames_` 由渲染 sink 回调计数，即使本机无可用播放设备也会增长，因此“听到声音”仍需人工确认。
- WASAPI 设备失败时当前停止本次音频设备并保留语音运行时；设备热切换、格式重采样和更细粒度用户提示仍是 TODO。
- 需要实现字幕数据模型、i18n 文案、图标资源与 HUD 渲染。
- 已取得 `D:\BSChat\lib\Dear-OreUI` 参考源码，并从 GitHub Release `v0.1.2` 核对 `DearOreUI.dll` 导出 `DearOreUI_QueryApi`。客户端新增可选动态加载器：尝试加载 `mods/DearOreUI/DearOreUI.dll`，注册 `voicechat` 的 Settings 面板；缺少 Dear-OreUI 时不阻塞语音模组。真实客户端页面注入和卸载顺序仍需验收。
- 客户端与服务端协议能力协商、位置上报及双端互聊仍需真机验收。
- LeviLamina 客户端事件 emitter 位于 SDK 静态库的自注册目标中；client 链接保留 LeviLamina 初始化代码，并在 enable 阶段按 `EventBus::hasEvent` 对缺失事件建立合法事件流，避免 SDK 构建差异导致监听器全部注册失败。
- Dear-OreUI 是可选运行时集成，不作为 voicechat 的硬依赖：voicechat 在缺少 DearOreUI.dll 时仍应能加载和启用。
